#include "rr/recommendation/popularity_recommender.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include "rr/domain/reel.hpp"
#include "rr/domain/user.hpp"
#include "rr/infrastructure/config.hpp"
#include "rr/infrastructure/random.hpp"
#include "rr/recommendation/recommender_factory.hpp"

namespace {

rr::Reel makeReel(uint32_t id, uint64_t impressions, uint64_t completions, uint64_t likes,
                  uint64_t shares) {
    rr::Reel reel{};
    reel.id = rr::ReelId{id};
    reel.durationSeconds = 10.0f;
    reel.active = true;
    reel.impressionCount = impressions;
    reel.completionCount = completions;
    reel.likeCount = likes;
    reel.shareCount = shares;
    return reel;
}

rr::User emptyUser() {
    rr::User user{};
    user.id = rr::UserId{0};
    return user;
}

rr::RecommendationRequest request(std::size_t feedSize) {
    rr::RecommendationRequest req{};
    req.userId = rr::UserId{0};
    req.feedSize = feedSize;
    return req;
}

} // namespace

TEST(PopularityRecommenderTest, EngagementNumeratorWeightsCompletionLikeShare) {
    // completion*1 + like*2 + share*4 = 3 + 2*5 + 4*7 = 3 + 10 + 28 = 41.
    EXPECT_DOUBLE_EQ(rr::popularityEngagement(makeReel(0, 100, 3, 5, 7)), 41.0);
}

TEST(PopularityRecommenderTest, SmoothedFormulaMatchesHandComputation) {
    // engagement = 10, impressions = 4, priorMean = 0.5, C = 20 (default):
    // (10 + 20*0.5) / (1 + 4 + 20) = 20 / 25 = 0.8.
    const rr::Reel reel = makeReel(0, /*imp=*/4, /*comp=*/10, 0, 0);
    EXPECT_DOUBLE_EQ(rr::smoothedPopularity(reel, /*priorMean=*/0.5), 0.8);
    // Custom C overrides the default.
    EXPECT_DOUBLE_EQ(rr::smoothedPopularity(reel, /*priorMean=*/0.0, /*C=*/0.0), 10.0 / 5.0);
}

TEST(PopularityRecommenderTest, TinySampleDoesNotOutrankLargeSample) {
    // Reel 0: 1 impression, 1 completion  (a noisy tiny sample).
    // Reel 1: 200 impressions, 150 completions (a well-supported reel).
    // Global mean m = (1 + 150) / (1 + 200) = 151/201 = 0.7512437...
    // score0 = (1  + 20*m) / (1 + 1  + 20) = 16.024875.../22  = 0.7284034...
    // score1 = (150+ 20*m) / (1 + 200+ 20) = 165.024875.../221 = 0.7467189...
    // score1 > score0, so the 200/150 reel must rank first. Note score0 != 0.5 (the unsmoothed
    // value), which proves smoothing is actually applied.
    std::vector<rr::Reel> reels{makeReel(0, 1, 1, 0, 0), makeReel(1, 200, 150, 0, 0)};
    const std::vector<rr::User> users{emptyUser()};
    const rr::ExperimentConfig config{};
    const rr::RecommenderDeps deps{reels, users, config};

    rr::PopularityRecommender rec(deps, rr::Rng(0));
    const rr::RecommendationResponse response = rec.recommend(request(2));

    ASSERT_EQ(response.reels.size(), 2u);
    EXPECT_EQ(response.reels[0].reelId, rr::ReelId{1});
    EXPECT_EQ(response.reels[1].reelId, rr::ReelId{0});
    EXPECT_EQ(response.reels[0].rank, 0u);
    EXPECT_EQ(response.reels[1].rank, 1u);
    EXPECT_NEAR(response.reels[0].score, 0.7467189f, 1e-4f);
    EXPECT_NEAR(response.reels[1].score, 0.7284034f, 1e-4f);
    ASSERT_EQ(response.reels[0].sources.size(), 1u);
    EXPECT_EQ(response.reels[0].sources[0], rr::CandidateSource::Popular);
}

TEST(PopularityRecommenderTest, ColdStartAllZeroTiesBreakByAscendingReelId) {
    std::vector<rr::Reel> reels;
    for (uint32_t i = 0; i < 5; ++i) {
        reels.push_back(makeReel(i, 0, 0, 0, 0)); // no impressions -> priorMean falls back to 0
    }
    const std::vector<rr::User> users{emptyUser()};
    const rr::ExperimentConfig config{};
    const rr::RecommenderDeps deps{reels, users, config};

    rr::PopularityRecommender rec(deps, rr::Rng(0));
    const rr::RecommendationResponse response = rec.recommend(request(3));

    ASSERT_EQ(response.reels.size(), 3u);
    EXPECT_EQ(response.reels[0].reelId, rr::ReelId{0});
    EXPECT_EQ(response.reels[1].reelId, rr::ReelId{1});
    EXPECT_EQ(response.reels[2].reelId, rr::ReelId{2});
    for (const rr::RankedReel &r : response.reels) {
        EXPECT_EQ(r.score, 0.0f);
    }
}

TEST(PopularityRecommenderTest, ExcludesSeenAndInactive) {
    std::vector<rr::Reel> reels;
    for (uint32_t i = 0; i < 5; ++i) {
        reels.push_back(makeReel(i, 100, 50, 0, 0));
    }
    reels[2].active = false;
    rr::User user = emptyUser();
    user.seenReels.insert(rr::ReelId{0});
    const std::vector<rr::User> users{user};
    const rr::ExperimentConfig config{};
    const rr::RecommenderDeps deps{reels, users, config};

    rr::PopularityRecommender rec(deps, rr::Rng(0));
    const rr::RecommendationResponse response = rec.recommend(request(10));
    ASSERT_EQ(response.reels.size(), 3u); // 5 - 1 inactive - 1 seen
    for (const rr::RankedReel &r : response.reels) {
        EXPECT_NE(r.reelId.value, 0u);
        EXPECT_NE(r.reelId.value, 2u);
    }
}

TEST(PopularityRecommenderTest, DeterministicAcrossCalls) {
    std::vector<rr::Reel> reels{makeReel(0, 10, 5, 1, 0), makeReel(1, 20, 18, 2, 1),
                                makeReel(2, 5, 1, 0, 0)};
    const std::vector<rr::User> users{emptyUser()};
    const rr::ExperimentConfig config{};
    const rr::RecommenderDeps deps{reels, users, config};

    rr::PopularityRecommender rec(deps, rr::Rng(0));
    const rr::RecommendationResponse a = rec.recommend(request(3));
    const rr::RecommendationResponse b = rec.recommend(request(3));
    ASSERT_EQ(a.reels.size(), b.reels.size());
    for (std::size_t i = 0; i < a.reels.size(); ++i) {
        EXPECT_EQ(a.reels[i].reelId, b.reels[i].reelId);
        EXPECT_EQ(a.reels[i].score, b.reels[i].score);
    }
}

TEST(PopularityRecommenderTest, NameIsPopularity) {
    std::vector<rr::Reel> reels{makeReel(0, 0, 0, 0, 0)};
    const std::vector<rr::User> users{emptyUser()};
    const rr::ExperimentConfig config{};
    const rr::RecommenderDeps deps{reels, users, config};
    rr::PopularityRecommender rec(deps, rr::Rng(0));
    EXPECT_EQ(rec.name(), "popularity");
}
