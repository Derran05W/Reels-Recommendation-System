#pragma once

#include <vector>

#include "rr/domain/creator.hpp"        // rr::Creator, rr::Topic
#include "rr/infrastructure/config.hpp" // rr::SimulationConfig
#include "rr/infrastructure/random.hpp" // rr::Rng

namespace rr {

// Generate config.creators synthetic creators (TDD 9.4). Each creator has 1-3 distinct topic
// specialties sampled from `topics`, a styleEmbedding = normalize(sum of specialty centres +
// noise), and a baseQuality drawn from a clamped gaussian around 0.5. CreatorId values are dense
// in [0, config.creators).
//
// The passed-in Rng is consumed as-is; the CALLER forks it on stream "creators" (D8). Returns an
// empty vector when config.creators == 0. Normal use requires a non-empty `topics`; if `topics` is
// empty the function still does not crash (each creator gets an empty specialty list and a
// noise-only style embedding).
std::vector<Creator> generateCreators(const SimulationConfig &config,
                                      const std::vector<Topic> &topics, Rng &rng);

} // namespace rr
