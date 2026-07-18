// Unit tests for OracleSatisfactionRecommender (Phase 15, Package B2 — the V2 TDD 4.4 arm-4
// evaluation-only ceiling). Small hand-built V2 datasets; graph quality is irrelevant here. These
// check: name, the D18 fail-fast guard + factory rejection (runner-side path), the retrievalIndex()
// hook, seen/inactive filtering, same-seed determinism, and — the heart of the arm — that the feed
// is ordered by EXPECTED HIDDEN SATISFACTION.
//
// Ordering is made hand-verifiable by leaving the user's hidden topicPreference EMPTY (so the topic
// channel dot is 0, confirmed by latent_model.cpp's empty-embedding rule) and every other channel /
// scalar neutral: the noise-free immediateSatisfaction then reduces to tanh(1.2 *
// satisfactionBias), strictly monotonic in the per-reel archetype satisfactionBias. So "descending
// satisfaction" is exactly "descending satisfactionBias, ties broken by ascending ReelId".
#include "rr/evaluation/oracle_satisfaction_recommender.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

#include "rr/core/embedding.hpp"
#include "rr/domain/creator.hpp"
#include "rr/domain/reel.hpp"
#include "rr/domain/user.hpp"
#include "rr/infrastructure/config.hpp"
#include "rr/infrastructure/random.hpp"
#include "rr/recommendation/recommender_factory.hpp"
#include "rr/recommendation/vector_index.hpp"
#include "rr/simulation/hidden/hidden_reel_state.hpp"
#include "rr/simulation/hidden/hidden_user_state.hpp"

using namespace rr;

namespace {

constexpr uint32_t kDim = 4;
const LanguageId kLang{1};

// A distinct-but-near-axis-0 unit vector so every reel is retrieved (all cluster around the query)
// yet no two share an embedding. The exact direction is irrelevant to satisfaction (the hidden
// topic channel is zeroed by the empty hiddenPreference).
Embedding nearAxis0(uint32_t i) {
    Embedding e(kDim, 0.0f);
    e[0] = 1.0f;
    e[1] = 0.02f * static_cast<float>(i + 1);
    normalize(e);
    return e;
}

// The whole hand-built world: reels (dense ids 0..N-1), one neutral user, one neutral creator, and
// index-aligned hidden state whose ONLY non-neutral field is each reel's satisfactionBias.
struct OracleFixture {
    ExperimentConfig config;
    std::vector<Reel> reels;
    std::vector<User> users;
    std::vector<Creator> creators;
    std::vector<HiddenUserState> hiddenStates;
    std::vector<HiddenReelState> hiddenReelStates;
};

OracleFixture makeFixture(const std::vector<double> &satisfactionBiases) {
    OracleFixture f;
    f.config.simulation.dimensions = kDim;

    const auto n = static_cast<uint32_t>(satisfactionBiases.size());
    for (uint32_t i = 0; i < n; ++i) {
        Reel reel{};
        reel.id = ReelId{i};
        reel.creatorId = CreatorId{0};
        reel.embedding = nearAxis0(i);
        reel.durationSeconds = 10.0f;
        reel.active = true;
        reel.language = kLang; // matches the user => no language penalty
        f.reels.push_back(std::move(reel));

        HiddenReelState hidden{};
        hidden.reelId = ReelId{i};
        hidden.satisfactionBias = static_cast<float>(satisfactionBiases[i]);
        // Everything else default 0: no regret bias, no niche cohort, no clickbait shape.
        f.hiddenReelStates.push_back(hidden);
    }

    // One neutral user. estimatedPreference (VISIBLE) drives retrieval; hiddenPreference (HIDDEN)
    // left EMPTY so the topic channel contributes 0 and satisfaction is pure archetype bias.
    User user{};
    user.id = UserId{0};
    user.estimatedPreference = nearAxis0(0);
    f.users.push_back(std::move(user));

    HiddenUserState hs{};
    hs.userId = UserId{0};
    hs.primaryLanguage = kLang;
    // hiddenPreference + every modality/scalar channel stay empty/0 => neutral user (susceptibility
    // factor 1, base utility 0).
    f.hiddenStates.push_back(hs);

    Creator creator{};
    creator.id = CreatorId{0};
    // styleEmbedding empty => creator-attachment term 0.
    f.creators.push_back(creator);

    return f;
}

RecommendationRequest request(std::size_t feedSize, std::size_t candidateLimit) {
    RecommendationRequest req{};
    req.userId = UserId{0};
    req.feedSize = feedSize;
    req.candidateLimit = candidateLimit;
    return req;
}

OracleSatisfactionRecommender makeOracle(const OracleFixture &f, uint64_t seed) {
    return OracleSatisfactionRecommender(f.config, f.reels, f.users, f.creators, f.hiddenStates,
                                         f.hiddenReelStates, Rng(seed));
}

std::vector<uint32_t> feedIds(const RecommendationResponse &resp) {
    std::vector<uint32_t> ids;
    ids.reserve(resp.reels.size());
    for (const RankedReel &r : resp.reels) {
        ids.push_back(r.reelId.value);
    }
    return ids;
}

} // namespace

TEST(OracleSatisfactionRecommenderTest, NameIsOracleSatisfaction) {
    OracleFixture f = makeFixture({0.1, 0.2});
    OracleSatisfactionRecommender oracle = makeOracle(f, 1);
    EXPECT_EQ(oracle.name(), "oracle_satisfaction");
    EXPECT_EQ(oracle.name(), toString(RecommendationAlgorithm::OracleSatisfaction));
}

// D18 fail-fast: the oracle needs V2 hidden state. A gate-off dataset (empty hiddenReelStates) must
// be rejected at construction rather than risk an out-of-bounds read on the ranking path.
TEST(OracleSatisfactionRecommenderTest, RejectsUnalignedHiddenReelStates) {
    OracleFixture f = makeFixture({0.1, 0.2, 0.3});
    EXPECT_THROW(OracleSatisfactionRecommender(f.config, f.reels, f.users, f.creators,
                                               f.hiddenStates, /*empty=*/{}, Rng(1)),
                 std::invalid_argument);
}

// D18 dispatch contract: the recommendation-side factory REJECTS the oracle (evaluation-only),
// while direct construction — the ExperimentRunner's path — works and serves a feed. (The
// config-level factory-throw is also covered by the recommender_factory tests; this pins the
// runner-side pairing.)
TEST(OracleSatisfactionRecommenderTest, FactoryRejectsButDirectConstructionServes) {
    OracleFixture f = makeFixture({0.1, 0.4, 0.2});
    RecommenderDeps deps{f.reels, f.users, f.config};
    EXPECT_THROW(makeRecommender(RecommendationAlgorithm::OracleSatisfaction, deps, Rng(1)),
                 std::invalid_argument);

    OracleSatisfactionRecommender oracle = makeOracle(f, 1);
    RecommendationResponse resp = oracle.recommend(request(3, 100));
    EXPECT_EQ(resp.reels.size(), 3u);
}

TEST(OracleSatisfactionRecommenderTest, RetrievalIndexExposesActiveReels) {
    OracleFixture f = makeFixture({0.1, 0.2, 0.3, 0.4});
    f.reels[3].active = false; // one inactive reel is not indexed
    OracleSatisfactionRecommender oracle = makeOracle(f, 1);

    const VectorIndex *index = oracle.retrievalIndex();
    ASSERT_NE(index, nullptr);
    EXPECT_EQ(index->size(), 3u); // 4 reels, 1 inactive
}

// The heart of the arm: the feed is ordered by descending EXPECTED HIDDEN SATISFACTION. With the
// neutral construction, that is descending satisfactionBias.
TEST(OracleSatisfactionRecommenderTest, RanksByExpectedSatisfactionDescending) {
    //             reel:   0     1     2      3     4
    OracleFixture f = makeFixture({0.2, 0.8, -0.4, 0.5, 0.0});
    OracleSatisfactionRecommender oracle = makeOracle(f, 1);

    RecommendationResponse resp = oracle.recommend(request(5, 100));
    // Descending bias: 0.8(r1) > 0.5(r3) > 0.2(r0) > 0.0(r4) > -0.4(r2).
    EXPECT_EQ(feedIds(resp), (std::vector<uint32_t>{1, 3, 0, 4, 2}));

    // Scores (expected satisfaction) are non-increasing down the feed.
    for (std::size_t i = 1; i < resp.reels.size(); ++i) {
        EXPECT_LE(resp.reels[i].score, resp.reels[i - 1].score);
        EXPECT_EQ(resp.reels[i].rank, i);
    }
    // Every feed item is labeled with its true retrieval provenance.
    for (const RankedReel &r : resp.reels) {
        ASSERT_EQ(r.sources.size(), 1u);
        EXPECT_EQ(r.sources[0], CandidateSource::VectorHNSW);
    }
}

// Equal expected satisfaction (equal bias) ties are broken by ascending ReelId, deterministically.
TEST(OracleSatisfactionRecommenderTest, TiesBrokenByAscendingReelId) {
    OracleFixture f = makeFixture({0.5, 0.5, 0.1}); // r0 and r1 tie at the top
    OracleSatisfactionRecommender oracle = makeOracle(f, 3);

    RecommendationResponse resp = oracle.recommend(request(3, 100));
    EXPECT_EQ(feedIds(resp), (std::vector<uint32_t>{0, 1, 2}));
    EXPECT_FLOAT_EQ(resp.reels[0].score, resp.reels[1].score); // the genuine tie
}

// Seen and inactive reels never reach the feed (rr::isEligible), and the survivors are still
// ordered by expected satisfaction.
TEST(OracleSatisfactionRecommenderTest, DropsSeenAndInactive) {
    OracleFixture f = makeFixture({0.2, 0.8, -0.4, 0.5, 0.0});
    f.users[0].seenReels.insert(ReelId{1}); // the best reel (0.8) is already seen
    f.reels[4].active = false;              // reel 4 (0.0) is deactivated (also not indexed)
    OracleSatisfactionRecommender oracle = makeOracle(f, 1);

    RecommendationResponse resp = oracle.recommend(request(10, 100));
    // Eligible: r0(0.2), r2(-0.4), r3(0.5). Descending: r3, r0, r2.
    EXPECT_EQ(feedIds(resp), (std::vector<uint32_t>{3, 0, 2}));
    for (const RankedReel &r : resp.reels) {
        EXPECT_NE(r.reelId, ReelId{1});
        EXPECT_NE(r.reelId, ReelId{4});
    }
}

TEST(OracleSatisfactionRecommenderTest, FeedBoundedByFeedSize) {
    OracleFixture f = makeFixture({0.2, 0.8, -0.4, 0.5, 0.0});
    OracleSatisfactionRecommender oracle = makeOracle(f, 1);
    RecommendationResponse resp = oracle.recommend(request(2, 100));
    EXPECT_EQ(resp.reels.size(), 2u);
    EXPECT_EQ(feedIds(resp), (std::vector<uint32_t>{1, 3})); // top two by satisfaction
}

// Determinism (D8): two instances built from the same seed over the same dataset serve identical
// feeds — same ids, scores, and ranks.
TEST(OracleSatisfactionRecommenderTest, SameSeedProducesIdenticalFeeds) {
    OracleFixture f = makeFixture({0.2, 0.8, -0.4, 0.5, 0.0});
    OracleSatisfactionRecommender a = makeOracle(f, 7);
    OracleSatisfactionRecommender b = makeOracle(f, 7);

    RecommendationResponse ra = a.recommend(request(5, 100));
    RecommendationResponse rb = b.recommend(request(5, 100));
    ASSERT_EQ(ra.reels.size(), rb.reels.size());
    for (std::size_t i = 0; i < ra.reels.size(); ++i) {
        EXPECT_EQ(ra.reels[i].reelId, rb.reels[i].reelId);
        EXPECT_FLOAT_EQ(ra.reels[i].score, rb.reels[i].score);
        EXPECT_EQ(ra.reels[i].rank, rb.reels[i].rank);
    }
}
