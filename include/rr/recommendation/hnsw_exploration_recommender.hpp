#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "rr/candidate_sources/creator_affinity_candidate_source.hpp"
#include "rr/candidate_sources/exploration_candidate_source.hpp"
#include "rr/candidate_sources/hnsw_candidate_source.hpp"
#include "rr/candidate_sources/popular_candidate_source.hpp"
#include "rr/candidate_sources/trending_candidate_source.hpp"
#include "rr/domain/reel.hpp"
#include "rr/domain/user.hpp"
#include "rr/infrastructure/random.hpp"
#include "rr/recommendation/orchestrator.hpp"
#include "rr/recommendation/recommender.hpp"
#include "rr/recommendation/recommender_factory.hpp"
#include "rr/recommendation/vector_index.hpp"
#include "rr/recommendation/weighted_ranker.hpp"
#include "rr/vindex/hnsw_vector_index.hpp"

namespace rr {

// TDD 16.7: HNSW retrieval + the full second-stage ranker (as HNSWRankerRecommender) PLUS
// epsilon-greedy exploration. The four Phase-6 exploitation sources feed the WeightedRanker, and an
// ExplorationCandidateSource injects discovery candidates whose guaranteed feed slots are wired to
// config.exploration.guaranteedSlots through the Orchestrator (TDD 12.7 task 3). For cold-start and
// discovery evaluation.
//
// D8 CONTRACT. The recommender OWNS its forked "recommender" rng. The HNSW index seed is the FIRST
// draw of that rng — IDENTICAL to hnsw and hnsw_ranker — so all three build byte-identical graphs
// from the same master seed; the per-request epsilon gate draws come AFTER, from the same owned
// rng. Member order is therefore load-bearing: rng_ is declared BEFORE index_ so index_'s seed is
// rng_'s first draw, and before explorationSource_ which holds a non-owning pointer back to rng_.
// Source order (HNSW, popular, trending, creator, exploration) fixes the first-seen label union
// order; the exploration source is last, and the Orchestrator elects Exploration as the
// representative label whenever present.
//
// DESIGN NOTE (recorded for commit.md): FreshCandidateSource is deliberately NOT wired in ungated.
// Fresh reels enter only via the exploration source's random-fresh mode, so epsilon=0 is EXACTLY
// hnsw_ranker (the "epsilon=0 is a verified no-op" exit criterion) and epsilon is the only variable
// in the sweep. TDD 13's ungated fresh:100 merge belongs to Phase 9's FullRecommender.
class HNSWExplorationRecommender final : public Recommender {
  public:
    HNSWExplorationRecommender(const RecommenderDeps &deps, Rng rng);

    RecommendationResponse recommend(const RecommendationRequest &request) override;

    std::string name() const override;

    // Evaluation-only hook (TDD 18.1), as HNSWRankerRecommender: live Recall@K / distance error
    // stay measurable. Never touched on the feed path.
    const VectorIndex *retrievalIndex() const override;

    // Inserts appended ACTIVE reels into the HNSW graph (D2 insert-only; mirrors the other HNSW
    // recommenders).
    void onReelsAppended(size_t firstNewIndex) override;

  private:
    const std::vector<Reel> &reels_;
    const std::vector<User> &users_;
    Rng rng_; // owned; FIRST draw = index seed (D8), later draws = per-request epsilon gates
    HNSWVectorIndex index_;
    HNSWCandidateSource hnswSource_;
    PopularCandidateSource popularSource_;
    TrendingCandidateSource trendingSource_;
    CreatorAffinityCandidateSource creatorSource_;
    ExplorationCandidateSource explorationSource_;
    WeightedRanker ranker_;
    Orchestrator orchestrator_;
};

} // namespace rr
