// Phase 19 §7 acceptance core (V2 TDD §7 "batch-size-one adapts no slower than larger stale
// batches" + Tier-3 core experiment, plan Phase 19 task 4): under abrupt whole-population
// preference drift, the batch-size-1 serving arm (serving.prefetch_depth=1) must adapt NO SLOWER
// than the batch-size-20 arm (serving.prefetch_depth=20) -- a small, immediately-consumed feed can
// react to the user's new preference on the very next request, while a large prefetched batch keeps
// serving pre-drift-ranked (now stale) content until it is exhausted. Run as in-process
// ExperimentRunner arms on a reduced event-mode config (content_v2 + latent_reactions +
// session_dynamics + simulation.scheduler="event_queue"), mirroring
// tests/property/personalized_vs_fixed_statistical_test.cpp's fixture shape (SetUpTestSuite runs
// every arm once; TEST_F cases share the results across assertions) and
// tests/property/session_exit_statistical_test.cpp's SFINAE "detect and branch" idiom.
//
// ============================================================================================
// WHAT IS STILL PENDING IN THIS TREE (package C's parallel sibling A, invisible here): package A
// owns the serving strategies + invalidation semantics AND the cost/staleness instrumentation
// (plan Phase 19 "Suggested package split" A + B). In THIS tree (Phase 19 package C, worktree
// p19-package-c):
//   - `serving.*` config EXISTS and PARSES (validated event-mode-only at load,
//     src/infrastructure/config.cpp), but the event runner (src/evaluation/event_driven_runner.cpp)
//     IGNORES every serving.* value and always performs Phase 18's depth-1 refill regardless of the
//     configured prefetch_depth -- so the batch1 and batch20 arms constructed below are EXPECTED to
//     be BIT-IDENTICAL (same event-log digest, same impression count, same welfare/session-health
//     numbers) until package A wires prefetch depth into the runner's refill logic.
//   - `EventModeReport` (include/rr/evaluation/experiment_runner.hpp) carries NO adaptation-delay
//     field of any kind yet -- verified by reading that header directly at this scaffold. Package
//     A/B's exact field name is unknown pre-merge; this file's BEST-EFFORT GUESS (documented at
//     `detail::extractAdaptationDelay` below) is `EventModeReport::meanAdaptationDelayInteractions`
//     (a signed/floating count of interactions-since-drift until the population's mean hidden
//     satisfaction recovers to within a threshold of its pre-drift baseline, mirroring the existing
//     P10 `AdaptationReport::recoveryInteractions` naming/sentinel convention -- see that struct in
//     include/rr/evaluation/experiment_runner.hpp -- but computed from SATISFACTION under event
//     mode rather than reward under round-robin/rounds, per this phase's brief: "adaptation delay
//     ... to recover pre-drift satisfaction, from P10 machinery re-used on satisfaction"). If
//     package A's actual field name differs, `detail::extractAdaptationDelay` is the ONE place
//     needing an edit -- nothing else in this file references the field directly.
//   - `AdaptationReport` (the EXISTING P10 top-level `result.adaptation`) is NOT a fallback source
//     here: Phase 18's commit.md explicitly records "coldStart/adaptation report blocks stay
//     unconfigured in event mode ... P19's drift experiment defines its own adaptation measures",
//     confirmed by reading src/evaluation/event_driven_runner.cpp (it never populates
//     `result.adaptation`). Event mode needs its own measure -- this phase's job, not a reuse.
// Either gap alone is sufficient to make "batch1 adapts no slower than batch20" untestable today
// for reasons that have nothing to do with whether the claim is true -- so the comparative
// assertion below is guarded by a RUNTIME DETECTOR (not an assumption) that inspects the actual
// extracted values and GTEST_SKIPs with the observed values when the field is absent OR
// present-but-identical for both arms ("zero-for-both", per this package's brief -- the case where
// package B may have added the instrumentation before package A's serving-depth mechanism actually
// affects anything). The detector auto-flips to "live" the moment both packages are merged, with no
// edits needed here.
// ============================================================================================
#include "rr/evaluation/experiment_runner.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>
#include <type_traits>
#include <utility>

#include "rr/infrastructure/config.hpp"

using namespace rr;

namespace {

namespace fs = std::filesystem;

// --- Reduced event-mode dataset (this package's brief: "~300 users/3000 reels/dim 32/horizon sized
//     for ~=60 interactions/user, drift at interaction 30 whole population single-topic mix").
//     Users /reels/creators/topics match tests/property/adaptation_statistical_test.cpp's P10 drift
//     fixture (~300/3000/150/16) so this file's scale is directly comparable to that established
//     precedent. --------------------------------------------------------------------------------
constexpr uint64_t kSeed = 20260718;
constexpr uint32_t kUsers = 300;
constexpr uint32_t kReels = 3000;
constexpr uint32_t kCreators = 150;
constexpr uint32_t kTopics = 16;
constexpr uint32_t kDimensions = 32;
constexpr uint32_t kVectorCandidates = 200;

// Horizon sizing: MEASURED DIRECTLY against this exact fixture shape rather than extrapolated from
// commit.md's Phase 18 prose (that entry's "200u/2000r/6h -> 12,380 impressions" figure does NOT
// reproduce from the committed tests/golden/event-digest/config.json -- re-running that exact file
// today yields only ~1,567 impressions/200 users [~7.8/user], not ~62/user; likely prose from an
// uncommitted intermediate run predating the pinned digest fixture). Empirically, at THIS fixture's
// exact shape (300 users/3000 reels/16 topics/dim 32, feed_size defaulted to 10, plus the drift
// event below), horizon_seconds=21600 (6h, the naive "same as the golden digest fixture" guess)
// yielded only ~7/user; horizon_seconds=86400 (1 simulated day) yields ~57/user (17,021 impressions
// / 300 users, measured with the drift event active) -- squarely at the brief's ~=60
// interactions/user target. The non-linearity (6h->~7/user but 24h->~57/user, far more than a 4x
// jump) is consistent with the default `scheduling.open_stagger_seconds=43200` (12h) and
// `return_delay_mean_seconds=21600` (6h) consuming a large, roughly FIXED chunk of any short
// horizon before most users even complete their first session/return cycle.
constexpr double kHorizonSeconds = 86400.0; // 1 simulated day

// Drift: whole-population (cohortLo=0, cohortHi=1), a SINGLE topic (weight 1.0, not the 3-topic
// mixes configs/phase10-drift.json uses for its per-quartile cohorts), at per-user interaction 30
// -- roughly the midpoint of this fixture's ~60-interaction/user horizon, giving each user a
// comparable pre-drift baseline window and post-drift recovery window on average.
constexpr uint32_t kDriftAtInteraction = 30;
constexpr uint32_t kDriftTopic = kTopics - 1; // 15: as far as possible from a uniformly-sampled
                                              // initial preference, same reasoning as
                                              // adaptation_statistical_test.cpp's drift target.

// --- Acceptance margin (named constant; STARTING placeholder -- pre-integration there is no real
//     batch-depth adaptation effect anywhere to calibrate against, so this is a documented,
//     directionally-motivated margin for the integrator to recalibrate once package A lands and the
//     actual effect size is measurable, exactly like the sibling statistical tests' margins). A
//     handful of interactions: small relative to the ~30 post-drift interactions most users get in
//     this fixture (horizon sized for ~60/user, drift at interaction 30) -- generous enough to
//     avoid flaking on noise, tight enough to still enforce the qualitative "no slower" claim.
//     ------------
constexpr double kAdaptationDelaySlackInteractions = 5.0;

namespace detail {

// SFINAE probe: true iff `T` (expected: EventModeReport) has a member named
// `meanAdaptationDelayInteractions`. Degrades to false_type via ordinary substitution failure (not
// a hard error) when the member does not exist -- the case at this scaffold. See the header TODO
// block above for the full contract; mirrors
// tests/property/session_exit_statistical_test.cpp's `HasSessionHealthMember` probe one level down
// the struct hierarchy (EventModeReport already exists as a member of ExperimentResult in this tree
// -- Phase 18 landed it -- only this ONE inner field is still missing).
template <typename T, typename = void> struct HasAdaptationDelayInteractions : std::false_type {};

template <typename T>
struct HasAdaptationDelayInteractions<
    T, std::void_t<decltype(std::declval<const T &>().meanAdaptationDelayInteractions)>>
    : std::true_type {};

struct AdaptationDelayView {
    bool available = false; // true iff the field exists AND holds a defined (>= 0) value
    double value = 0.0;     // interactions since drift until (population) satisfaction recovered;
                            // meaningless when !available
};

// Template so the true-branch's `em.meanAdaptationDelayInteractions` access is type-DEPENDENT and
// only checked by the compiler when actually instantiated with an EventModeReport that has the
// member -- `if constexpr` discards the untaken, dependent branch from semantic analysis entirely
// (the standard C++17 "detect and branch" idiom already used by session_exit_statistical_test.cpp
// and personalized_vs_fixed_statistical_test.cpp).
template <typename EventModeReportT>
AdaptationDelayView extractAdaptationDelay(const EventModeReportT &em) {
    if constexpr (HasAdaptationDelayInteractions<EventModeReportT>::value) {
        AdaptationDelayView v;
        // Best-effort guess at package A's sentinel convention, mirroring AdaptationReport's (P10)
        // recoveryInteractions: a negative value means "never recovered / undefined" within the
        // run.
        const double raw = static_cast<double>(em.meanAdaptationDelayInteractions);
        if (em.configured && raw >= 0.0) {
            v.available = true;
            v.value = raw;
        }
        return v;
    } else {
        return AdaptationDelayView{}; // available == false: package A not merged into this tree yet
    }
}

} // namespace detail

ExperimentConfig baseConfig() {
    ExperimentConfig c;
    c.simulation.seed = kSeed;
    c.simulation.users = kUsers;
    c.simulation.reels = kReels;
    c.simulation.creators = kCreators;
    c.simulation.topics = kTopics;
    c.simulation.dimensions = kDimensions;
    c.simulation.scheduler = "event_queue";
    c.simulation.horizonSeconds = kHorizonSeconds;
    c.recommendation.vectorCandidates = kVectorCandidates;
    c.algorithm = RecommendationAlgorithm::HnswRanker;
    // Gate ON (Phase 18/19): event_queue requires session_dynamics requires latent_reactions
    // requires content_v2 (D17). Constructed directly in C++ (not via JSON), so from_json's
    // "requires" checks never run, but every gate is set true anyway, satisfying the invariant.
    c.realism.contentV2 = true;
    c.realism.latentReactions = true;
    c.realism.sessionDynamics = true;
    // Keep the run fast and stream-aligned: no sampled oracle-regret / retrieval evaluation.
    c.evaluation.oracleSampleRate = 0.0;
    c.evaluation.retrievalSampleRate = 0.0;

    // Whole-population drift to a single topic at per-user interaction 30 (see the named constants'
    // doc comments above). DriftEvent.atInteraction keys off each user's own completed-interaction
    // count -- identical semantics under event mode and round-robin (commit.md's Phase 18 entry:
    // "drift keeps the verbatim interaction-count keying"), so no event-mode-specific retiming is
    // needed here beyond the fixture's horizon sizing.
    DriftEvent e;
    e.atInteraction = kDriftAtInteraction;
    e.cohortLo = 0.0;
    e.cohortHi = 1.0;
    e.topicMix = {DriftTopicWeight{kDriftTopic, 1.0}};
    c.drift.events = {e};
    return c;
}

// One arm's config: base + serving.prefetch_depth (refill_threshold fixed at 0 -- "refill only when
// the prefetched deque is empty", the Phase 18 depth-1-refill default -- so the SINGLE free
// variable across arms is prefetch depth, matching scripts/run_phase19_experiment.sh's experiment
// design).
ExperimentConfig batchConfig(uint32_t prefetchDepth) {
    ExperimentConfig c = baseConfig();
    c.serving.prefetchDepth = prefetchDepth;
    c.serving.refillThreshold = 0;
    return c;
}

// One arm's headline numbers: what already exists today (impressions, EventModeReport's real
// fields, for the sanity + determinism checks) plus the best-effort adaptation-delay view (for the
// pending-integration comparative check).
struct ArmSummary {
    std::string tag;
    std::size_t impressions = 0;
    bool eventModeConfigured = false;
    std::size_t eventCount = 0;
    uint64_t eventLogDigest = 0;
    detail::AdaptationDelayView adaptationDelay;
};

ArmSummary runArm(const std::string &tag, const ExperimentConfig &config) {
    const fs::path root = fs::path(::testing::TempDir()) / ("rr_batch_adaptation_" + tag);
    fs::remove_all(root);
    ExperimentRunner runner(config, root);
    const ExperimentResult r = runner.run();
    fs::remove_all(root); // reclaim the on-disk output; everything asserted on is in memory

    ArmSummary a;
    a.tag = tag;
    a.impressions = r.impressionCount;
    a.eventModeConfigured = r.eventMode.configured;
    a.eventCount = r.eventMode.eventCount;
    a.eventLogDigest = r.eventMode.eventLogDigest;
    a.adaptationDelay = detail::extractAdaptationDelay(r.eventMode);
    return a;
}

// Package-A runtime detector: is the adaptation-delay field both PRESENT and MEANINGFULLY DIFFERENT
// between the two batch depths? Covers the brief's two named pending states in one check: "absent"
// (SFINAE false, both views report !available) and "zero-for-both" (present but identical --
// package B may have landed the instrumentation before package A's serving-depth mechanism actually
// changes anything, or the field's sentinel/zero convention means "not yet wired").
bool adaptationDelayDistinguishable(const detail::AdaptationDelayView &a,
                                    const detail::AdaptationDelayView &b) {
    if (!a.available || !b.available) {
        return false;
    }
    if (a.value == 0.0 && b.value == 0.0) {
        return false; // "zero-for-both" per this package's brief
    }
    return a.value != b.value;
}

} // namespace

// Fixture: runs every arm ONCE (arms are expensive) in SetUpTestSuite and shares the results across
// the per-assertion tests below (mirrors PersonalizedVsFixedStatisticalTest's fixture shape,
// tests/property/personalized_vs_fixed_statistical_test.cpp).
class BatchAdaptationStatisticalTest : public ::testing::Test {
  protected:
    static ArmSummary batch1_;
    static ArmSummary batch20_;
    static ArmSummary batch1Rerun_; // same config+seed as batch1_, a determinism check

    static void SetUpTestSuite() {
        batch1_ = runArm("batch1", batchConfig(1));
        batch20_ = runArm("batch20", batchConfig(20));
        batch1Rerun_ = runArm("batch1_rerun", batchConfig(1));

        auto line = [](const ArmSummary &a) {
            std::cout << "[batch-adaptation] " << a.tag << ": impressions=" << a.impressions
                      << " eventModeConfigured=" << std::boolalpha << a.eventModeConfigured
                      << " eventCount=" << a.eventCount << " eventLogDigest=" << a.eventLogDigest
                      << " adaptationDelay.available=" << a.adaptationDelay.available
                      << " adaptationDelay.value=" << a.adaptationDelay.value << "\n";
        };
        std::cout << "===== Phase 19 batch-adaptation arm summary (reduced event-mode config) "
                     "=====\n";
        line(batch1_);
        line(batch20_);
        line(batch1Rerun_);
        std::cout << "[batch-adaptation] adaptation-delay distinguishable (package A live)="
                  << std::boolalpha
                  << adaptationDelayDistinguishable(batch1_.adaptationDelay,
                                                    batch20_.adaptationDelay)
                  << "\n"
                  << "========================================================================"
                     "==\n";
    }
};

ArmSummary BatchAdaptationStatisticalTest::batch1_;
ArmSummary BatchAdaptationStatisticalTest::batch20_;
ArmSummary BatchAdaptationStatisticalTest::batch1Rerun_;

// Sanity (HOLDS TODAY, no pending integration): both batch-depth arms run end-to-end through
// ExperimentRunner under the full event-mode gate stack + drift without throwing, and produce real
// event-mode data. Regression coverage for the Phase 19 config surface (serving.* parsing +
// event-mode validation, src/infrastructure/config.cpp) independent of whether package A has
// landed.
TEST_F(BatchAdaptationStatisticalTest, GateCombinationRunsProduceEventModeData) {
    for (const ArmSummary *a : {&batch1_, &batch20_}) {
        EXPECT_GT(a->impressions, 0u) << a->tag;
        EXPECT_TRUE(a->eventModeConfigured) << a->tag;
        EXPECT_GT(a->eventCount, 0u) << a->tag;
    }
}

// Assertion (V2 TDD §7 / plan Phase 19 task 4 / REELS-SIMULATION-V2.md §7's batch-size-one clause):
// batch-size-1 (prefetch_depth=1) must adapt NO SLOWER than batch-size-20 (prefetch_depth=20) after
// the whole-population drift, i.e. adaptationDelay(batch1) <= adaptationDelay(batch20) + slack.
// PENDING PACKAGE A INTEGRATION -- see the header TODO block; auto-activates once EventModeReport
// carries a real, differentiated adaptation-delay figure for the two batch depths.
TEST_F(BatchAdaptationStatisticalTest, Batch1AdaptsNoSlowerThanBatch20) {
    const bool live =
        adaptationDelayDistinguishable(batch1_.adaptationDelay, batch20_.adaptationDelay);
    if (!live) {
        GTEST_SKIP()
            << "PENDING PACKAGE A INTEGRATION: EventModeReport's adaptation-delay field ("
            << "best-effort guess: meanAdaptationDelayInteractions -- see this file's header TODO "
               "block if package A used a different name) is "
            << (batch1_.adaptationDelay.available ? "present" : "ABSENT")
            << " on batch1 (value=" << batch1_.adaptationDelay.value << ") and "
            << (batch20_.adaptationDelay.available ? "present" : "ABSENT")
            << " on batch20 (value=" << batch20_.adaptationDelay.value << "); "
            << (batch1_.adaptationDelay.available && batch20_.adaptationDelay.available
                    ? "both present but IDENTICAL -- package A's serving-depth mechanism does not "
                      "yet change the event runner's behaviour"
                    : "package A's serving/staleness instrumentation is not merged in this tree")
            << ". Expected post-merge: adaptationDelay(batch1) <= adaptationDelay(batch20) + "
            << kAdaptationDelaySlackInteractions << " (batch1's small, immediately-consumed feed "
            << "should react to the post-drift preference on the very next request, while "
            << "batch20's large prefetched batch keeps serving stale, pre-drift-ranked content "
            << "until exhausted).";
    }
    EXPECT_LE(batch1_.adaptationDelay.value,
              batch20_.adaptationDelay.value + kAdaptationDelaySlackInteractions)
        << "batch1 (prefetch_depth=1) adaptation delay " << batch1_.adaptationDelay.value
        << " vs batch20 (prefetch_depth=20) " << batch20_.adaptationDelay.value << " (+slack "
        << kAdaptationDelaySlackInteractions << ")";
}

// Determinism (D8, HOLDS TODAY regardless of package-A integration status): a same-seed,
// same-config rerun of the batch1 arm reproduces every existing event-mode number bit-identically,
// and (once real) the adaptation-delay figure too. Determinism does not depend on the serving-depth
// mechanism being behaviourally "real", only on it being deterministic, which the pre-integration
// stub (a fixed depth-1 refill regardless of config) already is.
TEST_F(BatchAdaptationStatisticalTest, Batch1ArmRerunIsDeterministic) {
    EXPECT_EQ(batch1_.impressions, batch1Rerun_.impressions);
    ASSERT_TRUE(batch1_.eventModeConfigured);
    ASSERT_TRUE(batch1Rerun_.eventModeConfigured);
    EXPECT_EQ(batch1_.eventCount, batch1Rerun_.eventCount);
    EXPECT_EQ(batch1_.eventLogDigest, batch1Rerun_.eventLogDigest);
    EXPECT_EQ(batch1_.adaptationDelay.available, batch1Rerun_.adaptationDelay.available);
    if (batch1_.adaptationDelay.available && batch1Rerun_.adaptationDelay.available) {
        EXPECT_EQ(batch1_.adaptationDelay.value, batch1Rerun_.adaptationDelay.value);
    }
}
