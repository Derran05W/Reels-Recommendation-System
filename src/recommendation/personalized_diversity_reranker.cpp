#include "rr/recommendation/personalized_diversity_reranker.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "rr/domain/candidate.hpp"
#include "rr/domain/ids.hpp"
#include "rr/recommendation/constraint_reranker.hpp"
#include "rr/recommendation/mmr_reranker.hpp"

namespace rr {

namespace {

// Piecewise-linear cap scale through (0, capScaleMin), (0.5, 1.0), (1, capScaleMax). CONTRACT: a
// neutral driver (0.5) returns EXACTLY 1.0, so a neutral-estimate user gets the fixed cap; higher
// drivers loosen up to capScaleMax, lower drivers tighten down to capScaleMin. `driver` is a
// tolerance-like value in [0,1] (clamped defensively).
double capScale(double driver, double capScaleMin, double capScaleMax) {
    const double t = std::clamp(driver, 0.0, 1.0);
    if (t >= 0.5) {
        return 1.0 + ((t - 0.5) / 0.5) * (capScaleMax - 1.0);
    }
    return 1.0 + ((t - 0.5) / 0.5) * (1.0 - capScaleMin); // (t-0.5)<0 => below 1.0, hits min at t=0
}

// Scale a base cap by the driver and clamp to [1, feedSize]. round() keeps neutral EXACT: at
// driver 0.5 the scale is 1.0 so round(base * 1.0) == base (base is a whole number).
std::uint32_t effectiveCap(std::uint32_t base, double driver, double capScaleMin,
                           double capScaleMax, std::size_t feedSize) {
    const double scaled =
        std::round(static_cast<double>(base) * capScale(driver, capScaleMin, capScaleMax));
    long v = static_cast<long>(scaled);
    if (v < 1) {
        v = 1;
    }
    if (static_cast<std::size_t>(v) > feedSize) {
        v = static_cast<long>(feedSize);
    }
    return static_cast<std::uint32_t>(v);
}

// Per-user MMR lambda: interpolate config.mmrLambda toward the personalized band with the estimated
// novelty tolerance. novTol 0.5 => exactly mmrLambda (continuity with fixed); novTol -> 1 =>
// lambdaMin (MORE diversity); novTol -> 0 => lambdaMax (more raw relevance). Monotone decreasing in
// novTol; anchored so the neutral point is continuous with the fixed reranker.
double personalizedLambda(double novTol, double mmrLambda, double lambdaMin, double lambdaMax) {
    const double t = std::clamp(novTol, 0.0, 1.0);
    if (t >= 0.5) {
        return mmrLambda + ((t - 0.5) / 0.5) * (lambdaMin - mmrLambda);
    }
    return mmrLambda + ((0.5 - t) / 0.5) * (lambdaMax - mmrLambda);
}

float fatigueOf(const std::unordered_map<TopicId, float> &m, TopicId key) {
    const auto it = m.find(key);
    return it == m.end() ? 0.0f : it->second;
}

bool neutralEstimates(const User &user) {
    // A user with no accumulated tolerance evidence: exactly-neutral scalar estimates and empty
    // fatigue maps (the estimator prunes decayed entries, so "no fatigue" means empty). Such a user
    // takes the fixed fast-path and is byte-identical to fixed diversity.
    return user.estimatedRepetitionTolerance == 0.5f && user.estimatedNoveltyTolerance == 0.5f &&
           user.estimatedTopicFatigue.empty() && user.estimatedCreatorFatigue.empty();
}

// A RankedReel from a selected candidate at a given rank — the exact emission ConstraintReranker /
// MMRReranker use (score == rankingScore, single representative source, contributions moved).
RankedReel toRanked(Candidate &c, std::size_t rank) {
    RankedReel r{};
    r.reelId = c.reelId;
    r.score = c.rankingScore;
    r.rank = rank;
    r.sources = {c.source};
    r.featureContributions = std::move(c.featureContributions);
    return r;
}

} // namespace

PersonalizedDiversityReranker::PersonalizedDiversityReranker(const std::vector<Reel> &reels,
                                                             const DiversityConfig &config)
    : reels_(reels), config_(config), fixed_(reels, config) {}

std::vector<RankedReel> PersonalizedDiversityReranker::rerank(const User &user,
                                                              const std::vector<Candidate> &ranked,
                                                              std::size_t feedSize) const {
    // NEUTRAL-EQUIVALENCE FAST-PATH (the regression contract): a user with no tolerance evidence
    // gets fixed diversity byte-for-byte.
    if (feedSize == 0 || neutralEstimates(user)) {
        return fixed_.rerank(user, ranked, feedSize);
    }

    // --- Per-user effective caps and lambda from the ESTIMATES (documented in the header). -------
    const double repTol = static_cast<double>(user.estimatedRepetitionTolerance);
    double maxCreatorFatigue = 0.0;
    for (const auto &[creator, f] : user.estimatedCreatorFatigue) {
        maxCreatorFatigue = std::max(maxCreatorFatigue, static_cast<double>(f));
    }
    const double creatorDriver = repTol * (1.0 - std::clamp(maxCreatorFatigue, 0.0, 1.0));

    const std::uint32_t maxPerTopicEff =
        effectiveCap(config_.maxPerTopic, repTol, config_.personalizedCapScaleMin,
                     config_.personalizedCapScaleMax, feedSize);
    const std::uint32_t maxPerCreatorEff =
        effectiveCap(config_.maxPerCreator, creatorDriver, config_.personalizedCapScaleMin,
                     config_.personalizedCapScaleMax, feedSize);
    // Base per-topic cap uses the SAME scaled-ceiling rule as the fixed reranker (topicCap()),
    // applied to the personalized maxPerTopicEff.
    const std::size_t baseTopicCap = ConstraintReranker::topicCap(maxPerTopicEff, feedSize);

    // Per-topic effective cap: tighten a topic the user is estimated-fatigued of toward 1 slot.
    auto topicCapFor = [&](TopicId t) -> std::size_t {
        const double fatigue = static_cast<double>(fatigueOf(user.estimatedTopicFatigue, t));
        const double reduced = std::round(static_cast<double>(baseTopicCap) * (1.0 - fatigue));
        const std::size_t cap = reduced < 1.0 ? 1 : static_cast<std::size_t>(reduced);
        return cap;
    };

    // --- Hard-rule greedy walk in relevance order, mirroring ConstraintReranker::selectFeed (same
    // no-dup / no-seen / out-of-range handling) but with the per-user effective caps. Caps are
    // HARD: no relax/backfill, so the feed is shorter than feedSize only when no remaining
    // candidate is addable (the honest short-feed behaviour, reported per cohort in the property
    // suite). --------
    std::vector<Candidate> feed;
    feed.reserve(std::min(feedSize, ranked.size()));
    std::unordered_set<ReelId> chosen;
    std::unordered_map<CreatorId, std::uint32_t> creatorCounts;
    std::unordered_map<TopicId, std::size_t> topicCounts;
    for (const Candidate &c : ranked) {
        if (feed.size() >= feedSize) {
            break;
        }
        const ReelId id = c.reelId;
        if (id.value >= reels_.size()) {
            continue; // out-of-range: ineligible (belt-and-braces, as ConstraintReranker)
        }
        if (chosen.contains(id) || user.seenReels.contains(id)) {
            continue; // no duplicate, no already-seen reel (universal hard rules)
        }
        const Reel &reel = reels_[id.value];
        if (creatorCounts[reel.creatorId] >= maxPerCreatorEff) {
            continue;
        }
        if (topicCounts[reel.primaryTopic] >= topicCapFor(reel.primaryTopic)) {
            continue;
        }
        chosen.insert(id);
        ++creatorCounts[reel.creatorId];
        ++topicCounts[reel.primaryTopic];
        feed.push_back(c);
    }

    // --- Ordering (mirrors DiversityReranker's composition).
    // --------------------------------------
    if (config_.useMmr) {
        // MMR orders the selected SET with the PER-USER lambda; feedSize = set size (the set is
        // already cap-compliant, MMR only permutes it). MMRReranker is cheap to construct per call.
        const double lambdaEff = personalizedLambda(
            static_cast<double>(user.estimatedNoveltyTolerance), config_.mmrLambda,
            config_.personalizedLambdaMin, config_.personalizedLambdaMax);
        const MMRReranker mmr(reels_, lambdaEff);
        return mmr.rerank(user, feed, feed.size());
    }

    // Constraints-only: the consecutive-same-topic swap pass (mirrors ConstraintReranker::rerank),
    // then emit RankedReels. Purely cosmetic — the SET is unchanged; provably terminating (each i
    // visited once, at most one swap). Reel ids are in range (walk dropped out-of-range
    // candidates).
    auto topicOf = [this](const Candidate &c) { return reels_[c.reelId.value].primaryTopic; };
    for (std::size_t i = 0; i + 1 < feed.size(); ++i) {
        const TopicId t = topicOf(feed[i]);
        if (topicOf(feed[i + 1]) != t) {
            continue;
        }
        for (std::size_t j = i + 2; j < feed.size(); ++j) {
            if (topicOf(feed[j]) != t) {
                std::swap(feed[i + 1], feed[j]);
                break;
            }
        }
    }

    std::vector<RankedReel> out;
    out.reserve(feed.size());
    for (std::size_t i = 0; i < feed.size(); ++i) {
        out.push_back(toRanked(feed[i], i));
    }
    return out;
}

} // namespace rr
