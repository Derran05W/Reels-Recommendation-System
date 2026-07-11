#include "rr/recommendation/random_recommender.hpp"

#include <algorithm>
#include <cstddef>
#include <utility>

#include "rr/recommendation/seen_filter.hpp"

namespace rr {

RandomRecommender::RandomRecommender(const RecommenderDeps &deps, Rng rng)
    : reels_(deps.reels), users_(deps.users), rng_(std::move(rng)) {}

RecommendationResponse RandomRecommender::recommend(const RecommendationRequest &request) {
    const User &user = users_[request.userId.value];

    // Base pool: every unseen+active reel, in ascending ReelId order (deterministic given the
    // dataset). A partial Fisher-Yates shuffle then selects a uniform sample without replacement.
    std::vector<std::size_t> pool = eligibleReelIndices(reels_, user);
    const std::size_t n = pool.size();
    const std::size_t k = std::min(static_cast<std::size_t>(request.feedSize), n);

    RecommendationResponse response{};
    response.candidatesRetrieved = n;
    response.reels.reserve(k);

    // Draw order (documented for reproducibility): exactly k draws. For output slot i in [0, k),
    // draw j = i + rng.uniformInt(n - i) - a uniform index into the still-unshuffled suffix
    // pool[i, n) - and swap pool[i] <-> pool[j]. pool[i] is then the reel placed at rank i.
    for (std::size_t i = 0; i < k; ++i) {
        const std::size_t j = i + static_cast<std::size_t>(rng_.uniformInt(n - i));
        std::swap(pool[i], pool[j]);
        const Reel &reel = reels_[pool[i]];
        response.reels.push_back(RankedReel{reel.id, 0.0f, i, {}});
    }
    response.candidatesRanked = response.reels.size();
    return response;
}

std::string RandomRecommender::name() const { return "random"; }

} // namespace rr
