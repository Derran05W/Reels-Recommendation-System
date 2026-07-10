#include "rr/simulation/topic_generator.hpp"

#include <gtest/gtest.h>

#include "rr/core/embedding.hpp"
#include "rr/infrastructure/config.hpp"
#include "rr/infrastructure/random.hpp"

using rr::forkRng;
using rr::generateTopics;
using rr::isValid;
using rr::SimulationConfig;
using rr::Topic;

namespace {
SimulationConfig smallConfig() {
    SimulationConfig c;
    c.seed = 42;
    c.topics = 8;
    c.dimensions = 16;
    return c;
}
} // namespace

TEST(TopicGeneratorTest, CountAndDenseIds) {
    SimulationConfig c = smallConfig();
    rr::Rng rng = forkRng(c.seed, "topics");
    std::vector<Topic> topics = generateTopics(c, rng);
    ASSERT_EQ(topics.size(), 8u);
    for (uint32_t i = 0; i < topics.size(); ++i) {
        EXPECT_EQ(topics[i].id.value, i);
    }
}

TEST(TopicGeneratorTest, EmbeddingsValidAndCorrectSize) {
    SimulationConfig c = smallConfig();
    rr::Rng rng = forkRng(c.seed, "topics");
    std::vector<Topic> topics = generateTopics(c, rng);
    for (const Topic &t : topics) {
        EXPECT_EQ(t.centre.size(), c.dimensions);
        EXPECT_TRUE(isValid(t.centre));
    }
}

TEST(TopicGeneratorTest, ZeroTopicsIsEmpty) {
    SimulationConfig c = smallConfig();
    c.topics = 0;
    rr::Rng rng = forkRng(c.seed, "topics");
    std::vector<Topic> topics = generateTopics(c, rng);
    EXPECT_TRUE(topics.empty());
}

TEST(TopicGeneratorTest, SameSeedIdentical) {
    SimulationConfig c = smallConfig();
    rr::Rng a = forkRng(c.seed, "topics");
    rr::Rng b = forkRng(c.seed, "topics");
    std::vector<Topic> ta = generateTopics(c, a);
    std::vector<Topic> tb = generateTopics(c, b);
    ASSERT_EQ(ta.size(), tb.size());
    for (size_t i = 0; i < ta.size(); ++i) {
        EXPECT_EQ(ta[i].centre, tb[i].centre); // exact, byte-identical float compare
    }
}
