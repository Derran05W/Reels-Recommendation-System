// Unit tests for the hidden-user-welfare metric group (Phase 15, V2 TDD §6). The aggregation math
// is exercised on hand-built WelfareImpression values — every derived number (means,
// satisfaction-per-minute, per-archetype exposure/means, placeholders) is checked by hand — plus
// determinism and the empty-bucket / name-fallback edge cases.

#include "rr/evaluation/welfare_metrics.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

using namespace rr;

namespace {

WelfareImpression mk(double satisfaction, double regret, double watchSeconds, uint32_t archetype) {
    WelfareImpression w;
    w.immediateSatisfaction = satisfaction;
    w.regret = regret;
    w.watchSeconds = watchSeconds;
    w.archetypeIndex = archetype;
    return w;
}

// Two named archetypes for the small tests; the runner passes the real catalog names.
std::vector<std::string> names2() { return {"satisfying", "ragebait"}; }

} // namespace

// --- satisfaction_per_minute free function: exact definition + divide-by-zero guard --------------
TEST(WelfareMetricsSpmTest, DefinitionAndZeroGuard) {
    // sum satisfaction 3.0 over 120 watch-seconds = 2.0 watch-minutes -> 1.5 satisfaction/minute.
    EXPECT_NEAR(satisfactionPerMinute(3.0, 120.0), 1.5, 1e-12);
    // Negative satisfaction -> negative per-minute (net dissatisfaction accrues per minute).
    EXPECT_NEAR(satisfactionPerMinute(-3.0, 120.0), -1.5, 1e-12);
    // No watch time -> 0, never NaN/inf.
    EXPECT_EQ(satisfactionPerMinute(5.0, 0.0), 0.0);
    EXPECT_EQ(satisfactionPerMinute(0.0, 0.0), 0.0);
}

// --- Overall means + satisfaction-per-minute over a hand-built bucket ----------------------------
TEST(WelfareMetricsTest, OverallMeansAndSatisfactionPerMinute) {
    WelfareMetrics wm(/*rounds=*/1, /*archetypeCount=*/2);
    wm.add(0, mk(/*sat=*/1.0, /*regret=*/0.2, /*watchSeconds=*/30.0, /*archetype=*/0));
    wm.add(0, mk(/*sat=*/-0.5, /*regret=*/0.8, /*watchSeconds=*/30.0, /*archetype=*/1));
    wm.add(0, mk(/*sat=*/0.5, /*regret=*/0.0, /*watchSeconds=*/60.0, /*archetype=*/0));

    const WelfareReport w = wm.reduce(names2());
    EXPECT_EQ(w.impressions, 3u);
    // mean satisfaction = (1.0 - 0.5 + 0.5) / 3 = 1.0/3.
    EXPECT_NEAR(w.meanSatisfaction, 1.0 / 3.0, 1e-9);
    // mean regret = (0.2 + 0.8 + 0.0) / 3 = 1.0/3.
    EXPECT_NEAR(w.meanRegret, 1.0 / 3.0, 1e-9);
    // watch-minutes = 120s / 60 = 2.0; satisfaction sum = 1.0; spm = 1.0 / 2.0 = 0.5.
    EXPECT_NEAR(w.watchMinutes, 2.0, 1e-12);
    EXPECT_NEAR(w.satisfactionPerMinute, 0.5, 1e-9);
    // Placeholders constant 0.
    EXPECT_EQ(w.harmfulFatigue, 0.0);
    EXPECT_EQ(w.platformTrust, 0.0);
}

// --- Per-round breakdown: independent means + spm per round --------------------------------------
TEST(WelfareMetricsTest, PerRoundBreakdown) {
    WelfareMetrics wm(/*rounds=*/2, /*archetypeCount=*/2);
    // Round 0: one impression, sat 1.0 over 60s (1 min) -> spm 1.0.
    wm.add(0, mk(1.0, 0.1, 60.0, 0));
    // Round 1: two impressions, sat 0.0+0.0 over 120s (2 min) -> spm 0.0, mean regret 0.5.
    wm.add(1, mk(0.0, 0.4, 60.0, 1));
    wm.add(1, mk(0.0, 0.6, 60.0, 1));

    const WelfareReport w = wm.reduce(names2());
    ASSERT_EQ(w.byRound.size(), 2u);

    EXPECT_EQ(w.byRound[0].round, 0u);
    EXPECT_EQ(w.byRound[0].impressions, 1u);
    EXPECT_NEAR(w.byRound[0].meanSatisfaction, 1.0, 1e-12);
    EXPECT_NEAR(w.byRound[0].meanRegret, 0.1, 1e-12);
    EXPECT_NEAR(w.byRound[0].watchMinutes, 1.0, 1e-12);
    EXPECT_NEAR(w.byRound[0].satisfactionPerMinute, 1.0, 1e-12);
    EXPECT_EQ(w.byRound[0].harmfulFatigue, 0.0);
    EXPECT_EQ(w.byRound[0].platformTrust, 0.0);

    EXPECT_EQ(w.byRound[1].round, 1u);
    EXPECT_EQ(w.byRound[1].impressions, 2u);
    EXPECT_NEAR(w.byRound[1].meanSatisfaction, 0.0, 1e-12);
    EXPECT_NEAR(w.byRound[1].meanRegret, 0.5, 1e-12);
    EXPECT_NEAR(w.byRound[1].watchMinutes, 2.0, 1e-12);
    EXPECT_NEAR(w.byRound[1].satisfactionPerMinute, 0.0, 1e-12);
}

// --- Per-archetype exposure + welfare, in index order, shares sum to 1 ---------------------------
TEST(WelfareMetricsTest, PerArchetypeExposureAndMeans) {
    WelfareMetrics wm(/*rounds=*/1, /*archetypeCount=*/3);
    // Archetype 0 x3 (satisfying), archetype 1 x1 (ragebait), archetype 2 x0 (never served).
    wm.add(0, mk(0.8, 0.1, 30.0, 0));
    wm.add(0, mk(0.6, 0.1, 30.0, 0));
    wm.add(0, mk(0.4, 0.1, 30.0, 0));
    wm.add(0, mk(-0.9, 0.9, 30.0, 1));

    const std::vector<std::string> names = {"satisfying", "ragebait", "useful"};
    const WelfareReport w = wm.reduce(names);

    // One row PER catalog archetype (stable schema), in index order — including the 0-impression
    // one.
    ASSERT_EQ(w.byArchetype.size(), 3u);
    EXPECT_EQ(w.byArchetype[0].archetypeIndex, 0u);
    EXPECT_EQ(w.byArchetype[0].name, "satisfying");
    EXPECT_EQ(w.byArchetype[0].impressions, 3u);
    EXPECT_NEAR(w.byArchetype[0].exposureShare, 3.0 / 4.0, 1e-12);
    EXPECT_NEAR(w.byArchetype[0].meanSatisfaction, (0.8 + 0.6 + 0.4) / 3.0, 1e-9);
    EXPECT_NEAR(w.byArchetype[0].meanRegret, 0.1, 1e-9);

    EXPECT_EQ(w.byArchetype[1].name, "ragebait");
    EXPECT_EQ(w.byArchetype[1].impressions, 1u);
    EXPECT_NEAR(w.byArchetype[1].exposureShare, 1.0 / 4.0, 1e-12);
    EXPECT_NEAR(w.byArchetype[1].meanSatisfaction, -0.9, 1e-9);
    EXPECT_NEAR(w.byArchetype[1].meanRegret, 0.9, 1e-9);

    // Zero-impression archetype: present, share 0, means 0 (no NaN).
    EXPECT_EQ(w.byArchetype[2].name, "useful");
    EXPECT_EQ(w.byArchetype[2].impressions, 0u);
    EXPECT_EQ(w.byArchetype[2].exposureShare, 0.0);
    EXPECT_EQ(w.byArchetype[2].meanSatisfaction, 0.0);
    EXPECT_EQ(w.byArchetype[2].meanRegret, 0.0);

    // Exposure shares over all archetypes sum to 1 (every impression is attributed exactly once).
    double shareSum = 0.0;
    for (const ArchetypeWelfare &a : w.byArchetype) {
        shareSum += a.exposureShare;
    }
    EXPECT_NEAR(shareSum, 1.0, 1e-12);
}

// --- Names fall back to "archetype_<i>" when the catalog name list is short ----------------------
TEST(WelfareMetricsTest, ArchetypeNameFallback) {
    WelfareMetrics wm(/*rounds=*/1, /*archetypeCount=*/3);
    wm.add(0, mk(0.5, 0.1, 10.0, 2));
    const WelfareReport w = wm.reduce({"only_one_name"}); // names shorter than the catalog
    ASSERT_EQ(w.byArchetype.size(), 3u);
    EXPECT_EQ(w.byArchetype[0].name, "only_one_name");
    EXPECT_EQ(w.byArchetype[1].name, "archetype_1");
    EXPECT_EQ(w.byArchetype[2].name, "archetype_2");
}

// --- Empty accumulator: all zero, no NaN, correctly-sized vectors --------------------------------
TEST(WelfareMetricsTest, EmptyIsAllZeroAndShaped) {
    WelfareMetrics wm(/*rounds=*/3, /*archetypeCount=*/2);
    const WelfareReport w = wm.reduce(names2());
    EXPECT_EQ(w.impressions, 0u);
    EXPECT_EQ(w.meanSatisfaction, 0.0);
    EXPECT_EQ(w.meanRegret, 0.0);
    EXPECT_EQ(w.satisfactionPerMinute, 0.0);
    EXPECT_EQ(w.watchMinutes, 0.0);
    // byRound has one point per round; byArchetype one per catalog archetype — all zeroed.
    ASSERT_EQ(w.byRound.size(), 3u);
    for (std::size_t r = 0; r < w.byRound.size(); ++r) {
        EXPECT_EQ(w.byRound[r].round, r);
        EXPECT_EQ(w.byRound[r].impressions, 0u);
        EXPECT_EQ(w.byRound[r].satisfactionPerMinute, 0.0);
    }
    ASSERT_EQ(w.byArchetype.size(), 2u);
    for (const ArchetypeWelfare &a : w.byArchetype) {
        EXPECT_EQ(a.impressions, 0u);
        EXPECT_EQ(a.exposureShare, 0.0);
    }
}

// --- Determinism: identical add sequences produce identical reports ------------------------------
TEST(WelfareMetricsTest, DeterministicReduction) {
    const auto build = [] {
        WelfareMetrics wm(2, 2);
        wm.add(0, mk(0.3, 0.2, 25.0, 0));
        wm.add(0, mk(-0.1, 0.7, 15.0, 1));
        wm.add(1, mk(0.9, 0.05, 40.0, 0));
        return wm.reduce(names2());
    };
    const WelfareReport a = build();
    const WelfareReport b = build();
    EXPECT_EQ(a.impressions, b.impressions);
    EXPECT_EQ(a.meanSatisfaction, b.meanSatisfaction);
    EXPECT_EQ(a.meanRegret, b.meanRegret);
    EXPECT_EQ(a.satisfactionPerMinute, b.satisfactionPerMinute);
    ASSERT_EQ(a.byRound.size(), b.byRound.size());
    for (std::size_t r = 0; r < a.byRound.size(); ++r) {
        EXPECT_EQ(a.byRound[r].meanSatisfaction, b.byRound[r].meanSatisfaction);
        EXPECT_EQ(a.byRound[r].satisfactionPerMinute, b.byRound[r].satisfactionPerMinute);
    }
    ASSERT_EQ(a.byArchetype.size(), b.byArchetype.size());
    for (std::size_t i = 0; i < a.byArchetype.size(); ++i) {
        EXPECT_EQ(a.byArchetype[i].exposureShare, b.byArchetype[i].exposureShare);
        EXPECT_EQ(a.byArchetype[i].meanSatisfaction, b.byArchetype[i].meanSatisfaction);
    }
}
