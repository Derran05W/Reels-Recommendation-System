// Unit tests for HNSWExplorationRecommender (Phase 8, TDD 16.7). Verify construction / name /
// retrieval-index wiring, onReelsAppended indexing, determinism, and the epsilon=0 no-op against
// HNSWRankerRecommender on a single seed (the many-seed flagship version lives in the property
// suite).
#include "rr/recommendation/hnsw_exploration_recommender.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "rr/core/embedding.hpp"
#include "rr/domain/recommendation.hpp"
#include "rr/domain/reel.hpp"
#include "rr/domain/user.hpp"
#include "rr/infrastructure/config.hpp"
#include "rr/infrastructure/random.hpp"
#include "rr/recommendation/recommender.hpp"
#include "rr/recommendation/recommender_factory.hpp"
#include "rr/recommendation/vector_index.hpp"
#include "rr/simulation/dataset_generator.hpp"

namespace {

rr::ExperimentConfig smallConfig() {
    rr::ExperimentConfig config{};
    config.simulation.reels = 400;
    config.simulation.users = 15;
    config.simulation.creators = 40;
    config.simulation.topics = 8;
    config.simulation.dimensions = 16;
    config.algorithm = rr::RecommendationAlgorithm::HnswRankerExploration;
    return config;
}

// Generated users start with an empty estimatedPreference (cold-start init is a later phase); give
// each their normalized hidden preference so the personalizing sources have a valid query (test
// setup only, mirroring pipeline_property_test).
void seedPreferences(rr::GeneratedDataset &ds) {
    for (std::size_t i = 0; i < ds.users.size(); ++i) {
        ds.users[i].estimatedPreference = ds.hiddenStates[i].hiddenPreference;
    }
}

std::size_t countActive(const std::vector<rr::Reel> &reels) {
    std::size_t n = 0;
    for (const rr::Reel &r : reels) {
        if (r.active) {
            ++n;
        }
    }
    return n;
}

rr::Reel appendedReel(uint32_t id, uint32_t dimensions) {
    rr::Reel reel{};
    reel.id = rr::ReelId{id};
    reel.creatorId = rr::CreatorId{id};
    rr::Embedding e(dimensions, 0.0f);
    e[id % dimensions] = 1.0f;
    e[(id + 1) % dimensions] = 0.5f;
    rr::normalize(e);
    reel.embedding = std::move(e);
    reel.intrinsicQuality = 0.5f;
    reel.durationSeconds = 20.0f;
    reel.createdAt = 100;
    reel.active = true;
    return reel;
}

rr::RecommendationRequest requestFor(const rr::User &user, bool enableExploration) {
    rr::RecommendationRequest req{};
    req.userId = user.id;
    req.feedSize = 10;
    req.candidateLimit = 200;
    req.enableExploration = enableExploration;
    req.requestTime = 1'000'000;
    return req;
}

void expectFeedsEqual(const rr::RecommendationResponse &a, const rr::RecommendationResponse &b) {
    ASSERT_EQ(a.reels.size(), b.reels.size());
    for (std::size_t i = 0; i < a.reels.size(); ++i) {
        EXPECT_EQ(a.reels[i].reelId, b.reels[i].reelId) << "position " << i;
        EXPECT_FLOAT_EQ(a.reels[i].score, b.reels[i].score) << "position " << i;
        EXPECT_EQ(a.reels[i].sources, b.reels[i].sources) << "position " << i;
        EXPECT_EQ(a.reels[i].featureContributions.size(), b.reels[i].featureContributions.size());
        for (const auto &[key, value] : a.reels[i].featureContributions) {
            ASSERT_EQ(b.reels[i].featureContributions.count(key), 1u) << key;
            EXPECT_FLOAT_EQ(value, b.reels[i].featureContributions.at(key)) << key;
        }
    }
}

} // namespace

TEST(HNSWExplorationRecommenderTest, ConstructsNameAndRetrievalIndex) {
    rr::ExperimentConfig config = smallConfig();
    rr::GeneratedDataset ds = rr::generateDataset(config.simulation, 1);
    seedPreferences(ds);
    rr::RecommenderDeps deps{ds.reels, ds.users, config};
    auto rec = rr::makeRecommender(rr::RecommendationAlgorithm::HnswRankerExploration, deps,
                                   rr::forkRng(1, "recommender"));
    ASSERT_NE(rec, nullptr);
    EXPECT_EQ(rec->name(), "hnsw_ranker_exploration");
    ASSERT_NE(rec->retrievalIndex(), nullptr);
    EXPECT_EQ(rec->retrievalIndex()->size(), countActive(ds.reels));
}

TEST(HNSWExplorationRecommenderTest, OnReelsAppendedIndexesNewActiveReels) {
    rr::ExperimentConfig config = smallConfig();
    rr::GeneratedDataset ds = rr::generateDataset(config.simulation, 2);
    seedPreferences(ds);
    rr::RecommenderDeps deps{ds.reels, ds.users, config};
    auto rec = rr::makeRecommender(rr::RecommendationAlgorithm::HnswRankerExploration, deps,
                                   rr::forkRng(2, "recommender"));
    const std::size_t before = rec->retrievalIndex()->size();

    const auto firstNew = static_cast<uint32_t>(ds.reels.size());
    for (uint32_t i = 0; i < 3; ++i) {
        ds.reels.push_back(appendedReel(firstNew + i, config.simulation.dimensions));
    }
    rec->onReelsAppended(firstNew);
    EXPECT_EQ(rec->retrievalIndex()->size(), before + 3);
}

TEST(HNSWExplorationRecommenderTest, EpsilonZeroMatchesHnswRankerAcrossUsers) {
    rr::ExperimentConfig config = smallConfig();
    config.exploration.enabled = true; // exploration ON...
    config.exploration.epsilon = 0.0;  // ...but epsilon 0 => a verified no-op
    rr::GeneratedDataset ds = rr::generateDataset(config.simulation, 7);
    seedPreferences(ds);
    rr::RecommenderDeps deps{ds.reels, ds.users, config};

    auto expl = rr::makeRecommender(rr::RecommendationAlgorithm::HnswRankerExploration, deps,
                                    rr::forkRng(7, "recommender"));
    auto ranker = rr::makeRecommender(rr::RecommendationAlgorithm::HnswRanker, deps,
                                      rr::forkRng(7, "recommender"));
    for (const rr::User &user : ds.users) {
        const rr::RecommendationResponse a =
            expl->recommend(requestFor(user, /*enableExploration=*/true));
        const rr::RecommendationResponse b =
            ranker->recommend(requestFor(user, /*enableExploration=*/true));
        expectFeedsEqual(a, b);
    }
}

TEST(HNSWExplorationRecommenderTest, DeterministicSameSeed) {
    rr::ExperimentConfig config = smallConfig();
    config.exploration.enabled = true;
    config.exploration.epsilon = 0.3;
    rr::GeneratedDataset ds = rr::generateDataset(config.simulation, 5);
    seedPreferences(ds);
    rr::RecommenderDeps deps{ds.reels, ds.users, config};

    auto recA = rr::makeRecommender(rr::RecommendationAlgorithm::HnswRankerExploration, deps,
                                    rr::forkRng(5, "recommender"));
    auto recB = rr::makeRecommender(rr::RecommendationAlgorithm::HnswRankerExploration, deps,
                                    rr::forkRng(5, "recommender"));
    for (const rr::User &user : ds.users) {
        const rr::RecommendationResponse a = recA->recommend(requestFor(user, true));
        const rr::RecommendationResponse b = recB->recommend(requestFor(user, true));
        expectFeedsEqual(a, b);
    }
}
