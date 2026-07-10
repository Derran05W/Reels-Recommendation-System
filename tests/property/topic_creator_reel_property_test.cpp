#include <gtest/gtest.h>

#include <vector>

#include "rr/core/embedding.hpp"
#include "rr/domain/creator.hpp"
#include "rr/domain/reel.hpp"
#include "rr/infrastructure/config.hpp"
#include "rr/infrastructure/random.hpp"
#include "rr/simulation/creator_generator.hpp"
#include "rr/simulation/reel_generator.hpp"
#include "rr/simulation/topic_generator.hpp"

using rr::Creator;
using rr::forkRng;
using rr::generateCreators;
using rr::generateReels;
using rr::generateTopics;
using rr::isValid;
using rr::Reel;
using rr::SimulationConfig;
using rr::Topic;

namespace {

// Byte-identical comparison helpers: floats compared with == (identical computation => identical
// bits). Each returns false on the first difference so the property tests spend one assertion.

bool topicsEqual(const std::vector<Topic> &a, const std::vector<Topic> &b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i].id.value != b[i].id.value || a[i].centre != b[i].centre) {
            return false;
        }
    }
    return true;
}

bool creatorsEqual(const std::vector<Creator> &a, const std::vector<Creator> &b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (size_t i = 0; i < a.size(); ++i) {
        const Creator &x = a[i];
        const Creator &y = b[i];
        if (x.id.value != y.id.value || x.baseQuality != y.baseQuality ||
            x.styleEmbedding != y.styleEmbedding ||
            x.topicSpecialties.size() != y.topicSpecialties.size()) {
            return false;
        }
        for (size_t k = 0; k < x.topicSpecialties.size(); ++k) {
            if (x.topicSpecialties[k].value != y.topicSpecialties[k].value) {
                return false;
            }
        }
    }
    return true;
}

bool reelsEqual(const std::vector<Reel> &a, const std::vector<Reel> &b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (size_t i = 0; i < a.size(); ++i) {
        const Reel &x = a[i];
        const Reel &y = b[i];
        if (x.id.value != y.id.value || x.creatorId.value != y.creatorId.value ||
            x.embedding != y.embedding || x.intrinsicQuality != y.intrinsicQuality ||
            x.freshnessScore != y.freshnessScore || x.durationSeconds != y.durationSeconds ||
            x.primaryTopic.value != y.primaryTopic.value ||
            x.secondaryTopics.size() != y.secondaryTopics.size() || x.createdAt != y.createdAt ||
            x.impressionCount != y.impressionCount || x.completionCount != y.completionCount ||
            x.likeCount != y.likeCount || x.shareCount != y.shareCount ||
            x.skipCount != y.skipCount || x.active != y.active) {
            return false;
        }
        for (size_t k = 0; k < x.secondaryTopics.size(); ++k) {
            if (x.secondaryTopics[k].value != y.secondaryTopics[k].value) {
                return false;
            }
        }
    }
    return true;
}

bool allTopicEmbeddingsValid(const std::vector<Topic> &t) {
    for (const Topic &x : t) {
        if (!isValid(x.centre)) {
            return false;
        }
    }
    return true;
}

bool allCreatorEmbeddingsValid(const std::vector<Creator> &c) {
    for (const Creator &x : c) {
        if (!isValid(x.styleEmbedding)) {
            return false;
        }
    }
    return true;
}

bool allReelEmbeddingsValid(const std::vector<Reel> &r) {
    for (const Reel &x : r) {
        if (!isValid(x.embedding)) {
            return false;
        }
    }
    return true;
}

SimulationConfig sweepConfig(uint64_t seed) {
    SimulationConfig c;
    c.seed = seed;
    c.topics = 16;
    c.creators = 64;
    c.reels = 256;
    c.dimensions = 32;
    return c;
}

} // namespace

// Same seed => byte-identical topics, creators, and reels, swept over many seeds. Also asserts
// every generated embedding is valid/normalized across all seeds. This is the structural guarantee
// behind "regenerating one subsystem never changes another" (independent named Rng streams, D8).
TEST(TopicCreatorReelProperty, DeterministicAndValidAcrossSeeds) {
    for (uint64_t seed = 0; seed < 25; ++seed) { // >= 20 distinct seeds
        SCOPED_TRACE(testing::Message() << "seed=" << seed);
        const SimulationConfig c = sweepConfig(seed);

        rr::Rng tA = forkRng(seed, "topics");
        rr::Rng tB = forkRng(seed, "topics");
        std::vector<Topic> topicsA = generateTopics(c, tA);
        std::vector<Topic> topicsB = generateTopics(c, tB);
        ASSERT_TRUE(topicsEqual(topicsA, topicsB));
        ASSERT_TRUE(allTopicEmbeddingsValid(topicsA));

        rr::Rng cA = forkRng(seed, "creators");
        rr::Rng cB = forkRng(seed, "creators");
        std::vector<Creator> creatorsA = generateCreators(c, topicsA, cA);
        std::vector<Creator> creatorsB = generateCreators(c, topicsA, cB);
        ASSERT_TRUE(creatorsEqual(creatorsA, creatorsB));
        ASSERT_TRUE(allCreatorEmbeddingsValid(creatorsA));

        rr::Rng rA = forkRng(seed, "reels");
        rr::Rng rB = forkRng(seed, "reels");
        std::vector<Reel> reelsA = generateReels(c, topicsA, creatorsA, rA);
        std::vector<Reel> reelsB = generateReels(c, topicsA, creatorsA, rB);
        ASSERT_TRUE(reelsEqual(reelsA, reelsB));
        ASSERT_TRUE(allReelEmbeddingsValid(reelsA));
    }
}

// Exit-criterion scale: 100k reels / 5k creators / 32 topics generate deterministically and with
// all embeddings valid. Kept to a single seed (two full generations) so runtime stays modest.
TEST(TopicCreatorReelProperty, FullScaleDeterministicAndValid) {
    SimulationConfig c;
    c.seed = 2024;
    c.topics = 32;
    c.creators = 5000;
    c.reels = 100000;
    c.dimensions = 64;

    rr::Rng tA = forkRng(c.seed, "topics");
    rr::Rng tB = forkRng(c.seed, "topics");
    std::vector<Topic> topicsA = generateTopics(c, tA);
    std::vector<Topic> topicsB = generateTopics(c, tB);
    ASSERT_TRUE(topicsEqual(topicsA, topicsB));

    rr::Rng cA = forkRng(c.seed, "creators");
    rr::Rng cB = forkRng(c.seed, "creators");
    std::vector<Creator> creatorsA = generateCreators(c, topicsA, cA);
    std::vector<Creator> creatorsB = generateCreators(c, topicsA, cB);
    ASSERT_TRUE(creatorsEqual(creatorsA, creatorsB));

    rr::Rng rA = forkRng(c.seed, "reels");
    rr::Rng rB = forkRng(c.seed, "reels");
    std::vector<Reel> reelsA = generateReels(c, topicsA, creatorsA, rA);
    std::vector<Reel> reelsB = generateReels(c, topicsA, creatorsA, rB);
    ASSERT_EQ(reelsA.size(), 100000u);
    ASSERT_TRUE(reelsEqual(reelsA, reelsB));

    EXPECT_TRUE(allTopicEmbeddingsValid(topicsA));
    EXPECT_TRUE(allCreatorEmbeddingsValid(creatorsA));
    EXPECT_TRUE(allReelEmbeddingsValid(reelsA));
}
