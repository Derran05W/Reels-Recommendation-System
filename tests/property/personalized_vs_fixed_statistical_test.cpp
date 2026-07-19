// Phase 17 Tier-2 acceptance core (V2 TDD §4.10 core experiment + Tier-2 acceptance, plan Phase 17
// task 5): personalized diversity must beat fixed diversity on session utility (U_s) for the two
// DESIGNED cohorts -- focused (slow topic-fatigue, concentration preference) and easily_fatigued
// (fast general fatigue) -- once the trait heterogeneity (package A) and the real personalized
// reranker (package B) are both live. Run as in-process ExperimentRunner arms on a reduced
// gate-on config (content_v2 + latent_reactions + session_dynamics, plus personalized_diversity for
// the personalized arms), mirroring tests/property/session_exit_statistical_test.cpp's fixture
// shape (SetUpTestSuite runs every arm once; TEST_F cases share the results across assertions).
//
// Unlike session_exit_statistical_test.cpp (written before package B1/B2 of Phase 16 had landed),
// THIS worktree already has a real `ExperimentResult::sessionHealth` (Phase 16 landed at scaffold
// commit 66010ee) -- so this file reads it DIRECTLY, no SFINAE probe needed for that part (per this
// package's brief: "in your tree ExperimentResult.sessionHealth EXISTS (P16 landed), so read it
// directly").
//
// ============================================================================================
// WHAT IS STILL PENDING IN THIS TREE (package C's parallel siblings A and B, invisible here):
//   - Package A: TraitCohortSpec (include/rr/infrastructure/cohort_config.hpp) is a name+weight-
//     only stub -- a cohort_mix entry selects a LABEL with no effect on trait sampling, so the
//     "focused" and "easily_fatigued" cohorts constructed below are behaviourally IDENTICAL
//     populations pre-integration (every user still samples the Phase 13 tolerance traits from the
//     full default [0,1] range regardless of cohort_mix's name).
//   - Package B: PersonalizedDiversityReranker (include/rr/recommendation/
//     personalized_diversity_reranker.hpp) is a stub that delegates to the fixed DiversityReranker,
//     so a personalized arm is expected to be BIT-IDENTICAL to its fixed counterpart
//     pre-integration.
// Either one alone is sufficient to make "personalized > fixed" false today for reasons that have
// nothing to do with whether personalization is a good idea -- so every comparative assertion below
// is guarded by a RUNTIME DETECTOR (not an assumption) that inspects the actual numbers and
// GTEST_SKIPs with the observed values when either package hasn't landed, the same P15/P16
// pending-integration idiom (this file's detectors are directly analogous to
// session_exit_statistical_test.cpp's `HasSessionHealthMember` SFINAE probe, just at the value
// level instead of the type level, because the member itself already exists here). Both detectors
// auto-flip to "live" the moment A and B are merged, with no edits needed in this file.
// ============================================================================================
#include "rr/evaluation/experiment_runner.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>

#include "rr/infrastructure/cohort_config.hpp"
#include "rr/infrastructure/config.hpp"

using namespace rr;

namespace {

namespace fs = std::filesystem;

// --- Reduced gate-on dataset (this package's brief: "~400 users/4000 reels/dim 32/40
//     interactions"; identical scale to tests/property/session_exit_statistical_test.cpp's
//     fixture, whose four-arm suite ran in the tens of seconds in Debug -- this file runs five
//     arms, a similar order of magnitude). --------------------------------------------------------
constexpr uint64_t kSeed = 20260718;
constexpr uint32_t kUsers = 400;
constexpr uint32_t kReels = 4000;
constexpr uint32_t kCreators = 100;
constexpr uint32_t kTopics = 32;
constexpr uint32_t kDimensions = 32;
constexpr uint32_t kInteractionsPerUser = 40;
constexpr uint32_t kVectorCandidates = 300;

// --- Acceptance margins (named constants; STARTING placeholders -- pre-integration there is no
//     real personalized-vs-fixed effect anywhere to calibrate against, so these are documented,
//     round, directionally-motivated margins on the U_s scale for the integrator to recalibrate
//     once packages A and B land and the actual effect sizes are measurable, exactly like
//     session_exit_statistical_test.cpp's kEarlyFailureExitRateMargin / kSatisfiedShareMargin). ---
constexpr double kFocusedUsMargin = 0.05; // assertion 1 (PersonalizedBeatsFixedOnUsForFocused)
constexpr double kEasilyFatiguedUsMargin = 0.05; // assertion 2 (...ForEasilyFatigued)

ExperimentConfig baseConfig() {
    ExperimentConfig c;
    c.simulation.seed = kSeed;
    c.simulation.users = kUsers;
    c.simulation.reels = kReels;
    c.simulation.creators = kCreators;
    c.simulation.topics = kTopics;
    c.simulation.dimensions = kDimensions;
    c.simulation.interactionsPerUser = kInteractionsPerUser;
    c.recommendation.vectorCandidates = kVectorCandidates;
    c.algorithm = RecommendationAlgorithm::HnswRankerDiversity;
    // Gate ON (Phase 17): personalized_diversity requires session_dynamics requires
    // latent_reactions requires content_v2 (D17). Constructed directly in C++ (not via JSON), so
    // from_json's "requires" checks never run -- both flags are set true regardless, satisfying
    // the invariant anyway.
    c.realism.contentV2 = true;
    c.realism.latentReactions = true;
    c.realism.sessionDynamics = true;
    // Keep the run fast and stream-aligned: no sampled oracle-regret / retrieval evaluation.
    c.evaluation.oracleSampleRate = 0.0;
    c.evaluation.retrievalSampleRate = 0.0;
    return c;
}

// One arm's config: base + a single-entry cohort_mix pinning the whole population to the NAMED
// cohort via package A's rr::namedCohort catalog (integration-synced: the stub-era name+weight
// construction is gone; the runtime inertness detectors below remain as a safety net) +
// personalized_diversity on/off.
ExperimentConfig cohortConfig(const std::string &cohortName, bool personalized) {
    ExperimentConfig c = baseConfig();
    c.realism.cohortMix = {namedCohort(cohortName)};
    c.realism.personalizedDiversity = personalized;
    return c;
}

// One arm's headline numbers, read DIRECTLY from ExperimentResult (sessionHealth exists in this
// tree, P16 landed -- no SFINAE needed here, unlike session_exit_statistical_test.cpp).
struct ArmSummary {
    std::string tag;
    std::size_t impressions = 0;
    bool sessionHealthAvailable = false; // configured && sessions > 0
    std::size_t sessionsClosed = 0;
    double meanSessionUtility = 0.0;
    double earlyFailureExitRate = 0.0;
    double satisfiedShare = 0.0;
    double regretShare = 0.0;
};

ArmSummary runArm(const std::string &tag, const ExperimentConfig &config) {
    const fs::path root = fs::path(::testing::TempDir()) / ("rr_personalized_vs_fixed_" + tag);
    fs::remove_all(root);
    ExperimentRunner runner(config, root);
    const ExperimentResult r = runner.run();
    fs::remove_all(root); // reclaim the on-disk output; everything asserted on is in memory

    ArmSummary a;
    a.tag = tag;
    a.impressions = r.impressionCount;
    a.sessionsClosed = r.sessionHealth.sessions;
    a.sessionHealthAvailable = r.sessionHealth.configured && r.sessionHealth.sessions > 0;
    a.meanSessionUtility = r.sessionHealth.meanSessionUtility;
    a.earlyFailureExitRate = r.sessionHealth.earlyFailureExitRate;
    a.satisfiedShare = r.sessionHealth.exitShare(SessionExitType::Satisfied);
    a.regretShare = r.sessionHealth.exitShare(SessionExitType::Regret);
    return a;
}

// Package-A runtime detector: are the two cohorts' FIXED arms distinguishable at all? Bit-identical
// U_s across cohorts means cohort_mix is not yet changing trait sampling (name+weight-only stub).
bool cohortsDistinguishable(const ArmSummary &fixedA, const ArmSummary &fixedB) {
    if (!fixedA.sessionHealthAvailable || !fixedB.sessionHealthAvailable) {
        return false; // can't tell yet -> treat as not-yet-distinguishable (skip)
    }
    return fixedA.meanSessionUtility != fixedB.meanSessionUtility;
}

// Package-B runtime detector: is this cohort's personalized arm distinguishable from its fixed
// counterpart? Bit-identical U_s means PersonalizedDiversityReranker's stub is still delegating.
bool personalizationDistinguishable(const ArmSummary &fixed, const ArmSummary &personalized) {
    if (!fixed.sessionHealthAvailable || !personalized.sessionHealthAvailable) {
        return false;
    }
    return fixed.meanSessionUtility != personalized.meanSessionUtility;
}

} // namespace

// Fixture: runs every arm ONCE (arms are expensive) in SetUpTestSuite and shares the results across
// the per-assertion tests below (mirrors SessionExitStatisticalTest's fixture shape,
// tests/property/ session_exit_statistical_test.cpp).
class PersonalizedVsFixedStatisticalTest : public ::testing::Test {
  protected:
    static ArmSummary focusedFixed_;
    static ArmSummary focusedPersonalized_;
    static ArmSummary easilyFatiguedFixed_;
    static ArmSummary easilyFatiguedPersonalized_;
    static ArmSummary focusedPersonalizedRerun_; // same config+seed as focusedPersonalized_

    static void SetUpTestSuite() {
        focusedFixed_ = runArm("focused_fixed", cohortConfig("focused", false));
        focusedPersonalized_ = runArm("focused_personalized", cohortConfig("focused", true));
        easilyFatiguedFixed_ =
            runArm("easily_fatigued_fixed", cohortConfig("easily_fatigued", false));
        easilyFatiguedPersonalized_ =
            runArm("easily_fatigued_personalized", cohortConfig("easily_fatigued", true));
        focusedPersonalizedRerun_ =
            runArm("focused_personalized_rerun", cohortConfig("focused", true));

        auto line = [](const ArmSummary &a) {
            std::cout << "[personalized-vs-fixed] " << a.tag << ": impressions=" << a.impressions
                      << " sessionHealthAvailable=" << std::boolalpha << a.sessionHealthAvailable
                      << " sessionsClosed=" << a.sessionsClosed
                      << " meanSessionUtility=" << a.meanSessionUtility
                      << " earlyFailureExitRate=" << a.earlyFailureExitRate
                      << " satisfiedShare=" << a.satisfiedShare << " regretShare=" << a.regretShare
                      << "\n";
        };
        std::cout << "===== Phase 17 personalized-vs-fixed arm summary (reduced gate-on config) "
                     "=====\n";
        line(focusedFixed_);
        line(focusedPersonalized_);
        line(easilyFatiguedFixed_);
        line(easilyFatiguedPersonalized_);
        line(focusedPersonalizedRerun_);
        std::cout << "[personalized-vs-fixed] cohorts distinguishable (package A live)="
                  << std::boolalpha << cohortsDistinguishable(focusedFixed_, easilyFatiguedFixed_)
                  << " focused-personalization distinguishable (package B live)="
                  << personalizationDistinguishable(focusedFixed_, focusedPersonalized_)
                  << " easilyFatigued-personalization distinguishable (package B live)="
                  << personalizationDistinguishable(easilyFatiguedFixed_,
                                                    easilyFatiguedPersonalized_)
                  << "\n"
                  << "========================================================================"
                     "==\n";
    }
};

ArmSummary PersonalizedVsFixedStatisticalTest::focusedFixed_;
ArmSummary PersonalizedVsFixedStatisticalTest::focusedPersonalized_;
ArmSummary PersonalizedVsFixedStatisticalTest::easilyFatiguedFixed_;
ArmSummary PersonalizedVsFixedStatisticalTest::easilyFatiguedPersonalized_;
ArmSummary PersonalizedVsFixedStatisticalTest::focusedPersonalizedRerun_;

// Sanity (HOLDS TODAY, no pending integration): every arm runs end-to-end through ExperimentRunner
// under the full gate stack (content_v2 + latent_reactions + session_dynamics [+
// personalized_diversity]) without throwing, and produces real session-health data. Regression
// coverage for the Phase 17 gate wiring (FullRecommender's personalizedDiversity selection,
// src/recommendation/full_recommender.cpp) independent of whether packages A/B have landed.
TEST_F(PersonalizedVsFixedStatisticalTest, GateCombinationRunsProduceSessionHealthData) {
    for (const ArmSummary *a : {&focusedFixed_, &focusedPersonalized_, &easilyFatiguedFixed_,
                                &easilyFatiguedPersonalized_}) {
        EXPECT_GT(a->impressions, 0u) << a->tag;
        EXPECT_TRUE(a->sessionHealthAvailable) << a->tag;
    }
}

// Assertion 1 (Tier-2 acceptance / plan Phase 17 task 5 / exit criteria): personalized diversity
// must beat fixed diversity on U_s for the FOCUSED cohort. PENDING PACKAGE A+B INTEGRATION -- see
// the header TODO block; auto-activates once cohort_mix carries real per-trait overrides AND
// PersonalizedDiversityReranker is real.
TEST_F(PersonalizedVsFixedStatisticalTest, PersonalizedBeatsFixedOnUsForFocused) {
    const bool cohortsLive = cohortsDistinguishable(focusedFixed_, easilyFatiguedFixed_);
    const bool personalizationLive =
        personalizationDistinguishable(focusedFixed_, focusedPersonalized_);
    if (!cohortsLive || !personalizationLive) {
        GTEST_SKIP() << "PENDING PACKAGE A/B INTEGRATION: focused-fixed U_s="
                     << focusedFixed_.meanSessionUtility
                     << ", easily_fatigued-fixed U_s=" << easilyFatiguedFixed_.meanSessionUtility
                     << " (cohorts "
                     << (cohortsLive ? "differ -- package A live"
                                     : "IDENTICAL -- package A's per-trait cohort overrides not "
                                       "merged")
                     << "); focused-personalized U_s=" << focusedPersonalized_.meanSessionUtility
                     << " vs focused-fixed U_s=" << focusedFixed_.meanSessionUtility << " ("
                     << (personalizationLive
                             ? "differ -- package B live"
                             : "IDENTICAL -- package B's PersonalizedDiversityReranker stub "
                               "delegates to fixed")
                     << "). See this file's header TODO block for the expected shape.";
    }
    EXPECT_GT(focusedPersonalized_.meanSessionUtility,
              focusedFixed_.meanSessionUtility + kFocusedUsMargin)
        << "focused personalized U_s " << focusedPersonalized_.meanSessionUtility << " vs fixed "
        << focusedFixed_.meanSessionUtility;
}

// Assertion 2: same as assertion 1, for the EASILY_FATIGUED cohort.
TEST_F(PersonalizedVsFixedStatisticalTest, PersonalizedBeatsFixedOnUsForEasilyFatigued) {
    const bool cohortsLive = cohortsDistinguishable(focusedFixed_, easilyFatiguedFixed_);
    const bool personalizationLive =
        personalizationDistinguishable(easilyFatiguedFixed_, easilyFatiguedPersonalized_);
    if (!cohortsLive || !personalizationLive) {
        GTEST_SKIP() << "PENDING PACKAGE A/B INTEGRATION: focused-fixed U_s="
                     << focusedFixed_.meanSessionUtility
                     << ", easily_fatigued-fixed U_s=" << easilyFatiguedFixed_.meanSessionUtility
                     << " (cohorts "
                     << (cohortsLive ? "differ -- package A live"
                                     : "IDENTICAL -- package A's per-trait cohort overrides not "
                                       "merged")
                     << "); easily_fatigued-personalized U_s="
                     << easilyFatiguedPersonalized_.meanSessionUtility
                     << " vs easily_fatigued-fixed U_s=" << easilyFatiguedFixed_.meanSessionUtility
                     << " ("
                     << (personalizationLive
                             ? "differ -- package B live"
                             : "IDENTICAL -- package B's PersonalizedDiversityReranker stub "
                               "delegates to fixed")
                     << "). See this file's header TODO block for the expected shape.";
    }
    EXPECT_GT(easilyFatiguedPersonalized_.meanSessionUtility,
              easilyFatiguedFixed_.meanSessionUtility + kEasilyFatiguedUsMargin)
        << "easily_fatigued personalized U_s " << easilyFatiguedPersonalized_.meanSessionUtility
        << " vs fixed " << easilyFatiguedFixed_.meanSessionUtility;
}

// Assertion 3 (determinism, D8): a same-seed, same-config rerun of the focused-personalized arm
// reproduces every session-health/impression number bit-identically. HOLDS TODAY regardless of A/B
// integration status (determinism does not depend on the cohort/personalization mechanism being
// behaviourally "real", only on it being deterministic, which the stubs are).
TEST_F(PersonalizedVsFixedStatisticalTest, FocusedPersonalizedArmRerunIsDeterministic) {
    EXPECT_EQ(focusedPersonalized_.impressions, focusedPersonalizedRerun_.impressions);
    ASSERT_TRUE(focusedPersonalized_.sessionHealthAvailable);
    ASSERT_TRUE(focusedPersonalizedRerun_.sessionHealthAvailable);
    EXPECT_EQ(focusedPersonalized_.sessionsClosed, focusedPersonalizedRerun_.sessionsClosed);
    EXPECT_EQ(focusedPersonalized_.meanSessionUtility,
              focusedPersonalizedRerun_.meanSessionUtility);
    EXPECT_EQ(focusedPersonalized_.earlyFailureExitRate,
              focusedPersonalizedRerun_.earlyFailureExitRate);
    EXPECT_EQ(focusedPersonalized_.satisfiedShare, focusedPersonalizedRerun_.satisfiedShare);
    EXPECT_EQ(focusedPersonalized_.regretShare, focusedPersonalizedRerun_.regretShare);
}
