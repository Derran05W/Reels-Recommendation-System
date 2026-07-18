#include "rr/evaluation/metrics_collector.hpp"

#include <algorithm>
#include <cmath>

namespace rr {

namespace {

// Combine a per-user session id into a globally unique key (SessionId is only unique within a
// user, so both components are needed to count distinct sessions across users).
uint64_t sessionKey(uint32_t userId, uint32_t sessionId) {
    return (static_cast<uint64_t>(userId) << 32) | static_cast<uint64_t>(sessionId);
}

} // namespace

void MetricAccumulator::add(const ImpressionSample &s) {
    ++impressions_;
    watchRatioSum_ += static_cast<double>(s.watchRatio);
    watchSecondsSum_ += static_cast<double>(s.watchSeconds);
    rewardSum_ += static_cast<double>(s.reward);
    trueAffinitySum_ += static_cast<double>(s.trueAffinity);
    skipCount_ += s.instantSkip ? 1u : 0u;
    completeCount_ += s.completed ? 1u : 0u;
    likeCount_ += s.liked ? 1u : 0u;
    shareCount_ += s.shared ? 1u : 0u;
    followCount_ += s.followed ? 1u : 0u;
    commentCount_ += s.commented ? 1u : 0u;
    saveCount_ += s.saved ? 1u : 0u;
    profileVisitCount_ += s.profileVisited ? 1u : 0u;
    sessions_.insert(sessionKey(s.userId, s.sessionId));
}

MetricsSummary MetricAccumulator::summary() const {
    MetricsSummary s;
    s.impressions = impressions_;
    s.sessions = sessions_.size();
    if (impressions_ > 0) {
        const double n = static_cast<double>(impressions_);
        s.meanWatchRatio = watchRatioSum_ / n;
        s.meanWatchSeconds = watchSecondsSum_ / n;
        s.instantSkipRate = static_cast<double>(skipCount_) / n;
        s.completionRate = static_cast<double>(completeCount_) / n;
        s.likeRate = static_cast<double>(likeCount_) / n;
        s.shareRate = static_cast<double>(shareCount_) / n;
        s.followRate = static_cast<double>(followCount_) / n;
        s.commentRate = static_cast<double>(commentCount_) / n;
        s.saveRate = static_cast<double>(saveCount_) / n;
        s.profileVisitRate = static_cast<double>(profileVisitCount_) / n;
        s.rewardPerImpression = rewardSum_ / n;
        s.meanTrueAffinity = trueAffinitySum_ / n;
    }
    if (!sessions_.empty()) {
        const double m = static_cast<double>(sessions_.size());
        s.meanSessionLength = static_cast<double>(impressions_) / m;
        s.rewardPerSession = rewardSum_ / m;
    }
    return s;
}

void MetricsCollector::add(size_t round, const ImpressionSample &s) {
    if (round >= rounds_.size()) {
        rounds_.resize(round + 1);
    }
    rounds_[round].add(s);
    overall_.add(s);
}

double percentile(std::vector<double> values, double p) {
    if (values.empty()) {
        return 0.0;
    }
    std::sort(values.begin(), values.end());
    const double rank = (p / 100.0) * static_cast<double>(values.size() - 1);
    const size_t lo = static_cast<size_t>(std::floor(rank));
    const size_t hi = static_cast<size_t>(std::ceil(rank));
    if (lo == hi) {
        return values[lo];
    }
    const double frac = rank - static_cast<double>(lo);
    return values[lo] * (1.0 - frac) + values[hi] * frac;
}

LatencyStats latencyStats(std::vector<double> samplesMs) {
    LatencyStats st;
    st.samples = samplesMs.size();
    if (samplesMs.empty()) {
        return st;
    }
    double sum = 0.0;
    double maxV = samplesMs.front();
    for (double v : samplesMs) {
        sum += v;
        maxV = std::max(maxV, v);
    }
    st.meanMs = sum / static_cast<double>(samplesMs.size());
    st.maxMs = maxV;
    st.p50Ms = percentile(samplesMs, 50.0);
    st.p95Ms = percentile(samplesMs, 95.0);
    st.p99Ms = percentile(samplesMs, 99.0);
    return st;
}

} // namespace rr
