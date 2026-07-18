// Unit tests for the Realism V2 ranking features (Phase 15, plan task 3): the FeatureExtractor V2
// fields (modality matches, content-value scalar passthroughs, language-match inference, the
// deferred save-popularity placeholder) and the WeightedRanker's gated V2 contributions. Every
// normalization is checked against hand-computed values on tiny constructed pools. The V2 fields
// are extracted ONLY when the extractor/ranker was constructed with contentV2 = true (the
// realism.content_v2 gate); gate-off keeps them zero and never emits their contribution keys (D17).
#include "rr/recommendation/feature_extractor.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "rr/core/embedding.hpp"
#include "rr/domain/candidate.hpp"
#include "rr/domain/ids.hpp"
#include "rr/domain/interaction.hpp"
#include "rr/domain/reel.hpp"
#include "rr/domain/user.hpp"
#include "rr/infrastructure/config.hpp"
#include "rr/recommendation/weighted_ranker.hpp"

namespace {

// A reel with inert V1 defaults; individual tests set the V2 attributes they exercise. The V1
// embedding is a unit {1,0} so the (unused-here) similarity/session paths stay well-defined.
rr::Reel makeReel(uint32_t id, uint32_t creator = 0, uint32_t topic = 0) {
    rr::Reel r{};
    r.id = rr::ReelId{id};
    r.creatorId = rr::CreatorId{creator};
    r.primaryTopic = rr::TopicId{topic};
    r.embedding = {1.0f, 0.0f};
    r.intrinsicQuality = 0.5f;
    r.durationSeconds = 30.0f;
    r.createdAt = 0;
    r.active = true;
    return r;
}

rr::Candidate cand(uint32_t id, float similarity = 0.0f) {
    rr::Candidate c{};
    c.reelId = rr::ReelId{id};
    c.source = rr::CandidateSource::VectorHNSW;
    c.retrievalSimilarity = similarity;
    return c;
}

rr::InteractionEvent event(uint32_t reelId) {
    rr::InteractionEvent e{};
    e.reelId = rr::ReelId{reelId};
    e.type = rr::InteractionType::CompleteWatch;
    return e;
}

rr::FeatureVector extractOneV2(const std::vector<rr::Reel> &reels, const rr::User &user,
                               const rr::Candidate &c, bool contentV2,
                               const rr::RankingConfig &config = {}) {
    rr::FeatureExtractor extractor(reels, config, contentV2);
    std::vector<rr::Candidate> pool{c};
    return extractor.extract(user, pool, 0).front();
}

} // namespace

// --- Gate-off: every V2 field stays at its zero default (D17 byte-identity) ---------------------

TEST(FeatureExtractorV2, GateOffLeavesEveryV2FieldZero) {
    std::vector<rr::Reel> reels{makeReel(0)};
    reels[0].visualStyleEmbedding = {0.0f, 1.0f};
    reels[0].musicEmbedding = {1.0f, 0.0f};
    reels[0].emotionalToneEmbedding = {0.0f, 1.0f};
    reels[0].clickbaitStrength = 0.7f;
    reels[0].emotionalIntensity = 0.6f;
    reels[0].usefulness = 0.5f;
    reels[0].productionQuality = 0.4f;
    reels[0].informationDensity = 0.3f;
    reels[0].language = rr::LanguageId{2};

    rr::User user{};
    user.estimatedVisualPreference = {0.0f, 1.0f};

    const rr::FeatureVector f = extractOneV2(reels, user, cand(0), /*contentV2=*/false);
    EXPECT_FLOAT_EQ(f.visualMatch, 0.0f);
    EXPECT_FLOAT_EQ(f.musicMatch, 0.0f);
    EXPECT_FLOAT_EQ(f.emotionalMatch, 0.0f);
    EXPECT_FLOAT_EQ(f.clickbait, 0.0f);
    EXPECT_FLOAT_EQ(f.emotionalIntensity, 0.0f);
    EXPECT_FLOAT_EQ(f.usefulness, 0.0f);
    EXPECT_FLOAT_EQ(f.productionQuality, 0.0f);
    EXPECT_FLOAT_EQ(f.informationDensity, 0.0f);
    EXPECT_FLOAT_EQ(f.languageMatch, 0.0f);
    EXPECT_FLOAT_EQ(f.savePopularity, 0.0f);
}

// --- Modality match: (cos(estimate, reel modality embedding) + 1) / 2 ---------------------------

TEST(FeatureExtractorV2, ModalityMatchAffineMap) {
    std::vector<rr::Reel> reels{makeReel(0)};
    reels[0].visualStyleEmbedding = {0.0f, 1.0f};
    reels[0].musicEmbedding = {1.0f, 0.0f};
    reels[0].emotionalToneEmbedding = {0.6f, 0.8f};

    rr::User user{};
    user.estimatedVisualPreference = {0.0f, 1.0f};    // aligned -> cos 1 -> 1.0
    user.estimatedMusicPreference = {0.0f, 1.0f};     // orthogonal -> cos 0 -> 0.5
    user.estimatedEmotionalPreference = {0.6f, 0.8f}; // aligned -> cos 1 -> 1.0

    const rr::FeatureVector f = extractOneV2(reels, user, cand(0), /*contentV2=*/true);
    EXPECT_FLOAT_EQ(f.visualMatch, 1.0f);
    EXPECT_FLOAT_EQ(f.musicMatch, 0.5f);
    EXPECT_NEAR(f.emotionalMatch, 1.0f, 1e-5f);
}

TEST(FeatureExtractorV2, ModalityMatchAntiAlignedIsZero) {
    std::vector<rr::Reel> reels{makeReel(0)};
    reels[0].visualStyleEmbedding = {1.0f, 0.0f};
    rr::User user{};
    user.estimatedVisualPreference = {-1.0f, 0.0f}; // cos -1 -> 0.0
    EXPECT_FLOAT_EQ(extractOneV2(reels, user, cand(0), true).visualMatch, 0.0f);
}

// Empty estimate (before the updater has cold-started this modality) => neutral 0.5.
TEST(FeatureExtractorV2, ModalityMatchNeutralWhenEstimateEmpty) {
    std::vector<rr::Reel> reels{makeReel(0)};
    reels[0].visualStyleEmbedding = {0.0f, 1.0f};
    rr::User user{}; // all estimates empty
    const rr::FeatureVector f = extractOneV2(reels, user, cand(0), /*contentV2=*/true);
    EXPECT_FLOAT_EQ(f.visualMatch, 0.5f);
    EXPECT_FLOAT_EQ(f.musicMatch, 0.5f);
    EXPECT_FLOAT_EQ(f.emotionalMatch, 0.5f);
}

// Defensive: a reel missing a modality embedding (absent under gate-off, should not happen under
// gate-on) yields the neutral 0.5 rather than letting rr::dot throw on a size mismatch.
TEST(FeatureExtractorV2, ModalityMatchNeutralWhenReelModalityAbsent) {
    std::vector<rr::Reel> reels{makeReel(0)}; // no modality embeddings set
    rr::User user{};
    user.estimatedVisualPreference = {0.0f, 1.0f};
    EXPECT_FLOAT_EQ(extractOneV2(reels, user, cand(0), true).visualMatch, 0.5f);
}

// --- Content-value scalar passthroughs (already [0,1] at generation) ----------------------------

TEST(FeatureExtractorV2, ScalarPassthroughs) {
    std::vector<rr::Reel> reels{makeReel(0)};
    reels[0].clickbaitStrength = 0.7f;
    reels[0].emotionalIntensity = 0.6f;
    reels[0].usefulness = 0.55f;
    reels[0].productionQuality = 0.4f;
    reels[0].informationDensity = 0.3f;
    rr::User user{};

    const rr::FeatureVector f = extractOneV2(reels, user, cand(0), /*contentV2=*/true);
    EXPECT_FLOAT_EQ(f.clickbait, 0.7f);
    EXPECT_FLOAT_EQ(f.emotionalIntensity, 0.6f);
    EXPECT_FLOAT_EQ(f.usefulness, 0.55f);
    EXPECT_FLOAT_EQ(f.productionQuality, 0.4f);
    EXPECT_FLOAT_EQ(f.informationDensity, 0.3f);
}

// --- Language match: reference = majority language among the user's recent-window reels ----------

TEST(FeatureExtractorV2, LanguageMatchMajorityInference) {
    // reels 0..3 have languages [1,1,2,5]. Reel 4 (candidate) will be scored against them.
    std::vector<rr::Reel> reels{makeReel(0), makeReel(1), makeReel(2), makeReel(3), makeReel(4)};
    reels[0].language = rr::LanguageId{1};
    reels[1].language = rr::LanguageId{1};
    reels[2].language = rr::LanguageId{2};
    reels[3].language = rr::LanguageId{5};

    rr::User user{};
    user.recentInteractions = {event(0), event(1), event(2), event(3)}; // majority language = 1

    reels[4].language = rr::LanguageId{1};
    EXPECT_FLOAT_EQ(extractOneV2(reels, user, cand(4), true).languageMatch, 1.0f);

    reels[4].language = rr::LanguageId{2}; // not the majority
    EXPECT_FLOAT_EQ(extractOneV2(reels, user, cand(4), true).languageMatch, 0.0f);
}

TEST(FeatureExtractorV2, LanguageMatchTieBreaksToLowerId) {
    // Two reels of language 3 and two of language 2 => tie; the lower id (2) is the reference.
    std::vector<rr::Reel> reels{makeReel(0), makeReel(1), makeReel(2), makeReel(3), makeReel(4)};
    reels[0].language = rr::LanguageId{3};
    reels[1].language = rr::LanguageId{3};
    reels[2].language = rr::LanguageId{2};
    reels[3].language = rr::LanguageId{2};

    rr::User user{};
    user.recentInteractions = {event(0), event(1), event(2), event(3)};

    reels[4].language = rr::LanguageId{2}; // the tie-break winner
    EXPECT_FLOAT_EQ(extractOneV2(reels, user, cand(4), true).languageMatch, 1.0f);
    reels[4].language = rr::LanguageId{3};
    EXPECT_FLOAT_EQ(extractOneV2(reels, user, cand(4), true).languageMatch, 0.0f);
}

TEST(FeatureExtractorV2, LanguageMatchEmptyHistoryIsNeutralOne) {
    std::vector<rr::Reel> reels{makeReel(0)};
    reels[0].language = rr::LanguageId{4};
    rr::User user{}; // no recent interactions
    EXPECT_FLOAT_EQ(extractOneV2(reels, user, cand(0), true).languageMatch, 1.0f);
}

// --- save-popularity placeholder (deferred until a phase adds Reel save counters) ---------------

TEST(FeatureExtractorV2, SavePopularityIsDeferredZeroPlaceholder) {
    std::vector<rr::Reel> reels{makeReel(0), makeReel(1)};
    reels[0].likeCount = 100;
    reels[0].shareCount = 50;
    reels[1].likeCount = 1;
    rr::User user{};
    rr::FeatureExtractor extractor(reels, rr::RankingConfig{}, /*contentV2=*/true);
    std::vector<rr::Candidate> pool{cand(0), cand(1)};
    const std::vector<rr::FeatureVector> f = extractor.extract(user, pool, 0);
    EXPECT_FLOAT_EQ(f[0].savePopularity, 0.0f);
    EXPECT_FLOAT_EQ(f[1].savePopularity, 0.0f);
}

// --- Determinism ---------------------------------------------------------------------------------

TEST(FeatureExtractorV2, V2FeaturesDeterministic) {
    std::vector<rr::Reel> reels{makeReel(0)};
    reels[0].visualStyleEmbedding = {0.6f, 0.8f};
    reels[0].clickbaitStrength = 0.42f;
    reels[0].language = rr::LanguageId{1};
    rr::User user{};
    user.estimatedVisualPreference = {0.8f, 0.6f};
    user.recentInteractions = {event(0)};

    const rr::FeatureVector a = extractOneV2(reels, user, cand(0), true);
    const rr::FeatureVector b = extractOneV2(reels, user, cand(0), true);
    EXPECT_EQ(a.visualMatch, b.visualMatch);
    EXPECT_EQ(a.clickbait, b.clickbait);
    EXPECT_EQ(a.languageMatch, b.languageMatch);
}

// =================================================================================================
// WeightedRanker gated V2 contributions
// =================================================================================================

namespace {

// The eleven always-present V1 contribution keys (frozen).
const std::vector<std::string> kV1Keys = {
    "similarity",     "session_topic",      "quality",           "freshness",
    "popularity",     "trending",           "creator_affinity",  "exploration",
    "duration_match", "repetition_penalty", "impression_penalty"};

// The ten gated V2 contribution keys (emitted only under contentV2).
const std::vector<std::string> kV2Keys = {
    "visual_match",        "music_match",    "emotional_match",    "clickbait",
    "emotional_intensity", "usefulness",     "production_quality", "information_density",
    "language_match",      "save_popularity"};

bool hasKey(const rr::Candidate &c, const std::string &k) {
    return c.featureContributions.find(k) != c.featureContributions.end();
}

} // namespace

// Gate-off: exactly the eleven V1 keys, no V2 key (contributions map byte-identical to pre-P15).
TEST(WeightedRankerV2, GateOffEmitsOnlyElevenV1Keys) {
    std::vector<rr::Reel> reels{makeReel(0)};
    reels[0].clickbaitStrength = 0.8f;
    rr::WeightedRanker ranker(reels, rr::RankingConfig{}, /*contentV2=*/false);
    const std::vector<rr::Candidate> out = ranker.rank(rr::User{}, {cand(0)}, 0);

    EXPECT_EQ(out[0].featureContributions.size(), kV1Keys.size());
    for (const std::string &k : kV1Keys) {
        EXPECT_TRUE(hasKey(out[0], k)) << "missing V1 key " << k;
    }
    for (const std::string &k : kV2Keys) {
        EXPECT_FALSE(hasKey(out[0], k)) << "V2 key leaked under gate-off: " << k;
    }
}

// Gate-on: the eleven V1 keys plus the ten gated V2 keys.
TEST(WeightedRankerV2, GateOnEmitsGatedV2Keys) {
    std::vector<rr::Reel> reels{makeReel(0)};
    rr::WeightedRanker ranker(reels, rr::RankingConfig{}, /*contentV2=*/true);
    const std::vector<rr::Candidate> out = ranker.rank(rr::User{}, {cand(0)}, 0);

    EXPECT_EQ(out[0].featureContributions.size(), kV1Keys.size() + kV2Keys.size());
    for (const std::string &k : kV1Keys) {
        EXPECT_TRUE(hasKey(out[0], k)) << "missing V1 key " << k;
    }
    for (const std::string &k : kV2Keys) {
        EXPECT_TRUE(hasKey(out[0], k)) << "missing V2 key " << k;
    }
}

// A NEGATIVE preset weight (e.g. the satisfaction-proxy arm penalizing clickbait) flows through as
// weight * feature: the contribution is negative and lowers the score.
TEST(WeightedRankerV2, NegativeWeightFlowsThrough) {
    std::vector<rr::Reel> reels{makeReel(0)};
    reels[0].clickbaitStrength = 0.8f;
    rr::RankingConfig config{};
    config.clickbaitWeight = -0.5; // penalize clickbait
    rr::WeightedRanker ranker(reels, config, /*contentV2=*/true);
    const std::vector<rr::Candidate> out = ranker.rank(rr::User{}, {cand(0)}, 0);

    // -0.5 * 0.8 = -0.4
    EXPECT_NEAR(out[0].featureContributions.at("clickbait"), -0.4f, 1e-5f);
}

// A POSITIVE preset weight (the engagement arm leaning on clickbait) raises the score.
TEST(WeightedRankerV2, PositiveWeightRaisesScore) {
    std::vector<rr::Reel> reels{makeReel(0)};
    reels[0].clickbaitStrength = 0.8f;
    rr::RankingConfig config{};
    config.clickbaitWeight = 0.5;
    rr::WeightedRanker ranker(reels, config, /*contentV2=*/true);
    const std::vector<rr::Candidate> out = ranker.rank(rr::User{}, {cand(0)}, 0);
    EXPECT_NEAR(out[0].featureContributions.at("clickbait"), 0.4f, 1e-5f);
}

// The contributions map (now 21 keys under gate-on with V2 weights) still sums to rankingScore to
// float tolerance (the header's summing contract, extended to the V2 keys).
TEST(WeightedRankerV2, ContributionsSumToScoreUnderV2Weights) {
    std::vector<rr::Reel> reels{makeReel(0)};
    reels[0].visualStyleEmbedding = {0.6f, 0.8f};
    reels[0].clickbaitStrength = 0.8f;
    reels[0].usefulness = 0.5f;
    reels[0].language = rr::LanguageId{1};
    rr::RankingConfig config{};
    config.clickbaitWeight = 0.3;
    config.usefulnessWeight = 0.2;
    config.visualMatchWeight = 0.4;
    config.languageMatchWeight = 0.1;
    rr::User user{};
    user.estimatedVisualPreference = {0.6f, 0.8f};

    rr::WeightedRanker ranker(reels, config, /*contentV2=*/true);
    const std::vector<rr::Candidate> out = ranker.rank(user, {cand(0, 0.5f)}, 0);

    float sum = 0.0f;
    for (const auto &[key, value] : out[0].featureContributions) {
        sum += value;
    }
    EXPECT_NEAR(sum, out[0].rankingScore, 1e-4f);
}

// Single-variable arm contract (D17): gate-on with the DEFAULT (all-zero) V2 weights ranks a pool
// in EXACTLY the V1 order — the V2 features add zero-weighted contributions that cannot perturb the
// score. Compared by ranked reel-id sequence.
TEST(WeightedRankerV2, ZeroWeightGateOnMatchesV1RankedOrder) {
    // A small pool with varied V1 signals AND varied V2 attributes, so a bug that let a V2 feature
    // leak into the score would reorder it.
    std::vector<rr::Reel> reels;
    for (uint32_t i = 0; i < 6; ++i) {
        rr::Reel r = makeReel(i);
        r.intrinsicQuality = 0.1f * static_cast<float>(i + 1);
        r.visualStyleEmbedding = {0.0f, 1.0f};
        r.clickbaitStrength = 0.1f * static_cast<float>(6 - i); // anti-correlated with quality
        r.emotionalIntensity = 0.5f;
        r.usefulness = 0.5f;
        r.language = rr::LanguageId{i % 3};
        reels.push_back(r);
    }
    rr::User user{};
    user.estimatedVisualPreference = {0.0f, 1.0f};
    user.recentInteractions = {event(0), event(1)};

    std::vector<rr::Candidate> pool;
    for (uint32_t i = 0; i < 6; ++i) {
        pool.push_back(cand(i, 0.1f * static_cast<float>(i)));
    }

    const rr::RankingConfig config{}; // all V2 weights default 0.0
    const std::vector<rr::Candidate> v1 =
        rr::WeightedRanker(reels, config, /*contentV2=*/false).rank(user, pool, 0);
    const std::vector<rr::Candidate> v2on =
        rr::WeightedRanker(reels, config, /*contentV2=*/true).rank(user, pool, 0);

    ASSERT_EQ(v1.size(), v2on.size());
    for (std::size_t i = 0; i < v1.size(); ++i) {
        EXPECT_EQ(v1[i].reelId.value, v2on[i].reelId.value) << "order diverged at rank " << i;
        EXPECT_FLOAT_EQ(v1[i].rankingScore, v2on[i].rankingScore);
    }
}
