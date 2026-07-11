#include "rr/evaluation/results_writer.hpp"

#include <fstream>
#include <iomanip>
#include <locale>
#include <sstream>
#include <string>

#include <nlohmann/json.hpp>

#include "rr/infrastructure/config.hpp"

namespace rr {

namespace {

// Fixed-precision, classic-locale double formatting. The classic locale makes the output
// independent of the ambient LC_NUMERIC so the deterministic CSVs are byte-identical regardless of
// the environment (D8 / TDD 24.6).
std::string num(double v, int precision = 6) {
    std::ostringstream oss;
    oss.imbue(std::locale::classic());
    oss << std::fixed << std::setprecision(precision) << v;
    return oss.str();
}

const char *kStaticEstimatesNote =
    "Baselines run with STATIC cold-start estimated preferences (TDD 11.1): every user's "
    "estimatedPreference is the global-average hidden preference and there are no online updates "
    "until Phase 7. Reported metrics therefore reflect fixed, non-personalized-per-user estimates; "
    "a flat learning curve is expected.";

const char *kRegretUnitsNote =
    "Regret is measured in TRUE-AFFINITY units, not reward units: simulating counterfactual oracle "
    "interactions would consume behaviour rng draws and perturb determinism (D8), so the oracle "
    "compares mean true affinity of its exhaustive top-k against the recommended feed. Affinity is "
    "the monotone core of the reward (TDD 10.1/10.5).";

const char *kBaselineFlagsNote =
    "Baseline requests set enableExploration=false and enableDiversity=false; candidateLimit = "
    "recommendation.vectorCandidates; requestTime = simulator logical clock; sessionId = the most "
    "recent interaction's session (SessionId{0} before any interaction).";

} // namespace

void ResultsWriter::writeConfigJson(const ExperimentResult &result) {
    // Fully-resolved config, written back out (D6). nlohmann orders object keys, so this file is
    // byte-identical across runs with the same config.
    const nlohmann::json j = result.config;
    std::ofstream out(result.directory / "config.json");
    out << j.dump(2) << "\n";
}

void ResultsWriter::writeSummaryJson(const ExperimentResult &result) {
    const MetricsSummary &m = result.overall;
    nlohmann::json j;
    j["experiment_id"] = result.experimentId;
    j["algorithm"] = toString(result.config.algorithm);
    j["seed"] = result.seed;

    j["counts"] = {{"users", result.userCount},       {"reels", result.reelCount},
                   {"requests", result.requestCount}, {"impressions", result.impressionCount},
                   {"sessions", m.sessions},          {"rounds", result.rounds.size()}};

    j["metrics"] = {{"mean_watch_ratio", m.meanWatchRatio},
                    {"mean_watch_seconds", m.meanWatchSeconds},
                    {"instant_skip_rate", m.instantSkipRate},
                    {"completion_rate", m.completionRate},
                    {"like_rate", m.likeRate},
                    {"share_rate", m.shareRate},
                    {"follow_rate", m.followRate},
                    {"mean_session_length", m.meanSessionLength},
                    {"reward_per_impression", m.rewardPerImpression},
                    {"reward_per_session", m.rewardPerSession},
                    {"mean_true_affinity", m.meanTrueAffinity}};

    j["oracle"] = {{"sample_rate", result.oracleSampleRate},
                   {"sampled_requests", result.sampledRequestCount},
                   {"mean_regret", result.meanRegret},
                   {"cumulative_regret", result.cumulativeRegret},
                   {"regret_units_note", kRegretUnitsNote}};

    j["notes"] = {{"static_estimates", kStaticEstimatesNote},
                  {"baseline_flags", kBaselineFlagsNote}};

    // Wall-clock timing is confined to this subsection + latency_metrics.csv + metadata.json; it is
    // intentionally NOT part of the determinism guarantee (D9).
    j["timing"] = {{"total_wall_seconds", result.totalWallSeconds},
                   {"recommend_latency_ms",
                    {{"p50", result.latency.p50Ms},
                     {"p95", result.latency.p95Ms},
                     {"p99", result.latency.p99Ms},
                     {"mean", result.latency.meanMs},
                     {"max", result.latency.maxMs},
                     {"samples", result.latency.samples}}}};

    std::ofstream out(result.directory / "summary.json");
    out << j.dump(2) << "\n";
}

void ResultsWriter::writeRecommendationMetricsCsv(const ExperimentResult &result) {
    std::ofstream csv(result.directory / "recommendation_metrics.csv");
    csv << "round,impressions,mean_watch_ratio,mean_watch_seconds,instant_skip_rate,"
           "completion_rate,like_rate,share_rate,follow_rate,mean_session_length,"
           "reward_per_impression,reward_per_session,mean_true_affinity\n";
    for (const RoundMetrics &r : result.rounds) {
        const MetricsSummary &m = r.metrics;
        csv << r.round << ',' << m.impressions << ',' << num(m.meanWatchRatio) << ','
            << num(m.meanWatchSeconds) << ',' << num(m.instantSkipRate) << ','
            << num(m.completionRate) << ',' << num(m.likeRate) << ',' << num(m.shareRate) << ','
            << num(m.followRate) << ',' << num(m.meanSessionLength) << ','
            << num(m.rewardPerImpression) << ',' << num(m.rewardPerSession) << ','
            << num(m.meanTrueAffinity) << '\n';
    }
}

void ResultsWriter::writeLearningCurveCsv(const ExperimentResult &result) {
    std::ofstream csv(result.directory / "learning_curve.csv");
    csv << "round,mean_reward_per_impression\n";
    for (const RoundMetrics &r : result.rounds) {
        csv << r.round << ',' << num(r.metrics.rewardPerImpression) << '\n';
    }
}

void ResultsWriter::writeRegretCurveCsv(const ExperimentResult &result) {
    std::ofstream csv(result.directory / "regret_curve.csv");
    csv << "round,sampled_requests,mean_regret,cumulative_regret\n";
    for (const RoundMetrics &r : result.rounds) {
        csv << r.round << ',' << r.sampledRequests << ',' << num(r.meanRegret) << ','
            << num(r.cumulativeRegret) << '\n';
    }
}

void ResultsWriter::writeLatencyMetricsCsv(const ExperimentResult &result) {
    // Wall-clock file: NOT part of the determinism guarantee.
    std::ofstream csv(result.directory / "latency_metrics.csv");
    csv << "recommend_p50_ms,recommend_p95_ms,recommend_p99_ms,recommend_mean_ms,recommend_max_ms,"
           "num_samples\n";
    const LatencyStats &l = result.latency;
    csv << num(l.p50Ms) << ',' << num(l.p95Ms) << ',' << num(l.p99Ms) << ',' << num(l.meanMs) << ','
        << num(l.maxMs) << ',' << l.samples << '\n';
}

void ResultsWriter::writeAll(const ExperimentResult &result, const RunMetadata &meta) {
    writeConfigJson(result);
    writeSummaryJson(result);
    writeRecommendationMetricsCsv(result);
    writeLearningCurveCsv(result);
    writeRegretCurveCsv(result);
    writeLatencyMetricsCsv(result);
    writeMetadataJson(result.directory, meta);
}

} // namespace rr
