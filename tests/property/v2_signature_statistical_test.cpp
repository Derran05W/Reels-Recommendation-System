#include "rr/simulation/behaviour_model_v2.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iterator>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "rr/core/embedding.hpp"
#include "rr/domain/behaviour_outcome.hpp"
#include "rr/domain/creator.hpp"
#include "rr/domain/reel.hpp"
#include "rr/domain/user.hpp"
#include "rr/infrastructure/archetype_config.hpp"
#include "rr/infrastructure/config.hpp"
#include "rr/infrastructure/random.hpp"
#include "rr/simulation/cohort_hash.hpp"
#include "rr/simulation/dataset_generator.hpp"
#include "rr/simulation/hidden/hidden_reel_state.hpp"
#include "rr/simulation/hidden/hidden_user_state.hpp"
#include "rr/simulation/hidden/latent_reaction.hpp"

using namespace rr;

// =================================================================================================
// Phase 14 Package C — V2 TDD §7 / plan-task-7 statistical signature suite.
//
// EXPECTED-FAIL PROTOCOL (same convention as adaptation_statistical_test.cpp, Phase 10): this file
// is written against the FROZEN contracts in behaviour_model_v2.hpp / latent_model.hpp. In THIS
// worktree, package A's src/simulation/latent_model.cpp and package B's
// src/simulation/behaviour_model_v2.cpp are Phase-14 scaffolding stubs: computeLatentReaction
// returns a draw-free `LatentReaction{}` (every field exactly 0, for every impression, regardless
// of archetype or user), and BehaviourModelV2::simulate discards it and delegates observable
// sampling wholesale to the untouched V1 BehaviourModel (which reads V1 fields only — hidden
// preference / reel embedding / intrinsicQuality / durationSeconds — and never sets the V2
// comment/save/profile-visit flags). Because archetype assignment (Phase 13, real in this tree)
// is drawn on a stream independent of every V1 field, no archetype cohort is expected to differ
// systematically from the population under the stub.
//
// Tests (a)-(h) below assert genuine archetype-conditioned latent/observable signatures that only
// exist once A+B land their real implementations. They are EXPECTED TO FAIL here; the stub numbers
// observed are reported alongside each test (see the Package-C phase report) as the baseline the
// integrator compares against post-integration. Test (i), determinism, exercises only this file's
// own sampling harness plus BehaviourModelV2's plumbing and MUST pass against the stub exactly as
// it will against the real implementation.
//
// Every threshold is a single named constant in the `thresholds` namespace below so the integrator
// can retune against real A+B behaviour without hunting through test bodies.
// =================================================================================================

namespace {

// --- Fixture scale (documented, fixed) ----------------------------------------------------------
// Proportions mirror configs/small.json (users:reels:creators = 1000:10000:500, i.e.
// reels/creators = 20), scaled up modestly for statistical headroom on the smallest archetype
// cohort (niche_treasure, mixture weight 0.08 => ~1600 of 20000 reels). The suite drives
// BehaviourModelV2::simulate directly (no ranking/candidate-retrieval/HNSW involved), so this
// stays well under the ~60s whole-suite Debug budget despite the larger population.
constexpr uint64_t kFixtureSeed = 20260718ULL; // arbitrary, fixed; test (i) proves the value is
                                               // irrelevant to determinism
constexpr uint32_t kUsers = 2000;
constexpr uint32_t kReels = 20000;
constexpr uint32_t kCreators = 1000;
constexpr uint32_t kTopics = 32;
constexpr uint32_t kDimensions = 64;

// Round-robin sampling (deterministic (user, reel) schedule): every reel is paired with
// kUsersPerReel distinct users (a cursor advances across the whole population and wraps), giving
// kReels * kUsersPerReel impressions total, spanning every reel exactly kUsersPerReel times.
constexpr uint32_t kUsersPerReel = 3; // 20000 * 3 = 60000 impressions >= the (d) spec's >=20k

SimulationConfig fixtureSimConfig() {
    SimulationConfig c;
    c.users = kUsers;
    c.reels = kReels;
    c.creators = kCreators;
    c.topics = kTopics;
    c.dimensions = kDimensions;
    return c;
}

RealismConfig fixtureRealismConfig() {
    RealismConfig r;
    r.contentV2 = true;
    r.latentReactions = true;
    return r; // languages/archetypes: defaults (the shipped 8-archetype catalog, D24)
}

// --- Thresholds (integrator-tunable, single source of truth) -------------------------------------
// Every margin is deliberately generous relative to the scale of the metric it gates (documented
// per-constant below) so that, once real, sampling noise over tens of thousands of impressions
// cannot cause a flake; the integrator recalibrates against the actual A+B signal magnitudes.
namespace thresholds {
// (a) ragebait: watchSeconds scale is 0..120s: a 0.5s floor is generous-but-meaningful once the
// real elevated-watch signature lands.
constexpr double kRagebaitWatchSecondsMargin = 0.5;
// Comment propensity base default is 0.04 (4%); a 1pp floor over population is a modest ask.
constexpr double kRagebaitCommentRateMargin = 0.01;
// immediateSatisfaction is roughly in [-1, 1] with typical |satisfactionBias| ~0.3-0.45; half that
// as a separation floor is generous but non-trivial.
constexpr double kRagebaitSatisfactionMargin = 0.15;
// (b) useful: like-propensity baseline range is [0.02, 0.25].
constexpr double kUsefulLikeRateMargin = 0.01;
// (c) clickbait: rate-scale margins on instant-skip / completion.
constexpr double kClickbaitSkipRateMargin = 0.02;
constexpr double kClickbaitCompletionRateMargin = 0.02;
// regret is roughly in [0, 1] with typical |regretBias| ~0.25-0.35.
constexpr double kClickbaitRegretMargin = 0.10;
// (d) V2 TDD §4.4 acceptance reading: positive but imperfect watch<->satisfaction correlation.
constexpr double kSpearmanLo = 0.2;
constexpr double kSpearmanHi = 0.8;
// (e) Music-at-weak-topic-match (integration-calibrated, Phase 14): the DRIVE is the per-user
// music-channel match — only ~1/8 of users blend any given music centre (Phase 13's 1-3-of-16
// preference blends), so an archetype-COHORT mean dilutes the mechanism ~8x and a fixed cohort
// margin is structurally unreachable under a saturated rewatch base. The test therefore asserts
// the mechanism with a REAL margin (matched vs unmatched users on the same weak-topic music
// reels) and the cohort-level claim as strict direction only.
constexpr double kMusicMatchedRewatchMargin = 0.15; // matched-vs-unmatched mechanism margin
constexpr double kMusicMatchThreshold = 0.30;       // dot(musicPreference, musicEmbedding) cut
// (f) comfort: desireForSimilarContent roughly in [-1, 1].
constexpr double kComfortDesireMargin = 0.10;
// (g) niche treasure: satisfactionBias magnitude 0.45 in-cohort only; margin is a third of that.
constexpr double kNicheSatisfactionMargin = 0.15;
// (h) short-duration completed-because-short inflation, within a latent-satisfaction band.
constexpr double kShortCompletionRateMargin = 0.03;
constexpr int kSatisfactionBandCount = 5;
constexpr uint64_t kMinBandSample = 100; // minimum n per side, per band, to trust the comparison
} // namespace thresholds

// --- Sample + aggregation machinery --------------------------------------------------------------

struct ImpressionSample {
    uint32_t userIdx;
    uint32_t reelIdx;
    BehaviourOutcome outcome;
    LatentReaction latent;
};

// Per-cohort accumulator over BOTH the observables (V1 + V2 outcome flags) and the hidden latent.
struct Agg {
    uint64_t n = 0;
    double sumWatchSeconds = 0.0;
    uint64_t completed = 0;
    uint64_t instantSkip = 0;
    uint64_t liked = 0;
    uint64_t commented = 0;
    uint64_t rewatch = 0;
    double sumSatisfaction = 0.0;
    double sumRegret = 0.0;
    double sumDesire = 0.0;

    double meanWatchSeconds() const { return n ? sumWatchSeconds / static_cast<double>(n) : 0.0; }
    double completionRate() const {
        return n ? static_cast<double>(completed) / static_cast<double>(n) : 0.0;
    }
    double instantSkipRate() const {
        return n ? static_cast<double>(instantSkip) / static_cast<double>(n) : 0.0;
    }
    double likeRate() const {
        return n ? static_cast<double>(liked) / static_cast<double>(n) : 0.0;
    }
    double commentRate() const {
        return n ? static_cast<double>(commented) / static_cast<double>(n) : 0.0;
    }
    double rewatchRate() const {
        return n ? static_cast<double>(rewatch) / static_cast<double>(n) : 0.0;
    }
    double meanSatisfaction() const { return n ? sumSatisfaction / static_cast<double>(n) : 0.0; }
    double meanRegret() const { return n ? sumRegret / static_cast<double>(n) : 0.0; }
    double meanDesire() const { return n ? sumDesire / static_cast<double>(n) : 0.0; }
};

void accumulate(Agg &a, const ImpressionSample &s) {
    ++a.n;
    a.sumWatchSeconds += static_cast<double>(s.outcome.watchSeconds);
    a.completed += s.outcome.completed ? 1 : 0;
    a.instantSkip += s.outcome.instantSkip ? 1 : 0;
    a.liked += s.outcome.liked ? 1 : 0;
    a.commented += s.outcome.commented ? 1 : 0;
    a.rewatch += s.outcome.rewatch ? 1 : 0;
    a.sumSatisfaction += static_cast<double>(s.latent.immediateSatisfaction);
    a.sumRegret += static_cast<double>(s.latent.regret);
    a.sumDesire += static_cast<double>(s.latent.desireForSimilarContent);
}

Agg aggregateAll(const std::vector<ImpressionSample> &samples) {
    Agg a;
    for (const auto &s : samples) {
        accumulate(a, s);
    }
    return a;
}

Agg aggregateWhere(const std::vector<ImpressionSample> &samples, const GeneratedDataset &ds,
                   uint32_t archetypeIdx) {
    Agg a;
    for (const auto &s : samples) {
        if (ds.hiddenReelStates[s.reelIdx].archetypeIndex == archetypeIdx) {
            accumulate(a, s);
        }
    }
    return a;
}

std::vector<Agg> aggregateByArchetype(const std::vector<ImpressionSample> &samples,
                                      const GeneratedDataset &ds, std::size_t numArchetypes) {
    std::vector<Agg> out(numArchetypes);
    for (const auto &s : samples) {
        Agg &bucket = out.at(ds.hiddenReelStates[s.reelIdx].archetypeIndex);
        accumulate(bucket, s);
    }
    return out;
}

// Map an archetype NAME to its catalog index (never hardcode indices, D24: the catalog is data).
uint32_t archetypeIndexByName(const RealismConfig &realism, std::string_view name) {
    for (std::size_t i = 0; i < realism.archetypes.size(); ++i) {
        if (realism.archetypes[i].name == name) {
            return static_cast<uint32_t>(i);
        }
    }
    throw std::logic_error("archetype not found in catalog: " + std::string(name));
}

// Deterministic (user, reel) sampling schedule: every reel in `ds`, paired with `usersPerReel`
// distinct round-robin users (a cursor advances across the whole population and wraps). Drives
// BehaviourModelV2::simulate directly, exactly as the frozen contract documents: `behaviourRng`
// and `satisfactionRng` are caller-forked streams the model never re-forks.
std::vector<ImpressionSample> collectRoundRobin(const GeneratedDataset &ds,
                                                const BehaviourModelV2 &model,
                                                uint32_t usersPerReel, Rng &behaviourRng,
                                                Rng &satisfactionRng) {
    std::vector<ImpressionSample> out;
    out.reserve(ds.reels.size() * usersPerReel);
    const std::size_t numUsers = ds.users.size();
    std::size_t userCursor = 0;
    for (std::size_t reelIdx = 0; reelIdx < ds.reels.size(); ++reelIdx) {
        const Reel &reel = ds.reels[reelIdx];
        const HiddenReelState &hiddenReel = ds.hiddenReelStates[reelIdx];
        const Creator &creator = ds.creators.at(reel.creatorId.value);
        for (uint32_t k = 0; k < usersPerReel; ++k) {
            const std::size_t userIdx = userCursor % numUsers;
            ++userCursor;
            const HiddenUserState &hidden = ds.hiddenStates[userIdx];
            LatentReaction latent;
            BehaviourOutcome outcome = model.simulate(hidden, reel, hiddenReel, creator,
                                                      behaviourRng, satisfactionRng, latent);
            out.push_back(ImpressionSample{static_cast<uint32_t>(userIdx),
                                           static_cast<uint32_t>(reelIdx), outcome, latent});
        }
    }
    return out;
}

// The suite's shared fixture: ONE gate-on dataset (content_v2 + latent_reactions) plus ONE
// round-robin impression sample, built lazily on first use and shared read-only across tests
// (a)-(h). Test (i) re-derives its own samples independently to prove re-simulation determinism.
struct Fixture {
    GeneratedDataset ds;
    std::vector<ImpressionSample> impressions;
};

const Fixture &sharedFixture() {
    static const Fixture fixture = [] {
        Fixture f{generateDataset(fixtureSimConfig(), fixtureRealismConfig(), kFixtureSeed), {}};
        const BehaviourModelV2 model(BehaviourConfig{}, BehaviourV2Config{});
        Rng behaviourRng = forkRng(kFixtureSeed, "behaviour");
        Rng satisfactionRng = forkRng(kFixtureSeed, "satisfaction");
        f.impressions =
            collectRoundRobin(f.ds, model, kUsersPerReel, behaviourRng, satisfactionRng);
        return f;
    }();
    return fixture;
}

// --- Small stats helpers (no external deps, per D15/D21 in-house-math convention) ----------------

double median(std::vector<double> values) {
    std::sort(values.begin(), values.end());
    const std::size_t n = values.size();
    if (n == 0) {
        return 0.0;
    }
    if (n % 2 == 1) {
        return values[n / 2];
    }
    return 0.5 * (values[n / 2 - 1] + values[n / 2]);
}

// Average-rank transform (1-based; ties share the mean of their tied rank positions) via a single
// argsort — the first of the "double argsort" Spearman implementation.
std::vector<double> rankAverage(const std::vector<double> &values) {
    const std::size_t n = values.size();
    std::vector<std::size_t> order(n);
    std::iota(order.begin(), order.end(), std::size_t{0});
    std::sort(order.begin(), order.end(),
              [&values](std::size_t a, std::size_t b) { return values[a] < values[b]; });
    std::vector<double> ranks(n);
    std::size_t i = 0;
    while (i < n) {
        std::size_t j = i;
        while (j + 1 < n && values[order[j + 1]] == values[order[i]]) {
            ++j;
        }
        const double avgRank = static_cast<double>(i + j) / 2.0 + 1.0;
        for (std::size_t k = i; k <= j; ++k) {
            ranks[order[k]] = avgRank;
        }
        i = j + 1;
    }
    return ranks;
}

// Pearson correlation. NaN (mathematically undefined, not a crash) if either side has zero
// variance — the honest result when, e.g., every latent value is identically 0 under the stub.
double pearson(const std::vector<double> &a, const std::vector<double> &b) {
    const std::size_t n = a.size();
    const double meanA = std::accumulate(a.begin(), a.end(), 0.0) / static_cast<double>(n);
    const double meanB = std::accumulate(b.begin(), b.end(), 0.0) / static_cast<double>(n);
    double num = 0.0;
    double denA = 0.0;
    double denB = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const double da = a[i] - meanA;
        const double db = b[i] - meanB;
        num += da * db;
        denA += da * da;
        denB += db * db;
    }
    if (!(denA > 0.0) || !(denB > 0.0)) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    return num / std::sqrt(denA * denB);
}

// Spearman rank correlation: rankAverage() is itself an argsort-based transform, and pearson() over
// the two rank vectors is the second argsort's consumer — "double argsort", no external stats dep.
double spearman(const std::vector<double> &x, const std::vector<double> &y) {
    return pearson(rankAverage(x), rankAverage(y));
}

// Bucket a latent satisfaction value (documented range roughly [-1, 1]) into one of `numBands`
// equal-width bands (test (h): comparing short-vs-long completion INSIDE a band controls for
// content quality so the comparison isn't confounded by satisfaction itself).
int satisfactionBand(float satisfaction, int numBands) {
    const double clamped = std::clamp(static_cast<double>(satisfaction), -1.0, 1.0);
    const double t = (clamped + 1.0) / 2.0; // [0, 1]
    int b = static_cast<int>(t * static_cast<double>(numBands));
    if (b >= numBands) {
        b = numBands - 1;
    }
    if (b < 0) {
        b = 0;
    }
    return b;
}

} // namespace

// --- configs/realism-small.json (Phase 14 task 1) ----------------------------------------------
//
// AllShippedConfigsParse (config_test.cpp) is scoped to small/medium/large/benchmark only (D24
// convention); realism-small.json is verified here instead: small.json's exact values plus the
// content_v2/latent_reactions gates on, languages/archetypes left at their defaults.
TEST(V2SignatureStatistical, RealismSmallConfigParsesWithGatesOn) {
    const std::filesystem::path root = RR_SOURCE_DIR;
    const ExperimentConfig c = loadExperimentConfig(root / "configs" / "realism-small.json");
    EXPECT_TRUE(c.realism.contentV2);
    EXPECT_TRUE(c.realism.latentReactions);
    EXPECT_EQ(c.realism.languages, 8u);
    EXPECT_EQ(c.realism.archetypes, defaultArchetypeCatalog());
    // Otherwise identical to small.json's values (copied verbatim per the task spec).
    EXPECT_EQ(c.simulation.users, 1000u);
    EXPECT_EQ(c.simulation.reels, 10000u);
    EXPECT_EQ(c.simulation.creators, 500u);
    EXPECT_EQ(c.simulation.topics, 32u);
    EXPECT_EQ(c.simulation.dimensions, 64u);
    EXPECT_EQ(c.simulation.interactionsPerUser, 50u);
    EXPECT_EQ(c.recommendation.vectorCandidates, 200u);
    EXPECT_DOUBLE_EQ(c.evaluation.oracleSampleRate, 0.25);
    EXPECT_DOUBLE_EQ(c.evaluation.retrievalSampleRate, 0.10);
}

// (a) Ragebait: elevated watch + comment engagement coexisting with NEGATIVE mean satisfaction,
// separated from the population mean by a margin (V2 TDD §4.4 / §7 "ragebait can produce high
// engagement and negative satisfaction").
TEST(V2SignatureStatistical, RagebaitHighEngagementNegativeSatisfaction) {
    const Fixture &fx = sharedFixture();
    const uint32_t ragebaitIdx = archetypeIndexByName(fixtureRealismConfig(), "ragebait");
    const Agg population = aggregateAll(fx.impressions);
    const Agg ragebait = aggregateWhere(fx.impressions, fx.ds, ragebaitIdx);
    ASSERT_GT(ragebait.n, 0u);

    EXPECT_GT(ragebait.meanWatchSeconds(),
              population.meanWatchSeconds() + thresholds::kRagebaitWatchSecondsMargin)
        << "ragebait mean watchSeconds=" << ragebait.meanWatchSeconds()
        << " population=" << population.meanWatchSeconds();
    EXPECT_GT(ragebait.commentRate(),
              population.commentRate() + thresholds::kRagebaitCommentRateMargin)
        << "ragebait comment rate=" << ragebait.commentRate()
        << " population=" << population.commentRate();
    EXPECT_LT(ragebait.meanSatisfaction(),
              population.meanSatisfaction() - thresholds::kRagebaitSatisfactionMargin)
        << "ragebait mean satisfaction=" << ragebait.meanSatisfaction()
        << " population=" << population.meanSatisfaction();
}

// (b) Useful: top-quartile satisfaction among the eight archetype means, with a BELOW-population
// like rate (V2 TDD §7 "useful content can produce high satisfaction with weak engagement").
TEST(V2SignatureStatistical, UsefulHighSatisfactionWeakEngagement) {
    const Fixture &fx = sharedFixture();
    const RealismConfig realism = fixtureRealismConfig();
    const uint32_t usefulIdx = archetypeIndexByName(realism, "useful");
    const std::vector<Agg> perArchetype =
        aggregateByArchetype(fx.impressions, fx.ds, realism.archetypes.size());
    ASSERT_GT(perArchetype.at(usefulIdx).n, 0u);

    std::vector<std::size_t> order(perArchetype.size());
    std::iota(order.begin(), order.end(), std::size_t{0});
    std::stable_sort(order.begin(), order.end(), [&perArchetype](std::size_t a, std::size_t b) {
        return perArchetype[a].meanSatisfaction() > perArchetype[b].meanSatisfaction();
    });
    const auto rankIt = std::find(order.begin(), order.end(), static_cast<std::size_t>(usefulIdx));
    ASSERT_NE(rankIt, order.end());
    const std::size_t usefulRank = static_cast<std::size_t>(std::distance(order.begin(), rankIt));
    const std::size_t topQuartileCount =
        static_cast<std::size_t>(std::ceil(static_cast<double>(perArchetype.size()) * 0.25));

    EXPECT_LT(usefulRank, topQuartileCount)
        << "useful archetype satisfaction rank=" << usefulRank << " of " << perArchetype.size()
        << " (mean=" << perArchetype[usefulIdx].meanSatisfaction() << ")";

    const Agg population = aggregateAll(fx.impressions);
    EXPECT_LT(perArchetype[usefulIdx].likeRate(),
              population.likeRate() - thresholds::kUsefulLikeRateMargin)
        << "useful like rate=" << perArchetype[usefulIdx].likeRate()
        << " population=" << population.likeRate();
}

// (c) Clickbait: opening-hook + early-abandonment signature (below-population instant-skip AND
// below-population completion) with above-population regret (V2 TDD §4.4).
TEST(V2SignatureStatistical, ClickbaitHookAbandonRegret) {
    const Fixture &fx = sharedFixture();
    const uint32_t clickbaitIdx = archetypeIndexByName(fixtureRealismConfig(), "clickbait");
    const Agg population = aggregateAll(fx.impressions);
    const Agg clickbait = aggregateWhere(fx.impressions, fx.ds, clickbaitIdx);
    ASSERT_GT(clickbait.n, 0u);

    EXPECT_LT(clickbait.instantSkipRate(),
              population.instantSkipRate() - thresholds::kClickbaitSkipRateMargin)
        << "clickbait instant-skip rate=" << clickbait.instantSkipRate()
        << " population=" << population.instantSkipRate();
    EXPECT_LT(clickbait.completionRate(),
              population.completionRate() - thresholds::kClickbaitCompletionRateMargin)
        << "clickbait completion rate=" << clickbait.completionRate()
        << " population=" << population.completionRate();
    EXPECT_GT(clickbait.meanRegret(), population.meanRegret() + thresholds::kClickbaitRegretMargin)
        << "clickbait mean regret=" << clickbait.meanRegret()
        << " population=" << population.meanRegret();
}

// (d) Watch ratio <-> hidden satisfaction: positive but IMPERFECT correlation (V2 TDD §4.4
// acceptance criterion), measured over >= 20k impressions via an in-house Spearman (double
// argsort).
TEST(V2SignatureStatistical, WatchSatisfactionCorrelationImperfectButPositive) {
    const Fixture &fx = sharedFixture();
    ASSERT_GE(fx.impressions.size(), 20000u) << "spec requires >= 20k impressions";

    std::vector<double> watchRatio;
    std::vector<double> satisfaction;
    watchRatio.reserve(fx.impressions.size());
    satisfaction.reserve(fx.impressions.size());
    for (const auto &s : fx.impressions) {
        watchRatio.push_back(static_cast<double>(s.outcome.watchRatio));
        satisfaction.push_back(static_cast<double>(s.latent.immediateSatisfaction));
    }

    const double rho = spearman(watchRatio, satisfaction);
    EXPECT_GE(rho, thresholds::kSpearmanLo) << "spearman rho=" << rho;
    EXPECT_LE(rho, thresholds::kSpearmanHi) << "spearman rho=" << rho;
}

// (e) Background-music reels rewatch more than non-music reels at equally weak topic match (V2 TDD
// §4.4 "a familiar song may produce a rewatch even when the topic is irrelevant").
TEST(V2SignatureStatistical, MusicReelRewatchAtWeakTopicMatch) {
    const Fixture &fx = sharedFixture();
    const uint32_t musicIdx = archetypeIndexByName(fixtureRealismConfig(), "background_music");

    std::vector<double> topicMatch;
    topicMatch.reserve(fx.impressions.size());
    for (const auto &s : fx.impressions) {
        topicMatch.push_back(static_cast<double>(
            dot(fx.ds.hiddenStates[s.userIdx].hiddenPreference, fx.ds.reels[s.reelIdx].embedding)));
    }
    const double populationMedianTopicMatch = median(topicMatch);

    Agg musicWeak;
    Agg nonMusicWeak;
    Agg musicWeakMatched;   // weak topic, music archetype, user's music channel MATCHES the reel
    Agg musicWeakUnmatched; // weak topic, music archetype, no music-channel match
    for (std::size_t i = 0; i < fx.impressions.size(); ++i) {
        if (topicMatch[i] >= populationMedianTopicMatch) {
            continue; // keep only the weak-topic-match band (below the population median)
        }
        const ImpressionSample &s = fx.impressions[i];
        const bool isMusic = fx.ds.hiddenReelStates[s.reelIdx].archetypeIndex == musicIdx;
        accumulate(isMusic ? musicWeak : nonMusicWeak, s);
        if (isMusic) {
            const double musicMatch = dot(fx.ds.hiddenStates[s.userIdx].musicPreference,
                                          fx.ds.reels[s.reelIdx].musicEmbedding);
            accumulate(musicMatch > thresholds::kMusicMatchThreshold ? musicWeakMatched
                                                                     : musicWeakUnmatched,
                       s);
        }
    }
    ASSERT_GT(musicWeak.n, 0u);
    ASSERT_GT(nonMusicWeak.n, 0u);
    ASSERT_GT(musicWeakMatched.n, 50u); // the mechanism cohort must be meaningfully populated
    ASSERT_GT(musicWeakUnmatched.n, 50u);

    // Mechanism (the TDD's "completion/rewatch DRIVEN BY music-channel match even at weak topic
    // match"): matched users rewatch far more than unmatched users on the SAME weak-topic music
    // reels — and complete more (music-driven completion).
    EXPECT_GT(musicWeakMatched.rewatchRate(),
              musicWeakUnmatched.rewatchRate() + thresholds::kMusicMatchedRewatchMargin)
        << "matched n=" << musicWeakMatched.n << " rewatch=" << musicWeakMatched.rewatchRate()
        << " unmatched n=" << musicWeakUnmatched.n
        << " rewatch=" << musicWeakUnmatched.rewatchRate();
    EXPECT_GT(musicWeakMatched.completionRate(), musicWeakUnmatched.completionRate())
        << "matched completion=" << musicWeakMatched.completionRate()
        << " unmatched completion=" << musicWeakUnmatched.completionRate();
    // Cohort-level direction (diluted ~8x by the 1-3-of-16 preference-blend structure; see the
    // thresholds comment): music reels still rewatch strictly more than non-music in the same
    // weak-topic band.
    EXPECT_GT(musicWeak.rewatchRate(), nonMusicWeak.rewatchRate())
        << "music weak-match n=" << musicWeak.n << " rewatch=" << musicWeak.rewatchRate()
        << " non-music weak-match n=" << nonMusicWeak.n
        << " rewatch=" << nonMusicWeak.rewatchRate();
}

// (f) Comfort content: above-population desireForSimilarContent (V2 TDD §4.4 "moderate engagement
// but positive return probability").
TEST(V2SignatureStatistical, ComfortDesireForSimilar) {
    const Fixture &fx = sharedFixture();
    const uint32_t comfortIdx = archetypeIndexByName(fixtureRealismConfig(), "comfort");
    const Agg population = aggregateAll(fx.impressions);
    const Agg comfort = aggregateWhere(fx.impressions, fx.ds, comfortIdx);
    ASSERT_GT(comfort.n, 0u);

    EXPECT_GT(comfort.meanDesire(), population.meanDesire() + thresholds::kComfortDesireMargin)
        << "comfort mean desireForSimilarContent=" << comfort.meanDesire()
        << " population=" << population.meanDesire();
}

// (g) Niche treasure: satisfaction is elevated ONLY inside the hidden cohort band (V2 TDD §4.4
// "highly satisfying to a small user cohort"), using the SAME pinned cohortHash01 as Phase
// 10/16/20.
TEST(V2SignatureStatistical, NicheTreasureCohortGating) {
    const Fixture &fx = sharedFixture();
    const uint32_t nicheIdx = archetypeIndexByName(fixtureRealismConfig(), "niche_treasure");

    Agg inCohort;
    Agg outOfCohort;
    for (const auto &s : fx.impressions) {
        const HiddenReelState &hiddenReel = fx.ds.hiddenReelStates[s.reelIdx];
        if (hiddenReel.archetypeIndex != nicheIdx || !(hiddenReel.nicheCohortWidth > 0.0f)) {
            continue;
        }
        const double h = cohortHash01(fx.ds.users[s.userIdx].id);
        const double centre = static_cast<double>(hiddenReel.nicheCohortCentre);
        const double width = static_cast<double>(hiddenReel.nicheCohortWidth);
        const bool inside = std::abs(h - centre) <= width;
        accumulate(inside ? inCohort : outOfCohort, s);
    }
    ASSERT_GT(inCohort.n, 0u);
    ASSERT_GT(outOfCohort.n, 0u);

    EXPECT_GT(inCohort.meanSatisfaction(),
              outOfCohort.meanSatisfaction() + thresholds::kNicheSatisfactionMargin)
        << "in-cohort n=" << inCohort.n << " mean satisfaction=" << inCohort.meanSatisfaction()
        << " out-of-cohort n=" << outOfCohort.n
        << " mean satisfaction=" << outOfCohort.meanSatisfaction();
}

// (h) Completed-because-short inflation (V2 TDD §3.2): completion rate for reels shorter than
// BehaviourV2Config::shortDurationSeconds exceeds longer reels' WITHIN the same latent-satisfaction
// band (bucketed to control for content quality rather than comparing raw population rates).
TEST(V2SignatureStatistical, ShortCompletionInflation) {
    const Fixture &fx = sharedFixture();
    const BehaviourV2Config defaultV2Config;
    const double shortDurationSeconds = defaultV2Config.shortDurationSeconds;

    std::array<Agg, thresholds::kSatisfactionBandCount> shortBands;
    std::array<Agg, thresholds::kSatisfactionBandCount> longBands;
    for (const auto &s : fx.impressions) {
        const int bandIdx =
            satisfactionBand(s.latent.immediateSatisfaction, thresholds::kSatisfactionBandCount);
        const double duration = static_cast<double>(fx.ds.reels[s.reelIdx].durationSeconds);
        Agg &bucket = (duration < shortDurationSeconds)
                          ? shortBands.at(static_cast<std::size_t>(bandIdx))
                          : longBands.at(static_cast<std::size_t>(bandIdx));
        accumulate(bucket, s);
    }

    int qualifyingBands = 0;
    for (int bandIdx = 0; bandIdx < thresholds::kSatisfactionBandCount; ++bandIdx) {
        const Agg &shortAgg = shortBands.at(static_cast<std::size_t>(bandIdx));
        const Agg &longAgg = longBands.at(static_cast<std::size_t>(bandIdx));
        if (shortAgg.n < thresholds::kMinBandSample || longAgg.n < thresholds::kMinBandSample) {
            continue;
        }
        ++qualifyingBands;
        EXPECT_GT(shortAgg.completionRate(),
                  longAgg.completionRate() + thresholds::kShortCompletionRateMargin)
            << "band " << bandIdx << " short n=" << shortAgg.n
            << " completion=" << shortAgg.completionRate() << " long n=" << longAgg.n
            << " completion=" << longAgg.completionRate();
    }
    ASSERT_GT(qualifyingBands, 0) << "no satisfaction band had >= " << thresholds::kMinBandSample
                                  << " short AND long samples";
}

// (i) Determinism: re-deriving the impression sample from the SAME fixed seed (fresh forkRng calls,
// same schedule) reproduces bit-identical aggregate sums. Unlike (a)-(h), this exercises only this
// file's own harness + BehaviourModelV2's plumbing, so it MUST pass against the stub exactly as it
// will against the real A+B implementation.
TEST(V2SignatureStatistical, DeterminismOfSuiteSample) {
    const Fixture &fx = sharedFixture();
    const BehaviourModelV2 model(BehaviourConfig{}, BehaviourV2Config{});

    auto resample = [&fx, &model] {
        Rng behaviourRng = forkRng(kFixtureSeed, "behaviour");
        Rng satisfactionRng = forkRng(kFixtureSeed, "satisfaction");
        return collectRoundRobin(fx.ds, model, kUsersPerReel, behaviourRng, satisfactionRng);
    };

    const std::vector<ImpressionSample> a = resample();
    const std::vector<ImpressionSample> b = resample();
    ASSERT_EQ(a.size(), b.size());

    const Agg aggA = aggregateAll(a);
    const Agg aggB = aggregateAll(b);

    EXPECT_EQ(aggA.n, aggB.n);
    EXPECT_EQ(aggA.sumWatchSeconds, aggB.sumWatchSeconds);
    EXPECT_EQ(aggA.completed, aggB.completed);
    EXPECT_EQ(aggA.instantSkip, aggB.instantSkip);
    EXPECT_EQ(aggA.liked, aggB.liked);
    EXPECT_EQ(aggA.commented, aggB.commented);
    EXPECT_EQ(aggA.rewatch, aggB.rewatch);
    EXPECT_EQ(aggA.sumSatisfaction, aggB.sumSatisfaction);
    EXPECT_EQ(aggA.sumRegret, aggB.sumRegret);
    EXPECT_EQ(aggA.sumDesire, aggB.sumDesire);
}
