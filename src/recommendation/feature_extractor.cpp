#include "rr/recommendation/feature_extractor.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>

#include "rr/core/embedding.hpp"
#include "rr/domain/interaction.hpp"
#include "rr/recommendation/popularity_recommender.hpp"
#include "rr/recommendation/scoring.hpp"

namespace rr {

namespace {

float clamp01(double v) { return static_cast<float>(std::clamp(v, 0.0, 1.0)); }

// V2 modality-match normalization (Phase 15): (cos(estimate, reel modality embedding) + 1) / 2 in
// [0,1], the SAME affine map as `similarity`/`session_topic`. Both vectors are unit-length by
// construction (the estimate is maintained unit-length by OnlineUserStateUpdater; the reel modality
// embeddings are L2-normalized at generation), so cosine == dot. Returns the neutral 0.5 when the
// estimate is empty (the updater has not cold-started this modality yet) or — defensively — when
// the reel carries no such modality embedding or the dimensions disagree, so rr::dot never throws
// on the hot path (this extractor's "a bad lookup can never throw" doctrine, D10).
float modalityMatch(const Embedding &est, const Embedding &modality) {
    if (est.empty() || modality.empty() || est.size() != modality.size()) {
        return 0.5f;
    }
    return clamp01((static_cast<double>(dot(est, modality)) + 1.0) / 2.0);
}

// Whether a candidate's representative source is Exploration (drives the exploration bonus).
bool isExploration(CandidateSource source) { return source == CandidateSource::Exploration; }

// True iff `id` indexes a real reel (dense-id invariant: reels[i].id.value == i). Guards every
// reel lookup on the hot path so an out-of-range candidate/interaction id can never throw (D10).
bool inRange(ReelId id, const std::vector<Reel> &reels) { return id.value < reels.size(); }

// Whether `type` is a "completed/liked" positive interaction that reveals a duration preference:
// a full/rewatched view or a like. Skips, partial watches, impressions, follows, not-interested
// do NOT count (they carry no duration preference or are ambiguous).
bool isDurationPositive(InteractionType type) {
    return type == InteractionType::CompleteWatch || type == InteractionType::Rewatch ||
           type == InteractionType::Like;
}

} // namespace

FeatureExtractor::FeatureExtractor(const std::vector<Reel> &reels, const RankingConfig &config,
                                   bool contentV2)
    : reels_(reels), config_(config), contentV2_(contentV2) {}

std::vector<FeatureVector> FeatureExtractor::extract(const User &user,
                                                     const std::vector<Candidate> &pool,
                                                     Timestamp now) const {
    const std::size_t n = pool.size();
    std::vector<FeatureVector> out(n);
    if (n == 0) {
        return out;
    }

    // ---- Preferred duration (for duration_match), computed ONCE over the recent window so the
    // per-candidate work stays O(1): mean durationSeconds of recently completed/liked reels.
    // FROZEN: with no usable history the feature is the neutral 0.5, signalled here by
    // hasPreferred == false. O(recentWindow).
    double durationSum = 0.0;
    std::size_t durationCount = 0;
    for (const InteractionEvent &e : user.recentInteractions) {
        if (isDurationPositive(e.type) && inRange(e.reelId, reels_)) {
            durationSum += static_cast<double>(reels_[e.reelId.value].durationSeconds);
            ++durationCount;
        }
    }
    const bool hasPreferred = durationCount > 0;
    const double preferredDuration =
        hasPreferred ? durationSum / static_cast<double>(durationCount) : 0.0;

    // ---- Reference language for the V2 language_match feature (Phase 15), computed ONCE over the
    // recent window: the MAJORITY language among the user's recently-interacted reels, ties broken
    // to the LOWER language id (deterministic). The User has no serving-visible language field, so
    // the reference is INFERRED from OBSERVABLES ONLY (the languages of recently-interacted reels),
    // never from any hidden trait (D18). Empty/absent history => no reference => language_match is
    // the neutral 1.0 for every candidate (documented). Only computed under contentV2_. O(window).
    bool hasRefLanguage = false;
    uint32_t refLanguage = 0;
    if (contentV2_) {
        std::map<uint32_t, int> counts; // ordered by language id for the lowest-id tie-break
        for (const InteractionEvent &e : user.recentInteractions) {
            if (inRange(e.reelId, reels_)) {
                counts[reels_[e.reelId.value].language.value] += 1;
            }
        }
        int best = 0;
        for (const auto &[lang, count] :
             counts) { // ascending id => first max wins ties (lowest id)
            if (count > best) {
                best = count;
                refLanguage = lang;
                hasRefLanguage = true;
            }
        }
    }

    // ---- Pool-local popularity prior (banned from touching the whole catalog): the mean
    // engagement rate over THIS pool's reels, 0 if the pool has no impressions. Feeds
    // smoothedPopularity so low-impression reels are pulled toward the pool mean. O(pool).
    double poolEngagement = 0.0;
    double poolImpressions = 0.0;
    for (const Candidate &c : pool) {
        if (inRange(c.reelId, reels_)) {
            const Reel &reel = reels_[c.reelId.value];
            poolEngagement += popularityEngagement(reel);
            poolImpressions += static_cast<double>(reel.impressionCount);
        }
    }
    const double poolPriorMean = poolImpressions > 0.0 ? poolEngagement / poolImpressions : 0.0;

    // First pass: every feature except the pool-relative popularity, plus the raw smoothed
    // popularity values (min-max'd in the second pass).
    std::vector<double> rawPopularity(n, 0.0);
    for (std::size_t i = 0; i < n; ++i) {
        const Candidate &c = pool[i];
        FeatureVector &f = out[i];

        // similarity: FIXED affine map (cosine s in [-1,1]) -> (s+1)/2 in [0,1], clamped. Not
        // pool-relative, so this contribution is directly comparable across requests.
        f.similarity = clamp01((static_cast<double>(c.retrievalSimilarity) + 1.0) / 2.0);

        // exploration bonus (TDD 14.1, activated Phase 8): 1.0 iff this candidate's REPRESENTATIVE
        // source is Exploration, else 0.0. Depends only on the label, not on the reel lookup, so it
        // is set here before the range guard. The Orchestrator elects Exploration as the
        // representative Candidate.source whenever a reel's merged label set contains it, so this
        // feature and the orchestrator's guaranteed-slot rule read the SAME signal.
        // explorationWeight (config) turns the 1.0 into a score bonus; with no exploration source
        // (or weight 0) every value is 0 and the term is inert — exactly the pre-Phase-8 behaviour.
        f.exploration = isExploration(c.source) ? 1.0f : 0.0f;

        if (!inRange(c.reelId, reels_)) {
            // Defensive: an out-of-range candidate (should never reach the ranker post-filter).
            // Neutral / zero everything, and a neutral session/duration match.
            f.sessionTopic = 0.5f;
            f.quality = 0.0f;
            f.freshness = 0.0f;
            f.trending = 0.0f;
            f.creatorAffinity = 0.0f;
            f.durationMatch = 0.5f;
            f.repetition = 0.0f;
            f.impressionCount = 0.0f;
            rawPopularity[i] = 0.0;
            continue;
        }
        const Reel &reel = reels_[c.reelId.value];

        // session_topic (TDD 14.1): FIXED affine map of the cosine between the candidate embedding
        // and the user's SESSION preference vector, (cos + 1) / 2 in [0,1], clamped -- IDENTICAL
        // normalization to `similarity` above. Both the embedding and the session vector are
        // unit-length by construction (embeddings at generation; sessionPreference at cold start
        // and after every OnlineUserStateUpdater::apply, Phase 7 contract), so cosine == dot. Not
        // pool-relative, so the contribution is directly comparable across requests. The Phase 6
        // WeightedRanker left this inert; Phase 7 activates it as the session vectors start
        // updating online. Defensive: if the session vector is absent / wrong-dimension (a caller
        // that bypassed cold start), fall back to the neutral 0.5 rather than let rr::dot throw on
        // the hot path (D10), matching this extractor's "a bad lookup can never throw" doctrine.
        if (user.sessionPreference.size() == reel.embedding.size() &&
            !user.sessionPreference.empty()) {
            f.sessionTopic = clamp01(
                (static_cast<double>(dot(user.sessionPreference, reel.embedding)) + 1.0) / 2.0);
        } else {
            f.sessionTopic = 0.5f;
        }

        // quality: reel.intrinsicQuality is ALREADY clamped to [0,1] at generation
        // (reel_generator.cpp: clamp01(baseQuality + N(0, 0.1))), so it is used as-is (clamp is a
        // no-op guard).
        f.quality = clamp01(static_cast<double>(reel.intrinsicQuality));

        // freshness: freshnessScore = 2^(-age/halfLife), already in (0,1]. Documented in
        // scoring.hpp.
        f.freshness =
            clamp01(freshnessScore(reel.createdAt, now, config_.freshnessHalfLifeSeconds));

        // trending: trendingScore is raw >= 0 and unbounded; map with the saturating rational
        // (soft-sigmoid) t = raw / (raw + 1) into [0,1). Monotone increasing, fixed (not
        // pool-relative), 0 at raw==0, half-saturates at raw==1. Documented, deterministic.
        const double trendRaw = trendingScore(reel, now, config_.trendingHalfLifeSeconds);
        f.trending = clamp01(trendRaw / (trendRaw + 1.0));

        // creator_affinity: user.creatorAffinity lookup; absent key => 0 (contract). Values are
        // maintained in [0,1] by the simulator; clamp is a defensive no-op.
        float affinity = 0.0f;
        if (auto it = user.creatorAffinity.find(reel.creatorId); it != user.creatorAffinity.end()) {
            affinity = it->second;
        }
        f.creatorAffinity = clamp01(static_cast<double>(affinity));

        // duration_match: preferred duration = mean duration of recently completed/liked reels;
        // match = 1 - |candDuration - preferred| / durationRange (5-120 s span => range 115),
        // clamped to [0,1] (exact match => 1, opposite end => ~0). FROZEN: no usable history =>
        // neutral 0.5.
        if (hasPreferred) {
            const double diff =
                std::fabs(static_cast<double>(reel.durationSeconds) - preferredDuration);
            f.durationMatch = clamp01(1.0 - diff / kDurationRangeSeconds);
        } else {
            f.durationMatch = 0.5f;
        }

        // repetition (penalty magnitude): user-relative saturation of the recent feed. Fraction of
        // the recent-interaction window whose event shares the candidate's creator OR primary
        // topic, in [0,1]. Empty history => 0. O(recentWindow) per candidate => O(pool*window)
        // total (within the performance budget). Enters the score as -repetitionPenalty * R.
        if (user.recentInteractions.empty()) {
            f.repetition = 0.0f;
        } else {
            std::size_t matches = 0;
            for (const InteractionEvent &e : user.recentInteractions) {
                const bool sameCreator = e.creatorId == reel.creatorId;
                const bool sameTopic = inRange(e.reelId, reels_) &&
                                       reels_[e.reelId.value].primaryTopic == reel.primaryTopic;
                if (sameCreator || sameTopic) {
                    ++matches;
                }
            }
            f.repetition = clamp01(static_cast<double>(matches) /
                                   static_cast<double>(user.recentInteractions.size()));
        }

        // impression_count (fatigue penalty magnitude): global reel.impressionCount log-normalized
        // to [0,1] with the FIXED scale log1p(count)/log1p(kImpressionLogScale), clamped. NOTE:
        // TDD 14.1's "number of previous impressions" is PER-USER, but the orchestrator seen-
        // filters, so per-user previous impressions are structurally 0 in v1; the GLOBAL count is
        // the operative reading. Enters the score as -impressionPenaltyWeight * I.
        f.impressionCount = clamp01(std::log1p(static_cast<double>(reel.impressionCount)) /
                                    std::log1p(kImpressionLogScale));

        // popularity raw signal: Bayesian-smoothed engagement rate with the POOL-LOCAL prior (the
        // global engagementPriorMean is banned on the hot path). Normalized in the second pass.
        rawPopularity[i] = smoothedPopularity(reel, poolPriorMean);

        // ---- Realism V2 features (Phase 15, plan task 3): extracted only under contentV2_; gate-
        // off leaves every field at its zero default so they are never emitted and cannot perturb a
        // score (D17 byte-identity). VISIBLE-only inputs: the user's estimated modality preferences
        // and the reel's V2 attributes. Each normalization is documented at its computation.
        if (contentV2_) {
            // Modality matches: (cos(estimate, reel modality embedding) + 1) / 2, neutral 0.5 for
            // an as-yet-unlearned (empty) estimate. Documented at modalityMatch's definition.
            f.visualMatch =
                modalityMatch(user.estimatedVisualPreference, reel.visualStyleEmbedding);
            f.musicMatch = modalityMatch(user.estimatedMusicPreference, reel.musicEmbedding);
            f.emotionalMatch =
                modalityMatch(user.estimatedEmotionalPreference, reel.emotionalToneEmbedding);

            // Content-value scalars: ALREADY clamped to [0,1] at generation (reel_augmenter_v2), so
            // used as-is (the clamp is a defensive no-op, matching the V1 `quality` passthrough).
            f.clickbait = clamp01(static_cast<double>(reel.clickbaitStrength));
            f.emotionalIntensity = clamp01(static_cast<double>(reel.emotionalIntensity));
            f.usefulness = clamp01(static_cast<double>(reel.usefulness));
            f.productionQuality = clamp01(static_cast<double>(reel.productionQuality));
            f.informationDensity = clamp01(static_cast<double>(reel.informationDensity));

            // language_match: 1.0 iff the reel's language equals the user's recent-window MAJORITY
            // language (ties -> lower id, computed once above); the neutral 1.0 when there is no
            // usable history. Binary indicator in {0,1}.
            f.languageMatch = (!hasRefLanguage || reel.language.value == refLanguage) ? 1.0f : 0.0f;

            // save_popularity: DEFERRED zero placeholder (recorded Phase 15 deviation). A save/
            // comment-derived pool-local popularity refinement needs additive save/comment counters
            // on Reel, which Phase 14 did NOT add (only per-event saved/commented flags exist, and
            // those are unreachable from this pool-only extractor). savePopularityWeight defaults 0
            // so the feature is inert; it stays 0.0 until a later phase adds the Reel counters.
            f.savePopularity = 0.0f;
        }
    }

    // Second pass -- popularity: pool MIN-MAX normalization of the smoothed engagement rate (TDD
    // 14.3 "min-max over pool for pool-relative features"). (raw - min) / (max - min); a degenerate
    // pool where every value is equal (including all-zero, e.g. a cold-start pool) maps to the
    // neutral 0.5. Deterministic and O(pool). Documented.
    double minPop = rawPopularity[0];
    double maxPop = rawPopularity[0];
    for (double v : rawPopularity) {
        minPop = std::min(minPop, v);
        maxPop = std::max(maxPop, v);
    }
    const double span = maxPop - minPop;
    for (std::size_t i = 0; i < n; ++i) {
        out[i].popularity = span > 0.0 ? clamp01((rawPopularity[i] - minPop) / span) : 0.5f;
    }

    return out;
}

} // namespace rr
