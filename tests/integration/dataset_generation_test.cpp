#include "rr/simulation/dataset_generator.hpp"

#include "rr/core/embedding.hpp"

#include <gtest/gtest.h>

#include <chrono>

using namespace rr;

namespace {

// Byte-identical comparison across two full dataset generations. Floats compared with == since
// identical computation on identical inputs produces identical bits.
void expectIdenticalDatasets(const GeneratedDataset &a, const GeneratedDataset &b) {
    ASSERT_EQ(a.topics.size(), b.topics.size());
    for (size_t i = 0; i < a.topics.size(); ++i) {
        EXPECT_EQ(a.topics[i].id, b.topics[i].id);
        EXPECT_EQ(a.topics[i].centre, b.topics[i].centre);
    }

    ASSERT_EQ(a.creators.size(), b.creators.size());
    for (size_t i = 0; i < a.creators.size(); ++i) {
        EXPECT_EQ(a.creators[i].id, b.creators[i].id);
        EXPECT_EQ(a.creators[i].styleEmbedding, b.creators[i].styleEmbedding);
        EXPECT_EQ(a.creators[i].baseQuality, b.creators[i].baseQuality);
    }

    ASSERT_EQ(a.reels.size(), b.reels.size());
    for (size_t i = 0; i < a.reels.size(); ++i) {
        EXPECT_EQ(a.reels[i].id, b.reels[i].id);
        EXPECT_EQ(a.reels[i].creatorId, b.reels[i].creatorId);
        EXPECT_EQ(a.reels[i].embedding, b.reels[i].embedding);
        EXPECT_EQ(a.reels[i].createdAt, b.reels[i].createdAt);
    }

    ASSERT_EQ(a.users.size(), b.users.size());
    ASSERT_EQ(a.hiddenStates.size(), b.hiddenStates.size());
    for (size_t i = 0; i < a.hiddenStates.size(); ++i) {
        EXPECT_EQ(a.users[i].id, b.users[i].id);
        EXPECT_EQ(a.hiddenStates[i].userId, b.hiddenStates[i].userId);
        EXPECT_EQ(a.hiddenStates[i].hiddenPreference, b.hiddenStates[i].hiddenPreference);
    }
}

} // namespace

// Exit criterion (Phase 2): 100k reels + 10k users + 5k creators generate deterministically, in a
// few seconds, using the default SimulationConfig (which already matches these scale targets).
TEST(DatasetGenerationTest, FullScaleDeterministicAndTimed) {
    SimulationConfig config; // defaults: 10k users, 100k reels, 5k creators, 32 topics, dim 64.

    const auto start = std::chrono::steady_clock::now();
    GeneratedDataset a = generateDataset(config, /*seed=*/2024);
    const auto elapsed = std::chrono::steady_clock::now() - start;

    EXPECT_EQ(a.topics.size(), config.topics);
    EXPECT_EQ(a.creators.size(), config.creators);
    EXPECT_EQ(a.reels.size(), config.reels);
    EXPECT_EQ(a.users.size(), config.users);
    EXPECT_EQ(a.hiddenStates.size(), config.users);
    EXPECT_LT(std::chrono::duration_cast<std::chrono::seconds>(elapsed).count(), 10)
        << "full-scale generation should complete in a few seconds";

    GeneratedDataset b = generateDataset(config, /*seed=*/2024);
    expectIdenticalDatasets(a, b);

    for (const auto &t : a.topics) {
        EXPECT_TRUE(isValid(t.centre));
    }
    for (const auto &c : a.creators) {
        EXPECT_TRUE(isValid(c.styleEmbedding));
    }
    for (const auto &r : a.reels) {
        EXPECT_TRUE(isValid(r.embedding));
    }
    for (const auto &h : a.hiddenStates) {
        EXPECT_TRUE(isValid(h.hiddenPreference));
    }
}

// Structural guarantee behind D8's independent named Rng streams: regenerating reels at a
// different scale must not perturb the generated users at all, for the same master seed.
TEST(DatasetGenerationTest, RegeneratingReelsNeverChangesUsers) {
    SimulationConfig small;
    small.topics = 8;
    small.creators = 20;
    small.reels = 50;
    small.users = 100;
    small.dimensions = 32;

    SimulationConfig widerReels = small;
    widerReels.reels = 5000; // only the reel count changes

    const uint64_t seed = 7;
    GeneratedDataset withFewReels = generateDataset(small, seed);
    GeneratedDataset withManyReels = generateDataset(widerReels, seed);

    ASSERT_EQ(withFewReels.users.size(), withManyReels.users.size());
    ASSERT_EQ(withFewReels.hiddenStates.size(), withManyReels.hiddenStates.size());
    for (size_t i = 0; i < withFewReels.hiddenStates.size(); ++i) {
        EXPECT_EQ(withFewReels.users[i].id, withManyReels.users[i].id);
        EXPECT_EQ(withFewReels.hiddenStates[i].userId, withManyReels.hiddenStates[i].userId);
        EXPECT_EQ(withFewReels.hiddenStates[i].hiddenPreference,
                  withManyReels.hiddenStates[i].hiddenPreference);
    }
    EXPECT_NE(withFewReels.reels.size(), withManyReels.reels.size());
}
