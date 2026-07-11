#include "rr/evaluation/experiment_runner.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace rr;

namespace {

namespace fs = std::filesystem;

// Tiny but non-degenerate experiment: enough users/reels to exercise sessions, multiple rounds,
// and oracle sampling, while finishing in milliseconds.
ExperimentConfig tinyConfig(RecommendationAlgorithm algo) {
    ExperimentConfig c;
    c.simulation.seed = 7;
    c.simulation.users = 30;
    c.simulation.reels = 300;
    c.simulation.creators = 10;
    c.simulation.topics = 8;
    c.simulation.dimensions = 32;
    c.simulation.interactionsPerUser = 10;
    c.recommendation.feedSize = 5; // -> ceil(10/5) = 2 rounds
    c.recommendation.vectorCandidates = 100;
    c.evaluation.oracleSampleRate = 0.5;
    c.algorithm = algo;
    return c;
}

std::string readFile(const fs::path &p) {
    std::ifstream f(p, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

std::vector<std::string> splitCsv(const std::string &line) {
    std::vector<std::string> fields;
    std::stringstream ss(line);
    std::string field;
    while (std::getline(ss, field, ',')) {
        fields.push_back(field);
    }
    return fields;
}

// Data rows (excluding the header) of a CSV file.
std::vector<std::vector<std::string>> readCsvRows(const fs::path &p) {
    std::ifstream f(p);
    std::string line;
    std::vector<std::vector<std::string>> rows;
    bool header = true;
    while (std::getline(f, line)) {
        if (header) {
            header = false;
            continue;
        }
        if (!line.empty()) {
            rows.push_back(splitCsv(line));
        }
    }
    return rows;
}

} // namespace

// (a) End-to-end tiny run produces every §26 file with sane contents.
TEST(ExperimentRunnerTest, ProducesAllOutputFilesWithSaneContents) {
    const fs::path root = fs::path(::testing::TempDir()) / "rr_exp_files";
    fs::remove_all(root);

    ExperimentRunner runner(tinyConfig(RecommendationAlgorithm::ExactVector), root);
    ExperimentResult result = runner.run();

    // Counts add up.
    EXPECT_EQ(result.userCount, 30u);
    EXPECT_EQ(result.reelCount, 300u);
    EXPECT_EQ(result.rounds.size(), 2u);
    EXPECT_EQ(result.requestCount, 30u * 2u); // one request per user per round
    EXPECT_EQ(result.impressionCount, 30u * 10u);

    // All §26 files exist.
    for (const char *name :
         {"config.json", "summary.json", "recommendation_metrics.csv", "learning_curve.csv",
          "regret_curve.csv", "latency_metrics.csv", "metadata.json"}) {
        EXPECT_TRUE(fs::exists(result.directory / name)) << "missing " << name;
    }

    // recommendation_metrics.csv: one row per round, rates in [0,1], reward in [-1,1], affinity
    // finite.
    auto recRows = readCsvRows(result.directory / "recommendation_metrics.csv");
    ASSERT_EQ(recRows.size(), 2u);
    for (const auto &row : recRows) {
        ASSERT_EQ(row.size(), 13u);
        for (int col : {4, 5, 6, 7, 8}) { // skip/completion/like/share/follow rates
            const double v = std::stod(row[col]);
            EXPECT_GE(v, 0.0);
            EXPECT_LE(v, 1.0);
        }
        const double rewardPerImp = std::stod(row[10]);
        EXPECT_GE(rewardPerImp, -1.0);
        EXPECT_LE(rewardPerImp, 1.0);
        EXPECT_TRUE(std::isfinite(std::stod(row[12]))); // mean_true_affinity
    }

    // learning_curve.csv and regret_curve.csv also one row per round.
    EXPECT_EQ(readCsvRows(result.directory / "learning_curve.csv").size(), 2u);
    EXPECT_EQ(readCsvRows(result.directory / "regret_curve.csv").size(), 2u);

    // latency_metrics.csv: header + exactly one aggregate row.
    EXPECT_EQ(readCsvRows(result.directory / "latency_metrics.csv").size(), 1u);

    // Overall metric ranges.
    EXPECT_GE(result.overall.completionRate, 0.0);
    EXPECT_LE(result.overall.completionRate, 1.0);
    EXPECT_TRUE(std::isfinite(result.overall.meanTrueAffinity));
    EXPECT_GT(result.sampledRequestCount, 0u); // 0.5 rate over 60 requests: overwhelmingly likely
}

// (b) Full-run determinism: same config + seed twice ⇒ byte-identical deterministic outputs.
// latency_metrics.csv, metadata.json, and summary.json's timing subsection are excluded (wall
// clock, D9/TDD 24.6).
TEST(ExperimentRunnerTest, FullRunDeterminismByteIdentical) {
    const fs::path rootA = fs::path(::testing::TempDir()) / "rr_exp_det_a";
    const fs::path rootB = fs::path(::testing::TempDir()) / "rr_exp_det_b";
    fs::remove_all(rootA);
    fs::remove_all(rootB);

    // Random exercises the recommender rng stream, the strongest determinism case.
    ExperimentRunner runnerA(tinyConfig(RecommendationAlgorithm::Random), rootA);
    ExperimentRunner runnerB(tinyConfig(RecommendationAlgorithm::Random), rootB);
    ExperimentResult a = runnerA.run();
    ExperimentResult b = runnerB.run();

    for (const char *name :
         {"config.json", "recommendation_metrics.csv", "learning_curve.csv", "regret_curve.csv"}) {
        EXPECT_EQ(readFile(a.directory / name), readFile(b.directory / name))
            << name << " differs between two same-seed runs";
    }
}
