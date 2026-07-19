// Phase 20 retention / long-term PROPERTY tests (package B, D8/D17/D22). Determinism and gate-off
// byte-identity of the package-B outputs, driven through the ExperimentRunner:
//   * same seed twice => byte-identical longterm_metrics.csv AND hidden_preference_final.csv (the
//     deterministic, timestamp-free P20 files) and bit-equal LongTermReport scalars;
//   * churn determinism: same seed => identical CHURNED user set (contract §7 / plan task 6c);
//   * gate-off (both P20 gates off): NO longterm_metrics.csv, NO hidden_preference_final.csv, the
//     welfare TRUST column stays the 0 placeholder, and two gate-off runs are byte-identical — the
//     in-process complement to the committed event-digest golden (D17 byte-identity anchor).

#include "rr/evaluation/experiment_runner.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using namespace rr;

namespace {

namespace fs = std::filesystem;

ExperimentConfig eventConfig(uint64_t seed, bool retention, bool evolution,
                             double churnThreshold = 604800.0) {
    ExperimentConfig c;
    c.simulation.seed = seed;
    c.simulation.users = 100;
    c.simulation.reels = 1000;
    c.simulation.creators = 20;
    c.simulation.topics = 8;
    c.simulation.dimensions = 16;
    c.simulation.scheduler = "event_queue";
    c.simulation.horizonSeconds = 2.0 * 86400.0; // 2 simulated days
    c.recommendation.feedSize = 10;
    c.recommendation.vectorCandidates = 100;
    c.evaluation.oracleSampleRate = 0.0;
    c.evaluation.retrievalSampleRate = 0.0;
    c.algorithm = RecommendationAlgorithm::HnswRanker;
    c.realism.contentV2 = true;
    c.realism.latentReactions = true;
    c.realism.sessionDynamics = true;
    c.realism.preferenceEvolution = evolution;
    c.retention.enabled = retention;
    c.retention.churnDelayThresholdSeconds = churnThreshold;
    c.scheduling.openStaggerSeconds = 3600.0;
    c.scheduling.returnDelayMeanSeconds = 3600.0;
    return c;
}

std::string readFile(const fs::path &p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

ExperimentResult runInto(const fs::path &root, const ExperimentConfig &cfg) {
    fs::remove_all(root);
    ExperimentRunner runner(cfg, root);
    return runner.run();
}

// The set of churned user_ids read from a run's hidden_preference_final.csv (column index 2).
std::set<uint32_t> churnedUsers(const fs::path &dir) {
    std::set<uint32_t> churned;
    std::ifstream in(dir / "hidden_preference_final.csv");
    std::string line;
    std::getline(in, line); // header
    while (std::getline(in, line)) {
        std::stringstream ss(line);
        std::string userId;
        std::string plasticity;
        std::string churnedCell;
        std::getline(ss, userId, ',');
        std::getline(ss, plasticity, ',');
        std::getline(ss, churnedCell, ',');
        if (churnedCell == "1") {
            churned.insert(static_cast<uint32_t>(std::stoul(userId)));
        }
    }
    return churned;
}

} // namespace

// ===== Determinism: same seed => byte-identical P20 files + bit-equal scalars ====================

TEST(RetentionDeterminismTest, SameSeedByteIdenticalLongtermAndExport) {
    const auto cfg = eventConfig(4242, /*retention=*/true, /*evolution=*/true);
    const fs::path a = fs::path(::testing::TempDir()) / "rr_p20_det_a";
    const fs::path b = fs::path(::testing::TempDir()) / "rr_p20_det_b";
    const ExperimentResult ra = runInto(a, cfg);
    const ExperimentResult rb = runInto(b, cfg);

    // The two deterministic, timestamp-free P20 files are byte-for-byte identical.
    EXPECT_EQ(readFile(ra.directory / "longterm_metrics.csv"),
              readFile(rb.directory / "longterm_metrics.csv"));
    EXPECT_EQ(readFile(ra.directory / "hidden_preference_final.csv"),
              readFile(rb.directory / "hidden_preference_final.csv"));

    // Run-end scalars are bit-equal (deterministic reduction, D8).
    EXPECT_EQ(ra.longTerm.retention1d, rb.longTerm.retention1d);
    EXPECT_EQ(ra.longTerm.retention7d, rb.longTerm.retention7d);
    EXPECT_EQ(ra.longTerm.churnRate, rb.longTerm.churnRate);
    EXPECT_EQ(ra.longTerm.meanChurnProbability, rb.longTerm.meanChurnProbability);
    EXPECT_EQ(ra.longTerm.meanFinalTrust, rb.longTerm.meanFinalTrust);
    EXPECT_EQ(ra.longTerm.meanFinalHabit, rb.longTerm.meanFinalHabit);
    EXPECT_EQ(ra.longTerm.meanFinalPreferenceEntropy, rb.longTerm.meanFinalPreferenceEntropy);
    EXPECT_EQ(ra.welfare.platformTrust, rb.welfare.platformTrust);
}

// ===== Churn determinism: same seed => identical churned SET =====================================

TEST(RetentionChurnDeterminismTest, SameSeedIdenticalChurnSet) {
    // A low churn threshold (2h) so a non-trivial set of users churns — determinism must hold for
    // the actual set, not just an empty one.
    const auto cfg = eventConfig(777, /*retention=*/true, /*evolution=*/false,
                                 /*churnThreshold=*/7200.0);
    const fs::path a = fs::path(::testing::TempDir()) / "rr_p20_churn_a";
    const fs::path b = fs::path(::testing::TempDir()) / "rr_p20_churn_b";
    const ExperimentResult ra = runInto(a, cfg);
    const ExperimentResult rb = runInto(b, cfg);

    const std::set<uint32_t> ca = churnedUsers(ra.directory);
    const std::set<uint32_t> cb = churnedUsers(rb.directory);
    EXPECT_EQ(ca, cb);
    EXPECT_FALSE(ca.empty()) << "the 2h threshold should churn some users (meaningful set)";
    // churn_rate matches the extracted set size.
    EXPECT_EQ(ra.longTerm.churnRate, rb.longTerm.churnRate);
    EXPECT_NEAR(ra.longTerm.churnRate, static_cast<double>(ca.size()) / ra.userCount, 1e-12);
}

// ===== Gate-off: no P20 artifacts, trust placeholder preserved, byte-identical
// =====================

TEST(RetentionGateOffTest, NoP20FilesAndTrustPlaceholderPreserved) {
    const auto cfg = eventConfig(99, /*retention=*/false, /*evolution=*/false);
    const fs::path root = fs::path(::testing::TempDir()) / "rr_p20_gateoff";
    const ExperimentResult r = runInto(root, cfg);

    EXPECT_FALSE(r.longTerm.configured);
    EXPECT_FALSE(fs::exists(r.directory / "longterm_metrics.csv"));
    EXPECT_FALSE(fs::exists(r.directory / "hidden_preference_final.csv"));
    // Welfare block still present (latent_reactions on) but trust is the untouched 0 placeholder.
    ASSERT_TRUE(r.welfare.configured);
    EXPECT_FALSE(r.welfare.trustModeled);
    EXPECT_EQ(r.welfare.platformTrust, 0.0);
}

TEST(RetentionGateOffTest, TwoGateOffRunsByteIdenticalWelfare) {
    // My welfare-trust wiring must not perturb a gate-off run: the welfare CSV (which carries the
    // trust column) is byte-identical across two same-seed gate-off runs, and its trust column is
    // all-zero. (The committed event-digest golden covers cross-commit byte-identity of the event
    // stream; this is the in-process file-level complement.)
    const auto cfg = eventConfig(31337, /*retention=*/false, /*evolution=*/false);
    const fs::path a = fs::path(::testing::TempDir()) / "rr_p20_gateoff_a";
    const fs::path b = fs::path(::testing::TempDir()) / "rr_p20_gateoff_b";
    const ExperimentResult ra = runInto(a, cfg);
    const ExperimentResult rb = runInto(b, cfg);
    const std::string wa = readFile(ra.directory / "welfare_metrics.csv");
    EXPECT_EQ(wa, readFile(rb.directory / "welfare_metrics.csv"));
    // Every trust cell (last column) is the placeholder 0.
    std::stringstream ss(wa);
    std::string line;
    std::getline(ss, line); // header
    while (std::getline(ss, line)) {
        if (line.empty()) {
            continue;
        }
        const std::string trust = line.substr(line.find_last_of(',') + 1);
        EXPECT_EQ(trust, "0.000000");
    }
}
