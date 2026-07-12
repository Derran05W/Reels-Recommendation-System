#include "rr/evaluation/experiment_runner.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "rr/evaluation/cold_start.hpp"
#include "rr/infrastructure/random.hpp"
#include "rr/recommendation/recommender.hpp"
#include "rr/recommendation/recommender_factory.hpp"
#include "rr/recommendation/vector_index.hpp"
#include "rr/simulation/dataset_generator.hpp"

using namespace rr;

namespace {

namespace fs = std::filesystem;

// Small-but-real cold-start config: reel + user injection at the start of round 1 (0-based), with
// round 0 supplying a pre-injection reward baseline. 2 rounds (20 interactions / feed 10) means
// injected users get exactly their first feed-round of impressions - a clean cold-start window.
ExperimentConfig injectionConfig(RecommendationAlgorithm algo) {
    ExperimentConfig c;
    c.simulation.seed = 11;
    c.simulation.users = 200;
    c.simulation.reels = 2000;
    c.simulation.creators = 40;
    c.simulation.topics = 16;
    c.simulation.dimensions = 32;
    c.simulation.interactionsPerUser = 20;
    c.simulation.newReels = 200;
    c.simulation.newReelsAt = 1;
    c.simulation.newUsers = 200;
    c.simulation.newUsersAt = 1;
    c.recommendation.feedSize = 10; // -> ceil(20/10) = 2 rounds
    c.recommendation.vectorCandidates = 200;
    c.evaluation.oracleSampleRate = 0.1;
    c.algorithm = algo;
    return c;
}

std::string readFile(const fs::path &p) {
    std::ifstream f(p, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

std::size_t countDataRows(const fs::path &p) {
    std::ifstream f(p);
    std::string line;
    std::size_t rows = 0;
    bool header = true;
    while (std::getline(f, line)) {
        if (header) {
            header = false;
            continue;
        }
        if (!line.empty()) {
            ++rows;
        }
    }
    return rows;
}

} // namespace

// Mid-simulation REEL injection grows the recommender's vector index through onReelsAppended (the
// end-to-end mechanism the harness relies on). Verified directly at the recommender level because
// ExperimentRunner encapsulates the recommender.
TEST(InjectionExposureTest, ReelInjectionGrowsRecommenderIndex) {
    const ExperimentConfig cfg = injectionConfig(RecommendationAlgorithm::HnswRanker);
    const SimulationConfig &sc = cfg.simulation;

    GeneratedDataset ds = generateDataset(sc, sc.seed);
    applyColdStart(ds.users, globalAveragePreference(ds.hiddenStates));

    RecommenderDeps deps{ds.reels, ds.users, cfg};
    std::unique_ptr<Recommender> rec =
        makeRecommender(RecommendationAlgorithm::HnswRanker, deps, forkRng(sc.seed, "recommender"));
    ASSERT_NE(rec->retrievalIndex(), nullptr);

    const std::size_t before = rec->retrievalIndex()->size();
    EXPECT_EQ(before, sc.reels); // all original reels are active

    const std::size_t firstNew = appendReels(ds, sc, sc.seed, sc.newReels, /*createdAt=*/500000);
    rec->onReelsAppended(firstNew);

    const std::size_t after = rec->retrievalIndex()->size();
    EXPECT_EQ(after, before + sc.newReels) << "injected active reels were not indexed via the hook";
}

// A full hnsw_ranker run with reel + user injection: run completes, the two Phase-8 files are
// written and populated, injected users receive feeds, and the new files are deterministic across
// two same-seed runs (byte-identical).
TEST(InjectionExposureTest, MidRunInjectionWritesDeterministicColdStartFiles) {
    const fs::path rootA = fs::path(::testing::TempDir()) / "rr_inject_a";
    const fs::path rootB = fs::path(::testing::TempDir()) / "rr_inject_b";
    fs::remove_all(rootA);
    fs::remove_all(rootB);

    const ExperimentConfig cfg = injectionConfig(RecommendationAlgorithm::HnswRanker);
    ExperimentRunner runnerA(cfg, rootA);
    ExperimentRunner runnerB(cfg, rootB);
    const ExperimentResult a = runnerA.run();
    const ExperimentResult b = runnerB.run();

    // Run completed with the full population (originals + injected).
    EXPECT_EQ(a.userCount, cfg.simulation.users + cfg.simulation.newUsers);
    EXPECT_EQ(a.reelCount, cfg.simulation.reels + cfg.simulation.newReels);

    // Cold-start report configured and populated.
    ASSERT_TRUE(a.coldStart.configured);
    EXPECT_EQ(a.coldStart.newUsers, cfg.simulation.newUsers);
    EXPECT_EQ(a.coldStart.newReels, cfg.simulation.newReels);

    // Injected users received feeds: every injected user reaches its first impression (index 0).
    ASSERT_FALSE(a.coldStart.newUserCurve.empty());
    EXPECT_EQ(a.coldStart.newUserCurve.front().impressionIndex, 0u);
    EXPECT_EQ(a.coldStart.newUserCurve.front().usersAtIndex, cfg.simulation.newUsers);
    // Injected at round 1 with 2 rounds total -> exactly feedSize (10) impressions each.
    EXPECT_EQ(a.coldStart.newUserCurve.size(), cfg.recommendation.feedSize);

    // Exposure rows: one per round.
    EXPECT_EQ(a.coldStart.newReelExposure.size(), a.rounds.size());

    // Pre-injection reward baseline is defined (round 0 ran before injection at round 1).
    EXPECT_TRUE(a.coldStart.targetDefined);

    // The two Phase-8 files exist and are populated.
    const fs::path userCurve = a.directory / "new_user_curve.csv";
    const fs::path reelExposure = a.directory / "new_reel_exposure.csv";
    ASSERT_TRUE(fs::exists(userCurve));
    ASSERT_TRUE(fs::exists(reelExposure));
    EXPECT_EQ(countDataRows(userCurve), cfg.recommendation.feedSize);
    EXPECT_EQ(countDataRows(reelExposure), a.rounds.size());

    // Determinism: same seed twice -> byte-identical Phase-8 files.
    EXPECT_EQ(readFile(userCurve), readFile(b.directory / "new_user_curve.csv"));
    EXPECT_EQ(readFile(reelExposure), readFile(b.directory / "new_reel_exposure.csv"));
}

// CROSS-PACKAGE TEST (EXPECTED FAIL until Package A's exploration recommender is merged):
// with mid-simulation injection, exploration ON should give injected (fresh, zero-history) reels
// MORE impressions than exploration OFF. Today makeRecommender throws std::invalid_argument for
// `hnsw_ranker_exploration` ("delivered in Phase 8"), so run() throws and this test FAILS. It is
// correct and PASSES once the real HNSWExplorationRecommender + factory dispatch land at merge.
TEST(InjectionExposureTest, ExplorationIncreasesNewReelExposure) {
    const fs::path rootOn = fs::path(::testing::TempDir()) / "rr_inject_explore_on";
    const fs::path rootOff = fs::path(::testing::TempDir()) / "rr_inject_explore_off";
    fs::remove_all(rootOn);
    fs::remove_all(rootOff);

    ExperimentConfig on = injectionConfig(RecommendationAlgorithm::HnswRankerExploration);
    on.exploration.enabled = true;
    ExperimentConfig off = injectionConfig(RecommendationAlgorithm::HnswRankerExploration);
    off.exploration.enabled = false;

    // Both constructions currently throw in makeRecommender (factory: not implemented until merge).
    ExperimentRunner runnerOn(on, rootOn);
    ExperimentRunner runnerOff(off, rootOff);
    const ExperimentResult rOn = runnerOn.run();
    const ExperimentResult rOff = runnerOff.run();

    EXPECT_GT(rOn.coldStart.totalInjectedImpressions, rOff.coldStart.totalInjectedImpressions)
        << "exploration ON should surface injected fresh reels more than exploration OFF";
}
