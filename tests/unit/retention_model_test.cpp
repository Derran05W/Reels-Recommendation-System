// RetentionModel unit + model-level statistical tests (Phase 20 package B, V2 TDD §4.17).
//
// The RetentionModel is the satisfaction-coupled replacement for P18's baseline return-delay draw
// under retention.enabled. These tests exercise it directly (no runner) against the frozen
// interface (retention_model.hpp): the hand-computed habit strengthen/decay dynamics, the
// return-delay monotonicity in last-session welfare (a model-level statistical test on fixed rng
// streams), churn determinism + the exact strict-`>` threshold boundary, the named-constant
// time-of-day curve shape, and the model-implied churn probability. The named shape constants live
// in retention_model.cpp (D24); the hand-computed expectations below restate them as literals with
// their arithmetic, so a constant change trips these pins (test-first, V1 TDD §30).
//
// Calibration: the delay is exponential with a per-day hazard h = baselineDailyUsage *
// exp(welfare + habit + (trust-0.7)) (time-of-day factor multiplied in), so the delay DISTRIBUTION
// is compared by MEDIAN (robust to the exponential tail and the rare churn cap); cohorts are chosen
// so both have negligible churn, and the asserted margins (>=2x separation) are far outside the
// Monte-Carlo noise at the sample sizes used.

#include "rr/simulation/retention_model.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include "rr/infrastructure/config.hpp"
#include "rr/infrastructure/random.hpp"
#include "rr/simulation/hidden/hidden_user_state.hpp"

using namespace rr;

namespace {

RetentionConfig enabledConfig() {
    RetentionConfig cfg;
    cfg.enabled = true; // defaults: churn threshold 604800s (7d), hazard floor 0.02/day.
    return cfg;
}

// A hidden state with just the fields nextReturnDelay reads. sessionsCompleted=1 marks it as a
// post-first-session state (the last-session memory is meaningful).
HiddenUserState delayState(double lastSat, double lastRegret, double habit, double trust,
                           double usage) {
    HiddenUserState h;
    h.platformTrust = 0.7f;
    h.baselineDailyUsage = static_cast<float>(usage);
    h.retention.lastSessionSatisfaction = lastSat;
    h.retention.lastSessionRegret = lastRegret;
    h.retention.habitStrength = habit;
    h.retention.trust = trust; // >=0 so it is used directly (not read from platformTrust)
    h.retention.sessionsCompleted = 1;
    return h;
}

double medianOf(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    const std::size_t n = v.size();
    return n % 2 ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);
}

// Median return delay for a fixed cohort over a fixed rng stream (fresh state per draw so a churn
// draw never leaks its `churned` flag into the next).
double medianDelay(RetentionModel &model, double lastSat, double lastRegret, double habit,
                   double trust, double usage, uint64_t seed, int n = 8000) {
    Rng rng(seed);
    std::vector<double> delays;
    delays.reserve(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) {
        HiddenUserState h = delayState(lastSat, lastRegret, habit, trust, usage);
        delays.push_back(static_cast<double>(model.nextReturnDelay(h, /*now=*/0, rng)));
    }
    return medianOf(std::move(delays));
}

} // namespace

// ===== Habit strengthen / decay (hand-computed, V2 §4.17) ========================================

TEST(RetentionHabitTest, SeedsFromTraitThenStrengthensOnFirstSatisfyingSession) {
    RetentionModel model(enabledConfig());
    HiddenUserState h;
    h.habitStrength = 0.2f; // the per-user baseline habit TRAIT (seeds retention.habitStrength).
    h.retention.trust = -1.0;

    model.onSessionEnd(h, /*meanSat=*/0.8, /*meanRegret=*/0.0, /*exitAt=*/1000);

    // First touch: seed = 0.2, then strengthen by kHabitGain(0.15)*welfare(0.8)*(1-0.2):
    // 0.2 + 0.15*0.8*0.8 = 0.296.
    EXPECT_NEAR(h.retention.habitStrength, 0.296, 1e-6);
    EXPECT_EQ(h.retention.sessionsCompleted, 1u);
    EXPECT_EQ(h.retention.lastExitAt, 1000);
    EXPECT_DOUBLE_EQ(h.retention.lastSessionSatisfaction, 0.8);
    // Package B must NEVER write trust (package A owns it): still uninitialized.
    EXPECT_DOUBLE_EQ(h.retention.trust, -1.0);
}

TEST(RetentionHabitTest, DecaysWithTimeAwayThenStrengthens) {
    RetentionModel model(enabledConfig());
    HiddenUserState h;
    h.habitStrength = 0.2f;
    model.onSessionEnd(h, 0.8, 0.0, 1000); // -> 0.296 (as above)

    // Second session one half-life (259200s = 3 days) later: decay by 2^-1 = 0.5 first, then
    // strengthen. 0.296*0.5 = 0.148; + 0.15*0.5*(1-0.148) = 0.148 + 0.06390 = 0.21190.
    model.onSessionEnd(h, 0.5, 0.0, 1000 + 259200);
    EXPECT_NEAR(h.retention.habitStrength, 0.21190, 1e-5);
    EXPECT_EQ(h.retention.sessionsCompleted, 2u);
}

TEST(RetentionHabitTest, RegretfulSessionErodesHabit) {
    RetentionModel model(enabledConfig());
    HiddenUserState h;
    h.habitStrength = 0.4f;
    // welfare = sat - regret = 0.2 - 0.6 = -0.4 (< 0) => erode branch on the seeded 0.4:
    // 0.4 + kHabitErodeGain(0.10)*(-0.4)*0.4 = 0.4 - 0.016 = 0.384.
    model.onSessionEnd(h, 0.2, 0.6, 500);
    EXPECT_NEAR(h.retention.habitStrength, 0.384, 1e-6);
}

TEST(RetentionHabitTest, HabitStaysInUnitInterval) {
    RetentionModel model(enabledConfig());
    HiddenUserState h;
    h.habitStrength = 0.9f;
    for (int i = 0; i < 50; ++i) {
        model.onSessionEnd(h, 1.0, 0.0, 1000 + i); // relentless satisfaction, tiny gaps
    }
    EXPECT_LE(h.retention.habitStrength, 1.0);
    EXPECT_GE(h.retention.habitStrength, 0.0);
    HiddenUserState g;
    g.habitStrength = 0.1f;
    for (int i = 0; i < 50; ++i) {
        model.onSessionEnd(g, -1.0, 1.0, 1000 + i); // relentless misery
    }
    EXPECT_GE(g.retention.habitStrength, 0.0);
    EXPECT_LE(g.retention.habitStrength, 1.0);
}

// ===== Return-delay monotonicity in welfare (model-level statistical test) =======================

TEST(RetentionMonotonicityTest, BetterLastSessionWelfareReturnsStochasticallySooner) {
    RetentionModel model(enabledConfig());
    // Two cohorts, same baselineDailyUsage/time-of-day, differing only in last-session welfare +
    // the habit/trust it produced. Both have negligible churn, so medians are clean.
    const double better = medianDelay(model, /*sat=*/0.6, /*regret=*/0.0, /*habit=*/0.4,
                                      /*trust=*/0.8, /*usage=*/2.0, /*seed=*/424242);
    const double worse = medianDelay(model, /*sat=*/-0.2, /*regret=*/0.3, /*habit=*/0.1,
                                     /*trust=*/0.5, /*usage=*/2.0, /*seed=*/424242);
    // Calibrated margin: hazards differ by exp(1.1)/exp(-0.6) ~ 5.5x, so the better cohort's median
    // delay is well under half the worse cohort's — orders of magnitude outside MC noise at n=8000.
    EXPECT_LT(better, worse);
    EXPECT_LT(better, 0.5 * worse);
}

TEST(RetentionMonotonicityTest, HeavierUsageReturnsSooner) {
    RetentionModel model(enabledConfig());
    const double heavy = medianDelay(model, 0.3, 0.1, 0.3, 0.7, /*usage=*/4.0, 99);
    const double light = medianDelay(model, 0.3, 0.1, 0.3, 0.7, /*usage=*/1.0, 99);
    EXPECT_LT(heavy, light);
    EXPECT_LT(heavy, 0.5 * light); // hazard scales linearly with usage (4x) -> median ~1/4.
}

// ===== Churn: determinism + exact strict-`>` threshold boundary ==================================

TEST(RetentionChurnTest, SameSeedSameDelayAndChurnDecision) {
    RetentionModel model(enabledConfig());
    for (uint64_t seed = 1; seed <= 200; ++seed) {
        HiddenUserState a = delayState(-0.6, 0.8, 0.0, 0.4, 0.5); // a churn-prone cohort
        HiddenUserState b = delayState(-0.6, 0.8, 0.0, 0.4, 0.5);
        Rng ra(seed), rb(seed);
        EXPECT_EQ(model.nextReturnDelay(a, 12345, ra), model.nextReturnDelay(b, 12345, rb));
        EXPECT_EQ(a.retention.churned, b.retention.churned);
    }
}

TEST(RetentionChurnTest, ThresholdIsExactStrictGreaterThan) {
    // Discover a concrete delay D under a threshold so large it never churns.
    RetentionConfig big = enabledConfig();
    big.churnDelayThresholdSeconds = 1e18;
    RetentionModel mBig(big);
    // A low-hazard cohort so the discovered delay is large (well above the 60s floor), making the
    // D-1 vs D boundary unambiguous.
    HiddenUserState hd = delayState(-1.0, 1.0, 0.0, 0.4, 0.5);
    Rng probe(20260719);
    const Timestamp D = mBig.nextReturnDelay(hd, /*now=*/777, probe);
    EXPECT_FALSE(hd.retention.churned);
    ASSERT_GT(D, 60); // a normal (non-floored) delay so D-1 vs D is meaningful

    // threshold == D: delay D is NOT strictly greater than D -> NOT churned.
    RetentionConfig eq = enabledConfig();
    eq.churnDelayThresholdSeconds = static_cast<double>(D);
    RetentionModel mEq(eq);
    HiddenUserState hEq = delayState(-1.0, 1.0, 0.0, 0.4, 0.5);
    Rng rEq(20260719);
    EXPECT_EQ(mEq.nextReturnDelay(hEq, 777, rEq), D);
    EXPECT_FALSE(hEq.retention.churned);

    // threshold == D-1: delay D IS strictly greater -> churned, and no ReturnToApp is scheduled by
    // the caller.
    RetentionConfig below = enabledConfig();
    below.churnDelayThresholdSeconds = static_cast<double>(D) - 1.0;
    RetentionModel mBelow(below);
    HiddenUserState hBelow = delayState(-1.0, 1.0, 0.0, 0.4, 0.5);
    Rng rBelow(20260719);
    EXPECT_EQ(mBelow.nextReturnDelay(hBelow, 777, rBelow), D);
    EXPECT_TRUE(hBelow.retention.churned);
}

TEST(RetentionChurnTest, ChurnProbabilityMonotonicInWelfareAndBounded) {
    RetentionModel model(enabledConfig());
    const HiddenUserState healthy = delayState(0.7, 0.0, 0.6, 0.9, 3.0);
    const HiddenUserState miserable = delayState(-0.6, 0.8, 0.0, 0.4, 0.5);
    const double pHealthy = model.churnProbability(healthy);
    const double pMiserable = model.churnProbability(miserable);
    EXPECT_GE(pHealthy, 0.0);
    EXPECT_LE(pHealthy, 1.0);
    EXPECT_GE(pMiserable, 0.0);
    EXPECT_LE(pMiserable, 1.0);
    EXPECT_LT(pHealthy, pMiserable); // worse welfare => higher churn probability
    EXPECT_LT(pHealthy, 0.01);       // a healthy heavy user almost never churns
    EXPECT_GT(pMiserable, 0.2);      // a miserable low-usage user churns often
}

TEST(RetentionChurnTest, UninitializedTrustReadsPlatformTrust) {
    // Two states identical except one carries trust=-1 (uninitialized) with platformTrust matching
    // the other's explicit trust: churn probabilities must coincide (the -1 sentinel reads the
    // trait).
    RetentionModel model(enabledConfig());
    HiddenUserState explicitTrust = delayState(0.1, 0.2, 0.3, 0.55, 1.5);
    HiddenUserState sentinel = delayState(0.1, 0.2, 0.3, /*trust=*/-1.0, 1.5);
    sentinel.platformTrust = 0.55f;
    EXPECT_NEAR(model.churnProbability(explicitTrust), model.churnProbability(sentinel), 1e-9);
}

// ===== Time-of-day curve (named-constant, determinism + shape) ===================================

TEST(RetentionTimeOfDayTest, DeterministicPureFunction) {
    for (double s = 0; s < 86400; s += 137.0) {
        EXPECT_DOUBLE_EQ(retentionTimeOfDayFactor(s), retentionTimeOfDayFactor(s));
    }
}

TEST(RetentionTimeOfDayTest, PositiveBoundedAndDailyPeriodic) {
    for (double s = 0; s < 86400; s += 60.0) {
        const double f = retentionTimeOfDayFactor(s);
        EXPECT_GT(f, 0.0);
        EXPECT_GE(f, 0.7 - 1e-9); // amplitude 0.3 about 1.0
        EXPECT_LE(f, 1.3 + 1e-9);
        EXPECT_NEAR(f, retentionTimeOfDayFactor(s + 86400.0), 1e-9); // 24h periodic
    }
}

TEST(RetentionTimeOfDayTest, PeaksInTheEveningTroughsTwelveHoursOpposite) {
    // Peak at the 20:00 usage window (kPeakSecondsOfDay = 72000), trough 12h opposite.
    EXPECT_NEAR(retentionTimeOfDayFactor(72000.0), 1.3, 1e-9);
    EXPECT_NEAR(retentionTimeOfDayFactor(72000.0 - 43200.0), 0.7, 1e-9);
    for (double s = 0; s < 86400; s += 300.0) {
        EXPECT_LE(retentionTimeOfDayFactor(s), retentionTimeOfDayFactor(72000.0) + 1e-9);
    }
}

TEST(RetentionTimeOfDayTest, MeanIsOneOverADay) {
    double sum = 0.0;
    int n = 0;
    for (double s = 0; s < 86400; s += 1.0, ++n) {
        sum += retentionTimeOfDayFactor(s);
    }
    EXPECT_NEAR(sum / n, 1.0, 1e-3); // a pure cosine offset by 1 integrates to 1 over the day
}
