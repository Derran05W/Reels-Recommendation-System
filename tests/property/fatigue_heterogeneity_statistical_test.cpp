// Phase 17 (package A, plan tasks 2-4 + the heterogeneity statistics) — the V2 TDD 4.10 / Tier 2
// acceptance-item-1 / V2 TDD 10-item-4 core proof: IDENTICAL repetition-heavy feeds produce
// MEASURABLY DIFFERENT fatigue / satisfaction / exit outcomes across the four named trait cohorts.
//
// Two families of tests:
//  A. Cohort-sampling stream discipline (augmentUsersV2): empty cohortMix takes ZERO extra
//     users-v2 draws => byte-identical to Phase 13-16 (determinism, >= 24 seeds); a non-empty mix
//     adds exactly the per-user cohort-selection draw; and cohort CHOICE changes trait RANGES but
//     never draw COUNTS (two single-cohort mixes leave the stream at the identical position and
//     differ only by the overridable traits' range mapping of the SAME raw draws).
//  B. Fatigue heterogeneity: single-cohort populations pinned to focused / novelty_seeker /
//     creator_loyal / easily_fatigued (content_v2 + latent_reactions + session_dynamics all on)
//     driven through an IDENTICAL repetition-heavy feed (one reel repeated) via Simulator::stepV2:
//       - focused's satisfaction declines SLOWER than easily_fatigued's and novelty_seeker's;
//       - creator_loyal's same-creator satisfaction penalty is SMALLER than a non-loyal cohort's;
//       - novelty_seeker fatigues faster / exits earlier than focused;
//       - easily_fatigued declines fastest of all (the noveltyTolerance general-fatigue lever).

#include "rr/simulation/simulator.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "rr/core/embedding.hpp"
#include "rr/domain/creator.hpp"
#include "rr/domain/reel.hpp"
#include "rr/domain/user.hpp"
#include "rr/infrastructure/cohort_config.hpp"
#include "rr/infrastructure/config.hpp"
#include "rr/infrastructure/random.hpp"
#include "rr/simulation/dataset_generator.hpp"
#include "rr/simulation/hidden/hidden_user_state.hpp"
#include "rr/simulation/modality_space.hpp"
#include "rr/simulation/user_augmenter_v2.hpp"

using namespace rr;

namespace {

// ============================================================================================
//  Shared fixtures
// ============================================================================================

constexpr uint32_t kNumSeeds = 24; // >= 20 (D17 property-test requirement)

// Reduced-scale generated world for the cohort drives (fast; hundreds of users average out the
// per-user base-affinity noise while the pinned trait effect stays systematic).
SimulationConfig heteroCfg() {
    SimulationConfig c;
    c.users = 250;
    c.reels = 64;
    c.creators = 8;
    c.topics = 6;
    c.dimensions = 16;
    return c;
}

// content_v2 + latent_reactions + session_dynamics on, with a given trait-cohort mix.
RealismConfig gatesOn(std::vector<TraitCohortSpec> mix) {
    RealismConfig r;
    r.contentV2 = true;
    r.latentReactions = true;
    r.sessionDynamics = true;
    r.cohortMix = std::move(mix);
    return r;
}

Simulator makeSim(const SessionDynamicsConfig &sdc, uint64_t seed) {
    return Simulator(BehaviourConfig{}, BehaviourV2Config{}, sdc, RewardConfig{},
                     forkRng(seed, "behaviour"), forkRng(seed, "satisfaction"),
                     forkRng(seed, "session-exit"), forkRng(seed, "external-interruption"),
                     /*recentWindow=*/20, /*trendingHalfLifeSeconds=*/3600.0);
}

// A single-cohort mix that pins a whole population to `spec`.
std::vector<TraitCohortSpec> only(const TraitCohortSpec &spec) { return {spec}; }

// A test-local "creator_disloyal" control: overrides ONLY creatorLoyalty low, everything else
// default — the clean non-loyal counterpart to namedCohort("creator_loyal") for isolating the
// creator-fatigue penalty on a same-creator run.
TraitCohortSpec creatorDisloyal() {
    TraitCohortSpec c;
    c.name = "creator_disloyal";
    c.creatorLoyalty = TraitOverride{0.0, 0.2, true};
    return c;
}

// ============================================================================================
//  Family A — cohort-sampling stream discipline (direct augmentUsersV2 drives)
// ============================================================================================

std::vector<Embedding> makeCentres(uint32_t count, uint32_t dim, uint64_t seed) {
    Rng rng(seed);
    std::vector<Embedding> centres;
    centres.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        Embedding c(dim);
        for (uint32_t d = 0; d < dim; ++d) {
            c[d] = static_cast<float>(rng.gaussian());
        }
        normalize(c);
        centres.push_back(std::move(c));
    }
    return centres;
}

ModalitySpaces makeSpaces(uint32_t dim, uint64_t seed) {
    ModalitySpaces s;
    s.visualCentres = makeCentres(4, dim, seed + 1);
    s.musicCentres = makeCentres(4, dim, seed + 2);
    s.emotionalCentres = makeCentres(4, dim, seed + 3);
    return s;
}

std::vector<HiddenUserState> makeStates(uint32_t n) {
    std::vector<HiddenUserState> v(n);
    for (uint32_t i = 0; i < n; ++i) {
        v[i].userId = UserId{i};
    }
    return v;
}

SimulationConfig augCfg(uint32_t users, uint32_t dim) {
    SimulationConfig c;
    c.users = users;
    c.dimensions = dim;
    return c;
}

void expectSameV2Fields(const HiddenUserState &a, const HiddenUserState &b) {
    EXPECT_EQ(a.visualPreference, b.visualPreference);
    EXPECT_EQ(a.musicPreference, b.musicPreference);
    EXPECT_EQ(a.emotionalPreference, b.emotionalPreference);
    EXPECT_EQ(a.usefulnessPreference, b.usefulnessPreference);
    EXPECT_EQ(a.humourPreference, b.humourPreference);
    EXPECT_EQ(a.controversyTolerance, b.controversyTolerance);
    EXPECT_EQ(a.noveltySeeking, b.noveltySeeking);
    EXPECT_EQ(a.clickbaitSusceptibility, b.clickbaitSusceptibility);
    EXPECT_EQ(a.informationTolerance, b.informationTolerance);
    EXPECT_EQ(a.primaryLanguage.value, b.primaryLanguage.value);
    EXPECT_EQ(a.languageMismatchTolerance, b.languageMismatchTolerance);
    EXPECT_EQ(a.repetitionTolerance, b.repetitionTolerance);
    EXPECT_EQ(a.noveltyTolerance, b.noveltyTolerance);
    EXPECT_EQ(a.creatorLoyalty, b.creatorLoyalty);
    EXPECT_EQ(a.habitStrength, b.habitStrength);
    EXPECT_EQ(a.platformTrust, b.platformTrust);
    EXPECT_EQ(a.baselineDailyUsage, b.baselineDailyUsage);
    EXPECT_EQ(a.preferencePlasticity, b.preferencePlasticity);
}

// Empty cohortMix: the whole generated V2 population is byte-identical across two same-seed runs
// (>= 24 seeds). Combined with the zero-extra-draw structural guarantee (the cohort block is
// guarded by !cohortMix.empty()), this is the D17 "empty mix stays byte-identical to Phase 13-16"
// proof — the default path takes exactly the pre-Phase-17 users-v2 draw sequence.
TEST(FatigueHeterogeneityStatistical, EmptyMixIsDeterministicAcrossSeeds) {
    const uint32_t n = 64;
    const uint32_t dim = 16;
    for (uint64_t seed = 1; seed <= kNumSeeds; ++seed) {
        SCOPED_TRACE("seed=" + std::to_string(seed));
        const ModalitySpaces spaces = makeSpaces(dim, seed);
        const SimulationConfig sc = augCfg(n, dim);
        const RealismConfig realism = gatesOn({}); // empty mix

        std::vector<HiddenUserState> a = makeStates(n);
        std::vector<HiddenUserState> b = makeStates(n);
        Rng ra = forkRng(seed, "users-v2");
        Rng rb = forkRng(seed, "users-v2");
        augmentUsersV2(a, spaces, sc, realism, ra);
        augmentUsersV2(b, spaces, sc, realism, rb);
        for (uint32_t i = 0; i < n; ++i) {
            SCOPED_TRACE("user=" + std::to_string(i));
            expectSameV2Fields(a[i], b[i]);
        }
    }
}

// A NON-EMPTY mix adds exactly the per-user cohort-selection draw, so it perturbs the users-v2
// stream relative to the empty default: a NON-overridable trait (usefulnessPreference) differs,
// proving the empty path takes strictly fewer draws (zero cohort draws) than any non-empty mix.
TEST(FatigueHeterogeneityStatistical, NonEmptyMixPerturbsStreamVsEmpty) {
    const uint32_t n = 32;
    const uint32_t dim = 16;
    const uint64_t seed = 7;
    const ModalitySpaces spaces = makeSpaces(dim, seed);
    const SimulationConfig sc = augCfg(n, dim);

    std::vector<HiddenUserState> empty = makeStates(n);
    std::vector<HiddenUserState> cohort = makeStates(n);
    Rng re = forkRng(seed, "users-v2");
    Rng rc = forkRng(seed, "users-v2");
    augmentUsersV2(empty, spaces, sc, gatesOn({}), re);
    augmentUsersV2(cohort, spaces, sc, gatesOn(only(namedCohort("focused"))), rc);

    // usefulnessPreference is not cohort-overridable, so any difference is purely the stream shift
    // from the cohort draw (the "empty mix takes zero extra draws" invariant, contrapositive).
    size_t differing = 0;
    for (uint32_t i = 0; i < n; ++i) {
        differing += (empty[i].usefulnessPreference != cohort[i].usefulnessPreference) ? 1U : 0U;
    }
    EXPECT_GT(differing, 0U) << "a non-empty mix must add the per-user cohort draw";
}

// Cohort CHOICE changes RANGES, never draw COUNTS: two single-cohort mixes leave the users-v2
// stream at the IDENTICAL position (equal post-augmentation probe draw) and produce byte-identical
// non-overridable fields; only the overridable traits differ, and they map the SAME raw draw
// through different [lo, hi] ranges (proven by reconstructing the raw draw from each range).
TEST(FatigueHeterogeneityStatistical, CohortChoiceChangesRangesNotDrawCounts) {
    const uint32_t n = 40;
    const uint32_t dim = 16;
    const uint64_t seed = 11;
    const ModalitySpaces spaces = makeSpaces(dim, seed);
    const SimulationConfig sc = augCfg(n, dim);

    const TraitCohortSpec focused = namedCohort("focused");
    const TraitCohortSpec easily = namedCohort("easily_fatigued");

    std::vector<HiddenUserState> f = makeStates(n);
    std::vector<HiddenUserState> e = makeStates(n);
    Rng rf = forkRng(seed, "users-v2");
    Rng re = forkRng(seed, "users-v2");
    augmentUsersV2(f, spaces, sc, gatesOn(only(focused)), rf);
    augmentUsersV2(e, spaces, sc, gatesOn(only(easily)), re);

    // Same draw count => the stream ends at the same position => the next draw matches bit-for-bit.
    EXPECT_EQ(rf.uniform01(), re.uniform01()) << "cohort choice must not change the draw count";

    for (uint32_t i = 0; i < n; ++i) {
        SCOPED_TRACE("user=" + std::to_string(i));
        // Non-overridable channels: byte-identical (same raw draws, same default ranges).
        EXPECT_EQ(f[i].visualPreference, e[i].visualPreference);
        EXPECT_EQ(f[i].usefulnessPreference, e[i].usefulnessPreference);
        EXPECT_EQ(f[i].humourPreference, e[i].humourPreference);
        EXPECT_EQ(f[i].habitStrength, e[i].habitStrength);
        // repetitionTolerance: both cohorts override it, mapping the SAME raw draw through their
        // ranges (focused [0.8,1.0], easily_fatigued [0.0,0.2]). Reconstruct the raw and match.
        const double rawF = (static_cast<double>(f[i].repetitionTolerance) - 0.8) / 0.2;
        const double rawE = (static_cast<double>(e[i].repetitionTolerance) - 0.0) / 0.2;
        EXPECT_NEAR(rawF, rawE, 1e-3) << "overridable trait must be the same raw draw, remapped";
        // And the pinned ranges actually bind.
        EXPECT_GE(f[i].repetitionTolerance, 0.8F - 1e-4F);
        EXPECT_LE(e[i].repetitionTolerance, 0.2F + 1e-4F);
    }
}

// A non-empty mix is itself deterministic across two same-seed runs (>= 24 seeds).
TEST(FatigueHeterogeneityStatistical, NonEmptyMixDeterministicAcrossSeeds) {
    const uint32_t n = 48;
    const uint32_t dim = 16;
    const std::vector<TraitCohortSpec> mix = {namedCohort("focused"), namedCohort("novelty_seeker"),
                                              namedCohort("creator_loyal"),
                                              namedCohort("easily_fatigued")};
    for (uint64_t seed = 1; seed <= kNumSeeds; ++seed) {
        SCOPED_TRACE("seed=" + std::to_string(seed));
        const ModalitySpaces spaces = makeSpaces(dim, seed);
        const SimulationConfig sc = augCfg(n, dim);
        std::vector<HiddenUserState> a = makeStates(n);
        std::vector<HiddenUserState> b = makeStates(n);
        Rng ra = forkRng(seed, "users-v2");
        Rng rb = forkRng(seed, "users-v2");
        augmentUsersV2(a, spaces, sc, gatesOn(mix), ra);
        augmentUsersV2(b, spaces, sc, gatesOn(mix), rb);
        for (uint32_t i = 0; i < n; ++i) {
            expectSameV2Fields(a[i], b[i]);
        }
    }
}

// ============================================================================================
//  Family B — fatigue heterogeneity (Simulator::stepV2 over an identical repetition-heavy feed)
// ============================================================================================

struct DeclineStats {
    double meanSat = 0.0;   // mean fatigue-adjusted satisfaction over the whole run
    double earlyMean = 0.0; // mean over the first `window` impressions
    double lateMean = 0.0;  // mean over the last `window` impressions
    double decline = 0.0;   // lateMean - earlyMean (<= 0: satisfaction decays under repetition)
    uint64_t users = 0;
};

// Drive a single reel repeated `impressions` times through every user of a single-cohort
// population, with session EXITS SUPPRESSED (a pure fatigue-accumulation window: no session reset,
// so the fatigue-adjusted satisfaction decays monotonically under repetition). Returns the
// cohort's early/late satisfaction means; the DECLINE (late - early) cancels each user's base
// affinity, isolating the fatigue heterogeneity.
DeclineStats driveDecline(const TraitCohortSpec &cohort, uint64_t seed, size_t reelIndex,
                          uint32_t impressions, uint32_t window) {
    const SimulationConfig sc = heteroCfg();
    GeneratedDataset ds = generateDataset(sc, gatesOn(only(cohort)), seed);

    SessionDynamicsConfig sdc; // exits suppressed: measure pure fatigue accumulation, no reset
    sdc.exitBias = -1000.0;
    sdc.exitFatigueWeight = 0.0;
    sdc.exitRegretWeight = 0.0;
    sdc.exitPoorStreakWeight = 0.0;
    sdc.exitSatisfactionWeight = 0.0;
    sdc.exitInterruptionWeight = 0.0;
    sdc.externalInterruptionHazard = 0.0;
    Simulator sim = makeSim(sdc, seed);

    DeclineStats stats;
    double sumEarly = 0.0;
    double sumLate = 0.0;
    double sumAll = 0.0;
    uint64_t reqId = 0;
    for (size_t u = 0; u < ds.users.size(); ++u) {
        double early = 0.0;
        double late = 0.0;
        double all = 0.0;
        for (uint32_t k = 0; k < impressions; ++k) {
            Reel &reel = ds.reels[reelIndex];
            const Creator &creator = ds.creators[reel.creatorId.value];
            StepV2Inputs v2;
            v2.hiddenReel = &ds.hiddenReelStates[reelIndex];
            v2.positionInFeed = k;
            v2.requestId = ++reqId;
            v2.requestTimestamp = sim.now();
            LatentReaction latent;
            sim.stepV2(ds.users[u], ds.hiddenStates[u], reel, creator, v2, latent);
            const double s = static_cast<double>(latent.immediateSatisfaction);
            all += s;
            if (k < window) {
                early += s;
            }
            if (k >= impressions - window) {
                late += s;
            }
        }
        sumEarly += early / window;
        sumLate += late / window;
        sumAll += all / impressions;
        ++stats.users;
    }
    stats.earlyMean = sumEarly / static_cast<double>(stats.users);
    stats.lateMean = sumLate / static_cast<double>(stats.users);
    stats.meanSat = sumAll / static_cast<double>(stats.users);
    stats.decline = stats.lateMean - stats.earlyMean;
    return stats;
}

struct ExitStats {
    double meanExitsPerUser = 0.0; // total probabilistic exits over the fixed window per user
    uint64_t users = 0;
};

// Drive the same repeated reel with the DEFAULT (shipped) exit model on, for the FULL fixed window
// (sessions exit and restart). Integrating the total exit count over the whole run is far more
// sensitive to sustained-satisfaction differences than first-exit timing (which is swamped by the
// base exit hazard before fatigue accumulates): a cohort that sustains higher satisfaction exits
// LESS OFTEN (longer sessions). Away-decay between consecutive impressions is negligible (half-life
// 3600 s vs a few tens of seconds per impression), so fatigue persists across the restarts.
ExitStats driveExit(const TraitCohortSpec &cohort, uint64_t seed, size_t reelIndex,
                    uint32_t impressions) {
    const SimulationConfig sc = heteroCfg();
    GeneratedDataset ds = generateDataset(sc, gatesOn(only(cohort)), seed);
    Simulator sim = makeSim(SessionDynamicsConfig{}, seed);

    ExitStats stats;
    uint64_t exits = 0;
    uint64_t reqId = 0;
    for (size_t u = 0; u < ds.users.size(); ++u) {
        for (uint32_t k = 0; k < impressions; ++k) {
            Reel &reel = ds.reels[reelIndex];
            const Creator &creator = ds.creators[reel.creatorId.value];
            StepV2Inputs v2;
            v2.hiddenReel = &ds.hiddenReelStates[reelIndex];
            v2.positionInFeed = k;
            v2.requestId = ++reqId;
            v2.requestTimestamp = sim.now();
            LatentReaction latent;
            SessionRecord closed{};
            const StepResult sr =
                sim.stepV2(ds.users[u], ds.hiddenStates[u], reel, creator, v2, latent, &closed);
            exits += sr.event.observedExitAfterImpression ? 1U : 0U;
        }
        ++stats.users;
    }
    stats.meanExitsPerUser = static_cast<double>(exits) / static_cast<double>(stats.users);
    return stats;
}

// A test-local single-trait cohort pinning ONLY noveltyTolerance, for isolating the Phase 17
// general-fatigue lever (repetitionTolerance / noveltySeeking stay on their default ranges, so the
// base utility and repetition drag are identical between two such cohorts — only the general-
// fatigue factor differs).
TraitCohortSpec noveltyTolCohort(double lo, double hi) {
    TraitCohortSpec c;
    c.name = "novelty_tol_probe";
    c.noveltyTolerance = TraitOverride{lo, hi, true};
    return c;
}

// The seed + repeated reel are fixed so the assertions are a deterministic non-flaky snapshot.
constexpr uint64_t kSeed = 20260717;
constexpr size_t kReel = 0;
constexpr uint32_t kImpressions = 24;
constexpr uint32_t kWindow = 5;

// Margins, set well inside the measured gaps at kSeed (satisfaction units are in [-1, 1]; exit
// counts are exits/user over the window). Gaps at kSeed: focused-vs-fast decline ~0.19-0.31 and
// mean ~0.22-0.27; the isolated noveltyTolerance lever ~0.10 on mean; easily-vs-seeker mean ~0.05;
// creator loyal-vs-disloyal mean ~0.08; focused-vs-fast total exits ~1.2-1.7.
constexpr double kDeclineMargin = 0.05;        // focused decline slower than the fast cohorts
constexpr double kFocusedMeanMargin = 0.10;    // focused keeps more satisfaction than fast cohorts
constexpr double kLeverMeanMargin = 0.04;      // isolated noveltyTolerance general-fatigue lever
constexpr double kCohortDistinctMargin = 0.02; // easily_fatigued is the worst-off named cohort
constexpr double kCreatorSatMargin = 0.03;     // loyal keeps more satisfaction on same-creator runs
constexpr double kExitCountMargin = 0.5;       // focused exits less often (longer sessions)

// Tier 2 acceptance item 1 / V2 TDD 10-item-4: repetition affects users differently by hidden
// trait. focused (high repetitionTolerance) declines SLOWER than easily_fatigued and
// novelty_seeker (both low repetitionTolerance) on an identical repetition-heavy feed.
TEST(FatigueHeterogeneityStatistical, FocusedDeclinesSlowerThanFastCohorts) {
    const DeclineStats focused =
        driveDecline(namedCohort("focused"), kSeed, kReel, kImpressions, kWindow);
    const DeclineStats easily =
        driveDecline(namedCohort("easily_fatigued"), kSeed, kReel, kImpressions, kWindow);
    const DeclineStats seeker =
        driveDecline(namedCohort("novelty_seeker"), kSeed, kReel, kImpressions, kWindow);

    // decline is <= 0; focused's is closest to 0 (slowest decay). focused does not saturate the
    // [-1, 1] floor, so its decline is a clean, un-compressed signal here.
    EXPECT_GT(focused.decline, easily.decline + kDeclineMargin)
        << "focused decline=" << focused.decline << " easily_fatigued decline=" << easily.decline;
    EXPECT_GT(focused.decline, seeker.decline + kDeclineMargin)
        << "focused decline=" << focused.decline << " novelty_seeker decline=" << seeker.decline;
    // And focused keeps materially more satisfaction overall (mean is robust to the [-1, 1] floor
    // that compresses the low-satisfaction cohorts' decline).
    EXPECT_GT(focused.meanSat, easily.meanSat + kFocusedMeanMargin)
        << "focused meanSat=" << focused.meanSat << " easily_fatigued meanSat=" << easily.meanSat;
    EXPECT_GT(focused.meanSat, seeker.meanSat + kFocusedMeanMargin)
        << "focused meanSat=" << focused.meanSat << " novelty_seeker meanSat=" << seeker.meanSat;
}

// The Phase 17 noveltyTolerance -> general-fatigue linkage, ISOLATED: two cohorts that differ ONLY
// in noveltyTolerance (repetitionTolerance / noveltySeeking on their default ranges => identical
// base utility and repetition drag) get measurably different satisfaction under the repetitive
// feed — the low-noveltyTolerance cohort (1.5x general-fatigue drag) ends lower than the high-
// noveltyTolerance cohort (0.5x). This proves the wiring directly, with no base-affinity confound.
// It also makes easily_fatigued the worst-off NAMED cohort (its low noveltyTolerance compounds its
// low repetitionTolerance), distinguishing it from novelty_seeker.
TEST(FatigueHeterogeneityStatistical, NoveltyToleranceGeneralFatigueLeverLowersSatisfaction) {
    const DeclineStats highDrag =
        driveDecline(noveltyTolCohort(0.0, 0.2), kSeed, kReel, kImpressions, kWindow);
    const DeclineStats lowDrag =
        driveDecline(noveltyTolCohort(0.8, 1.0), kSeed, kReel, kImpressions, kWindow);
    EXPECT_LT(highDrag.meanSat, lowDrag.meanSat - kLeverMeanMargin)
        << "low-noveltyTolerance meanSat=" << highDrag.meanSat
        << " high-noveltyTolerance meanSat=" << lowDrag.meanSat;

    // The named cohorts are distinguishable: easily_fatigued ends below novelty_seeker.
    const DeclineStats easily =
        driveDecline(namedCohort("easily_fatigued"), kSeed, kReel, kImpressions, kWindow);
    const DeclineStats seeker =
        driveDecline(namedCohort("novelty_seeker"), kSeed, kReel, kImpressions, kWindow);
    EXPECT_LT(easily.meanSat, seeker.meanSat - kCohortDistinctMargin)
        << "easily_fatigued meanSat=" << easily.meanSat
        << " novelty_seeker meanSat=" << seeker.meanSat;
}

// creator_loyal's creator-fatigue satisfaction penalty is SMALLER than a non-loyal cohort's on an
// identical same-creator run (the reel repeats => the SAME creator every impression). The two
// cohorts differ ONLY in creatorLoyalty, so the satisfaction gap isolates the creator-fatigue term.
TEST(FatigueHeterogeneityStatistical, CreatorLoyalOutlastsDisloyalOnSameCreatorRun) {
    const DeclineStats loyal =
        driveDecline(namedCohort("creator_loyal"), kSeed, kReel, kImpressions, kWindow);
    const DeclineStats disloyal =
        driveDecline(creatorDisloyal(), kSeed, kReel, kImpressions, kWindow);
    // The two cohorts differ ONLY in creatorLoyalty, so this gap is purely the creator-fatigue
    // term: loyal keeps materially more satisfaction (smaller penalty) on the same-creator run.
    // (Mean, not decline: the disloyal cohort saturates the [-1, 1] floor, which compresses its
    // decline.)
    EXPECT_GT(loyal.meanSat, disloyal.meanSat + kCreatorSatMargin)
        << "loyal meanSat=" << loyal.meanSat << " disloyal meanSat=" << disloyal.meanSat;
}

// novelty_seeker / easily_fatigued fatigue faster than focused, so under the shipped exit model
// they exit MORE OFTEN (shorter sessions) over the identical repetition-heavy feed — focused's
// sustained satisfaction keeps its sessions alive longer.
TEST(FatigueHeterogeneityStatistical, FastCohortsExitMoreOftenThanFocused) {
    const ExitStats focused = driveExit(namedCohort("focused"), kSeed, kReel, kImpressions);
    const ExitStats seeker = driveExit(namedCohort("novelty_seeker"), kSeed, kReel, kImpressions);
    const ExitStats easily = driveExit(namedCohort("easily_fatigued"), kSeed, kReel, kImpressions);

    EXPECT_LT(focused.meanExitsPerUser, seeker.meanExitsPerUser - kExitCountMargin)
        << "focused exits/user=" << focused.meanExitsPerUser
        << " novelty_seeker=" << seeker.meanExitsPerUser;
    EXPECT_LT(focused.meanExitsPerUser, easily.meanExitsPerUser - kExitCountMargin)
        << "focused exits/user=" << focused.meanExitsPerUser
        << " easily_fatigued=" << easily.meanExitsPerUser;
    // The arm genuinely exercises the exit model (not a censored no-op).
    EXPECT_GT(focused.meanExitsPerUser, 0.0);
}

// The whole heterogeneity pipeline is seed-deterministic (D17 / V2 TDD 7).
TEST(FatigueHeterogeneityStatistical, HeterogeneityDriveIsDeterministic) {
    const DeclineStats a =
        driveDecline(namedCohort("focused"), kSeed, kReel, kImpressions, kWindow);
    const DeclineStats b =
        driveDecline(namedCohort("focused"), kSeed, kReel, kImpressions, kWindow);
    EXPECT_EQ(a.meanSat, b.meanSat);
    EXPECT_EQ(a.decline, b.decline);
    const ExitStats ea = driveExit(namedCohort("novelty_seeker"), kSeed, kReel, kImpressions);
    const ExitStats eb = driveExit(namedCohort("novelty_seeker"), kSeed, kReel, kImpressions);
    EXPECT_EQ(ea.meanExitsPerUser, eb.meanExitsPerUser);
}

} // namespace
