#include "rr/recommendation/exact_vector_recommender.hpp"

#include <algorithm>
#include <cstddef>

#include "rr/core/embedding.hpp"
#include "rr/domain/candidate.hpp"
#include "rr/recommendation/seen_filter.hpp"

namespace rr {

ExactVectorRecommender::ExactVectorRecommender(const RecommenderDeps &deps, Rng /*rng*/)
    : reels_(deps.reels), users_(deps.users), index_(deps.config.simulation.dimensions) {
    // Build the exact index once over all active reels; embeddings are immutable (D2). Insert
    // validates dimension/finiteness and throws std::invalid_argument on a bad embedding, which
    // is a setup error (D10) and correctly surfaces here in the constructor.
    for (const Reel &reel : reels_) {
        if (reel.active) {
            index_.insert(reel.id, reel.embedding);
        }
    }
}

void ExactVectorRecommender::appendEligible(const std::vector<VectorSearchResult> &results,
                                            const User &user, std::size_t feedSize,
                                            RecommendationResponse &response) const {
    for (const VectorSearchResult &result : results) {
        if (response.reels.size() >= feedSize) {
            break;
        }
        // Dense ids: the live reel for a result is reels_[id.value]. Re-check eligibility against
        // live state (a reel indexed while active may have been deactivated since; seen reels are
        // dropped here per TDD 13 item 5).
        const Reel &reel = reels_[result.reelId.value];
        if (!isEligible(reel, user)) {
            continue;
        }
        response.reels.push_back(RankedReel{result.reelId,
                                            similarityFromEuclidean(result.distance),
                                            response.reels.size(),
                                            {CandidateSource::VectorExact}});
    }
}

RecommendationResponse ExactVectorRecommender::recommend(const RecommendationRequest &request) {
    const User &user = users_[request.userId.value];
    const Embedding &query = effectivePreference(user);

    RecommendationResponse response{};
    const std::size_t indexSize = index_.size();
    const std::size_t feedSize = static_cast<std::size_t>(request.feedSize);
    if (indexSize == 0 || feedSize == 0) {
        return response;
    }

    // Over-fetch so that after dropping the user's seen reels we still have feedSize left.
    std::size_t k = feedSize + user.seenReels.size();
    if (k > indexSize) {
        k = indexSize;
    }
    std::vector<VectorSearchResult> results = index_.search(query, k);
    response.candidatesRetrieved = results.size();
    appendEligible(results, user, feedSize, response);

    // Pathological shortfall (e.g. inactive reels or seen reels crowded out the top-k window):
    // fall back to a full-index scan and re-select.
    if (response.reels.size() < feedSize && k < indexSize) {
        results = index_.search(query, indexSize);
        response.candidatesRetrieved = results.size();
        response.reels.clear();
        appendEligible(results, user, feedSize, response);
    }

    response.candidatesRanked = response.reels.size();
    return response;
}

std::string ExactVectorRecommender::name() const { return "exact_vector"; }

} // namespace rr
