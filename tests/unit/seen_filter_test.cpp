#include "rr/recommendation/seen_filter.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include "rr/domain/reel.hpp"
#include "rr/domain/user.hpp"

namespace {

rr::Reel makeReel(uint32_t id, bool active) {
    rr::Reel reel{};
    reel.id = rr::ReelId{id};
    reel.durationSeconds = 10.0f;
    reel.active = active;
    return reel;
}

rr::User makeUser(std::vector<uint32_t> seen) {
    rr::User user{};
    user.id = rr::UserId{0};
    for (uint32_t s : seen) {
        user.seenReels.insert(rr::ReelId{s});
    }
    return user;
}

} // namespace

TEST(SeenFilterTest, ActiveUnseenIsEligible) {
    const rr::User user = makeUser({});
    EXPECT_TRUE(rr::isEligible(makeReel(7, true), user));
}

TEST(SeenFilterTest, InactiveIsNotEligible) {
    const rr::User user = makeUser({});
    EXPECT_FALSE(rr::isEligible(makeReel(7, false), user));
}

TEST(SeenFilterTest, SeenIsNotEligible) {
    const rr::User user = makeUser({7});
    EXPECT_FALSE(rr::isEligible(makeReel(7, true), user));
}

TEST(SeenFilterTest, SeenAndInactiveIsNotEligible) {
    const rr::User user = makeUser({7});
    EXPECT_FALSE(rr::isEligible(makeReel(7, false), user));
}

TEST(SeenFilterTest, EligibleIndicesExcludeSeenAndInactiveInAscendingOrder) {
    std::vector<rr::Reel> reels;
    for (uint32_t i = 0; i < 6; ++i) {
        reels.push_back(makeReel(i, /*active=*/true));
    }
    reels[2].active = false;             // inactive -> excluded
    const rr::User user = makeUser({4}); // seen -> excluded

    const std::vector<std::size_t> indices = rr::eligibleReelIndices(reels, user);
    EXPECT_EQ(indices, (std::vector<std::size_t>{0, 1, 3, 5}));
}

TEST(SeenFilterTest, EligibleIndicesEmptyWhenAllExcluded) {
    std::vector<rr::Reel> reels{makeReel(0, false), makeReel(1, true)};
    const rr::User user = makeUser({1});
    EXPECT_TRUE(rr::eligibleReelIndices(reels, user).empty());
}
