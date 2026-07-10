#include "rr/simulation/reel_generator.hpp"

#include <gtest/gtest.h>

#include <algorithm>

#include "rr/core/embedding.hpp"
#include "rr/infrastructure/config.hpp"
#include "rr/infrastructure/random.hpp"
#include "rr/simulation/creator_generator.hpp"
#include "rr/simulation/topic_generator.hpp"

using rr::Creator;
using rr::dot;
using rr::forkRng;
using rr::generateCreators;
using rr::generateReels;
using rr::generateTopics;
using rr::isValid;
using rr::Reel;
using rr::SimulationConfig;
using rr::Topic;

namespace {
// createdAt window must match reel_generator.cpp's constant (30 days in logical seconds).
constexpr uint64_t kCreationWindowSeconds = 30ULL * 24 * 60 * 60;

struct World {
    SimulationConfig config;
    std::vector<Topic> topics;
    std::vector<Creator> creators;
    std::vector<Reel> reels;
};

World makeWorld() {
    World w;
    w.config.seed = 123;
    w.config.topics = 8;
    w.config.creators = 40;
    w.config.reels = 500;
    w.config.dimensions = 16;
    rr::Rng tRng = forkRng(w.config.seed, "topics");
    w.topics = generateTopics(w.config, tRng);
    rr::Rng cRng = forkRng(w.config.seed, "creators");
    w.creators = generateCreators(w.config, w.topics, cRng);
    rr::Rng rRng = forkRng(w.config.seed, "reels");
    w.reels = generateReels(w.config, w.topics, w.creators, rRng);
    return w;
}
} // namespace

TEST(ReelGeneratorTest, CountAndDenseIds) {
    World w = makeWorld();
    ASSERT_EQ(w.reels.size(), 500u);
    for (uint32_t i = 0; i < w.reels.size(); ++i) {
        EXPECT_EQ(w.reels[i].id.value, i);
        EXPECT_LT(w.reels[i].creatorId.value, w.config.creators);
    }
}

TEST(ReelGeneratorTest, EmbeddingsValid) {
    World w = makeWorld();
    for (const Reel &r : w.reels) {
        EXPECT_EQ(r.embedding.size(), w.config.dimensions);
        EXPECT_TRUE(isValid(r.embedding));
    }
}

TEST(ReelGeneratorTest, TopicLabelsConsistentWithConstruction) {
    World w = makeWorld();
    for (const Reel &r : w.reels) {
        // Primary label is a real topic id.
        ASSERT_LT(r.primaryTopic.value, w.config.topics);
        // Secondary labels are valid, distinct from the primary, and mutually distinct.
        std::vector<uint32_t> seen{r.primaryTopic.value};
        for (rr::TopicId t : r.secondaryTopics) {
            EXPECT_LT(t.value, w.config.topics);
            EXPECT_EQ(std::count(seen.begin(), seen.end(), t.value), 0);
            seen.push_back(t.value);
        }
        // The primary topic is a dominant contributor to the embedding: because it carries the
        // largest construction weight, the embedding correlates strongly with its centre. This
        // ties the stored label to the vector actually built from it.
        EXPECT_GT(dot(r.embedding, w.topics[r.primaryTopic.value].centre), 0.1f);
    }
}

TEST(ReelGeneratorTest, PrimaryTopicBiasedTowardCreatorSpecialties) {
    World w = makeWorld();
    size_t inSpecialty = 0;
    for (const Reel &r : w.reels) {
        const Creator &cr = w.creators[r.creatorId.value];
        if (std::count(cr.topicSpecialties.begin(), cr.topicSpecialties.end(), r.primaryTopic) >
            0) {
            ++inSpecialty;
        }
    }
    // Bias is 0.8; even accounting for uniform-fallback picks that happen to hit a specialty,
    // a comfortable majority must be creator-aligned.
    EXPECT_GT(static_cast<double>(inSpecialty) / w.reels.size(), 0.6);
}

TEST(ReelGeneratorTest, DurationsWithinDefinedBuckets) {
    World w = makeWorld();
    for (const Reel &r : w.reels) {
        EXPECT_GE(r.durationSeconds, 5.0f);
        EXPECT_LT(r.durationSeconds, 120.0f);
    }
}

TEST(ReelGeneratorTest, QualityInUnitInterval) {
    World w = makeWorld();
    for (const Reel &r : w.reels) {
        EXPECT_GE(r.intrinsicQuality, 0.0f);
        EXPECT_LE(r.intrinsicQuality, 1.0f);
    }
}

TEST(ReelGeneratorTest, CountersZeroedAndActiveAndFreshWindow) {
    World w = makeWorld();
    for (const Reel &r : w.reels) {
        EXPECT_EQ(r.impressionCount, 0u);
        EXPECT_EQ(r.completionCount, 0u);
        EXPECT_EQ(r.likeCount, 0u);
        EXPECT_EQ(r.shareCount, 0u);
        EXPECT_EQ(r.skipCount, 0u);
        EXPECT_TRUE(r.active);
        EXPECT_EQ(r.freshnessScore, 0.0f);
        EXPECT_LT(r.createdAt, kCreationWindowSeconds);
    }
}

TEST(ReelGeneratorTest, ZeroReelsIsEmpty) {
    World w = makeWorld();
    w.config.reels = 0;
    rr::Rng rRng = forkRng(w.config.seed, "reels");
    std::vector<Reel> reels = generateReels(w.config, w.topics, w.creators, rRng);
    EXPECT_TRUE(reels.empty());
}

TEST(ReelGeneratorTest, EmptyInputsDoNotCrash) {
    SimulationConfig c;
    c.reels = 10;
    c.dimensions = 16;
    rr::Rng rng = forkRng(c.seed, "reels");
    std::vector<Topic> noTopics;
    std::vector<Creator> noCreators;
    // No topics or no creators => nothing to build a reel from => empty, no crash.
    EXPECT_TRUE(generateReels(c, noTopics, noCreators, rng).empty());
}
