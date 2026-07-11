#include "rr/evaluation/cold_start.hpp"

#include <gtest/gtest.h>

#include <stdexcept>

using namespace rr;

namespace {

HiddenUserState withPreference(Embedding e) {
    HiddenUserState h;
    h.hiddenPreference = std::move(e);
    return h;
}

} // namespace

TEST(ColdStartTest, GlobalAverageIsNormalizedMean) {
    std::vector<HiddenUserState> hidden;
    hidden.push_back(withPreference({1.0f, 0.0f, 0.0f}));
    hidden.push_back(withPreference({0.0f, 1.0f, 0.0f}));

    Embedding avg = globalAveragePreference(hidden);
    ASSERT_TRUE(isValid(avg));
    // mean = (0.5, 0.5, 0) -> normalized = (1/sqrt(2), 1/sqrt(2), 0)
    EXPECT_NEAR(avg[0], 0.70710678f, 1e-6f);
    EXPECT_NEAR(avg[1], 0.70710678f, 1e-6f);
    EXPECT_NEAR(avg[2], 0.0f, 1e-6f);
}

TEST(ColdStartTest, EmptyPopulationThrows) {
    EXPECT_THROW(globalAveragePreference({}), std::invalid_argument);
}

TEST(ColdStartTest, OpposedPopulationHasNoDirectionAndThrows) {
    std::vector<HiddenUserState> hidden;
    hidden.push_back(withPreference({1.0f, 0.0f}));
    hidden.push_back(withPreference({-1.0f, 0.0f}));
    EXPECT_THROW(globalAveragePreference(hidden), std::invalid_argument);
}

TEST(ColdStartTest, ApplyColdStartSetsAllThreeVectors) {
    std::vector<User> users(3);
    Embedding prior = {0.6f, 0.8f};
    applyColdStart(users, prior);
    for (const User &u : users) {
        EXPECT_EQ(u.estimatedPreference, prior);
        EXPECT_EQ(u.longTermPreference, prior);
        EXPECT_EQ(u.sessionPreference, prior);
    }
}
