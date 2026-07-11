#pragma once

#include <cstddef>
#include <unordered_set>
#include <vector>

#include "rr/core/embedding.hpp"
#include "rr/domain/ids.hpp"
#include "rr/domain/reel.hpp"

namespace rr {

// True hidden affinity of a reel for a hidden preference (TDD 18.2): the same dot product the
// behaviour model uses as base affinity. Evaluation-only hidden-state access (D11 carve-out).
float trueAffinity(const Embedding &hiddenPreference, const Reel &reel);

// Result of one oracle comparison for a single request (TDD 19).
struct OracleResult {
    double oracleMeanAffinity = 0.0;      // mean true affinity of the oracle's top-k
    double recommendedMeanAffinity = 0.0; // mean true affinity of the recommended feed
    // regret = oracleMeanAffinity - recommendedMeanAffinity, in TRUE-AFFINITY units, not reward
    // units. Simulating counterfactual oracle interactions would consume behaviour rng draws and
    // perturb determinism (D8); affinity is the monotone core of the reward (TDD 10.1/10.5), so it
    // is the deterministic stand-in. Non-negative whenever the recommended feed is a subset of the
    // scored candidate pool with the same k.
    double regret = 0.0;
    size_t oracleK = 0;      // number of oracle items averaged (<= feedSize, <= candidates)
    size_t recommendedK = 0; // number of recommended items averaged
};

// Oracle regret for one request (TDD 19, phase-4 task 5). Exhaustively scores every ACTIVE reel
// the user has not yet seen (`seenBeforeFeed` is the seen-set snapshot BEFORE this feed is
// consumed) by true affinity, takes the top `feedSize`, and compares its mean affinity against the
// mean affinity of `recommendedFeed`. `reels` is the dense-by-id reel table; `recommendedFeed`
// holds the reel ids the recommender returned. Ties in affinity break by ascending reel id so the
// selection is deterministic (D8).
OracleResult computeOracleRegret(const Embedding &hiddenPreference, const std::vector<Reel> &reels,
                                 const std::unordered_set<ReelId> &seenBeforeFeed,
                                 const std::vector<ReelId> &recommendedFeed, size_t feedSize);

} // namespace rr
