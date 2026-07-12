#include "rr/recommendation/hnsw_exploration_recommender.hpp"

#include <utility>

#include "rr/domain/candidate.hpp"
#include "rr/infrastructure/config.hpp"

namespace rr {

HNSWExplorationRecommender::HNSWExplorationRecommender(const RecommenderDeps &deps, Rng rng)
    : reels_(deps.reels), users_(deps.users),
      // D8: rng_ is moved from the forked "recommender" stream BEFORE index_ is initialized, so
      // index_'s seed (rng_.nextU64()) is the FIRST draw of that stream — byte-identical to hnsw
      // and hnsw_ranker. The epsilon gate draws happen later, from this same rng_.
      rng_(std::move(rng)),
      index_(deps.config.simulation.dimensions, deps.config.hnsw, rng_.nextU64()),
      hnswSource_(index_), popularSource_(reels_, deps.config.recommendation.popularCandidates),
      trendingSource_(reels_, deps.config.recommendation.trendingCandidates,
                      deps.config.ranking.trendingHalfLifeSeconds),
      creatorSource_(reels_, deps.config.recommendation.creatorAffinityCandidates),
      explorationSource_(reels_, deps.config.exploration.epsilon,
                         deps.config.recommendation.explorationCandidates,
                         deps.config.exploration.freshWindowSeconds, &rng_),
      ranker_(reels_, deps.config.ranking),
      orchestrator_(
          {&hnswSource_, &popularSource_, &trendingSource_, &creatorSource_, &explorationSource_},
          reels_, &ranker_, &explorationSource_, deps.config.exploration.guaranteedSlots) {
    // Build the graph once over all active reels; embeddings are immutable (D2). Mirrors the other
    // HNSW recommenders exactly.
    for (const Reel &reel : reels_) {
        if (reel.active) {
            index_.insert(reel.id, reel.embedding);
        }
    }
}

RecommendationResponse HNSWExplorationRecommender::recommend(const RecommendationRequest &request) {
    const User &user = users_[request.userId.value];
    return orchestrator_.recommend(user, request);
}

std::string HNSWExplorationRecommender::name() const {
    return toString(RecommendationAlgorithm::HnswRankerExploration);
}

const VectorIndex *HNSWExplorationRecommender::retrievalIndex() const { return &index_; }

void HNSWExplorationRecommender::onReelsAppended(size_t firstNewIndex) {
    // Same eligibility rule as the constructor: appended reels are indexed once, insert-only (D2).
    for (size_t i = firstNewIndex; i < reels_.size(); ++i) {
        const Reel &reel = reels_[i];
        if (reel.active) {
            index_.insert(reel.id, reel.embedding);
        }
    }
}

} // namespace rr
