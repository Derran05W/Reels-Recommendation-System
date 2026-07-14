#pragma once

#include <cstdint>

namespace rr {

// Process-wide resource readers for benchmark apps (Phase 11, TDD 18.7). Callable from any
// thread; values are process-wide, not per-thread. Wall-clock pairing for CPU-utilization
// computation is the caller's job (D9: wall clock only inside latency/benchmark measurement).
struct CpuTimes {
    double userSeconds = 0.0;
    double systemSeconds = 0.0;
};

// Instantaneous resident set size in bytes (0 if unavailable on this platform).
uint64_t currentRssBytes();

// Lifetime peak resident set size in bytes (0 if unavailable). Monotone over process lifetime.
uint64_t peakRssBytes();

// Cumulative process CPU time split user/system, in seconds.
CpuTimes processCpuTimes();

} // namespace rr
