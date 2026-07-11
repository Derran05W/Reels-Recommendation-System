#include "rr/evaluation/metrics_collector.hpp"

#include <gtest/gtest.h>

#include <vector>

using namespace rr;

namespace {

// A fully-specified impression; every metric-relevant field set explicitly so the aggregation math
// is unambiguous.
ImpressionSample mk(uint32_t user, uint32_t session, float watchRatio, float watchSeconds,
                    bool skip, bool completed, bool liked, bool shared, bool followed, float reward,
                    float affinity) {
    ImpressionSample s;
    s.userId = user;
    s.sessionId = session;
    s.watchRatio = watchRatio;
    s.watchSeconds = watchSeconds;
    s.instantSkip = skip;
    s.completed = completed;
    s.liked = liked;
    s.shared = shared;
    s.followed = followed;
    s.reward = reward;
    s.trueAffinity = affinity;
    return s;
}

} // namespace

// Hand-built four-impression, three-session bucket; every derived metric checked by hand.
TEST(MetricAccumulatorTest, DerivesAllMetrics) {
    MetricAccumulator acc;
    acc.add(mk(1, 0, 0.5f, 10.0f, /*skip=*/true, /*completed=*/false, false, false, false, -0.2f,
               0.1f));
    acc.add(
        mk(1, 0, 1.0f, 30.0f, false, /*completed=*/true, /*liked=*/true, false, false, 0.5f, 0.4f));
    acc.add(mk(1, 1, 0.8f, 20.0f, false, true, false, /*shared=*/true, false, 0.3f, 0.2f));
    acc.add(
        mk(2, 0, 1.2f, 40.0f, false, true, /*liked=*/true, false, /*followed=*/true, 0.7f, 0.5f));

    MetricsSummary s = acc.summary();
    EXPECT_EQ(s.impressions, 4u);
    EXPECT_EQ(s.sessions, 3u); // (1,0), (1,1), (2,0)
    EXPECT_NEAR(s.meanWatchRatio, 0.875, 1e-6);
    EXPECT_NEAR(s.meanWatchSeconds, 25.0, 1e-6);
    EXPECT_NEAR(s.instantSkipRate, 0.25, 1e-6);
    EXPECT_NEAR(s.completionRate, 0.75, 1e-6);
    EXPECT_NEAR(s.likeRate, 0.5, 1e-6);
    EXPECT_NEAR(s.shareRate, 0.25, 1e-6);
    EXPECT_NEAR(s.followRate, 0.25, 1e-6);
    EXPECT_NEAR(s.meanSessionLength, 4.0 / 3.0, 1e-6);
    EXPECT_NEAR(s.rewardPerImpression, 1.3 / 4.0, 1e-5);
    EXPECT_NEAR(s.rewardPerSession, 1.3 / 3.0, 1e-5);
    EXPECT_NEAR(s.meanTrueAffinity, 0.3, 1e-5);
}

// Empty buckets divide by nothing: everything is 0, never NaN.
TEST(MetricAccumulatorTest, EmptyBucketIsAllZero) {
    MetricAccumulator acc;
    MetricsSummary s = acc.summary();
    EXPECT_EQ(s.impressions, 0u);
    EXPECT_EQ(s.sessions, 0u);
    EXPECT_EQ(s.meanWatchRatio, 0.0);
    EXPECT_EQ(s.rewardPerSession, 0.0);
    EXPECT_EQ(s.meanTrueAffinity, 0.0);
}

// The collector routes to per-round buckets AND an overall bucket; overall is the union.
TEST(MetricsCollectorTest, RoutesRoundsAndOverall) {
    MetricsCollector c;
    c.add(0, mk(1, 0, 1.0f, 10.0f, false, true, false, false, false, 0.4f, 0.5f));
    c.add(0, mk(2, 0, 0.0f, 0.0f, true, false, false, false, false, -0.3f, -0.1f));
    c.add(1, mk(1, 0, 0.5f, 5.0f, false, true, false, false, false, 0.2f, 0.3f));

    ASSERT_EQ(c.roundCount(), 2u);
    EXPECT_EQ(c.roundSummary(0).impressions, 2u);
    EXPECT_EQ(c.roundSummary(1).impressions, 1u);
    EXPECT_NEAR(c.roundSummary(0).instantSkipRate, 0.5, 1e-6);

    MetricsSummary all = c.overall();
    EXPECT_EQ(all.impressions, 3u);
    EXPECT_NEAR(all.meanTrueAffinity, (0.5 - 0.1 + 0.3) / 3.0, 1e-5);
}

// Gaps between round indices are filled with empty buckets so indexing stays dense.
TEST(MetricsCollectorTest, FillsRoundGaps) {
    MetricsCollector c;
    c.add(3, mk(1, 0, 1.0f, 10.0f, false, true, false, false, false, 0.4f, 0.5f));
    ASSERT_EQ(c.roundCount(), 4u);
    EXPECT_EQ(c.roundSummary(0).impressions, 0u);
    EXPECT_EQ(c.roundSummary(3).impressions, 1u);
}

// Linear-interpolated percentile, matching vector-db's harness convention.
TEST(PercentileTest, LinearInterpolation) {
    std::vector<double> v = {1.0, 2.0, 3.0, 4.0, 5.0};
    EXPECT_NEAR(percentile(v, 50.0), 3.0, 1e-9);
    EXPECT_NEAR(percentile(v, 95.0), 4.8, 1e-9); // rank 3.8 -> 4*0.2 + 5*0.8
    EXPECT_NEAR(percentile(v, 0.0), 1.0, 1e-9);
    EXPECT_NEAR(percentile(v, 100.0), 5.0, 1e-9);
    EXPECT_EQ(percentile({}, 50.0), 0.0);
}

TEST(LatencyStatsTest, ComputesAggregates) {
    LatencyStats st = latencyStats({5.0, 1.0, 3.0, 2.0, 4.0}); // unsorted input
    EXPECT_EQ(st.samples, 5u);
    EXPECT_NEAR(st.p50Ms, 3.0, 1e-9);
    EXPECT_NEAR(st.meanMs, 3.0, 1e-9);
    EXPECT_NEAR(st.maxMs, 5.0, 1e-9);
}
