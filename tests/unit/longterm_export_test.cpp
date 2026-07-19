// Phase 20 long-term metrics + exports END-TO-END (package B, V2 TDD §4.15-4.17/§6, D22). Drives a
// tiny full-gate event_queue run (retention + preference_evolution ON) through the ExperimentRunner
// and asserts the package-B deliverables that need the whole pipeline: the frozen `long_term`
// summary block + longterm_metrics.csv (schema + coherence), the per-user
// hidden_preference_final.csv export (frozen header, ordering, hand-computed shift math), and the
// P15 welfare TRUST column going LIVE (welfare block + welfare_metrics.csv emit mean trust, not the
// placeholder 0).
//
// In package B's worktree package A's PreferenceEvolution is a no-op stub, so no hidden preference
// MOVES: every *_shift is exactly 0 (cos(p0,p0)=1) and mean_preference_shift_from_initial is 0 — a
// deliberate, hand-computable pre-integration state. The mechanism (capture p0, export shifts) is
// what these tests pin; the integrator re-checks the shifts go non-zero once A lands.

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

// Tiny full-gate P20 event config: content_v2+latent+session (event_queue requires them) plus both
// P20 gates. 3 simulated days so retention_1d has room and there are multiple longterm rows.
ExperimentConfig p20Config(uint64_t seed = 20260720, bool retention = true, bool evolution = true) {
    ExperimentConfig c;
    c.simulation.seed = seed;
    c.simulation.users = 120;
    c.simulation.reels = 1200;
    c.simulation.creators = 24;
    c.simulation.topics = 8;
    c.simulation.dimensions = 16;
    c.simulation.scheduler = "event_queue";
    c.simulation.horizonSeconds = 3.0 * 86400.0; // 3 simulated days
    c.recommendation.feedSize = 10;
    c.recommendation.vectorCandidates = 100;
    c.evaluation.oracleSampleRate = 0.05;
    c.evaluation.retrievalSampleRate = 0.0;
    c.algorithm = RecommendationAlgorithm::HnswRanker;
    c.realism.contentV2 = true;
    c.realism.latentReactions = true;
    c.realism.sessionDynamics = true;
    c.realism.preferenceEvolution = evolution;
    c.retention.enabled = retention;
    c.scheduling.openStaggerSeconds = 3600.0;
    c.scheduling.returnDelayMeanSeconds = 3600.0;
    return c;
}

std::vector<std::string> readLines(const fs::path &p) {
    std::ifstream in(p);
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(in, line)) {
        lines.push_back(line);
    }
    return lines;
}

nlohmann::json readJson(const fs::path &p) {
    std::ifstream in(p);
    return nlohmann::json::parse(in);
}

ExperimentResult runInto(const fs::path &root, const ExperimentConfig &cfg) {
    fs::remove_all(root);
    ExperimentRunner runner(cfg, root);
    return runner.run();
}

} // namespace

// ===== long_term summary block + longterm_metrics.csv (schema + coherence) =======================

TEST(LongTermSchemaTest, SummaryBlockCarriesEveryFrozenKey) {
    const fs::path root = fs::path(::testing::TempDir()) / "rr_p20_schema";
    const ExperimentResult r = runInto(root, p20Config());
    ASSERT_TRUE(r.longTerm.configured);
    ASSERT_TRUE(r.longTerm.retentionConfigured);

    const nlohmann::json j = readJson(r.directory / "summary.json");
    ASSERT_TRUE(j.contains("long_term"));
    const nlohmann::json &lt = j["long_term"];
    for (const char *key :
         {"retention_configured", "retention_1d", "retention_7d", "sessions_per_user_per_day",
          "satisfaction_weighted_retention", "churn_rate", "mean_churn_probability",
          "mean_final_trust", "mean_final_habit", "mean_preference_shift_from_initial",
          "mean_final_preference_entropy", "note"}) {
        EXPECT_TRUE(lt.contains(key)) << "missing long_term key: " << key;
    }
    // The summary block mirrors the in-memory struct exactly.
    EXPECT_DOUBLE_EQ(lt["retention_1d"].get<double>(), r.longTerm.retention1d);
    EXPECT_DOUBLE_EQ(lt["mean_final_trust"].get<double>(), r.longTerm.meanFinalTrust);
    EXPECT_DOUBLE_EQ(lt["churn_rate"].get<double>(), r.longTerm.churnRate);
}

TEST(LongTermSchemaTest, LongtermCsvHeaderAndRowShape) {
    const fs::path root = fs::path(::testing::TempDir()) / "rr_p20_csv";
    const ExperimentResult r = runInto(root, p20Config());
    const fs::path csv = r.directory / "longterm_metrics.csv";
    ASSERT_TRUE(fs::exists(csv));
    const std::vector<std::string> lines = readLines(csv);
    ASSERT_FALSE(lines.empty());
    EXPECT_EQ(lines[0],
              "day,sessions,active_users,sessions_per_active_user,mean_session_satisfaction,mean_"
              "trust,cumulative_churned,mean_pref_shift_from_initial,mean_preference_entropy");
    // One row per simulated day (byDay), plus the header. numDays = floor(horizon/86400)+1 = 4.
    EXPECT_EQ(lines.size(), r.longTerm.byDay.size() + 1);
    EXPECT_EQ(r.longTerm.byDay.size(), 4u);
}

TEST(LongTermSchemaTest, MetricsAreInPlausibleRanges) {
    const fs::path root = fs::path(::testing::TempDir()) / "rr_p20_ranges";
    // Evolution OFF / retention ON: the static-trust, zero-shift world where the exact range
    // expectations below are provable (trust is never written -- package A only writes it under
    // the evolution gate -- so meanTrust reads the platformTrust trait; preferences never move).
    // The live-evolution ranges are covered by MetricsAreInPlausibleRangesWithEvolutionLive.
    const ExperimentResult r = runInto(root, p20Config(20260720, /*retention=*/true,
                                                       /*evolution=*/false));
    const LongTermReport &lt = r.longTerm;
    EXPECT_GE(lt.retention1d, 0.0);
    EXPECT_LE(lt.retention1d, 1.0);
    EXPECT_GT(lt.retention1d, 0.0); // healthy users return within a day at these delays
    EXPECT_GE(lt.churnRate, 0.0);
    EXPECT_LE(lt.churnRate, 1.0);
    EXPECT_GE(lt.meanChurnProbability, 0.0);
    EXPECT_LE(lt.meanChurnProbability, 1.0);
    // Evolution off in this fixture: trust is never written, so it reads platformTrust in
    // [0.4, 1.0].
    EXPECT_GE(lt.meanFinalTrust, 0.4 - 1e-9);
    EXPECT_LE(lt.meanFinalTrust, 1.0 + 1e-9);
    EXPECT_GE(lt.meanFinalHabit, 0.0);
    EXPECT_LE(lt.meanFinalHabit, 1.0);
    EXPECT_GT(lt.meanFinalHabit, 0.0); // habit strengthens from satisfying sessions
    EXPECT_GE(lt.meanFinalPreferenceEntropy, 0.0);
    // Evolution off, no drift: preferences never move, so shift is 0 up to the float noise of
    // renormalized cosines.
    EXPECT_NEAR(lt.meanPreferenceShiftFromInitial, 0.0, 1e-9);
    // Per-day cumulative churn is monotonic non-decreasing.
    uint64_t prev = 0;
    for (const LongTermDayPoint &p : lt.byDay) {
        EXPECT_GE(p.cumulativeChurned, prev);
        prev = p.cumulativeChurned;
    }
}

TEST(LongTermSchemaTest, MetricsAreInPlausibleRangesWithEvolutionLive) {
    const fs::path root = fs::path(::testing::TempDir()) / "rr_p20_ranges_live";
    // Both gates ON: package A's evolution writes trust (erosion outpaces recovery under regretful
    // exposure -- observed meanFinalTrust ~0.056 at this scale/seed at integration) and moves
    // preferences. Ranges here assert the live world's invariants, not the static-trust bounds.
    const ExperimentResult r = runInto(root, p20Config());
    const LongTermReport &lt = r.longTerm;
    EXPECT_GE(lt.meanFinalTrust, 0.0);
    EXPECT_LE(lt.meanFinalTrust, 1.0 + 1e-9);
    EXPECT_GE(lt.meanFinalHabit, 0.0);
    EXPECT_LE(lt.meanFinalHabit, 1.0);
    // Evolution is live: preferences must actually have moved.
    EXPECT_GT(lt.meanPreferenceShiftFromInitial, 0.0);
    EXPECT_GE(lt.meanFinalPreferenceEntropy, 0.0);
    EXPECT_GE(lt.retention1d, 0.0);
    EXPECT_LE(lt.retention1d, 1.0);
    EXPECT_GE(lt.churnRate, 0.0);
    EXPECT_LE(lt.churnRate, 1.0);
}

// ===== hidden_preference_final.csv (frozen header, ordering, hand-computed shift) ================

TEST(HiddenPreferenceExportTest, HeaderRowCountOrderingAndShiftMath) {
    const fs::path root = fs::path(::testing::TempDir()) / "rr_p20_export";
    // Evolution OFF / retention ON: the export is still written (long_term.configured), and with
    // no preference movement every *_shift column is EXACTLY 0 -- keeping the shift math
    // hand-checkable. Live-evolution movement is asserted by PolicyDivergenceTest and
    // LongTermSchemaTest.MetricsAreInPlausibleRangesWithEvolutionLive.
    const ExperimentConfig cfg = p20Config(20260720, /*retention=*/true, /*evolution=*/false);
    const ExperimentResult r = runInto(root, cfg);
    const fs::path csv = r.directory / "hidden_preference_final.csv";
    ASSERT_TRUE(fs::exists(csv));
    const std::vector<std::string> lines = readLines(csv);
    ASSERT_FALSE(lines.empty());

    // Frozen header: fixed columns then sem_v0..sem_v{D-1} (D = dimensions = 16).
    std::ostringstream expectedHeader;
    expectedHeader
        << "user_id,plasticity,churned,sem_shift,visual_shift,music_shift,emotional_shift";
    for (std::uint32_t d = 0; d < cfg.simulation.dimensions; ++d) {
        expectedHeader << ",sem_v" << d;
    }
    EXPECT_EQ(lines[0], expectedHeader.str());

    // One row per user, ascending user_id, each with D semantic components after the 7 fixed cols.
    ASSERT_EQ(lines.size(), cfg.simulation.users + 1);
    long prevId = -1;
    for (std::size_t i = 1; i < lines.size(); ++i) {
        std::stringstream ss(lines[i]);
        std::string cell;
        std::vector<std::string> cells;
        while (std::getline(ss, cell, ',')) {
            cells.push_back(cell);
        }
        ASSERT_EQ(cells.size(), 7u + cfg.simulation.dimensions);
        const long id = std::stol(cells[0]);
        EXPECT_GT(id, prevId); // strictly ascending user_id
        prevId = id;
        // Hand-computed: no channel moved (evolution off, no drift) => every *_shift is exactly 0.
        EXPECT_EQ(cells[3], "0.000000") << "sem_shift should be 0 with evolution off";
        EXPECT_EQ(cells[4], "0.000000");
        EXPECT_EQ(cells[5], "0.000000");
        EXPECT_EQ(cells[6], "0.000000");
    }
}

TEST(HiddenPreferenceExportTest, NotWrittenWhenGatesOff) {
    const fs::path root = fs::path(::testing::TempDir()) / "rr_p20_export_off";
    const ExperimentResult r =
        runInto(root, p20Config(20260720, /*retention=*/false, /*evolution=*/false));
    EXPECT_FALSE(r.longTerm.configured);
    EXPECT_FALSE(fs::exists(r.directory / "hidden_preference_final.csv"));
    EXPECT_FALSE(fs::exists(r.directory / "longterm_metrics.csv"));
}

// ===== welfare TRUST column goes LIVE (P15 placeholder resolved) =================================

TEST(WelfareTrustLiveTest, WelfareBlockEmitsMeanTrustNotPlaceholder) {
    const fs::path root = fs::path(::testing::TempDir()) / "rr_p20_trust_live";
    // Evolution OFF / retention ON: trust stays the static platformTrust trait (in [0.4, 1.0]),
    // so "live and clearly not the 0 placeholder" is provable with a fixed bound. Live-erosion
    // trust ranges are covered by MetricsAreInPlausibleRangesWithEvolutionLive.
    const ExperimentResult r = runInto(root, p20Config(20260720, /*retention=*/true,
                                                       /*evolution=*/false));
    ASSERT_TRUE(r.welfare.configured);
    EXPECT_TRUE(r.welfare.trustModeled);
    // Live mean trust == the long-term run-end mean, and clearly not the 0 placeholder.
    EXPECT_GT(r.welfare.platformTrust, 0.3);
    EXPECT_DOUBLE_EQ(r.welfare.platformTrust, r.longTerm.meanFinalTrust);

    const nlohmann::json j = readJson(r.directory / "summary.json");
    const nlohmann::json &w = j["welfare"];
    EXPECT_GT(w["platform_trust"].get<double>(), 0.3);
    EXPECT_TRUE(w.contains("platform_trust_source"));
    // platform_trust is no longer in the not_yet_modeled list.
    for (const auto &item : w["not_yet_modeled"]) {
        EXPECT_NE(item.get<std::string>(), "platform_trust");
    }

    // welfare_metrics.csv trust column (last field) is live for every row.
    const std::vector<std::string> lines = readLines(r.directory / "welfare_metrics.csv");
    ASSERT_GT(lines.size(), 1u);
    for (std::size_t i = 1; i < lines.size(); ++i) {
        const std::size_t comma = lines[i].find_last_of(',');
        ASSERT_NE(comma, std::string::npos);
        const double trust = std::stod(lines[i].substr(comma + 1));
        EXPECT_GT(trust, 0.3) << "welfare_metrics.csv trust must be live, row " << i;
    }
}

TEST(WelfareTrustLiveTest, PlaceholderStaysZeroWhenGatesOff) {
    const fs::path root = fs::path(::testing::TempDir()) / "rr_p20_trust_off";
    const ExperimentResult r =
        runInto(root, p20Config(20260720, /*retention=*/false, /*evolution=*/false));
    ASSERT_TRUE(r.welfare.configured); // latent_reactions is on (event mode requires it)
    EXPECT_FALSE(r.welfare.trustModeled);
    EXPECT_DOUBLE_EQ(r.welfare.platformTrust, 0.0); // placeholder preserved (byte-identity, D17)

    const nlohmann::json j = readJson(r.directory / "summary.json");
    const nlohmann::json &w = j["welfare"];
    EXPECT_FALSE(w.contains("platform_trust_source"));
    bool listed = false;
    for (const auto &item : w["not_yet_modeled"]) {
        listed = listed || item.get<std::string>() == "platform_trust";
    }
    EXPECT_TRUE(listed); // still a documented placeholder
}
