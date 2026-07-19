// Unit tests for the Phase 17 PersonalizedDiversityReranker (V2 TDD 4.10, plan task 3) and the
// gated WeightedRanker repetition-penalty scaling (plan task 3 "repetition penalty in ranking
// scales with estimated tolerance"). The KEY regression contract is neutral-estimate equivalence:
// a user with default estimates gets the fixed DiversityReranker output byte-for-byte. Beyond that:
// a low-repetition-tolerance user gets tighter per-topic/per-creator caps, a high-tolerance user
// looser ones (up to scaleMax), estimated topic/creator fatigue tightens the affected channel, the
// per-user MMR lambda changes ordering, and the universal hard rules (no dup / no seen) are never
// violated. Deterministic throughout (pure function of inputs).
#include "rr/recommendation/personalized_diversity_reranker.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include "rr/core/embedding.hpp"
#include "rr/domain/candidate.hpp"
#include "rr/domain/ids.hpp"
#include "rr/domain/reel.hpp"
#include "rr/domain/user.hpp"
#include "rr/infrastructure/config.hpp"
#include "rr/recommendation/diversity_reranker.hpp"
#include "rr/recommendation/weighted_ranker.hpp"

namespace {

struct Spec {
    uint32_t creator;
    uint32_t topic;
    rr::Embedding embedding;
};

std::vector<rr::Reel> makeReels(const std::vector<Spec> &specs) {
    std::vector<rr::Reel> reels;
    reels.reserve(specs.size());
    for (std::size_t i = 0; i < specs.size(); ++i) {
        rr::Reel r{};
        r.id = rr::ReelId{static_cast<uint32_t>(i)};
        r.creatorId = rr::CreatorId{specs[i].creator};
        r.primaryTopic = rr::TopicId{specs[i].topic};
        r.embedding = specs[i].embedding;
        r.active = true;
        reels.push_back(std::move(r));
    }
    return reels;
}

rr::Candidate cand(uint32_t id, float score) {
    rr::Candidate c{};
    c.reelId = rr::ReelId{id};
    c.source = rr::CandidateSource::VectorHNSW;
    c.rankingScore = score;
    return c;
}

rr::DiversityConfig cfg(bool useMmr) {
    rr::DiversityConfig d{};
    d.maxPerCreator = 2;
    d.maxPerTopic = 3;
    d.mmrLambda = 0.75;
    d.useMmr = useMmr;
    return d;
}

std::vector<uint32_t> feedIds(const std::vector<rr::RankedReel> &feed) {
    std::vector<uint32_t> ids;
    for (const rr::RankedReel &r : feed) {
        ids.push_back(r.reelId.value);
    }
    return ids;
}

// Max number of feed items sharing any single primary topic.
std::size_t maxPerTopic(const std::vector<rr::RankedReel> &feed,
                        const std::vector<rr::Reel> &reels) {
    std::unordered_map<uint32_t, std::size_t> counts;
    std::size_t mx = 0;
    for (const rr::RankedReel &r : feed) {
        mx = std::max(mx, ++counts[reels[r.reelId.value].primaryTopic.value]);
    }
    return mx;
}

std::size_t topicCount(const std::vector<rr::RankedReel> &feed, const std::vector<rr::Reel> &reels,
                       uint32_t topic) {
    std::size_t n = 0;
    for (const rr::RankedReel &r : feed) {
        if (reels[r.reelId.value].primaryTopic.value == topic) {
            ++n;
        }
    }
    return n;
}

} // namespace

// THE regression contract: a neutral-estimate user (default estimates, empty fatigue maps) gets the
// fixed DiversityReranker output BYTE-FOR-BYTE, in both composition modes.
TEST(PersonalizedDiversityRerankerTest, NeutralEstimatesAreByteIdenticalToFixed) {
    std::vector<rr::Reel> reels = makeReels(
        {{0, 10, {1, 0}}, {1, 10, {1, 0}}, {2, 20, {0, 1}}, {3, 30, {0, 1}}, {4, 10, {1, 0}}});
    const std::vector<rr::Candidate> pool = {cand(0, 0.95f), cand(1, 0.90f), cand(2, 0.85f),
                                             cand(3, 0.80f), cand(4, 0.75f)};
    rr::User user{}; // default estimates: 0.5 / 0.5 / empty maps

    for (bool useMmr : {false, true}) {
        const rr::DiversityReranker fixed(reels, cfg(useMmr));
        const rr::PersonalizedDiversityReranker personalized(reels, cfg(useMmr));
        const std::vector<rr::RankedReel> want = fixed.rerank(user, pool, 10);
        const std::vector<rr::RankedReel> got = personalized.rerank(user, pool, 10);
        ASSERT_EQ(got.size(), want.size()) << "useMmr=" << useMmr;
        for (std::size_t i = 0; i < got.size(); ++i) {
            EXPECT_EQ(got[i].reelId, want[i].reelId) << "useMmr=" << useMmr << " i=" << i;
            EXPECT_FLOAT_EQ(got[i].score, want[i].score) << "useMmr=" << useMmr << " i=" << i;
            EXPECT_EQ(got[i].rank, want[i].rank) << "useMmr=" << useMmr << " i=" << i;
        }
    }
}

// A low-repetition-tolerance user gets a TIGHTER per-topic cap; a high-tolerance user a LOOSER one,
// up to scaleMax. Six same-topic candidates (distinct creators) isolate the topic cap.
TEST(PersonalizedDiversityRerankerTest, RepetitionToleranceScalesTopicCap) {
    std::vector<rr::Reel> reels = makeReels({{0, 1, {1, 0}},
                                             {1, 1, {1, 0}},
                                             {2, 1, {1, 0}},
                                             {3, 1, {1, 0}},
                                             {4, 1, {1, 0}},
                                             {5, 1, {1, 0}}});
    const std::vector<rr::Candidate> pool = {cand(0, 0.95f), cand(1, 0.90f), cand(2, 0.85f),
                                             cand(3, 0.80f), cand(4, 0.75f), cand(5, 0.70f)};
    const rr::PersonalizedDiversityReranker reranker(reels, cfg(/*useMmr=*/false));
    const rr::DiversityReranker fixed(reels, cfg(false));

    rr::User neutral{};
    const std::size_t fixedTopic = maxPerTopic(fixed.rerank(neutral, pool, 10), reels);
    EXPECT_EQ(fixedTopic, 3u); // maxPerTopic 3, feedSize 10 => cap 3

    rr::User low{};
    low.estimatedRepetitionTolerance = 0.1f; // intolerant => tighter
    const std::size_t lowTopic = maxPerTopic(reranker.rerank(low, pool, 10), reels);

    rr::User high{};
    high.estimatedRepetitionTolerance = 1.0f; // very tolerant => looser (scaleMax = 2.0)
    const std::size_t highTopic = maxPerTopic(reranker.rerank(high, pool, 10), reels);

    EXPECT_LT(lowTopic, fixedTopic) << "low tolerance must tighten the topic cap";
    EXPECT_GT(highTopic, fixedTopic) << "high tolerance must loosen the topic cap";
    EXPECT_EQ(highTopic, 6u) << "scaleMax=2.0 => round(3*2)=6 admitted";
}

// A topic the user is estimated-fatigued of is squeezed toward a single slot, while unfatigued
// topics keep the base cap.
TEST(PersonalizedDiversityRerankerTest, EstimatedTopicFatigueTightensThatTopic) {
    // Four topic-1 candidates + two topic-2 candidates.
    std::vector<rr::Reel> reels = makeReels({{0, 1, {1, 0}},
                                             {1, 1, {1, 0}},
                                             {2, 1, {1, 0}},
                                             {3, 1, {1, 0}},
                                             {4, 2, {0, 1}},
                                             {5, 2, {0, 1}}});
    const std::vector<rr::Candidate> pool = {cand(0, 0.95f), cand(1, 0.90f), cand(2, 0.85f),
                                             cand(3, 0.80f), cand(4, 0.75f), cand(5, 0.70f)};
    const rr::PersonalizedDiversityReranker reranker(reels, cfg(/*useMmr=*/false));

    rr::User user{};
    user.estimatedTopicFatigue[rr::TopicId{1}] = 0.9f; // fatigued of topic 1 only
    const std::vector<rr::RankedReel> feed = reranker.rerank(user, pool, 10);

    EXPECT_EQ(topicCount(feed, reels, 1), 1u) << "fatigued topic 1 squeezed to a single slot";
    EXPECT_EQ(topicCount(feed, reels, 2), 2u) << "unfatigued topic 2 keeps its items";
}

// Peak estimated creator fatigue tightens the per-creator cap.
TEST(PersonalizedDiversityRerankerTest, EstimatedCreatorFatigueTightensCreatorCap) {
    // Three candidates from creator 5 (distinct topics so only the creator cap binds).
    std::vector<rr::Reel> reels =
        makeReels({{5, 10, {1, 0}}, {5, 20, {0, 1}}, {5, 30, {1, 1}}, {6, 40, {1, 0}}});
    const std::vector<rr::Candidate> pool = {cand(0, 0.95f), cand(1, 0.90f), cand(2, 0.85f),
                                             cand(3, 0.80f)};
    const rr::PersonalizedDiversityReranker reranker(reels, cfg(/*useMmr=*/false));

    rr::User neutral{};
    std::size_t creator5neutral = 0;
    for (const rr::RankedReel &r : reranker.rerank(neutral, pool, 10)) {
        if (reels[r.reelId.value].creatorId.value == 5) {
            ++creator5neutral;
        }
    }
    EXPECT_EQ(creator5neutral, 2u); // fixed maxPerCreator 2

    rr::User fatigued{};
    fatigued.estimatedCreatorFatigue[rr::CreatorId{5}] = 0.9f;
    std::size_t creator5fatigued = 0;
    for (const rr::RankedReel &r : reranker.rerank(fatigued, pool, 10)) {
        if (reels[r.reelId.value].creatorId.value == 5) {
            ++creator5fatigued;
        }
    }
    EXPECT_LT(creator5fatigued, creator5neutral) << "creator fatigue must tighten the creator cap";
}

// The per-user MMR lambda (from estimated novelty tolerance) changes the WITHIN-set ordering:
// a novelty-tolerant user (lower lambda => more diversity) orders the same set differently from a
// novelty-averse user (higher lambda => raw relevance).
TEST(PersonalizedDiversityRerankerTest, NoveltyToleranceChangesMmrOrdering) {
    // id0,id1 cosine-identical; id2 orthogonal; strictly decreasing relevance.
    std::vector<rr::Reel> reels = makeReels({{0, 10, {1, 0}}, {1, 20, {1, 0}}, {2, 30, {0, 1}}});
    const std::vector<rr::Candidate> pool = {cand(0, 1.0f), cand(1, 0.93f), cand(2, 0.90f)};
    const rr::PersonalizedDiversityReranker reranker(reels, cfg(/*useMmr=*/true));

    rr::User averse{};
    averse.estimatedNoveltyTolerance = 0.0f; // lambda -> lambdaMax (0.90): relevance-heavy
    rr::User tolerant{};
    tolerant.estimatedNoveltyTolerance = 1.0f; // lambda -> lambdaMin (0.60): diversity-heavy

    const std::vector<uint32_t> averseOrder = feedIds(reranker.rerank(averse, pool, 10));
    const std::vector<uint32_t> tolerantOrder = feedIds(reranker.rerank(tolerant, pool, 10));

    // Same SET, different ORDER: the novelty-tolerant user pulls the orthogonal id2 above the near-
    // duplicate id1.
    EXPECT_EQ(averseOrder, (std::vector<uint32_t>{0, 1, 2}));
    EXPECT_EQ(tolerantOrder, (std::vector<uint32_t>{0, 2, 1}));
}

// Universal hard rules hold for a personalized (non-neutral) user: no duplicate, no seen reel.
TEST(PersonalizedDiversityRerankerTest, HardRulesHoldForPersonalizedUser) {
    std::vector<rr::Reel> reels =
        makeReels({{0, 1, {1, 0}}, {1, 1, {1, 0}}, {2, 2, {0, 1}}, {3, 3, {0, 1}}, {4, 1, {1, 0}}});
    std::vector<rr::Candidate> pool = {cand(0, 0.95f), cand(1, 0.90f), cand(2, 0.85f),
                                       cand(3, 0.80f), cand(4, 0.75f)};
    pool.push_back(cand(1, 0.60f)); // duplicate id
    rr::User user{};
    user.seenReels.insert(rr::ReelId{2}); // already seen
    user.estimatedRepetitionTolerance = 0.9f;
    user.estimatedNoveltyTolerance = 0.2f;

    for (bool useMmr : {false, true}) {
        const rr::PersonalizedDiversityReranker reranker(reels, cfg(useMmr));
        const std::vector<rr::RankedReel> feed = reranker.rerank(user, pool, 10);
        std::unordered_map<uint32_t, int> seen;
        for (const rr::RankedReel &r : feed) {
            EXPECT_EQ(++seen[r.reelId.value], 1)
                << "duplicate id in feed (useMmr=" << useMmr << ")";
            EXPECT_NE(r.reelId.value, 2u) << "seen reel in feed (useMmr=" << useMmr << ")";
        }
    }
}

// Pure function of inputs: same call twice => identical feed.
TEST(PersonalizedDiversityRerankerTest, Deterministic) {
    std::vector<rr::Reel> reels =
        makeReels({{0, 1, {1, 0}}, {1, 2, {0, 1}}, {2, 1, {1, 0}}, {3, 3, {1, 1}}, {4, 2, {0, 1}}});
    const std::vector<rr::Candidate> pool = {cand(0, 0.95f), cand(1, 0.90f), cand(2, 0.85f),
                                             cand(3, 0.80f), cand(4, 0.75f)};
    rr::User user{};
    user.estimatedRepetitionTolerance = 0.3f;
    user.estimatedNoveltyTolerance = 0.8f;
    user.estimatedTopicFatigue[rr::TopicId{1}] = 0.5f;
    for (bool useMmr : {false, true}) {
        const rr::PersonalizedDiversityReranker reranker(reels, cfg(useMmr));
        EXPECT_EQ(feedIds(reranker.rerank(user, pool, 10)),
                  feedIds(reranker.rerank(user, pool, 10)));
    }
}

// ============================================================================================
//  WeightedRanker repetition-penalty scaling (plan task 3). The gate is a defaulted ctor param;
//  neutral tolerance (or gate-off) is byte-identical, tolerant removes the penalty, intolerant
//  doubles it. This lives here (an owned NEW file) because it is part of the personalized-diversity
//  feature; the existing weighted_ranker_test.cpp is left untouched and stays green.
// ============================================================================================
namespace {

// One reel (creator 0 / topic 0) plus one recent interaction on it, so the ranker's repetition
// feature for that candidate is non-zero (shares creator + topic with the recent window).
struct RepFixture {
    std::vector<rr::Reel> reels;
    std::vector<rr::Candidate> pool;
    rr::User user;
};

RepFixture repFixture(float estimatedRepetitionTolerance) {
    RepFixture fx;
    fx.reels = makeReels({{0, 0, {1, 0}}});
    rr::Candidate c{};
    c.reelId = rr::ReelId{0};
    c.source = rr::CandidateSource::VectorHNSW;
    c.retrievalSimilarity = 0.5f;
    fx.pool = {c};
    fx.user.estimatedRepetitionTolerance = estimatedRepetitionTolerance;
    rr::InteractionEvent e{};
    e.reelId = rr::ReelId{0};
    e.creatorId = rr::CreatorId{0};
    e.type = rr::InteractionType::CompleteWatch;
    e.watchRatio = 1.0f;
    fx.user.recentInteractions.push_back(e); // repetition feature => 1.0 for candidate 0
    return fx;
}

float repetitionContribution(const std::vector<rr::Candidate> &ranked) {
    const auto it = ranked.front().featureContributions.find("repetition_penalty");
    return it == ranked.front().featureContributions.end() ? 0.0f : it->second;
}

} // namespace

TEST(WeightedRankerRepetitionScalingTest, NeutralAndGateOffAreByteIdentical) {
    const rr::RankingConfig config; // repetitionPenalty default 0.15
    RepFixture off = repFixture(0.5f);
    RepFixture on = repFixture(0.5f);

    const rr::WeightedRanker gateOff(off.reels, config, /*contentV2=*/false,
                                     /*personalizedDiversity=*/false);
    const rr::WeightedRanker gateOnNeutral(on.reels, config, false, /*personalizedDiversity=*/true);

    const std::vector<rr::Candidate> a = gateOff.rank(off.user, off.pool, 0);
    const std::vector<rr::Candidate> b = gateOnNeutral.rank(on.user, on.pool, 0);

    // Neutral tolerance => x1.0 => byte-identical repetition contribution AND overall score.
    EXPECT_FLOAT_EQ(repetitionContribution(a), repetitionContribution(b));
    EXPECT_FLOAT_EQ(a.front().rankingScore, b.front().rankingScore);
    EXPECT_LT(repetitionContribution(a), 0.0f); // a real (negative) penalty is present
}

TEST(WeightedRankerRepetitionScalingTest, ToleranceScalesThePenalty) {
    const rr::RankingConfig config;
    RepFixture neutral = repFixture(0.5f);
    RepFixture tolerant = repFixture(1.0f);
    RepFixture intolerant = repFixture(0.0f);

    const rr::WeightedRanker ranker0(neutral.reels, config, false, true);
    const rr::WeightedRanker ranker1(tolerant.reels, config, false, true);
    const rr::WeightedRanker ranker2(intolerant.reels, config, false, true);

    const float pNeutral = repetitionContribution(ranker0.rank(neutral.user, neutral.pool, 0));
    const float pTolerant = repetitionContribution(ranker1.rank(tolerant.user, tolerant.pool, 0));
    const float pIntolerant =
        repetitionContribution(ranker2.rank(intolerant.user, intolerant.pool, 0));

    // Integration-calibrated bounds [0.6, 1.4] (weighted_ranker.cpp): the raw [0, 2] doubling
    // stacked with the reranker's tighter caps into a net U_s loss for intolerant cohorts at
    // medium scale, so the ranking-side lever is deliberately gentler than the cap lever.
    EXPECT_FLOAT_EQ(pTolerant, 0.6f * pNeutral);   // clamp(2*(1-1)=0, 0.6, 1.4) => x0.6
    EXPECT_FLOAT_EQ(pIntolerant, 1.4f * pNeutral); // clamp(2*(1-0)=2, 0.6, 1.4) => x1.4
    EXPECT_LT(pIntolerant, pNeutral);              // more negative
    EXPECT_GT(pTolerant, pNeutral);                // less negative
}
