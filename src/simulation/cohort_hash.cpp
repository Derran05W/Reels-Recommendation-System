#include "rr/simulation/cohort_hash.hpp"

#include <cstdint>

namespace rr {

// PINNED SplitMix64-finalizer hash (Phase 10; see cohort_hash.hpp). The body is exactly
// rr::splitmix64(userId.value) (the same finalizer rr::Rng seeds with) followed by the canonical
// 53-bit-mantissa scaling; it is inlined here rather than calling infrastructure/random so the
// golden values are self-contained and cannot drift if that helper is ever refactored. DO NOT
// change these constants — cohort membership is a cross-package contract (drift cohorts split
// "drifted vs control" on it; niche-treasure reels gate satisfaction on it).
double cohortHash01(UserId userId) {
    uint64_t x = static_cast<uint64_t>(userId.value) + 0x9E3779B97F4A7C15ULL;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
    x ^= x >> 31;
    return static_cast<double>(x >> 11) * 0x1.0p-53; // [0, 1)
}

} // namespace rr
