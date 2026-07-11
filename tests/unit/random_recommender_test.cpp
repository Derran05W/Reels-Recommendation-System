#include "rr/recommendation/random_recommender.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <unordered_set>
#include <vector>

#include "rr/domain/reel.hpp"
#include "rr/domain/user.hpp"
#include "rr/infrastructure/config.hpp"
#include "rr/infrastructure/random.hpp"
#include "rr/recommendation/recommender_factory.hpp"

namespace {

rr::Reel makeReel(uint32_t id, bool active) {
    rr::Reel reel{};
    reel.id = rr::ReelId{id};
    reel.durationSeconds = 10.0f;
    reel.active = active;
    return reel;
}

// Dense reels 0..n-1, all active.
std::vector<rr::Reel> denseReels(uint32_t n) {
    std::vector<rr::Reel> reels;
    for (uint32_t i = 0; i < n; ++i) {
        reels.push_back(makeReel(i, /*active=*/true));
    }
    return reels;
}

rr::User makeUser(std::vector<uint32_t> seen) {
    rr::User user{};
    user.id = rr::UserId{0};
    for (uint32_t s : seen) {
        user.seenReels.insert(rr::ReelId{s});
    }
    return user;
}

rr::RecommendationRequest request(std::size_t feedSize) {
    rr::RecommendationRequest req{};
    req.userId = rr::UserId{0};
    req.feedSize = feedSize;
    return req;
}

std::vector<uint32_t> feedIds(const rr::RecommendationResponse &response) {
    std::vector<uint32_t> ids;
    for (const rr::RankedReel &r : response.reels) {
        ids.push_back(r.reelId.value);
    }
    return ids;
}

} // namespace

TEST(RandomRecommenderTest, ReturnsExactFeedSizeDistinctUnseenActiveWithRanks) {
    const std::vector<rr::Reel> reels = denseReels(20);
    const std::vector<rr::User> users{makeUser({})};
    const rr::ExperimentConfig config{};
    const rr::RecommenderDeps deps{reels, users, config};

    rr::RandomRecommender rec(deps, rr::Rng(123));
    const rr::RecommendationResponse response = rec.recommend(request(10));

    ASSERT_EQ(response.reels.size(), 10u);
    std::unordered_set<uint32_t> seen;
    for (std::size_t i = 0; i < response.reels.size(); ++i) {
        const rr::RankedReel &r = response.reels[i];
        EXPECT_EQ(r.rank, i);           // ranks are 0..n-1 in output order
        EXPECT_EQ(r.score, 0.0f);       // no scoring for random
        EXPECT_TRUE(r.sources.empty()); // no "Random" candidate source
        EXPECT_TRUE(seen.insert(r.reelId.value).second) << "duplicate reel in feed";
        EXPECT_LT(r.reelId.value, 20u);
    }
    EXPECT_EQ(response.candidatesRetrieved, 20u);
    EXPECT_EQ(response.candidatesRanked, 10u);
}

TEST(RandomRecommenderTest, ExcludesSeenAndInactive) {
    std::vector<rr::Reel> reels = denseReels(10);
    reels[3].active = false;
    reels[7].active = false;
    const std::vector<rr::User> users{makeUser({1, 5})};
    const rr::ExperimentConfig config{};
    const rr::RecommenderDeps deps{reels, users, config};

    rr::RandomRecommender rec(deps, rr::Rng(9));
    // 10 reels - 2 inactive - 2 seen = 6 eligible.
    const rr::RecommendationResponse response = rec.recommend(request(6));
    ASSERT_EQ(response.reels.size(), 6u);
    for (const rr::RankedReel &r : response.reels) {
        EXPECT_NE(r.reelId.value, 1u);
        EXPECT_NE(r.reelId.value, 5u);
        EXPECT_NE(r.reelId.value, 3u);
        EXPECT_NE(r.reelId.value, 7u);
    }
}

TEST(RandomRecommenderTest, FewerThanFeedSizeWhenPoolExhausted) {
    std::vector<rr::Reel> reels = denseReels(5);
    reels[0].active = false;
    reels[1].active = false;
    const std::vector<rr::User> users{makeUser({})};
    const rr::ExperimentConfig config{};
    const rr::RecommenderDeps deps{reels, users, config};

    rr::RandomRecommender rec(deps, rr::Rng(3));
    const rr::RecommendationResponse response = rec.recommend(request(10));
    EXPECT_EQ(response.reels.size(), 3u); // only 3 eligible
    EXPECT_EQ(response.candidatesRetrieved, 3u);
}

TEST(RandomRecommenderTest, SameSeedProducesIdenticalFeed) {
    const std::vector<rr::Reel> reels = denseReels(50);
    const std::vector<rr::User> users{makeUser({})};
    const rr::ExperimentConfig config{};
    const rr::RecommenderDeps deps{reels, users, config};

    rr::RandomRecommender a(deps, rr::Rng(777));
    rr::RandomRecommender b(deps, rr::Rng(777));
    EXPECT_EQ(feedIds(a.recommend(request(10))), feedIds(b.recommend(request(10))));
}

TEST(RandomRecommenderTest, DifferentSeedsProduceDifferentFeeds) {
    const std::vector<rr::Reel> reels = denseReels(100);
    const std::vector<rr::User> users{makeUser({})};
    const rr::ExperimentConfig config{};
    const rr::RecommenderDeps deps{reels, users, config};

    rr::RandomRecommender a(deps, rr::Rng(1));
    rr::RandomRecommender b(deps, rr::Rng(2));
    // With a 100-reel pool and a 10-item feed the chance of an identical ordering is negligible.
    EXPECT_NE(feedIds(a.recommend(request(10))), feedIds(b.recommend(request(10))));
}

TEST(RandomRecommenderTest, NameIsRandom) {
    const std::vector<rr::Reel> reels = denseReels(1);
    const std::vector<rr::User> users{makeUser({})};
    const rr::ExperimentConfig config{};
    const rr::RecommenderDeps deps{reels, users, config};
    rr::RandomRecommender rec(deps, rr::Rng(0));
    EXPECT_EQ(rec.name(), "random");
}
