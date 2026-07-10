#pragma once

#include <vector>

#include "rr/domain/creator.hpp"        // rr::Topic
#include "rr/infrastructure/config.hpp" // rr::SimulationConfig
#include "rr/infrastructure/random.hpp" // rr::Rng

namespace rr {

// Generate config.topics synthetic topics (TDD 9.1). Each topic is a random, L2-normalized
// Embedding of config.dimensions components with a dense TopicId value in [0, config.topics).
//
// The passed-in Rng is consumed as-is; the CALLER is responsible for forking it on stream
// "topics" (design decision D8) so regenerating one subsystem never perturbs another. Returns an
// empty vector when config.topics == 0.
std::vector<Topic> generateTopics(const SimulationConfig &config, Rng &rng);

} // namespace rr
