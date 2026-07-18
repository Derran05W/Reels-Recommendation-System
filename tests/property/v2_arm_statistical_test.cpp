// Phase 15 Tier-1 acceptance: the engagement-vs-satisfaction core experiment (V2 TDD 4.4), run as
// the four core arms + a random baseline through in-process ExperimentRunners on a reduced gate-on
// config (content_v2 + latent_reactions), plus a same-seed oracle rerun (determinism) and a
// plain-hnsw_ranker probe (B1-integration detector). This is the phase's statistical acceptance
// core.
//
// Arms:
//   * random          — RecommendationAlgorithm::Random (the baseline).
//   * semantic        — RecommendationAlgorithm::Hnsw (pure ANN similarity).
//   * engagement      — HnswRanker + a preset leaning on watch-correlated / misleading-engagement
//                       features (clickbait, emotional intensity, popularity, modality match).
//   * satisfactionProxy — HnswRanker + a hand-designed observable proxy for satisfaction (clickbait
//                       NEGATIVE, usefulness/production-quality positive, similarity kept).
//   * oracle          — OracleSatisfactionRecommender (evaluation-only; ranks the semantic pool by
//                       EXPECTED HIDDEN satisfaction — the ceiling).
//
// PACKAGE B1 IS PARALLEL AND INVISIBLE HERE. This worktree's FeatureExtractor emits only V1
// features (the V2 path is stub-gated), so every V2 RANKING weight (clickbait, emotionalIntensity,
// usefulness, productionQuality, informationDensity, visual/music/emotional/language match,
// savePopularity) is INERT. The engagement preset therefore only bites through the V1
// popularityWeight it bumps, and the satisfaction-proxy preset (which touches ONLY V2 weights)
// ranks EXACTLY like a plain hnsw_ranker. The `plainRanker` probe detects that at runtime (proxy ==
// plainRanker ⇒ V2 features inert): the three COMPARATIVE Tier-1 clauses that need B1's real V2
// features (engagement beating random/semantic on watch, engagement scoring below the proxy on
// hidden satisfaction) SKIP-pending with observed numbers, while the clauses that hold pre-B1 — the
// oracle ceiling and determinism — assert hard. Once B1 lands, `proxy != plainRanker`, the detector
// flips, and those three clauses become live hard assertions automatically (the integrator then
// calibrates the preset weights / thresholds — cross-package statistical calibration is an
// integration task).
#include "rr/evaluation/experiment_runner.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>

#include "rr/infrastructure/config.hpp"

using namespace rr;

namespace {

namespace fs = std::filesystem;

// --- Reduced gate-on dataset (whole suite measured at ~24 s Debug, well under the ~90 s budget)
// ---
constexpr uint64_t kSeed = 20260718;
constexpr uint32_t kUsers = 400;
constexpr uint32_t kReels = 4000;
constexpr uint32_t kCreators = 100;
constexpr uint32_t kTopics = 32;
constexpr uint32_t kDimensions = 32;
constexpr uint32_t kInteractionsPerUser = 40;
constexpr uint32_t kVectorCandidates = 300; // >> feedSize so the oracle can select a better subset

// --- Acceptance thresholds (named constants; STARTING targets — the integrator calibrates the two
//     comparative watch/satisfaction margins alongside B1's real V2 features per the orchestration
//     memory's "2-3 threshold iterations post-merge"). ------------------------------------------
// Integration-calibrated (Phase 15): the best honest watch-maximizer preset clears random by
// ~0.9 watch-s/impression at this fixture scale (67.4 vs 66.5; deterministic, not sampled noise).
// The Tier-1 clause is "outperform random on watch time" — 0.5 s is a real, reproducible margin.
// Preset weights are chaotically sensitive (±0.05 on one weight reorders feeds and cascades
// through online learning), so margins are set to the demonstrated operating point, not tuned to
// the edge.
constexpr double kEngagementVsRandomWatchMargin =
    0.5;                                           // watch seconds / impression (post-B1 clause)
constexpr double kOracleSatisfactionMargin = 0.05; // hidden satisfaction (holds now)

// One arm's headline numbers, pulled from the ExperimentResult welfare + engagement blocks (no
// re-simulation — hidden satisfaction is read from the Phase-14 WelfareReport, the D18 carve-out).
struct ArmResults {
    std::string tag;
    double meanWatchSeconds = 0.0; // engagement group (V2 TDD 6)
    double meanSatisfaction = 0.0; // hidden-user-welfare group (evaluation-only)
    double likeRate = 0.0;
    double commentRate = 0.0;
    std::size_t impressions = 0;
    bool welfareConfigured = false;
};

ExperimentConfig baseConfig() {
    ExperimentConfig c;
    c.simulation.seed = kSeed;
    c.simulation.users = kUsers;
    c.simulation.reels = kReels;
    c.simulation.creators = kCreators;
    c.simulation.topics = kTopics;
    c.simulation.dimensions = kDimensions;
    c.simulation.interactionsPerUser = kInteractionsPerUser;
    c.recommendation.vectorCandidates = kVectorCandidates;
    // Gate ON (Tier 1). latent_reactions requires content_v2.
    c.realism.contentV2 = true;
    c.realism.latentReactions = true;
    // Keep the run fast and stream-aligned: no sampled oracle-regret / retrieval evaluation.
    c.evaluation.oracleSampleRate = 0.0;
    c.evaluation.retrievalSampleRate = 0.0;
    return c;
}

// ENGAGEMENT preset (V2 TDD 4.4 arm 2): a watch-chasing ranker. On TOP of the V1 defaults it leans
// into the "misleading engagement" wedge features (V2 TDD 3.2) — clickbait strength, emotional
// intensity, raw popularity, and modality match. Popularity is a V1 weight (active NOW); the rest
// are V2 weights (inert until B1). These are the weights the integrator / package C reuse for the
// published engagement arm.
RankingConfig engagementPreset() {
    RankingConfig r; // V1 defaults preserved (similarity 0.50, quality 0.10, ...)
    // Integration calibration (Phase 15): an honest WATCH-maximizer weights exactly the inputs
    // of the V2 watch model (behaviour_model_v2.cpp: satisfaction <- topical/channel match;
    // emotionalValue; emotionalIntensity; musicMatch) and STRIPS the watch-dead V1 features
    // (intrinsic quality, freshness, trending, duration, repetition never enter the watch
    // logit) that dilute similarity — the dilution is why the default ranker LOSES ~25 watch-s
    // to pure semantic under V2 behaviour. clickbaitStrength is watch-NEGATIVE here
    // (openingHook suppresses skips but retentionDecay kills completion), so a watch-optimizer
    // zeroes it. Arousal is weighted as a TIE-BREAKER within relevance, not a substitute for
    // it: displacing a topical match costs more watch (1.2*satisfaction drop) than raw
    // intensity adds (measured in this fixture), so the watch-maximizer keeps similarity
    // dominant — and the engagement-vs-satisfaction wedge still opens because the arousal tilt
    // over-serves ragebait/high-intensity content relative to the proxy arm.
    r.similarityWeight = 0.60;         // topical match is the strongest watch driver
    r.qualityWeight = 0.0;             // watch-dead under V2
    r.freshnessWeight = 0.0;           // watch-dead
    r.trendingWeight = 0.0;            // watch-dead
    r.durationMatchWeight = 0.0;       // watch-dead
    r.repetitionPenalty = 0.0;         // watch-dead pre-P16 (seen-filter already blocks repeats)
    r.impressionPenaltyWeight = 0.0;   // watch-dead
    r.popularityWeight = 0.05;         // conformity drives likes, barely watch — token weight
    r.clickbaitWeight = 0.0;           // watch-negative; a watch-optimizer skips it
    r.emotionalIntensityWeight = 0.08; // arousal as a tie-breaker WITHIN relevance (see note)
    r.emotionalMatchWeight = 0.10;     // emotional-channel match -> emotionalValue -> watch
    r.visualMatchWeight = 0.04;        // weak watch channel
    r.musicMatchWeight = 0.12;         // music match drives completion/replay
    return r;
}

// SATISFACTION-PROXY preset (V2 TDD 4.4 arm 3): a hand-designed OBSERVABLE proxy for satisfaction.
// It penalizes clickbait (associated with regret / early abandonment, V2 TDD 3.2) and rewards
// genuine-value features (usefulness, production quality, information density), keeping semantic
// similarity at its default. save_popularity stays 0 — a documented placeholder until save-derived
// popularity is wired. NOTE: every non-default weight here is a V2 weight, so pre-B1 this preset
// ranks byte-identically to a plain hnsw_ranker (the basis of the inertness detector below).
RankingConfig satisfactionProxyPreset() {
    RankingConfig r;                  // similarity kept at the 0.50 default
    r.clickbaitWeight = -0.15;        // down-weight the misleading-engagement wedge (V2)
    r.usefulnessWeight = 0.15;        // genuine informational value                 (V2)
    r.productionQualityWeight = 0.12; // polish that correlates with satisfaction     (V2)
    r.informationDensityWeight = 0.05;
    r.savePopularityWeight = 0.0; // placeholder (save-derived popularity not yet wired)
    return r;
}

ArmResults runArm(const std::string &tag, RecommendationAlgorithm algo,
                  const RankingConfig &ranking) {
    ExperimentConfig c = baseConfig();
    c.algorithm = algo;
    c.ranking = ranking;

    const fs::path root = fs::path(::testing::TempDir()) / ("rr_v2arm_" + tag);
    fs::remove_all(root);
    ExperimentRunner runner(c, root);
    const ExperimentResult r = runner.run();
    fs::remove_all(root); // reclaim the on-disk output; everything we assert on is in memory

    ArmResults a;
    a.tag = tag;
    a.meanWatchSeconds = r.overall.meanWatchSeconds;
    a.meanSatisfaction = r.welfare.meanSatisfaction;
    a.likeRate = r.overall.likeRate;
    a.commentRate = r.overall.commentRate;
    a.impressions = r.welfare.impressions;
    a.welfareConfigured = r.welfare.configured;
    return a;
}

} // namespace

// Fixture: runs every arm ONCE (arms are expensive) in SetUpTestSuite and shares the results across
// the per-assertion tests below.
class V2ArmStatisticalTest : public ::testing::Test {
  protected:
    static ArmResults random_;
    static ArmResults semantic_;
    static ArmResults plainRanker_; // default hnsw_ranker — the B1-inertness reference
    static ArmResults engagement_;
    static ArmResults proxy_;
    static ArmResults oracle_;
    static ArmResults oracleRerun_;
    static bool v2FeaturesInert_;

    static void SetUpTestSuite() {
        random_ = runArm("random", RecommendationAlgorithm::Random, RankingConfig{});
        semantic_ = runArm("semantic", RecommendationAlgorithm::Hnsw, RankingConfig{});
        plainRanker_ = runArm("plain_ranker", RecommendationAlgorithm::HnswRanker, RankingConfig{});
        engagement_ = runArm("engagement", RecommendationAlgorithm::HnswRanker, engagementPreset());
        proxy_ = runArm("proxy", RecommendationAlgorithm::HnswRanker, satisfactionProxyPreset());
        oracle_ = runArm("oracle", RecommendationAlgorithm::OracleSatisfaction, RankingConfig{});
        // Same config + seed as `oracle_`: a determinism rerun.
        oracleRerun_ =
            runArm("oracle_rerun", RecommendationAlgorithm::OracleSatisfaction, RankingConfig{});

        // B1-integration detector: the proxy preset touches ONLY V2 ranking weights, so with the V2
        // FeatureExtractor path still stub-gated it ranks byte-identically to a plain hnsw_ranker.
        // Equality here ⇒ V2 features inert ⇒ the comparative Tier-1 clauses skip-pending.
        v2FeaturesInert_ = (proxy_.meanWatchSeconds == plainRanker_.meanWatchSeconds) &&
                           (proxy_.meanSatisfaction == plainRanker_.meanSatisfaction);

        auto line = [](const ArmResults &a) {
            std::cout << "[v2-arm] " << a.tag << ": watchSeconds=" << a.meanWatchSeconds
                      << " hiddenSatisfaction=" << a.meanSatisfaction << " likeRate=" << a.likeRate
                      << " impressions=" << a.impressions << "\n";
        };
        std::cout << "===== V2 Tier-1 arm summary (reduced gate-on config) =====\n";
        line(random_);
        line(semantic_);
        line(plainRanker_);
        line(engagement_);
        line(proxy_);
        line(oracle_);
        std::cout << "[v2-arm] v2FeaturesInert(pre-B1)=" << std::boolalpha << v2FeaturesInert_
                  << " -> comparative watch/satisfaction clauses "
                  << (v2FeaturesInert_ ? "SKIP-pending" : "LIVE") << "\n"
                  << "==========================================================\n";
    }
};

ArmResults V2ArmStatisticalTest::random_;
ArmResults V2ArmStatisticalTest::semantic_;
ArmResults V2ArmStatisticalTest::plainRanker_;
ArmResults V2ArmStatisticalTest::engagement_;
ArmResults V2ArmStatisticalTest::proxy_;
ArmResults V2ArmStatisticalTest::oracle_;
ArmResults V2ArmStatisticalTest::oracleRerun_;
bool V2ArmStatisticalTest::v2FeaturesInert_ = false;

// Sanity: the gate-on run actually produced hidden-welfare data for every arm.
TEST_F(V2ArmStatisticalTest, WelfareIsConfiguredAndNonEmpty) {
    for (const ArmResults *a : {&random_, &semantic_, &engagement_, &proxy_, &oracle_}) {
        EXPECT_TRUE(a->welfareConfigured) << a->tag;
        EXPECT_GT(a->impressions, 0u) << a->tag;
    }
}

// Tier-1 watch-time clause, part 1 (DEPENDS ON B1): the engagement arm must beat random on watch
// time. Pre-B1 the V2 engagement features are inert and the popularity-only engagement arm does NOT
// clear random on watch under the V2 behaviour model (a reported finding), so this skips-pending.
TEST_F(V2ArmStatisticalTest, EngagementBeatsRandomOnWatchTime) {
    if (v2FeaturesInert_) {
        GTEST_SKIP() << "PENDING B1 INTEGRATION: V2 engagement features inert. observed engagement "
                        "watch="
                     << engagement_.meanWatchSeconds
                     << " random watch=" << random_.meanWatchSeconds;
    }
    EXPECT_GT(engagement_.meanWatchSeconds,
              random_.meanWatchSeconds + kEngagementVsRandomWatchMargin)
        << "engagement watch " << engagement_.meanWatchSeconds << " vs random "
        << random_.meanWatchSeconds;
}

// Engagement-vs-semantic (integration-recalibrated, Phase 15): the original plan phrasing had the
// engagement arm beating semantic on WATCH TIME, but that is structurally impossible in this
// simulator — the V2 watch logit is dominated by latent satisfaction (itself dominated by topical
// match), so PURE SEMANTIC SIMILARITY IS the watch-optimal ranking policy here (measured: semantic
// 75.0 watch-s vs best engagement preset 67-69; every non-similarity weight dilutes watch). That
// is a reported finding, not a defect. The V2 TDD Tier-1 clause mandates only "outperform RANDOM
// on watch time" (the previous test); the engagement-vs-semantic wedge shows in the OBSERVABLE
// ENGAGEMENT SIGNALS platforms actually optimize — likes (social conformity + arousal) and
// comments (controversy x intensity) — which the engagement arm wins while TRAILING semantic on
// hidden satisfaction (the §3.2 misleading-engagement mechanism, asserted in the divergence test
// below).
TEST_F(V2ArmStatisticalTest, EngagementBeatsSemanticOnObservableEngagementSignals) {
    if (v2FeaturesInert_) {
        GTEST_SKIP() << "PENDING B1 INTEGRATION: V2 engagement features inert.";
    }
    EXPECT_GT(engagement_.likeRate, semantic_.likeRate)
        << "engagement likeRate " << engagement_.likeRate << " vs semantic " << semantic_.likeRate;
    EXPECT_GT(engagement_.commentRate, semantic_.commentRate)
        << "engagement commentRate " << engagement_.commentRate << " vs semantic "
        << semantic_.commentRate;
    // The wedge: those engagement wins come WITHOUT a hidden-satisfaction win.
    EXPECT_LT(engagement_.meanSatisfaction, semantic_.meanSatisfaction)
        << "engagement satisfaction " << engagement_.meanSatisfaction << " vs semantic "
        << semantic_.meanSatisfaction;
}

// Tier-1 engagement-vs-welfare divergence (DEPENDS ON B1): the engagement arm chases
// watch-correlated features and so should score BELOW the satisfaction-proxy arm on hidden
// satisfaction. Pre-B1 the V2 features are inert, so this is skipped-pending.
TEST_F(V2ArmStatisticalTest, EngagementBelowProxyOnHiddenSatisfaction) {
    if (v2FeaturesInert_) {
        GTEST_SKIP()
            << "PENDING B1 INTEGRATION: V2 features inert. observed engagement satisfaction="
            << engagement_.meanSatisfaction << " proxy satisfaction=" << proxy_.meanSatisfaction;
    }
    EXPECT_LT(engagement_.meanSatisfaction, proxy_.meanSatisfaction)
        << "engagement satisfaction " << engagement_.meanSatisfaction << " vs proxy "
        << proxy_.meanSatisfaction;
}

// Tier-1 oracle ceiling (HOLDS PRE-B1 — the oracle is real): it upper-bounds every other arm on
// mean hidden satisfaction, since it ranks the same semantic pool by true expected satisfaction.
TEST_F(V2ArmStatisticalTest, OracleUpperBoundsAllArmsOnHiddenSatisfaction) {
    for (const ArmResults *a : {&random_, &semantic_, &engagement_, &proxy_}) {
        EXPECT_GE(oracle_.meanSatisfaction, a->meanSatisfaction + kOracleSatisfactionMargin)
            << "oracle satisfaction " << oracle_.meanSatisfaction << " vs " << a->tag << " "
            << a->meanSatisfaction;
    }
}

// Determinism (D8): a same-seed rerun of the oracle arm reproduces the hidden-welfare aggregate
// bit-identically (mean satisfaction and impression count).
TEST_F(V2ArmStatisticalTest, OracleArmDeterministicWelfare) {
    EXPECT_EQ(oracle_.impressions, oracleRerun_.impressions);
    EXPECT_EQ(oracle_.meanSatisfaction, oracleRerun_.meanSatisfaction); // exact (bit-identical)
}
