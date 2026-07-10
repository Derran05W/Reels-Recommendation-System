#pragma once

#include <vector>

#include "rr/domain/creator.hpp"        // rr::Creator, rr::Topic
#include "rr/domain/reel.hpp"           // rr::Reel
#include "rr/infrastructure/config.hpp" // rr::SimulationConfig
#include "rr/infrastructure/random.hpp" // rr::Rng

namespace rr {

// Generate config.reels synthetic reels (TDD 9.2). Each reel picks a creator uniformly, a primary
// topic biased toward that creator's specialties (enabling creator-affinity experiments, TDD 9.4),
// and optionally a secondary topic. The embedding is
//   normalize(w1*t1.centre + w2*t2.centre + ws*creator.styleEmbedding + noise),
// and reel.primaryTopic / reel.secondaryTopics are set to exactly the topic ids used to build it.
// Duration follows the 4-bucket distribution of TDD 9.2, quality is a clamped gaussian around the
// creator's baseQuality, createdAt is spread over a fixed window, and all engagement counters are
// zeroed. ReelId values are dense in [0, config.reels).
//
// The passed-in Rng is consumed as-is; the CALLER forks it on stream "reels" (D8). Returns an
// empty vector when config.reels == 0, or (documented no-crash fallback) when `topics` or
// `creators` is empty, since a reel cannot be constructed without both.
std::vector<Reel> generateReels(const SimulationConfig &config, const std::vector<Topic> &topics,
                                const std::vector<Creator> &creators, Rng &rng);

} // namespace rr
