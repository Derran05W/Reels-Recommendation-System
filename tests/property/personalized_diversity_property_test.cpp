// Phase 17 personalized-diversity property suite (plan task 5). Two guarantees:
//   (1) UNIVERSAL HARD RULES (V1 TDD 15.1, kept universal under personalization): across >= 20
//       randomized fixtures with RANDOM per-user tolerance estimates + fatigue maps, in BOTH
//       composition modes, no PersonalizedDiversityReranker feed ever contains a duplicate id or a
//       seen reel, is longer than feedSize, or has non-contiguous ranks; and the reranker is
//       deterministic. (The per-user caps themselves are a personalized soft surface, not a
//       universal invariant, so — mirroring P9 — the universal cross-check is the no-dup/no-seen
//       floor that must hold for EVERY user.)
//   (2) FEED-LENGTH REPORT across the tolerance spectrum (the Phase 9 short-feed hazard measured
//       honestly): on a deliberately CONCENTRATED pool (few topics/creators, so caps bind and feeds
//       run short), delivered feed length is measured for repetition tolerance 0.0..1.0 and
//       PRINTED. We assert only the DIRECTION that always holds — looser caps never deliver fewer
//       items — and document the numbers rather than pinning a magic length.
#include "rr/recommendation/personalized_diversity_reranker.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "rr/core/embedding.hpp"
#include "rr/domain/candidate.hpp"
#include "rr/domain/ids.hpp"
#include "rr/domain/reel.hpp"
#include "rr/domain/user.hpp"
#include "rr/infrastructure/config.hpp"
#include "rr/infrastructure/random.hpp"

namespace {

constexpr int kNumSeeds = 24; // >= 20 randomized fixtures (D17 property-test requirement)

struct Fixture {
    std::vector<rr::Reel> reels;
    rr::User user;
    std::vector<rr::Candidate> pool;
    rr::DiversityConfig config;
    std::size_t feedSize;
};

Fixture makeFixture(rr::Rng &rng) {
    Fixture fx;
    const std::size_t reelCount = 30 + rng.uniformInt(50);
    const uint32_t creators = 3 + static_cast<uint32_t>(rng.uniformInt(6));
    const uint32_t topics = 3 + static_cast<uint32_t>(rng.uniformInt(5));
    fx.reels.reserve(reelCount);
    for (std::size_t i = 0; i < reelCount; ++i) {
        rr::Reel r{};
        r.id = rr::ReelId{static_cast<uint32_t>(i)};
        r.creatorId = rr::CreatorId{static_cast<uint32_t>(rng.uniformInt(creators))};
        r.primaryTopic = rr::TopicId{static_cast<uint32_t>(rng.uniformInt(topics))};
        const double theta = rng.uniform(0.0, 6.283185307179586);
        r.embedding = {static_cast<float>(std::cos(theta)), static_cast<float>(std::sin(theta))};
        r.active = true;
        fx.reels.push_back(std::move(r));
        if (rng.bernoulli(0.2)) {
            fx.user.seenReels.insert(rr::ReelId{static_cast<uint32_t>(i)});
        }
    }

    std::vector<uint32_t> ids(reelCount);
    for (std::size_t i = 0; i < reelCount; ++i) {
        ids[i] = static_cast<uint32_t>(i);
    }
    for (std::size_t i = reelCount; i > 1; --i) {
        std::swap(ids[i - 1], ids[rng.uniformInt(i)]);
    }
    const std::size_t poolSize = 10 + rng.uniformInt(reelCount - 10 + 1);
    fx.pool.reserve(poolSize + 3);
    for (std::size_t i = 0; i < poolSize; ++i) {
        rr::Candidate c{};
        c.reelId = rr::ReelId{ids[i]};
        c.source = rr::CandidateSource::VectorHNSW;
        c.rankingScore = static_cast<float>(rng.uniform(-1.0, 1.0));
        fx.pool.push_back(c);
    }
    for (int d = 0; d < 3; ++d) { // occasional duplicate ids (exercise no-dup)
        if (rng.bernoulli(0.5) && !fx.pool.empty()) {
            rr::Candidate c = fx.pool[rng.uniformInt(fx.pool.size())];
            c.rankingScore = static_cast<float>(rng.uniform(-1.0, 1.0));
            fx.pool.push_back(c);
        }
    }

    // RANDOM per-user tolerance estimates + a few fatigue entries => exercise the general path.
    fx.user.estimatedRepetitionTolerance = static_cast<float>(rng.uniform(0.0, 1.0));
    fx.user.estimatedNoveltyTolerance = static_cast<float>(rng.uniform(0.0, 1.0));
    for (uint32_t t = 0; t < topics; ++t) {
        if (rng.bernoulli(0.4)) {
            fx.user.estimatedTopicFatigue[rr::TopicId{t}] =
                static_cast<float>(rng.uniform(0.0, 1.0));
        }
    }
    for (uint32_t c = 0; c < creators; ++c) {
        if (rng.bernoulli(0.3)) {
            fx.user.estimatedCreatorFatigue[rr::CreatorId{c}] =
                static_cast<float>(rng.uniform(0.0, 1.0));
        }
    }

    fx.config.enabled = true;
    fx.config.maxPerCreator = 1 + static_cast<uint32_t>(rng.uniformInt(3));
    fx.config.maxPerTopic = 1 + static_cast<uint32_t>(rng.uniformInt(4));
    fx.config.mmrLambda = rng.uniform(0.0, 1.0);
    fx.feedSize = 5 + rng.uniformInt(16);
    return fx;
}

std::vector<uint32_t> feedIds(const std::vector<rr::RankedReel> &feed) {
    std::vector<uint32_t> ids;
    for (const rr::RankedReel &r : feed) {
        ids.push_back(r.reelId.value);
    }
    return ids;
}

} // namespace

TEST(PersonalizedDiversityPropertyTest, NoFeedViolatesUniversalHardRules) {
    for (int seed = 0; seed < kNumSeeds; ++seed) {
        rr::Rng rng(static_cast<uint64_t>(17000 + seed));
        const Fixture fx = makeFixture(rng);
        for (bool useMmr : {false, true}) {
            rr::DiversityConfig cfg = fx.config;
            cfg.useMmr = useMmr;
            const rr::PersonalizedDiversityReranker reranker(fx.reels, cfg);
            const std::vector<rr::RankedReel> feed = reranker.rerank(fx.user, fx.pool, fx.feedSize);

            std::unordered_set<uint32_t> seenInFeed;
            for (std::size_t i = 0; i < feed.size(); ++i) {
                const uint32_t id = feed[i].reelId.value;
                EXPECT_TRUE(seenInFeed.insert(id).second)
                    << "seed " << seed << " useMmr " << useMmr << ": duplicate id " << id;
                EXPECT_FALSE(fx.user.seenReels.contains(rr::ReelId{id}))
                    << "seed " << seed << " useMmr " << useMmr << ": seen id " << id;
                ASSERT_LT(id, fx.reels.size());
                EXPECT_EQ(feed[i].rank, i) << "seed " << seed << " useMmr " << useMmr;
            }
            EXPECT_LE(feed.size(), fx.feedSize) << "seed " << seed << " useMmr " << useMmr;
        }
    }
}

TEST(PersonalizedDiversityPropertyTest, Deterministic) {
    for (int seed = 0; seed < kNumSeeds; ++seed) {
        rr::Rng rng(static_cast<uint64_t>(4700 + seed));
        const Fixture fx = makeFixture(rng);
        for (bool useMmr : {false, true}) {
            rr::DiversityConfig cfg = fx.config;
            cfg.useMmr = useMmr;
            const rr::PersonalizedDiversityReranker reranker(fx.reels, cfg);
            const std::vector<rr::RankedReel> a = reranker.rerank(fx.user, fx.pool, fx.feedSize);
            const std::vector<rr::RankedReel> b = reranker.rerank(fx.user, fx.pool, fx.feedSize);
            EXPECT_EQ(feedIds(a), feedIds(b)) << "seed " << seed << " useMmr " << useMmr;
        }
    }
}

// FEED-LENGTH REPORT (the P9 short-feed hazard, per cohort). A concentrated pool (3 topics /
// 4 creators, 40 candidates, feedSize 10) makes the caps bind so feeds run short; we sweep the
// repetition-tolerance spectrum and print delivered feed length. Direction assertion only: looser
// caps (higher tolerance) never deliver FEWER items on the same pool.
TEST(PersonalizedDiversityPropertyTest, FeedLengthAcrossToleranceSpectrum) {
    constexpr std::size_t kFeedSize = 10;
    std::vector<rr::Reel> reels;
    std::vector<rr::Candidate> pool;
    for (uint32_t i = 0; i < 40; ++i) {
        rr::Reel r{};
        r.id = rr::ReelId{i};
        r.creatorId = rr::CreatorId{i % 4};   // 4 creators
        r.primaryTopic = rr::TopicId{i % 3};  // 3 topics => heavy concentration
        const double theta = (i % 3) * 2.094; // 3 embedding clusters
        r.embedding = {static_cast<float>(std::cos(theta)), static_cast<float>(std::sin(theta))};
        r.active = true;
        reels.push_back(r);
        rr::Candidate c{};
        c.reelId = rr::ReelId{i};
        c.source = rr::CandidateSource::VectorHNSW;
        c.rankingScore = static_cast<float>(40 - i);
        pool.push_back(c);
    }
    rr::DiversityConfig cfg;
    cfg.maxPerCreator = 2;
    cfg.maxPerTopic = 3;
    cfg.useMmr = true;
    const rr::PersonalizedDiversityReranker reranker(reels, cfg);

    const double spectrum[] = {0.0, 0.25, 0.5, 0.75, 1.0};
    std::size_t previous = 0;
    bool first = true;
    std::cout
        << "===== Phase 17 feed-length vs repetition tolerance (concentrated pool: 3 topics / "
           "4 creators / feedSize 10) =====\n";
    for (double t : spectrum) {
        rr::User user{};
        user.estimatedRepetitionTolerance = static_cast<float>(t);
        const std::size_t len = reranker.rerank(user, pool, kFeedSize).size();
        std::cout << "[p17-feedlen] repetitionTolerance=" << t << " deliveredFeedLength=" << len
                  << " / " << kFeedSize << "\n";
        if (!first) {
            EXPECT_GE(len, previous)
                << "looser caps (higher tolerance) must not deliver fewer items (t=" << t << ")";
        }
        previous = len;
        first = false;
    }
    std::cout
        << "=================================================================================="
           "==============\n";
}
