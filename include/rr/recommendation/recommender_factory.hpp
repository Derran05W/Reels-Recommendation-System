#pragma once

#include <memory>
#include <vector>

#include "rr/domain/reel.hpp"
#include "rr/domain/user.hpp"
#include "rr/infrastructure/config.hpp"
#include "rr/infrastructure/random.hpp"
#include "rr/recommendation/recommender.hpp"

namespace rr {

// Everything a baseline recommender may observe (TDD 16.1-16.3). Non-owning views into the
// experiment's dataset: `reels` counters update live as the simulation progresses (popularity
// reads them each request), `users` carries recommender-visible state only — hidden ground truth
// stays on the simulator side (D11). Both vectors are dense: element i has id value i (the
// generators construct them that way; the factory verifies). The referenced objects must outlive
// the recommender.
struct RecommenderDeps {
    const std::vector<Reel> &reels;
    const std::vector<User> &users;
    const ExperimentConfig &config;
};

// Construct the recommender for `algorithm` (TDD 16). `rng` is forked by the caller on the
// "recommender" stream (D8); the recommender owns it from here on. Phase 4 implements Random,
// Popularity, and ExactVector; the HNSW-based algorithms throw std::invalid_argument until their
// phases (5, 6, 8, 9) land.
std::unique_ptr<Recommender> makeRecommender(RecommendationAlgorithm algorithm,
                                             const RecommenderDeps &deps, Rng rng);

} // namespace rr
