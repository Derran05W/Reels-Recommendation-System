// Serving-strategy PIPELINE integration test (Phase 19, V2 TDD §4.13, D17/D20/D22). Drives full
// event_queue experiments end-to-end through the ExperimentRunner (which dispatches to the
// EventDrivenRunner) and asserts the Phase-19 contracts that need the whole pipeline:
//   1. the REGRESSION CONTRACT — DEFAULT serving reproduces the P18 pinned event-log digest AND
//      byte-identical event CSVs (my serving/instrumentation changes must not perturb the default
//      event stream);
//   2. non-default serving stays same-seed deterministic yet demonstrably changes the event stream;
//   3. the batch-size × cost frontier smoke — batch-1 and batch-20 both run, emit
//   serving_metrics.csv
//      + the summary.json event_mode.serving block, and batch-20 costs less (fewer requests / less
//      ranking) — the statistical "batch-1 adapts no slower" test is package C's.
// The exact serving decision math is unit-tested in tests/unit/serving_strategy_test.cpp.

#include "rr/evaluation/experiment_runner.hpp"

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

using namespace rr;

namespace {

namespace fs = std::filesystem;

// The Phase 18 golden event-digest fixture (tests/golden/event-digest/config.json), reproduced in
// C++ so the in-process run is self-contained. DEFAULT serving (no serving block). The pinned
// digest
// + count below are the committed golden (tests/golden/event-digest/digest.txt).
constexpr uint64_t kGoldenDigest = 1533553118870293663ULL;
constexpr std::size_t kGoldenEventCount = 5602;

ExperimentConfig goldenConfig() {
    ExperimentConfig c;
    c.simulation.seed = 42;
    c.simulation.users = 200;
    c.simulation.reels = 2000;
    c.simulation.creators = 40;
    c.simulation.topics = 8;
    c.simulation.dimensions = 32;
    c.simulation.scheduler = "event_queue";
    c.simulation.horizonSeconds = 21600.0; // 6 simulated hours
    c.recommendation.feedSize = 5;
    c.recommendation.vectorCandidates = 100;
    c.recommendation.popularCandidates = 50;
    c.recommendation.freshCandidates = 50;
    c.recommendation.explorationCandidates = 25;
    c.hnsw.m = 16;
    c.hnsw.efConstruction = 200;
    c.hnsw.efSearch = 64;
    c.evaluation.oracleSampleRate = 0.1;
    c.evaluation.retrievalSampleRate = 0.0;
    c.algorithm = RecommendationAlgorithm::HnswRanker;
    c.realism.contentV2 = true;
    c.realism.latentReactions = true;
    c.realism.sessionDynamics = true;
    c.scheduling.openStaggerSeconds = 43200.0;
    c.scheduling.returnDelayMeanSeconds = 21600.0;
    c.scheduling.returnDelaySpreadRel = 0.5;
    // serving.* left at defaults (prefetch_depth 0, refill_threshold 0, invalidate off).
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

ExperimentResult runAt(const ExperimentConfig &c, const std::string &tag) {
    const fs::path root = fs::path(::testing::TempDir()) / ("rr_p19i_" + tag);
    fs::remove_all(root);
    return ExperimentRunner(c, root).run();
}

// The deterministic CSVs the runner emits (D8): byte-identical across same-seed runs. Includes the
// new Phase-19 serving_metrics.csv.
const char *const kDeterministicCsvs[] = {"welfare_metrics.csv",   "welfare_archetype_metrics.csv",
                                          "session_health.csv",    "recommendation_metrics.csv",
                                          "diversity_metrics.csv", "serving_metrics.csv"};

} // namespace

// --- 1. REGRESSION CONTRACT: default serving reproduces the P18 pinned digest
// ---------------------
TEST(ServingPipelineTest, DefaultServingReproducesPinnedGoldenDigest) {
    const ExperimentResult r = runAt(goldenConfig(), "golden");
    ASSERT_TRUE(r.eventMode.configured);
    // The exact P18 golden — proves the serving/instrumentation refactor left the DEFAULT event
    // stream byte-identical (D17/D20). If this fails, default serving changed behaviour.
    EXPECT_EQ(r.eventMode.eventLogDigest, kGoldenDigest);
    EXPECT_EQ(r.eventMode.eventCount, kGoldenEventCount);
    // Default serving is echoed: depth resolves to feed_size (5), threshold 0, invalidate off.
    EXPECT_EQ(r.eventMode.servingPrefetchDepth, 5u);
    EXPECT_EQ(r.eventMode.servingRefillThreshold, 0u);
    EXPECT_FALSE(r.eventMode.servingInvalidateOnIntentChange);
    // Preserve-downloaded default => no feed is ever invalidated.
    EXPECT_EQ(r.eventMode.feedInvalidationCount, 0u);
    // NOTE: staleness is NON-zero even under the P18 default — feed_size 5 with learning on means
    // items 2..5 of each prefetched batch are served against a model updated since the batch was
    // ranked. The instrumentation surfaces this pre-existing depth>1 staleness (it does not create
    // it): the digest above is byte-identical, so the event stream is unchanged.
    EXPECT_GT(r.eventMode.staleImpressionCount, 0u);
    EXPECT_GT(r.eventMode.meanStaleness, 0.0);
}

// --- Default serving: same seed => byte-identical deterministic CSVs (incl. serving_metrics.csv)
// ---
TEST(ServingPipelineTest, DefaultServingSameSeedByteIdenticalCsvs) {
    const ExperimentResult a = runAt(goldenConfig(), "golden_a");
    const ExperimentResult b = runAt(goldenConfig(), "golden_b");
    EXPECT_EQ(a.eventMode.eventLogDigest, b.eventMode.eventLogDigest);
    for (const char *f : kDeterministicCsvs) {
        EXPECT_EQ(readFile(a.directory / f), readFile(b.directory / f))
            << "non-deterministic file: " << f;
    }
}

// --- 2. Non-default serving: deterministic, but a DIFFERENT event stream from default
// --------------
TEST(ServingPipelineTest, NonDefaultDepthIsDeterministicAndChangesBehaviour) {
    ExperimentConfig deep = goldenConfig();
    deep.serving.prefetchDepth = 3; // a batch-3 client cache

    const ExperimentResult a = runAt(deep, "depth3_a");
    const ExperimentResult b = runAt(deep, "depth3_b");
    // Same seed + config => identical digest AND byte-identical deterministic CSVs.
    EXPECT_EQ(a.eventMode.eventLogDigest, b.eventMode.eventLogDigest);
    EXPECT_EQ(a.eventMode.eventCount, b.eventMode.eventCount);
    for (const char *f : kDeterministicCsvs) {
        EXPECT_EQ(readFile(a.directory / f), readFile(b.directory / f))
            << "non-deterministic file: " << f;
    }
    // ...yet a non-default depth genuinely changes the served event stream (staler, deeper
    // prefetch), so its digest differs from the pinned default golden.
    EXPECT_NE(a.eventMode.eventLogDigest, kGoldenDigest);
    EXPECT_EQ(a.eventMode.servingPrefetchDepth, 3u);
}

// --- 3. Batch-size frontier smoke: both batch sizes run + emit the cost/staleness columns
// ----------
TEST(ServingPipelineTest, BatchOneAndBatchTwentyBothRunAndExposeFrontierColumns) {
    ExperimentConfig b1 = goldenConfig();
    b1.serving.prefetchDepth = 1; // batch-1
    ExperimentConfig b20 = goldenConfig();
    b20.serving.prefetchDepth = 20; // batch-20

    const ExperimentResult r1 = runAt(b1, "batch1");
    const ExperimentResult r20 = runAt(b20, "batch20");

    // Both batch sizes complete and serve a real world.
    ASSERT_GT(r1.impressionCount, 0u);
    ASSERT_GT(r20.impressionCount, 0u);

    // The cost axis of the frontier: batch-20 amortizes requests + ranking over deeper prefetch.
    EXPECT_LT(r20.requestCount, r1.requestCount);
    EXPECT_LT(r20.eventMode.rankingComputations, r1.eventMode.rankingComputations);
    // Batch-1 is always fresh (staleness 0); batch-20 accrues staleness within each deep feed.
    EXPECT_EQ(r1.eventMode.staleImpressionCount, 0u);
    EXPECT_GT(r20.eventMode.staleImpressionRate, 0.0);

    // serving_metrics.csv is written with the per-day schema package C plots against.
    const fs::path csv = r20.directory / "serving_metrics.csv";
    ASSERT_TRUE(fs::exists(csv));
    const std::string head = readFile(csv).substr(0, 128);
    EXPECT_NE(head.find("day,feed_requests,ranking_computations,impressions,stale_impressions,"
                        "stale_impression_rate,mean_staleness,satisfaction_lost"),
              std::string::npos);

    // The summary.json event_mode.serving block carries the run-level frontier numbers (package C
    // reads event_mode.serving.* per run).
    const nlohmann::json summary = readJson(r20.directory / "summary.json");
    ASSERT_TRUE(summary.contains("event_mode"));
    ASSERT_TRUE(summary.at("event_mode").contains("serving"));
    const nlohmann::json &sv = summary.at("event_mode").at("serving");
    for (const char *key : {"prefetch_depth", "refill_threshold", "invalidate_on_intent_change",
                            "feed_requests", "ranking_computations", "mean_staleness",
                            "stale_impression_rate", "satisfaction_lost_before_refresh"}) {
        EXPECT_TRUE(sv.contains(key)) << "event_mode.serving missing key: " << key;
    }
    EXPECT_EQ(sv.at("prefetch_depth").get<uint32_t>(), 20u);
    EXPECT_EQ(sv.at("ranking_computations").get<std::uint64_t>(),
              r20.eventMode.rankingComputations);
}

// --- Adaptation-delay instrumentation is live and well-formed under configured drift -------------
// The batch-size-vs-adaptation STATISTICAL test ("batch-1 adapts no slower") is package C's; here
// we only prove the drift-only adaptation path runs end-to-end and populates sane numbers.
TEST(ServingPipelineTest, AdaptationDelayInstrumentationLiveUnderDrift) {
    ExperimentConfig c = goldenConfig();
    c.simulation.horizonSeconds = 12.0 * 3600.0;  // longer so users pass the drift interaction
    c.scheduling.openStaggerSeconds = 3600.0;     // users open early (within the first hour)...
    c.scheduling.returnDelayMeanSeconds = 1800.0; // ...and return often -> many interactions/user
    c.serving.prefetchDepth = 3;                  // a non-trivial batch under drift
    // Abrupt whole-population drift onto two high-index topics (valid for topics=8), at interaction
    // 10 (== the trailing window, so pre-drift history is measurable).
    DriftEvent ev;
    ev.atInteraction = 10;
    ev.cohortLo = 0.0;
    ev.cohortHi = 1.0;
    ev.topicMix = {DriftTopicWeight{6, 1.0}, DriftTopicWeight{7, 1.0}};
    c.drift.events = {ev};

    const ExperimentResult r = runAt(c, "drift_adapt");
    ASSERT_TRUE(r.eventMode.configured);
    ASSERT_GT(r.impressionCount, 0u);

    // Adaptation is drift-only: configured here, and the aggregates are internally consistent.
    EXPECT_TRUE(r.eventMode.adaptationConfigured);
    EXPECT_GT(r.eventMode.adaptationDriftedUsers, 0u); // active users pass interaction 10
    EXPECT_LE(r.eventMode.adaptationRecoveredUsers, r.eventMode.adaptationDriftedUsers);
    if (r.eventMode.adaptationRecoveredUsers > 0) {
        EXPECT_GT(r.eventMode.meanAdaptationDelayInteractions, 0.0);
        EXPECT_GT(r.eventMode.medianAdaptationDelayInteractions, 0.0);
    }

    // The summary.json serving block gains the adaptation_delay sub-block ONLY under drift.
    const nlohmann::json summary = readJson(r.directory / "summary.json");
    const nlohmann::json &sv = summary.at("event_mode").at("serving");
    ASSERT_TRUE(sv.contains("adaptation_delay"));
    EXPECT_EQ(sv.at("adaptation_delay").at("drifted_users").get<std::size_t>(),
              r.eventMode.adaptationDriftedUsers);
}

// --- Round-robin path carries no serving_metrics.csv and no event_mode.serving block (D17)
// --------
TEST(ServingPipelineTest, RoundRobinPathHasNoServingArtifacts) {
    ExperimentConfig c = goldenConfig();
    c.simulation.scheduler =
        "round_robin"; // legacy path; serving.* stays default (no validation err)
    c.simulation.interactionsPerUser = 10;
    const ExperimentResult r = runAt(c, "legacy");

    EXPECT_FALSE(r.eventMode.configured);
    EXPECT_FALSE(fs::exists(r.directory / "serving_metrics.csv"));
    const nlohmann::json summary = readJson(r.directory / "summary.json");
    EXPECT_FALSE(summary.contains("event_mode"))
        << "round-robin summary.json must carry no event_mode/serving block (D17 byte-identity)";
}
