// Serving-strategy UNIT tests (Phase 19, V2 TDD §4.13). Package A owns the serving strategies and
// the cost/staleness instrumentation on the EventDrivenRunner. These tests exercise:
//   - the PURE decision helpers in isolation — refill threshold, intent-swing cosine, invalidation,
//     and adaptation-delay recovery math — where the exact boundaries are asserted decisively; and
//   - the observable serving BEHAVIOUR via small full-gate event runs (prefetch depth honoured,
//     staleness-counter math, preserve-downloaded-by-default vs intent invalidation), per the phase
//     plan's guidance to "prefer small runs".
// The byte-identity regression contract (default serving reproduces the P18 pinned digest), the
// non-default determinism, and the batch-size frontier smoke live in serving_pipeline_test.cpp.

#include "rr/evaluation/event_driven_runner.hpp"
#include "rr/evaluation/experiment_runner.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "rr/core/embedding.hpp"
#include "rr/infrastructure/config.hpp"

using namespace rr;

namespace {

namespace fs = std::filesystem;

// ================================================================================================
// PURE decision helpers (the exact serving math, unit-tested away from the runner).
// ================================================================================================

// --- servingShouldRefill: fires at/below threshold, guarded by the outstanding-request flag
// -------
TEST(ServingRefillDecisionTest, FiresExactlyAtThresholdAndBelow) {
    EXPECT_TRUE(servingShouldRefill(3, 3, false));  // remaining == threshold -> fire
    EXPECT_TRUE(servingShouldRefill(2, 3, false));  // below -> fire
    EXPECT_TRUE(servingShouldRefill(0, 3, false));  // empty -> fire
    EXPECT_FALSE(servingShouldRefill(4, 3, false)); // one above threshold -> no
    EXPECT_FALSE(servingShouldRefill(10, 3, false));
}

TEST(ServingRefillDecisionTest, OutstandingRequestBlocksDoubleRequest) {
    // A request already in flight must never trigger a second one, at any inventory level.
    EXPECT_FALSE(servingShouldRefill(3, 3, true));
    EXPECT_FALSE(servingShouldRefill(0, 3, true));
    EXPECT_FALSE(servingShouldRefill(0, 0, true));
}

TEST(ServingRefillDecisionTest, ThresholdZeroIsRefillWhenEmpty) {
    // The byte-identical P18 default: fire only when the deque is empty (remaining 0 <= 0).
    EXPECT_TRUE(servingShouldRefill(0, 0, false));
    EXPECT_FALSE(servingShouldRefill(1, 0, false));
    EXPECT_FALSE(servingShouldRefill(5, 0, false));
}

// --- intentSwingCosine: proper cosine, undefined -> 1.0 (no swing)
// --------------------------------
TEST(IntentSwingCosineTest, IdenticalVectorsAreNoSwing) {
    const Embedding a = {0.6f, 0.8f};
    EXPECT_NEAR(intentSwingCosine(a, a), 1.0, 1e-6);
}

TEST(IntentSwingCosineTest, OrthogonalIsZeroOppositeIsMinusOne) {
    const Embedding x = {1.0f, 0.0f};
    const Embedding y = {0.0f, 1.0f};
    const Embedding negx = {-1.0f, 0.0f};
    EXPECT_NEAR(intentSwingCosine(x, y), 0.0, 1e-6);
    EXPECT_NEAR(intentSwingCosine(x, negx), -1.0, 1e-6);
}

TEST(IntentSwingCosineTest, KnownFortyFiveDegreeAngle) {
    const Embedding x = {1.0f, 0.0f};
    const Embedding d = {1.0f, 1.0f}; // 45 degrees from x -> cos = sqrt(1/2)
    EXPECT_NEAR(intentSwingCosine(x, d), std::sqrt(0.5), 1e-6);
}

TEST(IntentSwingCosineTest, UndefinedVectorsReturnOne) {
    const Embedding empty;
    const Embedding a = {1.0f, 0.0f};
    const Embedding zero = {0.0f, 0.0f};
    const Embedding diffSize = {1.0f, 0.0f, 0.0f};
    EXPECT_DOUBLE_EQ(intentSwingCosine(empty, a), 1.0);    // empty current
    EXPECT_DOUBLE_EQ(intentSwingCosine(a, empty), 1.0);    // size mismatch (empty snapshot)
    EXPECT_DOUBLE_EQ(intentSwingCosine(a, zero), 1.0);     // zero-norm snapshot
    EXPECT_DOUBLE_EQ(intentSwingCosine(zero, a), 1.0);     // zero-norm current
    EXPECT_DOUBLE_EQ(intentSwingCosine(a, diffSize), 1.0); // different dimensionality
}

// --- servingShouldInvalidate: strict below-threshold
// ----------------------------------------------
TEST(ServingInvalidateDecisionTest, FiresStrictlyBelowThreshold) {
    EXPECT_TRUE(servingShouldInvalidate(0.4, 0.5));
    EXPECT_FALSE(servingShouldInvalidate(0.5, 0.5)); // strict: equal does NOT fire
    EXPECT_FALSE(servingShouldInvalidate(0.6, 0.5));
}

TEST(ServingInvalidateDecisionTest, ThresholdOneFiresOnAnyRealMovement) {
    EXPECT_TRUE(servingShouldInvalidate(0.999, 1.0)); // any real swing invalidates
    EXPECT_FALSE(servingShouldInvalidate(1.0, 1.0));  // an unmoved feed (cos 1.0) survives
}

// --- adaptationDelayInteractions: P10-style recovery on satisfaction
// ------------------------------
TEST(AdaptationDelayTest, RecoversAtHandComputedInteraction) {
    // window=2, fraction=0.9. Pre-drift (indices 0,1) mean = 1.0 -> bar = 0.9. Drift at index 2.
    // seq = [1,1, 0,0, 1,1]: trailing-2 means at k=2..5 are 0.5, 0.0, 0.5, 1.0; first >= 0.9 at
    // k=5, so delay = k - driftIndex + 1 = 5 - 2 + 1 = 4.
    const std::vector<double> seq = {1, 1, 0, 0, 1, 1};
    EXPECT_EQ(adaptationDelayInteractions(seq, 2, 2, 0.9), 4);
}

TEST(AdaptationDelayTest, RecoversImmediatelyIsDelayOne) {
    // First post-drift trailing window already meets the bar -> delay 1.
    const std::vector<double> seq = {1, 1, 1, 1};
    EXPECT_EQ(adaptationDelayInteractions(seq, 2, 2, 0.9), 1);
}

TEST(AdaptationDelayTest, NeverRecoversReturnsMinusOne) {
    const std::vector<double> seq = {1, 1, 0, 0, 0, 0};
    EXPECT_EQ(adaptationDelayInteractions(seq, 2, 2, 0.9), -1);
}

TEST(AdaptationDelayTest, InsufficientPreDriftHistoryReturnsMinusOne) {
    const std::vector<double> seq = {1, 0, 1, 1};
    EXPECT_EQ(adaptationDelayInteractions(seq, 1, 2, 0.9), -1); // driftIndex 1 < window 2
}

TEST(AdaptationDelayTest, DriftAtOrBeyondSequenceEndReturnsMinusOne) {
    const std::vector<double> seq = {1, 1, 1};
    EXPECT_EQ(adaptationDelayInteractions(seq, 3, 2, 0.9), -1); // no post-drift interaction
    EXPECT_EQ(adaptationDelayInteractions(seq, 5, 2, 0.9), -1);
}

// ================================================================================================
// BEHAVIOURAL small-run tests (serving strategy observed end-to-end through the event runner).
// ================================================================================================

// A small full-gate event config; each test overrides the one serving knob under test. Kept tiny to
// stay fast while still producing multi-impression sessions (needed for depth/staleness to bite).
ExperimentConfig servingConfig(uint64_t seed = 424242) {
    ExperimentConfig c;
    c.simulation.seed = seed;
    c.simulation.users = 60;
    c.simulation.reels = 600;
    c.simulation.creators = 20;
    c.simulation.topics = 8;
    c.simulation.dimensions = 16;
    c.simulation.scheduler = "event_queue";
    c.simulation.horizonSeconds = 3.0 * 3600.0; // 3 simulated hours
    c.recommendation.feedSize = 5;
    c.recommendation.vectorCandidates = 80;
    c.evaluation.oracleSampleRate = 0.0; // keep behavioural runs cheap + focused on serving
    c.evaluation.retrievalSampleRate = 0.0;
    c.algorithm = RecommendationAlgorithm::HnswRanker;
    c.realism.contentV2 = true;
    c.realism.latentReactions = true;
    c.realism.sessionDynamics = true;
    c.learning.enabled = true; // applies advance the staleness clock; needed for staleness to exist
    c.scheduling.openStaggerSeconds = 1800.0;
    c.scheduling.returnDelayMeanSeconds = 1800.0;
    return c;
}

ExperimentResult runServing(const ExperimentConfig &c, const std::string &tag) {
    const fs::path root = fs::path(::testing::TempDir()) / ("rr_p19u_" + tag);
    fs::remove_all(root);
    return ExperimentRunner(c, root).run();
}

// --- Prefetch depth honoured: deeper prefetch amortizes requests + ranking cost
// -------------------
TEST(ServingBehaviourTest, DeeperPrefetchIssuesFewerRequestsAndLessCost) {
    ExperimentConfig shallow = servingConfig();
    shallow.serving.prefetchDepth = 1; // batch-1: one reel per request
    ExperimentConfig deep = servingConfig();
    deep.serving.prefetchDepth = 20; // batch-20

    const ExperimentResult s = runServing(shallow, "depth1");
    const ExperimentResult d = runServing(deep, "depth20");

    ASSERT_GT(s.impressionCount, 0u);
    ASSERT_GT(d.impressionCount, 0u);
    // Batch-1 re-ranks every impression; batch-20 amortizes a request over up to 20 reels -> far
    // fewer feed requests and far less ranking-computation cost (the frontier's cost axis).
    EXPECT_LT(d.requestCount, s.requestCount);
    EXPECT_LT(d.eventMode.rankingComputations, s.eventMode.rankingComputations);
    // The effective depth is echoed for package C's frontier x-axis.
    EXPECT_EQ(s.eventMode.servingPrefetchDepth, 1u);
    EXPECT_EQ(d.eventMode.servingPrefetchDepth, 20u);
}

// --- Staleness-counter math: staleness = updater applies between ranking and serving
// --------------
TEST(ServingBehaviourTest, BatchOneServesOnlyFreshImpressions) {
    ExperimentConfig c = servingConfig();
    c.serving.prefetchDepth = 1; // each impression is ranked fresh -> staleness always 0
    const ExperimentResult r = runServing(c, "stale_batch1");
    ASSERT_GT(r.impressionCount, 0u);
    EXPECT_EQ(r.eventMode.staleImpressionCount, 0u);
    EXPECT_DOUBLE_EQ(r.eventMode.meanStaleness, 0.0);
    EXPECT_DOUBLE_EQ(r.eventMode.staleImpressionRate, 0.0);
}

TEST(ServingBehaviourTest, DeepPrefetchWithLearningProducesStaleImpressions) {
    ExperimentConfig c = servingConfig();
    c.serving.prefetchDepth = 20; // a feed ranked once, consumed over many impressions
    c.learning.enabled = true;    // applies since ranking accumulate -> later items are stale
    const ExperimentResult r = runServing(c, "stale_deep");
    ASSERT_GT(r.impressionCount, 0u);
    EXPECT_GT(r.eventMode.staleImpressionCount, 0u);
    EXPECT_GT(r.eventMode.meanStaleness, 0.0);
    EXPECT_GT(r.eventMode.staleImpressionRate, 0.0);
}

TEST(ServingBehaviourTest, NoLearningMeansNothingIsStale) {
    ExperimentConfig c = servingConfig();
    c.serving.prefetchDepth = 20; // deep prefetch...
    c.learning.enabled = false; // ...but the model never updates -> zero applies -> zero staleness
    const ExperimentResult r = runServing(c, "stale_nolearn");
    ASSERT_GT(r.impressionCount, 0u);
    EXPECT_EQ(r.eventMode.staleImpressionCount, 0u);
    EXPECT_DOUBLE_EQ(r.eventMode.meanStaleness, 0.0);
}

// --- Preserve-downloaded (default) vs intent invalidation
// -----------------------------------------
TEST(ServingBehaviourTest, PreserveDownloadedDefaultNeverInvalidates) {
    ExperimentConfig c = servingConfig();
    c.serving.prefetchDepth = 20;
    // invalidate_on_intent_change defaults false: the downloaded client cache is preserved even as
    // the preference estimate moves, so no feed is ever dropped.
    const ExperimentResult r = runServing(c, "preserve");
    ASSERT_GT(r.impressionCount, 0u);
    EXPECT_EQ(r.eventMode.feedInvalidationCount, 0u);
    EXPECT_FALSE(r.eventMode.servingInvalidateOnIntentChange);
}

TEST(ServingBehaviourTest, IntentInvalidationDropsStaleFeedsAndRefetches) {
    ExperimentConfig on = servingConfig();
    on.serving.prefetchDepth = 20;
    on.serving.invalidateOnIntentChange = true;
    on.serving.intentSwingCosineThreshold = 1.0; // any real session-intent movement invalidates
    on.learning.enabled = true;                  // so sessionPreference actually moves

    ExperimentConfig off = on;
    off.serving.invalidateOnIntentChange = false; // the preserve-downloaded control

    const ExperimentResult onR = runServing(on, "invalidate_on");
    const ExperimentResult offR = runServing(off, "invalidate_off");

    ASSERT_GT(onR.impressionCount, 0u);
    EXPECT_GT(onR.eventMode.feedInvalidationCount, 0u); // invalidation actually fires
    EXPECT_EQ(offR.eventMode.feedInvalidationCount, 0u);
    // Dropping stale feeds forces extra re-fetches, so the invalidating arm issues MORE feed
    // requests than the preserve-downloaded control on the same seed.
    EXPECT_GT(onR.requestCount, offR.requestCount);
}

// --- Threshold refill runs, is deterministic, and does not double-request
// -------------------------
TEST(ServingBehaviourTest, ThresholdRefillRunsDeterministicallyWithoutExploding) {
    ExperimentConfig c = servingConfig();
    c.serving.prefetchDepth = 10;
    c.serving.refillThreshold = 3; // proactively refill when <= 3 remain (never stalls)
    const ExperimentResult a = runServing(c, "refill_a");
    const ExperimentResult b = runServing(c, "refill_b");

    ASSERT_GT(a.impressionCount, 0u);
    EXPECT_GT(a.requestCount, 0u);
    EXPECT_EQ(a.eventMode.servingRefillThreshold, 3u);
    // Same seed + config -> identical event stream (D20 determinism holds for non-default serving).
    EXPECT_EQ(a.eventMode.eventLogDigest, b.eventMode.eventLogDigest);
    EXPECT_EQ(a.eventMode.eventCount, b.eventMode.eventCount);
    // The outstanding-request guard keeps requests bounded: with depth 10, requests stay well below
    // impressions (no double-request explosion).
    EXPECT_LT(a.requestCount, a.impressionCount);
}

} // namespace
