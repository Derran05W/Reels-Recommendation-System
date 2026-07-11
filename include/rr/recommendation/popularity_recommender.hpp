#pragma once

#include <string>
#include <vector>

#include "rr/domain/recommendation.hpp"
#include "rr/domain/reel.hpp"
#include "rr/domain/user.hpp"
#include "rr/infrastructure/random.hpp"
#include "rr/recommendation/recommender.hpp"
#include "rr/recommendation/recommender_factory.hpp"

namespace rr {

// TDD 16.2 + 12.3 - the non-personalized baseline. Scores every eligible reel with a
// Bayesian-smoothed engagement rate and ranks by descending score (ties by ascending ReelId).
// Counters are read live from deps.reels at every request, so the ranking tracks the simulation.
class PopularityRecommender final : public Recommender {
  public:
    // Number of pseudo-impressions of Bayesian smoothing (TDD 12.3 "prevent tiny-sample videos
    // from dominating"). Every reel is mixed with this many impressions of the current global
    // mean engagement rate, so a 1-impression fluke is pulled hard toward the prior.
    static constexpr double kSmoothingPseudoImpressions = 20.0;

    PopularityRecommender(const RecommenderDeps &deps, Rng rng);

    RecommendationResponse recommend(const RecommendationRequest &request) override;

    std::string name() const override;

  private:
    const std::vector<Reel> &reels_;
    const std::vector<User> &users_;
};

// Raw engagement numerator completionCount + 2*likeCount + 4*shareCount (TDD 12.3). Free function
// so the smoothing math can be unit-tested against hand-computed values.
double popularityEngagement(const Reel &reel);

// Bayesian-smoothed popularity: (engagement + C*priorMean) / (1 + impressionCount + C), where C
// is `pseudoImpressions` and `priorMean` is the global mean engagement rate for this request.
double
smoothedPopularity(const Reel &reel, double priorMean,
                   double pseudoImpressions = PopularityRecommender::kSmoothingPseudoImpressions);

} // namespace rr
