// V2 metrics-pipeline integration test (Phase 15, V2 TDD §6, D22). Drives a tiny gate-ON experiment
// end-to-end through the ExperimentRunner and asserts the four-group welfare framework is emitted
// coherently: welfare_metrics.csv (per round) + welfare_archetype_metrics.csv (per archetype) exist
// with coherent rows (impressions sum to the total, rates in [0,1], archetype exposure shares sum
// to ~1), the summary.json welfare + metric_groups blocks are present with no aggregate score, the
// SAME config gated OFF emits none of the new files/keys while keeping the V1 files, and the new
// CSVs are byte-identical across two same-seed runs.
//
// Magnitudes are NOT asserted: the latent may be a neutral stub in an isolated package tree, so
// this test verifies the plumbing/coherence/determinism; the numeric signatures are the experiment
// package's concern.

#include "rr/evaluation/experiment_runner.hpp"

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace rr;

namespace {

namespace fs = std::filesystem;

// Smoke-scale config: ~100 users / 1000 reels / 2 rounds, hnsw_ranker. content_v2 +
// latent_reactions track the gate flag (latent_reactions requires content_v2, D17).
ExperimentConfig metricsConfig(bool gateOn) {
    ExperimentConfig c;
    c.simulation.seed = 20260718;
    c.simulation.users = 100;
    c.simulation.reels = 1000;
    c.simulation.creators = 20;
    c.simulation.topics = 8;
    c.simulation.dimensions = 16;
    c.simulation.interactionsPerUser = 10;
    c.recommendation.feedSize = 5; // ceil(10/5) = 2 rounds
    c.recommendation.vectorCandidates = 100;
    c.evaluation.oracleSampleRate = 0.1;
    c.algorithm = RecommendationAlgorithm::HnswRanker;
    c.realism.contentV2 = gateOn;
    c.realism.latentReactions = gateOn;
    return c;
}

std::string readFile(const fs::path &p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

nlohmann::json readJson(const fs::path &p) {
    std::ifstream in(p);
    return nlohmann::json::parse(in);
}

// Parse a CSV into (header, rows-of-cells). Trivial split on ',' (no quoted fields in our output).
struct Csv {
    std::vector<std::string> header;
    std::vector<std::vector<std::string>> rows;
};

std::vector<std::string> splitCells(const std::string &line) {
    std::vector<std::string> cells;
    std::stringstream ls(line);
    std::string cell;
    while (std::getline(ls, cell, ',')) {
        cells.push_back(cell);
    }
    return cells;
}

Csv readCsv(const fs::path &p) {
    Csv csv;
    std::ifstream in(p);
    std::string line;
    bool first = true;
    while (std::getline(in, line)) {
        if (line.empty()) {
            continue;
        }
        if (first) {
            csv.header = splitCells(line);
            first = false;
        } else {
            csv.rows.push_back(splitCells(line));
        }
    }
    return csv;
}

} // namespace

// --- Gate ON: welfare CSVs + summary blocks present and coherent ---------------------------------
TEST(V2MetricsPipelineTest, GateOnEmitsCoherentFourGroupOutput) {
    const fs::path root = fs::path(::testing::TempDir()) / "rr_v2_metrics_on";
    fs::remove_all(root);
    ExperimentRunner runner(metricsConfig(/*gateOn=*/true), root);
    const ExperimentResult r = runner.run();

    ASSERT_TRUE(r.welfare.configured);
    ASSERT_GT(r.impressionCount, 0u);

    // --- welfare_metrics.csv (per round) --------------------------------------------------------
    const fs::path welfareCsv = r.directory / "welfare_metrics.csv";
    ASSERT_TRUE(fs::exists(welfareCsv)) << "welfare_metrics.csv must exist under the gate";
    const Csv wm = readCsv(welfareCsv);
    const std::vector<std::string> expectHeader = {"round",
                                                   "impressions",
                                                   "mean_immediate_satisfaction",
                                                   "mean_regret",
                                                   "satisfaction_per_minute",
                                                   "watch_minutes",
                                                   "comment_rate",
                                                   "save_rate",
                                                   "profile_visit_rate",
                                                   "harmful_fatigue",
                                                   "platform_trust"};
    EXPECT_EQ(wm.header, expectHeader);
    EXPECT_EQ(wm.rows.size(), r.rounds.size());

    std::size_t impressionSum = 0;
    for (const std::vector<std::string> &row : wm.rows) {
        ASSERT_EQ(row.size(), expectHeader.size());
        impressionSum += static_cast<std::size_t>(std::stoull(row[1]));
        const double meanRegret = std::stod(row[3]);
        const double commentRate = std::stod(row[6]);
        const double saveRate = std::stod(row[7]);
        const double profileRate = std::stod(row[8]);
        const double harmfulFatigue = std::stod(row[9]);
        const double platformTrust = std::stod(row[10]);
        EXPECT_GE(meanRegret, 0.0);
        EXPECT_LE(meanRegret, 1.0);
        for (double rate : {commentRate, saveRate, profileRate}) {
            EXPECT_GE(rate, 0.0);
            EXPECT_LE(rate, 1.0);
        }
        // Placeholders are constant 0 (NOT-YET-MODELED; real in P16/P20).
        EXPECT_EQ(harmfulFatigue, 0.0);
        EXPECT_EQ(platformTrust, 0.0);
    }
    // Per-round impressions sum to the run's total (every impression has a latent reaction).
    EXPECT_EQ(impressionSum, r.impressionCount);

    // --- welfare_archetype_metrics.csv (per archetype) ------------------------------------------
    const fs::path archCsv = r.directory / "welfare_archetype_metrics.csv";
    ASSERT_TRUE(fs::exists(archCsv)) << "welfare_archetype_metrics.csv must exist under the gate";
    const Csv am = readCsv(archCsv);
    EXPECT_EQ(am.header, (std::vector<std::string>{"archetype_index", "archetype_name",
                                                   "impressions", "exposure_share",
                                                   "mean_immediate_satisfaction", "mean_regret"}));
    // One row per catalog archetype, in index order.
    EXPECT_EQ(am.rows.size(), r.config.realism.archetypes.size());
    std::size_t archImpressionSum = 0;
    double shareSum = 0.0;
    for (std::size_t i = 0; i < am.rows.size(); ++i) {
        const std::vector<std::string> &row = am.rows[i];
        ASSERT_EQ(row.size(), 6u);
        EXPECT_EQ(static_cast<std::size_t>(std::stoull(row[0])), i); // index order
        EXPECT_FALSE(row[1].empty());                                // resolved name
        archImpressionSum += static_cast<std::size_t>(std::stoull(row[2]));
        shareSum += std::stod(row[3]);
    }
    EXPECT_EQ(archImpressionSum, r.impressionCount);
    EXPECT_NEAR(shareSum, 1.0, 1e-4); // exposure shares partition the impressions

    // --- summary.json: welfare + metric_groups blocks, no aggregate score -----------------------
    const nlohmann::json summary = readJson(r.directory / "summary.json");
    ASSERT_TRUE(summary.contains("welfare"));
    const nlohmann::json &welfare = summary.at("welfare");
    for (const char *key :
         {"mean_immediate_satisfaction", "mean_regret", "satisfaction_per_minute", "watch_minutes",
          "harmful_fatigue", "platform_trust", "archetype_exposure", "not_yet_modeled"}) {
        EXPECT_TRUE(welfare.contains(key)) << "welfare block missing key: " << key;
    }
    EXPECT_EQ(welfare.at("archetype_exposure").size(), r.config.realism.archetypes.size());
    EXPECT_EQ(welfare.at("harmful_fatigue").get<double>(), 0.0);
    EXPECT_EQ(welfare.at("platform_trust").get<double>(), 0.0);

    ASSERT_TRUE(summary.contains("metric_groups"));
    const nlohmann::json &groups = summary.at("metric_groups");
    for (const char *g :
         {"engagement", "hidden_user_welfare", "session_health", "recommendation_quality"}) {
        EXPECT_TRUE(groups.contains(g)) << "metric_groups missing group: " << g;
    }
    // Engagement group carries the V2 additions; session-health is flagged limited pre-P16.
    EXPECT_TRUE(groups.at("engagement").contains("comment_rate"));
    EXPECT_TRUE(groups.at("engagement").contains("save_rate"));
    EXPECT_TRUE(groups.at("engagement").contains("profile_visit_rate"));
    EXPECT_EQ(groups.at("session_health").at("status").get<std::string>(), "limited_pre_p16");
    // No aggregate score is ever defined (D22).
    EXPECT_FALSE(summary.contains("aggregate_score"));
    EXPECT_FALSE(welfare.contains("aggregate_score"));
    EXPECT_FALSE(groups.contains("aggregate_score"));
}

// --- Gate OFF: none of the new files/keys, V1 files still present --------------------------------
TEST(V2MetricsPipelineTest, GateOffEmitsNoWelfareOutputButKeepsV1Files) {
    const fs::path root = fs::path(::testing::TempDir()) / "rr_v2_metrics_off";
    fs::remove_all(root);
    ExperimentRunner runner(metricsConfig(/*gateOn=*/false), root);
    const ExperimentResult r = runner.run();

    EXPECT_FALSE(r.welfare.configured);
    // No new welfare files.
    EXPECT_FALSE(fs::exists(r.directory / "welfare_metrics.csv"));
    EXPECT_FALSE(fs::exists(r.directory / "welfare_archetype_metrics.csv"));
    // No new summary keys.
    const nlohmann::json summary = readJson(r.directory / "summary.json");
    EXPECT_FALSE(summary.contains("welfare"))
        << "gate-off summary.json must not carry a welfare block (D17 byte-identity)";
    EXPECT_FALSE(summary.contains("metric_groups"));
    // The V1 output files are all still present (the four-group framework is purely additive).
    for (const char *f :
         {"recommendation_metrics.csv", "diversity_metrics.csv", "learning_curve.csv",
          "regret_curve.csv", "retrieval_metrics.csv", "summary.json", "config.json"}) {
        EXPECT_TRUE(fs::exists(r.directory / f)) << "missing V1 file: " << f;
    }
}

// --- Determinism: same seed -> byte-identical welfare CSVs
// ----------------------------------------
TEST(V2MetricsPipelineTest, WelfareCsvsAreByteIdenticalAcrossSameSeedRuns) {
    const fs::path rootA = fs::path(::testing::TempDir()) / "rr_v2_metrics_det_a";
    const fs::path rootB = fs::path(::testing::TempDir()) / "rr_v2_metrics_det_b";
    fs::remove_all(rootA);
    fs::remove_all(rootB);
    const ExperimentResult a = ExperimentRunner(metricsConfig(/*gateOn=*/true), rootA).run();
    const ExperimentResult b = ExperimentRunner(metricsConfig(/*gateOn=*/true), rootB).run();

    EXPECT_EQ(readFile(a.directory / "welfare_metrics.csv"),
              readFile(b.directory / "welfare_metrics.csv"));
    EXPECT_EQ(readFile(a.directory / "welfare_archetype_metrics.csv"),
              readFile(b.directory / "welfare_archetype_metrics.csv"));
}
