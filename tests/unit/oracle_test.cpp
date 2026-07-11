#include "rr/evaluation/oracle.hpp"

#include <gtest/gtest.h>

#include <unordered_set>
#include <vector>

using namespace rr;

namespace {

Reel makeReel(uint32_t id, Embedding emb, bool active) {
    Reel r{};
    r.id = ReelId{id};
    r.creatorId = CreatorId{0};
    r.embedding = std::move(emb);
    r.active = active;
    return r;
}

// r0..r5, hidden preference (1, 0); affinity = first component.
//   r0 (1.0)  r1 (0.0)  r2 (0.6)  r3 (-1.0)  r4 (0.8, inactive)  r5 (0.6, seen)
std::vector<Reel> tinyReels() {
    return {
        makeReel(0, {1.0f, 0.0f}, true),  makeReel(1, {0.0f, 1.0f}, true),
        makeReel(2, {0.6f, 0.8f}, true),  makeReel(3, {-1.0f, 0.0f}, true),
        makeReel(4, {0.8f, 0.6f}, false), makeReel(5, {0.6f, -0.8f}, true),
    };
}

} // namespace

TEST(OracleTest, TrueAffinityIsDotProduct) {
    Reel r = makeReel(0, {0.6f, 0.8f}, true);
    EXPECT_NEAR(trueAffinity({1.0f, 0.0f}, r), 0.6f, 1e-6f);
    EXPECT_NEAR(trueAffinity({0.0f, 1.0f}, r), 0.8f, 1e-6f);
}

// Oracle picks the top-feedSize by true affinity from ACTIVE, UNSEEN reels; regret is the
// difference of means against the recommended feed.
TEST(OracleTest, TopKAndRegretArithmetic) {
    const Embedding pref = {1.0f, 0.0f};
    const std::vector<Reel> reels = tinyReels();
    std::unordered_set<ReelId> seen = {ReelId{5}}; // r5 excluded as seen; r4 excluded as inactive

    // Recommended feed: r1 (aff 0.0) and r2 (aff 0.6) -> mean 0.3.
    OracleResult res =
        computeOracleRegret(pref, reels, seen, {ReelId{1}, ReelId{2}}, /*feedSize=*/2);

    // Active unseen pool = {r0:1.0, r1:0.0, r2:0.6, r3:-1.0}; oracle top-2 = {r0, r2} -> mean 0.8.
    EXPECT_EQ(res.oracleK, 2u);
    EXPECT_EQ(res.recommendedK, 2u);
    EXPECT_NEAR(res.oracleMeanAffinity, 0.8, 1e-5);
    EXPECT_NEAR(res.recommendedMeanAffinity, 0.3, 1e-5);
    EXPECT_NEAR(res.regret, 0.5, 1e-5);
}

// Recommending the oracle's own top-k yields zero regret (non-negativity boundary).
TEST(OracleTest, OptimalFeedHasZeroRegret) {
    const Embedding pref = {1.0f, 0.0f};
    const std::vector<Reel> reels = tinyReels();
    std::unordered_set<ReelId> seen;
    // Oracle top-2 over the active pool ({r0:1.0, r1:0.0, r2:0.6, r3:-1.0, r5:0.6}) is {r0, r2}.
    OracleResult res = computeOracleRegret(pref, reels, seen, {ReelId{0}, ReelId{2}}, 2);
    EXPECT_NEAR(res.regret, 0.0, 1e-5);
    EXPECT_GE(res.regret, -1e-6); // regret is non-negative for a subset feed at equal k
}

// feedSize larger than the available pool clamps oracleK to what exists.
TEST(OracleTest, ClampsToAvailablePool) {
    const Embedding pref = {1.0f, 0.0f};
    std::vector<Reel> reels = {makeReel(0, {1.0f, 0.0f}, true), makeReel(1, {0.0f, 1.0f}, false)};
    std::unordered_set<ReelId> seen;
    OracleResult res = computeOracleRegret(pref, reels, seen, {ReelId{0}}, /*feedSize=*/5);
    EXPECT_EQ(res.oracleK, 1u); // only r0 is active+unseen
    EXPECT_NEAR(res.oracleMeanAffinity, 1.0, 1e-5);
    EXPECT_NEAR(res.regret, 0.0, 1e-5);
}
