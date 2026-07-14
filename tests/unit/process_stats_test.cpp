#include "rr/infrastructure/process_stats.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <vector>

namespace rr {
namespace {

// Sanity tests only — these are resource READERS, not performance measurements (D7/TDD 24.5:
// perf numbers live in apps/, never in unit tests).

TEST(ProcessStatsTest, RssReadersReturnPlausibleValues) {
    // A running gtest process comfortably exceeds 1 MB resident.
    EXPECT_GT(currentRssBytes(), 1u << 20);
    EXPECT_GT(peakRssBytes(), 1u << 20);
}

TEST(ProcessStatsTest, PeakRssIsMonotoneAcrossAnAllocation) {
    const uint64_t before = peakRssBytes();
    constexpr std::size_t kBytes = 8u << 20;
    std::vector<unsigned char> block(kBytes, 1);
    for (std::size_t i = 0; i < block.size(); i += 4096) {
        block[i] = static_cast<unsigned char>(i);
    }
    volatile unsigned char sink = block[block.size() - 1];
    (void)sink;
    EXPECT_GE(peakRssBytes(), before);
}

TEST(ProcessStatsTest, CpuTimesAreNonNegativeAndNondecreasing) {
    const CpuTimes before = processCpuTimes();
    EXPECT_GE(before.userSeconds, 0.0);
    EXPECT_GE(before.systemSeconds, 0.0);

    // Burn a little user CPU; rusage granularity is microseconds, so this must register.
    volatile double acc = 0.0;
    for (int i = 0; i < 20'000'000; ++i) {
        acc += static_cast<double>(i) * 1e-9;
    }
    (void)acc;

    const CpuTimes after = processCpuTimes();
    EXPECT_GE(after.userSeconds, before.userSeconds);
    EXPECT_GE(after.systemSeconds, before.systemSeconds);
    EXPECT_GT(after.userSeconds + after.systemSeconds, before.userSeconds + before.systemSeconds);
}

} // namespace
} // namespace rr
