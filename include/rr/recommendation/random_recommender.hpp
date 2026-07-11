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

// TDD 16.1 - the quality floor / exploration baseline. Returns a uniform sample without
// replacement of feedSize distinct reels drawn from the user's unseen+active pool, using the
// owned Rng. There is no "Random" CandidateSource, so RankedReel.sources is left empty and
// score 0.
class RandomRecommender final : public Recommender {
  public:
    // `rng` is forked by the caller on the "recommender" stream (D8); the recommender owns it.
    RandomRecommender(const RecommenderDeps &deps, Rng rng);

    RecommendationResponse recommend(const RecommendationRequest &request) override;

    std::string name() const override;

  private:
    const std::vector<Reel> &reels_;
    const std::vector<User> &users_;
    Rng rng_;
};

} // namespace rr
