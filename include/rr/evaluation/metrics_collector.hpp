#pragma once

#include <cstddef>
#include <cstdint>
#include <unordered_set>
#include <vector>

namespace rr {

// One shown reel, reduced to the fields the metrics need. Fed to the collector by the
// ExperimentRunner (built from a StepResult plus the evaluation-side true affinity). Kept
// domain-type-free so the aggregation math can be unit-tested with hand-built samples.
struct ImpressionSample {
    float watchRatio = 0.0f;
    float watchSeconds = 0.0f;
    bool instantSkip = false;
    bool completed = false;
    bool liked = false;
    bool shared = false;
    bool followed = false;
    float reward = 0.0f;
    // Evaluation-only hidden-state read (TDD 18.2): dot(hidden.hiddenPreference, reel.embedding).
    float trueAffinity = 0.0f;
    // Raw id values; the (userId, sessionId) pair identifies a session for session-scoped metrics
    // (SessionId is per-user, so both components are required to distinguish sessions globally).
    uint32_t userId = 0;
    uint32_t sessionId = 0;
};

// The §18.3 behaviour metrics plus §18.2 mean true affinity, aggregated over a set of impressions
// (one round, or the whole run). Rates are in [0, 1]; means are unweighted per-impression means.
struct MetricsSummary {
    size_t impressions = 0;
    size_t sessions = 0;
    double meanWatchRatio = 0.0;
    double meanWatchSeconds = 0.0;
    double instantSkipRate = 0.0;
    double completionRate = 0.0;
    double likeRate = 0.0;
    double shareRate = 0.0;
    double followRate = 0.0;
    double meanSessionLength = 0.0;   // impressions / distinct sessions
    double rewardPerImpression = 0.0; // reward sum / impressions
    double rewardPerSession = 0.0;    // reward sum / distinct sessions
    double meanTrueAffinity = 0.0;    // §18.2 evaluation-only
};

// Accumulates raw sums for one bucket of impressions and derives a MetricsSummary on demand. All
// arithmetic is in double; division guards against empty buckets (returns 0, never NaN).
class MetricAccumulator {
  public:
    void add(const ImpressionSample &s);
    MetricsSummary summary() const;
    size_t impressions() const { return impressions_; }
    size_t sessions() const { return sessions_.size(); }

  private:
    size_t impressions_ = 0;
    double watchRatioSum_ = 0.0;
    double watchSecondsSum_ = 0.0;
    double rewardSum_ = 0.0;
    double trueAffinitySum_ = 0.0;
    size_t skipCount_ = 0;
    size_t completeCount_ = 0;
    size_t likeCount_ = 0;
    size_t shareCount_ = 0;
    size_t followCount_ = 0;
    // (userId << 32) | sessionId — SessionId is only unique within a user.
    std::unordered_set<uint64_t> sessions_;
};

// Fans impressions into per-round accumulators and one overall accumulator. Rounds are addressed
// by index; gaps are filled with empty accumulators as needed.
class MetricsCollector {
  public:
    void add(size_t round, const ImpressionSample &s);

    size_t roundCount() const { return rounds_.size(); }
    MetricsSummary roundSummary(size_t round) const { return rounds_.at(round).summary(); }
    MetricsSummary overall() const { return overall_.summary(); }

  private:
    std::vector<MetricAccumulator> rounds_;
    MetricAccumulator overall_;
};

// Wall-clock latency aggregates for the timed recommend() calls (D9: wall clock confined to
// latency reporting). Percentiles use the sorted-sample method from vector-db's harness.
struct LatencyStats {
    double p50Ms = 0.0;
    double p95Ms = 0.0;
    double p99Ms = 0.0;
    double meanMs = 0.0;
    double maxMs = 0.0;
    size_t samples = 0;
};

// Linear-interpolated percentile of a sample set (p in [0, 100]). Sorts a copy; empty -> 0.
double percentile(std::vector<double> values, double p);

// p50/p95/p99/mean/max over the given latency samples (milliseconds).
LatencyStats latencyStats(std::vector<double> samplesMs);

} // namespace rr
