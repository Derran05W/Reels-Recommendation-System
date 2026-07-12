// Property tests for Phase 8 exploration (TDD 24.3 seed sweep). Flagship properties:
//   (1) the ExplorationCandidateSource never returns more than its configured pool cap;
//   (2) epsilon=0 makes HNSWExplorationRecommender produce feeds IDENTICAL to HNSWRankerRecommender
//       on the same dataset/seed (the verified no-op) across many seeds and requests;
//   (3) with a large epsilon, exploration-labeled reels appear in feeds;
//   (4) feeds never contain seen, inactive, invalid, or duplicate reels with exploration on.
#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <unordered_set>
#include <utility>
#include <vector>

#include "rr/candidate_sources/exploration_candidate_source.hpp"
#include "rr/core/embedding.hpp"
#include "rr/domain/candidate.hpp"
#include "rr/domain/ids.hpp"
#include "rr/domain/recommendation.hpp"
#include "rr/domain/reel.hpp"
#include "rr/domain/user.hpp"
#include "rr/infrastructure/config.hpp"
#include "rr/infrastructure/random.hpp"
#include "rr/recommendation/recommender.hpp"
#include "rr/recommendation/recommender_factory.hpp"
#include "rr/simulation/dataset_generator.hpp"

namespace {

constexpr int kNumSeeds = 12; // >= 10, per the phase plan

rr::ExperimentConfig propConfig(double epsilon) {
    rr::ExperimentConfig config{};
    config.simulation.reels = 300;
    config.simulation.users = 12;
    config.simulation.creators = 30;
    config.simulation.topics = 8;
    config.simulation.dimensions = 16;
    config.exploration.enabled = true;
    config.exploration.epsilon = epsilon;
    config.algorithm = rr::RecommendationAlgorithm::HnswRankerExploration;
    return config;
}

void seedPreferences(rr::GeneratedDataset &ds) {
    for (std::size_t i = 0; i < ds.users.size(); ++i) {
        ds.users[i].estimatedPreference = ds.hiddenStates[i].hiddenPreference;
    }
}

rr::RecommendationRequest requestFor(const rr::User &user) {
    rr::RecommendationRequest req{};
    req.userId = user.id;
    req.feedSize = 10;
    req.candidateLimit = 500; // >= catalog, so exploration candidates are not capped out
    req.enableExploration = true;
    req.requestTime = 500'000;
    return req;
}

bool feedHasExploration(const rr::RecommendationResponse &resp) {
    for (const rr::RankedReel &r : resp.reels) {
        for (rr::CandidateSource s : r.sources) {
            if (s == rr::CandidateSource::Exploration) {
                return true;
            }
        }
    }
    return false;
}

rr::Reel randReel(rr::Rng &rng, uint32_t id) {
    rr::Reel reel{};
    reel.id = rr::ReelId{id};
    reel.creatorId = rr::CreatorId{100 + id};
    rr::Embedding e{static_cast<float>(rng.gaussian()), static_cast<float>(rng.gaussian()),
                    static_cast<float>(rng.gaussian())};
    rr::normalize(e);
    reel.embedding = std::move(e);
    reel.createdAt = static_cast<rr::Timestamp>(rng.uniformInt(20000));
    reel.impressionCount = rng.uniformInt(5000);
    reel.active = rng.bernoulli(0.85);
    return reel;
}

} // namespace

TEST(ExplorationPropertyTest, SourceNeverExceedsPoolCapAndDedups) {
    for (uint64_t seed = 0; seed < 25; ++seed) {
        rr::Rng dataRng(seed);
        const uint32_t reelCount = 5 + static_cast<uint32_t>(dataRng.uniformInt(60));
        std::vector<rr::Reel> reels;
        reels.reserve(reelCount);
        for (uint32_t i = 0; i < reelCount; ++i) {
            reels.push_back(randReel(dataRng, i));
        }
        const uint32_t poolCap = static_cast<uint32_t>(dataRng.uniformInt(40));
        rr::User user{};
        rr::Embedding pref{1.0f, 0.0f, 0.0f};
        rr::normalize(pref);
        user.estimatedPreference = pref;

        rr::Rng gateRng(seed + 777);
        rr::ExplorationCandidateSource source(reels, /*epsilon=*/1.0, poolCap,
                                              /*window=*/5000.0, &gateRng);
        rr::RecommendationRequest req{};
        req.feedSize = 10;
        req.enableExploration = true;
        req.requestTime = static_cast<rr::Timestamp>(dataRng.uniformInt(25000));

        const std::vector<rr::Candidate> cands = source.generate(user, req);
        EXPECT_LE(cands.size(), static_cast<std::size_t>(poolCap)) << "seed " << seed;
        std::unordered_set<uint32_t> seen;
        for (const rr::Candidate &c : cands) {
            EXPECT_TRUE(seen.insert(c.reelId.value).second) << "dup at seed " << seed;
            EXPECT_EQ(c.source, rr::CandidateSource::Exploration);
        }
    }
}

TEST(ExplorationPropertyTest, EpsilonZeroIsIdenticalToHnswRanker) {
    for (int s = 1; s <= kNumSeeds; ++s) {
        const auto seed = static_cast<uint64_t>(s);
        rr::ExperimentConfig config = propConfig(/*epsilon=*/0.0); // exploration on, epsilon 0
        rr::GeneratedDataset ds = rr::generateDataset(config.simulation, seed);
        seedPreferences(ds);
        rr::RecommenderDeps deps{ds.reels, ds.users, config};

        auto expl = rr::makeRecommender(rr::RecommendationAlgorithm::HnswRankerExploration, deps,
                                        rr::forkRng(seed, "recommender"));
        auto ranker = rr::makeRecommender(rr::RecommendationAlgorithm::HnswRanker, deps,
                                          rr::forkRng(seed, "recommender"));
        for (const rr::User &user : ds.users) {
            const rr::RecommendationResponse a = expl->recommend(requestFor(user));
            const rr::RecommendationResponse b = ranker->recommend(requestFor(user));
            ASSERT_EQ(a.reels.size(), b.reels.size()) << "seed " << seed;
            for (std::size_t i = 0; i < a.reels.size(); ++i) {
                EXPECT_EQ(a.reels[i].reelId, b.reels[i].reelId) << "seed " << seed << " pos " << i;
                EXPECT_FLOAT_EQ(a.reels[i].score, b.reels[i].score) << "seed " << seed;
                EXPECT_EQ(a.reels[i].sources, b.reels[i].sources) << "seed " << seed;
                ASSERT_EQ(a.reels[i].featureContributions.size(),
                          b.reels[i].featureContributions.size());
                for (const auto &[key, value] : a.reels[i].featureContributions) {
                    ASSERT_EQ(b.reels[i].featureContributions.count(key), 1u) << key;
                    EXPECT_FLOAT_EQ(value, b.reels[i].featureContributions.at(key)) << key;
                }
            }
        }
    }
}

TEST(ExplorationPropertyTest, LargeEpsilonSurfacesExplorationLabeledReels) {
    std::size_t feedsWithExploration = 0;
    for (int s = 1; s <= kNumSeeds; ++s) {
        const auto seed = static_cast<uint64_t>(s);
        rr::ExperimentConfig config = propConfig(/*epsilon=*/0.3);
        rr::GeneratedDataset ds = rr::generateDataset(config.simulation, seed);
        seedPreferences(ds);
        rr::RecommenderDeps deps{ds.reels, ds.users, config};
        auto rec = rr::makeRecommender(rr::RecommendationAlgorithm::HnswRankerExploration, deps,
                                       rr::forkRng(seed, "recommender"));
        for (const rr::User &user : ds.users) {
            if (feedHasExploration(rec->recommend(requestFor(user)))) {
                ++feedsWithExploration;
            }
        }
    }
    EXPECT_GT(feedsWithExploration, 0u)
        << "exploration must inject discovery reels into some feed with epsilon=0.3";
}

TEST(ExplorationPropertyTest, FeedsNeverContainSeenInactiveOrInvalidReels) {
    for (int s = 1; s <= kNumSeeds; ++s) {
        const auto seed = static_cast<uint64_t>(s);
        rr::ExperimentConfig config = propConfig(/*epsilon=*/0.3);
        rr::GeneratedDataset ds = rr::generateDataset(config.simulation, seed);
        seedPreferences(ds);
        rr::RecommenderDeps deps{ds.reels, ds.users, config};
        auto rec = rr::makeRecommender(rr::RecommendationAlgorithm::HnswRankerExploration, deps,
                                       rr::forkRng(seed, "recommender"));

        // Inject filter pressure AFTER the index is built (the orchestrator reads live state).
        for (std::size_t i = 0; i < 5 && i < ds.reels.size(); ++i) {
            ds.reels[i].active = false;
        }
        for (rr::User &user : ds.users) {
            for (uint32_t r = 10; r < 15 && r < ds.reels.size(); ++r) {
                user.seenReels.insert(rr::ReelId{r});
            }
        }

        for (const rr::User &user : ds.users) {
            const rr::RecommendationResponse resp = rec->recommend(requestFor(user));
            std::unordered_set<uint32_t> ids;
            for (const rr::RankedReel &r : resp.reels) {
                ASSERT_LT(r.reelId.value, ds.reels.size());
                EXPECT_TRUE(ds.reels[r.reelId.value].active)
                    << "inactive reel in feed, seed " << seed;
                EXPECT_FALSE(user.seenReels.contains(r.reelId))
                    << "seen reel in feed, seed " << seed;
                EXPECT_FALSE(ds.reels[r.reelId.value].embedding.empty()) << "invalid embedding";
                EXPECT_TRUE(ids.insert(r.reelId.value).second) << "duplicate reel in feed";
            }
        }
    }
}
