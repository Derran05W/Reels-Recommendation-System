#include "rr/evaluation/experiment_runner.hpp"

#include <gtest/gtest.h>

#include <filesystem>

using namespace rr;

namespace {

namespace fs = std::filesystem;

// Small-but-statistically-meaningful config shared by both arms (phase-4 task 7). Same seed so the
// two arms see the same dataset and behaviour stream; only the recommender differs.
ExperimentConfig comparisonConfig(RecommendationAlgorithm algo) {
    ExperimentConfig c;
    c.simulation.seed = 20260710;
    c.simulation.users = 300;
    c.simulation.reels = 3000;
    c.simulation.creators = 150;
    c.simulation.topics = 16;
    c.simulation.dimensions = 32;
    c.simulation.interactionsPerUser = 30;
    c.recommendation.feedSize = 10;
    c.recommendation.vectorCandidates = 300;
    c.evaluation.oracleSampleRate = 0.05;
    c.algorithm = algo;
    return c;
}

MetricsSummary runArm(RecommendationAlgorithm algo, const fs::path &root) {
    fs::remove_all(root);
    ExperimentRunner runner(comparisonConfig(algo), root);
    return runner.run().overall;
}

} // namespace

// Exact-vector personalization must beat random on BOTH mean true affinity and mean reward per
// impression by a clear margin (phase-4 exit criterion "exact personalization outperforms
// random"). Even with static cold-start estimates (every user shares the global-average estimate),
// ExactVector targets population-favourable content, so it wins on the population mean.
TEST(BaselineComparisonTest, ExactVectorBeatsRandom) {
    const MetricsSummary random =
        runArm(RecommendationAlgorithm::Random, fs::path(::testing::TempDir()) / "rr_cmp_random");
    const MetricsSummary exact = runArm(RecommendationAlgorithm::ExactVector,
                                        fs::path(::testing::TempDir()) / "rr_cmp_exact");

    EXPECT_GT(exact.meanTrueAffinity, random.meanTrueAffinity + 0.05)
        << "exact affinity " << exact.meanTrueAffinity << " vs random " << random.meanTrueAffinity;
    EXPECT_GT(exact.rewardPerImpression, random.rewardPerImpression + 0.02)
        << "exact reward " << exact.rewardPerImpression << " vs random "
        << random.rewardPerImpression;
}
