// Unit + property tests for FreshCandidateSource (Phase 8, TDD 12.5 / 13). Verify the recency
// window (boundary, wider-than-now no-underflow, future reels), the deterministic order (createdAt
// descending, ties by ascending ReelId), the count cap, inactive/empty-embedding exclusion, the
// optional topic-proximity filter, the Candidate field contract, and determinism.
#include "rr/candidate_sources/fresh_candidate_source.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include "rr/core/embedding.hpp"
#include "rr/domain/candidate.hpp"
#include "rr/domain/ids.hpp"
#include "rr/domain/recommendation.hpp"
#include "rr/domain/reel.hpp"
#include "rr/domain/user.hpp"
#include "rr/infrastructure/clock.hpp"
#include "rr/infrastructure/random.hpp"

namespace {

rr::Reel makeReel(uint32_t id, rr::Timestamp createdAt, rr::Embedding emb = {1.0f, 0.0f, 0.0f},
                  bool active = true) {
    rr::Reel reel{};
    reel.id = rr::ReelId{id};
    reel.creatorId = rr::CreatorId{100 + id};
    rr::normalize(emb);
    reel.embedding = std::move(emb);
    reel.createdAt = createdAt;
    reel.active = active;
    return reel;
}

rr::User makeUser(rr::Embedding pref = {1.0f, 0.0f, 0.0f}) {
    rr::User user{};
    user.id = rr::UserId{0};
    rr::normalize(pref);
    user.estimatedPreference = std::move(pref);
    return user;
}

rr::RecommendationRequest request(rr::Timestamp requestTime) {
    rr::RecommendationRequest req{};
    req.requestTime = requestTime;
    return req;
}

std::vector<rr::ReelId> ids(const std::vector<rr::Candidate> &cands) {
    std::vector<rr::ReelId> out;
    for (const rr::Candidate &c : cands) {
        out.push_back(c.reelId);
    }
    return out;
}

} // namespace

TEST(FreshCandidateSourceTest, OrdersByCreatedAtDescendingThenId) {
    // Three ties at createdAt 8000 (ids 4,2,7) break by ascending id -> 2,4,7 after the newest.
    std::vector<rr::Reel> reels{makeReel(0, 5000), makeReel(4, 8000), makeReel(9, 9000),
                                makeReel(2, 8000), makeReel(7, 8000)};
    rr::FreshCandidateSource source(reels, /*count=*/10, /*window=*/1'000'000.0);
    const std::vector<rr::Candidate> cands = source.generate(makeUser(), request(9000));
    EXPECT_EQ(ids(cands), (std::vector<rr::ReelId>{rr::ReelId{9}, rr::ReelId{2}, rr::ReelId{4},
                                                   rr::ReelId{7}, rr::ReelId{0}}));
    for (const rr::Candidate &c : cands) {
        EXPECT_EQ(c.source, rr::CandidateSource::Fresh);
    }
}

TEST(FreshCandidateSourceTest, WindowBoundaryInclusive) {
    // t=10000, window=3000 => cutoff 7000. createdAt 7000 qualifies (>=), 6999 does not.
    std::vector<rr::Reel> reels{makeReel(0, 7000), makeReel(1, 6999), makeReel(2, 8000)};
    rr::FreshCandidateSource source(reels, /*count=*/10, /*window=*/3000.0);
    const std::vector<rr::Candidate> cands = source.generate(makeUser(), request(10000));
    EXPECT_EQ(ids(cands), (std::vector<rr::ReelId>{rr::ReelId{2}, rr::ReelId{0}}));
}

TEST(FreshCandidateSourceTest, WindowWiderThanNowAdmitsEverything) {
    // window > t: the cutoff underflows conceptually but is computed in double as negative, so
    // every reel (including createdAt 0) qualifies.
    std::vector<rr::Reel> reels{makeReel(0, 0), makeReel(1, 500)};
    rr::FreshCandidateSource source(reels, /*count=*/10, /*window=*/1'000'000.0);
    const std::vector<rr::Candidate> cands = source.generate(makeUser(), request(100));
    EXPECT_EQ(cands.size(), 2u);
}

TEST(FreshCandidateSourceTest, FutureDatedReelQualifies) {
    // createdAt > t (only reachable in hand-built tests) trivially clears the lower bound.
    std::vector<rr::Reel> reels{makeReel(0, 50000)};
    rr::FreshCandidateSource source(reels, /*count=*/10, /*window=*/1000.0);
    const std::vector<rr::Candidate> cands = source.generate(makeUser(), request(100));
    ASSERT_EQ(cands.size(), 1u);
    EXPECT_EQ(cands.front().reelId, rr::ReelId{0});
}

TEST(FreshCandidateSourceTest, HonoursCountCap) {
    std::vector<rr::Reel> reels{makeReel(0, 100), makeReel(1, 200), makeReel(2, 300),
                                makeReel(3, 400)};
    rr::FreshCandidateSource source(reels, /*count=*/2, /*window=*/1'000'000.0);
    const std::vector<rr::Candidate> cands = source.generate(makeUser(), request(1000));
    EXPECT_EQ(ids(cands), (std::vector<rr::ReelId>{rr::ReelId{3}, rr::ReelId{2}}));
}

TEST(FreshCandidateSourceTest, ExcludesInactiveAndEmptyEmbedding) {
    std::vector<rr::Reel> reels{makeReel(0, 900), makeReel(1, 800), makeReel(2, 700)};
    reels[0].active = false;    // newest, but inactive
    reels[1].embedding.clear(); // invalid embedding
    rr::FreshCandidateSource source(reels, /*count=*/10, /*window=*/1'000'000.0);
    const std::vector<rr::Candidate> cands = source.generate(makeUser(), request(1000));
    ASSERT_EQ(cands.size(), 1u);
    EXPECT_EQ(cands.front().reelId, rr::ReelId{2});
}

TEST(FreshCandidateSourceTest, ZeroCountReturnsEmpty) {
    std::vector<rr::Reel> reels{makeReel(0, 900)};
    rr::FreshCandidateSource source(reels, /*count=*/0, /*window=*/1'000'000.0);
    EXPECT_TRUE(source.generate(makeUser(), request(1000)).empty());
}

TEST(FreshCandidateSourceTest, FillsSimilarityAndDistancePerD3) {
    std::vector<rr::Reel> reels{makeReel(0, 900, {1.0f, 0.0f, 0.0f}),
                                makeReel(1, 800, {0.0f, 1.0f, 0.0f})};
    rr::FreshCandidateSource source(reels, /*count=*/10, /*window=*/1'000'000.0);
    const rr::User user = makeUser({1.0f, 0.0f, 0.0f});
    const std::vector<rr::Candidate> cands = source.generate(user, request(1000));
    ASSERT_EQ(cands.size(), 2u);
    for (const rr::Candidate &c : cands) {
        const rr::Reel &reel = reels[c.reelId.value];
        const float expectedSim = rr::dot(user.estimatedPreference, reel.embedding);
        EXPECT_NEAR(c.retrievalSimilarity, expectedSim, 1e-5f);
        const float expectedDist = std::sqrt(std::max(0.0f, 2.0f - 2.0f * c.retrievalSimilarity));
        EXPECT_FLOAT_EQ(c.retrievalDistance, expectedDist);
        EXPECT_EQ(c.rankingScore, 0.0f);
    }
}

TEST(FreshCandidateSourceTest, TopicProximityFilterDefaultOffIncludesDistant) {
    // A near reel (cos 1.0) and a distant reel (cos 0.0). With the filter OFF (default) both pass.
    std::vector<rr::Reel> reels{makeReel(0, 900, {1.0f, 0.0f, 0.0f}),
                                makeReel(1, 800, {0.0f, 1.0f, 0.0f})};
    rr::FreshCandidateSource source(reels, /*count=*/10, /*window=*/1'000'000.0);
    const std::vector<rr::Candidate> cands =
        source.generate(makeUser({1.0f, 0.0f, 0.0f}), request(1000));
    EXPECT_EQ(cands.size(), 2u);
}

TEST(FreshCandidateSourceTest, TopicProximityFilterOnExcludesDistant) {
    // The distant reel (cos 0.0 < kTopicProximityMinSimilarity) is excluded; the near one stays.
    std::vector<rr::Reel> reels{makeReel(0, 900, {1.0f, 0.0f, 0.0f}),
                                makeReel(1, 800, {0.0f, 1.0f, 0.0f})};
    rr::FreshCandidateSource source(reels, /*count=*/10, /*window=*/1'000'000.0,
                                    /*topicProximity=*/true);
    const std::vector<rr::Candidate> cands =
        source.generate(makeUser({1.0f, 0.0f, 0.0f}), request(1000));
    ASSERT_EQ(cands.size(), 1u);
    EXPECT_EQ(cands.front().reelId, rr::ReelId{0});
    EXPECT_GE(cands.front().retrievalSimilarity,
              rr::FreshCandidateSource::kTopicProximityMinSimilarity);
}

TEST(FreshCandidateSourceTest, DeterministicAcrossCalls) {
    std::vector<rr::Reel> reels{makeReel(0, 900), makeReel(1, 800), makeReel(2, 850)};
    rr::FreshCandidateSource source(reels, /*count=*/10, /*window=*/1'000'000.0);
    const rr::User user = makeUser();
    const std::vector<rr::Candidate> a = source.generate(user, request(1000));
    const std::vector<rr::Candidate> b = source.generate(user, request(1000));
    ASSERT_EQ(a.size(), b.size());
    for (std::size_t i = 0; i < a.size(); ++i) {
        EXPECT_EQ(a[i].reelId, b[i].reelId);
        EXPECT_EQ(a[i].retrievalSimilarity, b[i].retrievalSimilarity);
    }
}

// Property: over many random fixtures the result never exceeds the configured count, and every
// returned reel is within the window.
TEST(FreshCandidateSourceTest, NeverExceedsCountAndRespectsWindowProperty) {
    for (uint64_t seed = 0; seed < 25; ++seed) {
        rr::Rng rng(seed);
        const uint32_t reelCount = 1 + static_cast<uint32_t>(rng.uniformInt(60));
        std::vector<rr::Reel> reels;
        reels.reserve(reelCount);
        for (uint32_t i = 0; i < reelCount; ++i) {
            reels.push_back(
                makeReel(i, static_cast<rr::Timestamp>(rng.uniformInt(20000)),
                         {1.0f + static_cast<float>(std::fabs(rng.gaussian())),
                          static_cast<float>(rng.gaussian()), static_cast<float>(rng.gaussian())},
                         rng.bernoulli(0.8)));
        }
        const uint32_t count = static_cast<uint32_t>(rng.uniformInt(30));
        const double window = rng.uniform(0.0, 15000.0);
        const auto t = static_cast<rr::Timestamp>(rng.uniformInt(20000));
        rr::FreshCandidateSource source(reels, count, window);
        const std::vector<rr::Candidate> cands = source.generate(makeUser(), request(t));
        EXPECT_LE(cands.size(), static_cast<std::size_t>(count)) << "seed " << seed;
        const double cutoff = static_cast<double>(t) - window;
        for (const rr::Candidate &c : cands) {
            EXPECT_GE(static_cast<double>(reels[c.reelId.value].createdAt), cutoff)
                << "seed " << seed;
        }
    }
}
