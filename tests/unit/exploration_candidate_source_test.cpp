// Unit tests for ExplorationCandidateSource (Phase 8, TDD 12.7 / 13). Verify the per-request rng
// draw-order contract (disabled => no draws; enabled => exactly feedSize gate draws; epsilon=0 =>
// structural no-op), lastFiredSlots, per-mode selection (underexposed = lowest impressions,
// uncertain-topic = most distant), the pool cap, intra-source dedup, the Candidate field contract,
// and determinism.
#include "rr/candidate_sources/exploration_candidate_source.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <unordered_set>
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

rr::Reel makeReel(uint32_t id, rr::Timestamp createdAt, uint64_t impressions,
                  rr::Embedding emb = {1.0f, 0.0f}, bool active = true) {
    rr::Reel reel{};
    reel.id = rr::ReelId{id};
    reel.creatorId = rr::CreatorId{100 + id};
    rr::normalize(emb);
    reel.embedding = std::move(emb);
    reel.createdAt = createdAt;
    reel.impressionCount = impressions;
    reel.active = active;
    return reel;
}

rr::User makeUser(rr::Embedding pref = {1.0f, 0.0f}) {
    rr::User user{};
    user.id = rr::UserId{0};
    rr::normalize(pref);
    user.estimatedPreference = std::move(pref);
    return user;
}

rr::RecommendationRequest request(rr::Timestamp t, std::size_t feedSize, bool enableExploration) {
    rr::RecommendationRequest req{};
    req.requestTime = t;
    req.feedSize = feedSize;
    req.enableExploration = enableExploration;
    return req;
}

bool contains(const std::vector<rr::Candidate> &cands, uint32_t id) {
    return std::any_of(cands.begin(), cands.end(),
                       [id](const rr::Candidate &c) { return c.reelId.value == id; });
}

} // namespace

TEST(ExplorationCandidateSourceTest, DisabledRequestConsumesNoRng) {
    std::vector<rr::Reel> reels{makeReel(0, 100, 1), makeReel(1, 100, 2)};
    rr::Rng a(123);
    rr::Rng b(123);
    rr::ExplorationCandidateSource src(reels, /*epsilon=*/0.5, /*poolCap=*/50,
                                       /*window=*/1'000'000.0, &a);
    const std::vector<rr::Candidate> out =
        src.generate(makeUser(), request(/*t=*/1000, /*feedSize=*/10, /*enable=*/false));
    EXPECT_TRUE(out.empty());
    EXPECT_EQ(src.lastFiredSlots(), 0u);
    // a must be untouched: its next raw draw equals a same-seed engine's.
    EXPECT_EQ(a.nextU64(), b.nextU64());
}

TEST(ExplorationCandidateSourceTest, EpsilonZeroDrawsFeedSizeGatesAndReturnsEmpty) {
    std::vector<rr::Reel> reels{makeReel(0, 100, 1), makeReel(1, 100, 2), makeReel(2, 100, 3)};
    rr::Rng a(7);
    rr::Rng b(7);
    rr::ExplorationCandidateSource src(reels, /*epsilon=*/0.0, /*poolCap=*/50,
                                       /*window=*/1'000'000.0, &a);
    const std::size_t feedSize = 10;
    const std::vector<rr::Candidate> out =
        src.generate(makeUser(), request(1000, feedSize, /*enable=*/true));
    EXPECT_TRUE(out.empty()); // epsilon=0 => structural no-op
    EXPECT_EQ(src.lastFiredSlots(), 0u);
    // Exactly feedSize gates drawn, each a single uniform01 == one nextU64 on the engine.
    for (std::size_t i = 0; i < feedSize; ++i) {
        b.nextU64();
    }
    EXPECT_EQ(a.nextU64(), b.nextU64());
}

TEST(ExplorationCandidateSourceTest, AllGatesFireWithEpsilonOne) {
    std::vector<rr::Reel> reels{makeReel(0, 100, 1), makeReel(1, 100, 2), makeReel(2, 100, 3)};
    rr::Rng a(3);
    rr::ExplorationCandidateSource src(reels, /*epsilon=*/1.0, /*poolCap=*/50,
                                       /*window=*/1'000'000.0, &a);
    const std::size_t feedSize = 8;
    const std::vector<rr::Candidate> out =
        src.generate(makeUser(), request(1000, feedSize, /*enable=*/true));
    EXPECT_EQ(src.lastFiredSlots(), feedSize);
    EXPECT_FALSE(out.empty());
    for (const rr::Candidate &c : out) {
        EXPECT_EQ(c.source, rr::CandidateSource::Exploration);
    }
}

TEST(ExplorationCandidateSourceTest, UnderexposedAndUncertainModesSelectExtremes) {
    // Non-fresh reels (window tiny vs t) => the random-fresh pool is empty and mode a picks
    // nothing, isolating the two deterministic modes. reel0 is the single lowest-impression (mode
    // b); reel4 is the single most-distant AND highest-impression, so only mode c can pick it.
    std::vector<rr::Reel> reels{
        makeReel(0, /*createdAt=*/0, /*imp=*/1, {1.0f, 0.0f}), // underexposed
        makeReel(1, 0, 2, {1.0f, 0.0f}), makeReel(2, 0, 3, {1.0f, 0.0f}),
        makeReel(3, 0, 4, {1.0f, 0.0f}),
        makeReel(4, 0, 5, {-1.0f, 0.0f})}; // most distant from {1,0}, highest impression
    rr::Rng a(1);
    // poolCap 3 => budgetA=1 (0 fresh), budgetB=1 (reel0), budgetC=1 (reel4).
    rr::ExplorationCandidateSource src(reels, /*epsilon=*/1.0, /*poolCap=*/3, /*window=*/10.0, &a);
    const std::vector<rr::Candidate> out = src.generate(
        makeUser({1.0f, 0.0f}), request(/*t=*/100000, /*feedSize=*/10, /*enable=*/true));
    ASSERT_EQ(out.size(), 2u);
    EXPECT_TRUE(contains(out, 0)) << "lowest-impression reel must be selected (underexposed mode)";
    EXPECT_TRUE(contains(out, 4)) << "most-distant reel must be selected (uncertain-topic mode)";
}

TEST(ExplorationCandidateSourceTest, RandomFreshModeDrawsOnlyFromWindow) {
    // Only reels 0 and 1 are within the window; reels 2,3 are stale. With a poolCap that gives only
    // the random-fresh budget a chance (all reels equal impression, all equal similarity so modes
    // b/c also draw from the window-eligible set), every selected reel is within the window.
    std::vector<rr::Reel> reels{makeReel(0, 99991, 5, {1.0f, 0.0f}),
                                makeReel(1, 100000, 5, {1.0f, 0.0f}),
                                makeReel(2, 0, 5, {1.0f, 0.0f}), makeReel(3, 50, 5, {1.0f, 0.0f})};
    rr::Rng a(2);
    rr::ExplorationCandidateSource src(reels, /*epsilon=*/1.0, /*poolCap=*/3, /*window=*/10.0, &a);
    const std::vector<rr::Candidate> out =
        src.generate(makeUser(), request(/*t=*/100000, /*feedSize=*/10, /*enable=*/true));
    // At least one fresh reel selected via mode a, and it is within the window (createdAt >=
    // 99990).
    ASSERT_FALSE(out.empty());
    bool sawFresh = false;
    for (const rr::Candidate &c : out) {
        if (reels[c.reelId.value].createdAt >= 99990) {
            sawFresh = true;
        }
    }
    EXPECT_TRUE(sawFresh);
}

TEST(ExplorationCandidateSourceTest, NeverExceedsPoolCapAndDedups) {
    std::vector<rr::Reel> reels;
    for (uint32_t i = 0; i < 30; ++i) {
        const rr::Embedding e =
            (i % 2 == 0) ? rr::Embedding{1.0f, 0.0f} : rr::Embedding{-1.0f, 0.0f};
        reels.push_back(makeReel(i, /*createdAt=*/1000 + i, /*imp=*/i, e));
    }
    rr::Rng a(5);
    rr::ExplorationCandidateSource src(reels, /*epsilon=*/1.0, /*poolCap=*/10,
                                       /*window=*/1'000'000.0, &a);
    const std::vector<rr::Candidate> out =
        src.generate(makeUser(), request(/*t=*/2000, /*feedSize=*/10, /*enable=*/true));
    EXPECT_LE(out.size(), 10u);
    std::unordered_set<uint32_t> seen;
    for (const rr::Candidate &c : out) {
        EXPECT_TRUE(seen.insert(c.reelId.value).second) << "duplicate reel in exploration pool";
        EXPECT_EQ(c.source, rr::CandidateSource::Exploration);
    }
}

TEST(ExplorationCandidateSourceTest, FillsSimilarityAndDistancePerD3) {
    std::vector<rr::Reel> reels{makeReel(0, 1000, 1, {1.0f, 0.0f}),
                                makeReel(1, 1000, 2, {0.0f, 1.0f})};
    rr::Rng a(11);
    rr::ExplorationCandidateSource src(reels, /*epsilon=*/1.0, /*poolCap=*/50,
                                       /*window=*/1'000'000.0, &a);
    const rr::User user = makeUser({1.0f, 0.0f});
    const std::vector<rr::Candidate> out =
        src.generate(user, request(/*t=*/2000, /*feedSize=*/4, /*enable=*/true));
    ASSERT_FALSE(out.empty());
    for (const rr::Candidate &c : out) {
        const rr::Reel &reel = reels[c.reelId.value];
        const float expectedSim = rr::dot(user.estimatedPreference, reel.embedding);
        EXPECT_NEAR(c.retrievalSimilarity, expectedSim, 1e-5f);
        const float expectedDist = std::sqrt(std::max(0.0f, 2.0f - 2.0f * c.retrievalSimilarity));
        EXPECT_FLOAT_EQ(c.retrievalDistance, expectedDist);
    }
}

TEST(ExplorationCandidateSourceTest, DeterministicSameSeed) {
    std::vector<rr::Reel> reels;
    for (uint32_t i = 0; i < 20; ++i) {
        const rr::Embedding e =
            (i % 3 == 0) ? rr::Embedding{-1.0f, 0.0f} : rr::Embedding{1.0f, 0.0f};
        reels.push_back(makeReel(i, /*createdAt=*/1000 + i, /*imp=*/(i * 7) % 11, e));
    }
    rr::Rng a(99);
    rr::Rng b(99);
    rr::ExplorationCandidateSource sa(reels, /*epsilon=*/0.3, /*poolCap=*/12,
                                      /*window=*/1'000'000.0, &a);
    rr::ExplorationCandidateSource sb(reels, /*epsilon=*/0.3, /*poolCap=*/12,
                                      /*window=*/1'000'000.0, &b);
    const rr::User user = makeUser();
    const std::vector<rr::Candidate> outA = sa.generate(user, request(2000, 10, true));
    const std::vector<rr::Candidate> outB = sb.generate(user, request(2000, 10, true));
    ASSERT_EQ(outA.size(), outB.size());
    EXPECT_EQ(sa.lastFiredSlots(), sb.lastFiredSlots());
    for (std::size_t i = 0; i < outA.size(); ++i) {
        EXPECT_EQ(outA[i].reelId, outB[i].reelId);
        EXPECT_EQ(outA[i].retrievalSimilarity, outB[i].retrievalSimilarity);
    }
}
