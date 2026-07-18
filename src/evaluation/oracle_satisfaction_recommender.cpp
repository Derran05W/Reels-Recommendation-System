#include "rr/evaluation/oracle_satisfaction_recommender.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include "rr/candidate_sources/hnsw_candidate_source.hpp"
#include "rr/domain/candidate.hpp"
#include "rr/infrastructure/clock.hpp"
#include "rr/recommendation/seen_filter.hpp"
#include "rr/simulation/latent_model.hpp"
#include "rr/vindex/hnsw_vector_index.hpp"

namespace rr {

// Package B2, Phase 15 — the evaluation-only oracle-satisfaction ceiling (V2 TDD 4.4 core
// experiment, arm 4). It answers "what is the best a feed could do on HIDDEN satisfaction, drawing
// only from the SAME semantic candidate pool the hnsw arm sees?" — the upper bound no observable-
// only policy can beat.
//
// SHAPE (three-stage pipeline, mirroring HNSWRecommender + the Orchestrator so the comparison is
// apples-to-apples):
//   1. RETRIEVAL — byte-identical to the hnsw arm. One HNSWVectorIndex over every ACTIVE reel is
//      built at construction (embeddings immutable, D2); each request is answered by an
//      HNSWCandidateSource querying it with the user's effectivePreference (=
//      User.estimatedPreference, the recommender-VISIBLE estimate; semantic-only, D23) for
//      request.candidateLimit
//      (= RecommendationConfig.vectorCandidates) results. The large candidateLimit OVERSHOOTS
//      feedSize exactly as ExactVectorRecommender/HnswRecommender do, so after seen-filtering there
//      are still >= feedSize eligible reels.
//   2. FILTER — rr::isEligible (active AND not already seen), the same predicate every baseline
//      shares. A single HNSW source emits unique reel ids, so no dedup stage is needed.
//   3. RANK by EXPECTED HIDDEN SATISFACTION — the oracle's substitute for the hnsw arm's similarity
//      ordering. Each survivor is scored by the deterministic, noise-free part of the hidden latent
//      reaction: rr::computeLatentReaction with a ZERO-noise BehaviourV2Config copy
//      (behaviourV2.latentNoiseStd = 0) and a THROWAWAY rr::Rng. computeLatentReaction draws
//      EXACTLY one gaussian (its documented invariant) which, at zero noise std, is scaled to 0 and
//      affects nothing — so the score is a pure deterministic function of (user, reel) and the
//      throwaway rng may be reused across candidates and calls. The score is
//      LatentReaction.immediateSatisfaction (the exact quantity welfare metrics aggregate). Order:
//      descending satisfaction, ties broken by ascending ReelId (a total order over the unique
//      post-filter ids ⇒ fully deterministic). Take the top feedSize.
//
// D18 CARVE-OUT: this reads HiddenUserState / HiddenReelState, so it lives in evaluation/, is named
// oracle*, is constructed ONLY by the ExperimentRunner (the recommendation-side factory throws for
// RecommendationAlgorithm::OracleSatisfaction), and is barred from being a trainable/serving
// policy. It is an experiment ceiling — nothing else.
//
// DETERMINISM (D8): the constructor makes EXACTLY ONE draw from the forked "recommender" rng — the
// HNSW index seed — the first and only use of that stream, following the convention every vector
// recommender obeys. Two same-seed oracles over the same dataset build byte-identical graphs and,
// with a purely deterministic ranking stage, serve identical feeds. The throwaway rng (stage 3) is
// independent and never touches the "recommender" stream.

namespace {

// Fixed seed for the throwaway satisfaction rng. computeLatentReaction's single mandatory gaussian
// is scaled to 0 at latentNoiseStd == 0 (documented in latent_model.hpp), so this draw never
// influences a score; the constant only pins the (irrelevant) draw sequence for reproducibility.
constexpr uint64_t kOracleThrowawaySeed = 0xC0FFEEULL;
} // namespace

struct OracleSatisfactionRecommender::Impl {
    const std::vector<Reel> &reels;
    const std::vector<User> &users;
    const std::vector<Creator> &creators;
    const std::vector<HiddenUserState> &hiddenStates;
    const std::vector<HiddenReelState> &hiddenReelStates;

    // config.behaviourV2 with latentNoiseStd forced to 0 (the noise-free expected-satisfaction
    // source). Copied once at construction; never mutated afterwards.
    BehaviourV2Config zeroNoiseConfig;

    HNSWVectorIndex index;      // stage-1 retrieval graph (seeded from the single rng draw)
    HNSWCandidateSource source; // wraps `index`; declared AFTER it so index outlives the reference

    Impl(const ExperimentConfig &config, const std::vector<Reel> &reelsRef,
         const std::vector<User> &usersRef, const std::vector<Creator> &creatorsRef,
         const std::vector<HiddenUserState> &hiddenStatesRef,
         const std::vector<HiddenReelState> &hiddenReelStatesRef, uint64_t indexSeed)
        : reels(reelsRef), users(usersRef), creators(creatorsRef), hiddenStates(hiddenStatesRef),
          hiddenReelStates(hiddenReelStatesRef), zeroNoiseConfig(config.behaviourV2),
          index(config.simulation.dimensions, config.hnsw, indexSeed), source(index) {
        zeroNoiseConfig.latentNoiseStd = 0.0;
        // Build the graph once over all ACTIVE reels (mirrors HNSWRecommender exactly); insert
        // validates dimension/finiteness and throws on a bad embedding — a setup error (D10).
        for (const Reel &reel : reels) {
            if (reel.active) {
                index.insert(reel.id, reel.embedding);
            }
        }
    }

    // Expected hidden satisfaction of `reelIndex` for `userIndex`: the noise-free
    // LatentReaction.immediateSatisfaction. Deterministic in (user, reel) — the throwaway gaussian
    // is scaled to 0. Reads live hidden state (post-drift) via the held references.
    double expectedSatisfaction(uint32_t reelIndex, uint32_t userIndex, Rng &throwaway) const {
        const Reel &reel = reels[reelIndex];
        const HiddenUserState &hiddenUser = hiddenStates[userIndex];
        const HiddenReelState &hiddenReel = hiddenReelStates[reelIndex];
        const Creator &creator = creators[reel.creatorId.value];
        const LatentReaction reaction = computeLatentReaction(zeroNoiseConfig, hiddenUser, reel,
                                                              hiddenReel, creator, throwaway);
        return static_cast<double>(reaction.immediateSatisfaction);
    }
};

OracleSatisfactionRecommender::OracleSatisfactionRecommender(
    const ExperimentConfig &config, const std::vector<Reel> &reels, const std::vector<User> &users,
    const std::vector<Creator> &creators, const std::vector<HiddenUserState> &hiddenStates,
    const std::vector<HiddenReelState> &hiddenReelStates, Rng rng) {
    // Fail fast (D10 setup error): the oracle scores every candidate against per-reel and per-user
    // HIDDEN state, so it REQUIRES the V2 gates on (content_v2 + latent_reactions), under which
    // hiddenReelStates is index-aligned with reels and hiddenStates with users. Gate-off leaves the
    // hidden vectors empty (D17); reject that misconfiguration up front rather than risk an
    // out-of-bounds read on the ranking path. (appendUsers grows users+hiddenStates together, so
    // the user invariant also survives Phase-8 injection; injected reels are handled per-request.)
    if (hiddenReelStates.size() != reels.size()) {
        throw std::invalid_argument(
            "OracleSatisfactionRecommender: hiddenReelStates not aligned with reels (" +
            std::to_string(hiddenReelStates.size()) + " vs " + std::to_string(reels.size()) +
            ") — the oracle arm requires realism.content_v2 + realism.latent_reactions");
    }
    if (hiddenStates.size() != users.size()) {
        throw std::invalid_argument(
            "OracleSatisfactionRecommender: hiddenStates not aligned with users (" +
            std::to_string(hiddenStates.size()) + " vs " + std::to_string(users.size()) + ")");
    }
    // D8: the SINGLE draw from the forked "recommender" stream — the HNSW index seed. No other draw
    // is taken from `rng`.
    const uint64_t indexSeed = rng.nextU64();
    impl_ = std::make_unique<Impl>(config, reels, users, creators, hiddenStates, hiddenReelStates,
                                   indexSeed);
}

OracleSatisfactionRecommender::~OracleSatisfactionRecommender() = default;

RecommendationResponse
OracleSatisfactionRecommender::recommend(const RecommendationRequest &request) {
    Stopwatch total;
    RecommendationResponse response{};
    const User &user = impl_->users[request.userId.value];

    // --- Stage 1: retrieval, byte-identical to the hnsw arm (same index, query =
    // effectivePreference,
    //     candidateLimit overshoot, VectorHNSW label, D3 similarity). Timed as one stage.
    Stopwatch retrieval;
    const std::vector<Candidate> raw = impl_->source.generate(user, request);
    response.candidatesRetrieved = raw.size();
    response.retrievalLatencyMs = retrieval.elapsedMs();

    // --- Stages 2+3: filter to eligible reels, then rank by expected hidden satisfaction. Both run
    //     inside the ranking Stopwatch (this is where the oracle diverges from similarity
    //     ordering).
    Stopwatch ranking;
    // One throwaway rng for the whole call: computeLatentReaction draws exactly one gaussian per
    // score but it is scaled to 0 (zero noise std), so reusing the stream across candidates changes
    // nothing — every score stays a deterministic function of (user, reel).
    Rng throwaway(kOracleThrowawaySeed);

    struct Scored {
        ReelId reelId;
        double satisfaction;
    };
    std::vector<Scored> scored;
    scored.reserve(raw.size());
    for (const Candidate &candidate : raw) {
        const uint32_t reelIndex = candidate.reelId.value;
        // Guard the hidden-state bounds: a Phase-8 injected reel is appended to `reels` but NOT to
        // `hiddenReelStates` (it carries no archetype), so it cannot be scored — drop it from the
        // oracle pool. No-op when injection is off (the phase-15 experiment). Also covers the
        // orchestrator's out-of-range id guard.
        if (reelIndex >= impl_->reels.size() || reelIndex >= impl_->hiddenReelStates.size()) {
            continue;
        }
        const Reel &reel = impl_->reels[reelIndex];
        if (!isEligible(reel, user) || reel.embedding.empty()) {
            continue;
        }
        scored.push_back(Scored{candidate.reelId, impl_->expectedSatisfaction(
                                                      reelIndex, request.userId.value, throwaway)});
    }
    response.candidatesRanked = scored.size();

    // Descending expected satisfaction, ties broken by ascending ReelId (total order ⇒
    // deterministic).
    std::sort(scored.begin(), scored.end(), [](const Scored &a, const Scored &b) {
        if (a.satisfaction != b.satisfaction) {
            return a.satisfaction > b.satisfaction;
        }
        return a.reelId.value < b.reelId.value;
    });
    response.rankingLatencyMs = ranking.elapsedMs();

    // --- Reranking = identity. Measured (~0) so the field is populated like every other arm.
    Stopwatch reranking;
    response.rerankingLatencyMs = reranking.elapsedMs();

    // --- Truncate to feedSize: ranks 0..n-1, score = the ranking criterion (expected
    // satisfaction),
    //     source = VectorHNSW (the true retrieval provenance, matching the hnsw arm's labels).
    const std::size_t feedCount =
        std::min(static_cast<std::size_t>(request.feedSize), scored.size());
    response.reels.reserve(feedCount);
    for (std::size_t i = 0; i < feedCount; ++i) {
        response.reels.push_back(RankedReel{scored[i].reelId,
                                            static_cast<float>(scored[i].satisfaction),
                                            i,
                                            {CandidateSource::VectorHNSW}});
    }

    response.totalLatencyMs = total.elapsedMs();
    return response;
}

std::string OracleSatisfactionRecommender::name() const {
    return toString(RecommendationAlgorithm::OracleSatisfaction);
}

const VectorIndex *OracleSatisfactionRecommender::retrievalIndex() const { return &impl_->index; }

void OracleSatisfactionRecommender::onReelsAppended(size_t firstNewIndex) {
    // Same eligibility rule as construction: appended ACTIVE reels are indexed once, insert-only
    // (D2). Mirrors HNSWRecommender so live recall evaluation keeps working after Phase-8
    // injection. Injected reels have no hiddenReelState, so recommend() drops them from the SCORING
    // pool, but they still belong in the retrieval graph for recall measurement.
    for (size_t i = firstNewIndex; i < impl_->reels.size(); ++i) {
        const Reel &reel = impl_->reels[i];
        if (reel.active) {
            impl_->index.insert(reel.id, reel.embedding);
        }
    }
}

} // namespace rr
