#pragma once

#include <filesystem>

#include "rr/evaluation/experiment_runner.hpp"
#include "rr/evaluation/run_metadata.hpp"

namespace rr {

// Serializes an ExperimentResult to the §26 output layout in `result.directory`:
//   config.json, summary.json, retrieval_metrics.csv, recommendation_metrics.csv,
//   learning_curve.csv, regret_curve.csv, latency_metrics.csv, metadata.json.
//
// Phase 8 cold-start additions: when injection is configured (result.coldStart.configured), TWO
// extra deterministic files are written - new_user_curve.csv and new_reel_exposure.csv - and a
// `cold_start` block is added to summary.json. When injection is NOT configured, NONE of these
// appear and every file is byte-identical to a pre-Phase-8 run (the regression contract).
//
// Determinism (D8/TDD 24.6): every file EXCEPT latency_metrics.csv, metadata.json, and the
// `timing` subsection of summary.json is byte-identical across two runs with the same seed. This
// includes retrieval_metrics.csv and the two Phase-8 files: all come from deterministic
// computations. All floating-point output uses fixed precision under the classic locale so
// formatting never depends on the ambient locale.
class ResultsWriter {
  public:
    // Writes every §26 file. `meta` supplies the wall-clock/hardware provenance for metadata.json.
    static void writeAll(const ExperimentResult &result, const RunMetadata &meta);

    // Individual writers (exposed for targeted tests).
    static void writeConfigJson(const ExperimentResult &result);
    static void writeSummaryJson(const ExperimentResult &result);
    static void writeRetrievalMetricsCsv(const ExperimentResult &result);
    static void writeRecommendationMetricsCsv(const ExperimentResult &result);
    static void writeLearningCurveCsv(const ExperimentResult &result);
    static void writeRegretCurveCsv(const ExperimentResult &result);
    static void writeLatencyMetricsCsv(const ExperimentResult &result);
    // Phase 8 (TDD 18.5). Written by writeAll ONLY when result.coldStart.configured; exposed here
    // for targeted tests. new_user_curve.csv: impression_index, users_at_index, mean_reward,
    // mean_regret. new_reel_exposure.csv: round, injected_impressions, injected_impressions_cum,
    // distinct_injected_exposed_cum, share_of_round_impressions.
    static void writeNewUserCurveCsv(const ExperimentResult &result);
    static void writeNewReelExposureCsv(const ExperimentResult &result);
};

} // namespace rr
