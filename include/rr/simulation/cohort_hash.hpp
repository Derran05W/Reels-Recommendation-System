#pragma once

#include "rr/domain/ids.hpp"

namespace rr {

// Deterministic, rng-free UserId -> [0, 1) map, PINNED to the SplitMix64 finalizer (Phase 10):
// cohort membership must be stable across platforms and phases, so this exact bit pattern is
// golden-tripwire-tested (drift_scheduler_test.cpp). Promoted from a drift_scheduler.cpp local
// in Phase 14 so the niche-treasure archetype (V2 TDD 4.4: satisfaction boost only for users
// with cohortHash01(userId) within the reel's hidden [centre - width, centre + width] band) can
// reuse the SAME pinned hash — do NOT reimplement or change this mapping.
double cohortHash01(UserId userId);

} // namespace rr
