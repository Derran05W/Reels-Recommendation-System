#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "rr/core/embedding.hpp"
#include "rr/domain/recommendation.hpp"
#include "rr/domain/reel.hpp"
#include "rr/domain/user.hpp"
#include "rr/infrastructure/random.hpp"
#include "rr/recommendation/recommender.hpp"
#include "rr/recommendation/recommender_factory.hpp"
#include "rr/recommendation/vector_index.hpp"
#include "rr/vindex/exact_vector_index.hpp"

namespace rr {

// The query vector a recommender scores a user against. At Phase 4 this is simply the cold-start
// static estimate (TDD 11.1) - there are no online updates yet. Phase 7 will replace the body
// with the TDD 8.3 blend longTermWeight*longTermPreference + sessionWeight*sessionPreference;
// every personalizing recommender routes through this one helper so that upgrade is a single
// edit.
inline const Embedding &effectivePreference(const User &user) { return user.estimatedPreference; }

// TDD 16.3 - the personalization ceiling. Brute-force exact nearest-neighbour retrieval over the
// user's effective preference. The index is built once over all active reels in the constructor
// (embeddings are immutable, D2) and reused for every request. Ascending Euclidean distance over
// unit vectors is exactly descending cosine similarity (D3).
class ExactVectorRecommender final : public Recommender {
  public:
    ExactVectorRecommender(const RecommenderDeps &deps, Rng rng);

    RecommendationResponse recommend(const RecommendationRequest &request) override;

    std::string name() const override;

  private:
    // Walk `results` in ascending-distance order, emit the eligible ones (active + unseen) as
    // ranked reels until feedSize is reached, assigning rank 0..n-1 and score = cosine similarity.
    void appendEligible(const std::vector<VectorSearchResult> &results, const User &user,
                        std::size_t feedSize, RecommendationResponse &response) const;

    const std::vector<Reel> &reels_;
    const std::vector<User> &users_;
    ExactVectorIndex index_;
};

} // namespace rr
