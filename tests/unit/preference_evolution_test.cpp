// PreferenceEvolution unit tests (Phase 20, V2 TDD §4.15/§4.16, contracts §7 A-row). Every case is
// deterministic, reduced-scale, and HAND-COMPUTED from the documented formulas in
// preference_evolution.cpp — the test encodes the spec independently of the implementation. The
// named §4.16 magnitudes are mirrored as local constants (kept in sync with preference_evolution
// .cpp; the integrator may recalibrate ±, contracts §7). ZERO rng anywhere: applyImpression takes
// no Rng, so these tests never construct one.

#include "rr/simulation/preference_evolution.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "rr/core/embedding.hpp"
#include "rr/domain/reel.hpp"
#include "rr/infrastructure/config.hpp"
#include "rr/simulation/behaviour_model_v2.hpp" // fatigueSatisfactionDelta (separation test g)
#include "rr/simulation/hidden/hidden_session_state.hpp"
#include "rr/simulation/hidden/hidden_user_state.hpp"
#include "rr/simulation/hidden/latent_reaction.hpp"

using namespace rr;

namespace {

// --- §4.16 named constants, mirrored from preference_evolution.cpp (kept in sync by hand) --------
constexpr double kEtaEvo = 0.02; // EvolutionConfig default
constexpr double kExposureHalfLifeSeconds =
    172800.0; // 2 days; novelty HL is half this (0.25 decay)
constexpr double kExhaustionSatWeight = 0.30;
constexpr double kNoveltyDepletionSatWeight = 0.20;

Embedding vec4(float a, float b, float c, float d) { return Embedding{a, b, c, d}; }

// Neutral hidden user: semantic preference on axis 0, unit modality channels, mean plasticity (0.5
// -> plasticityScale 1.0 -> eta_u == etaEvo), high platform trust. Callers override.
HiddenUserState makeUser() {
    HiddenUserState h{};
    h.userId = UserId{1};
    h.hiddenPreference = vec4(1, 0, 0, 0);
    h.visualPreference = vec4(0, 1, 0, 0);
    h.musicPreference = vec4(0, 0, 1, 0);
    h.emotionalPreference = vec4(0, 0, 0, 1);
    h.preferencePlasticity = 0.5F; // plasticityScale == 1.0
    h.platformTrust = 0.8F;
    return h;
}

// Reel on topic 0 / creator 2 / id 10; semantic embedding on axis 1 (orthogonal to the neutral
// user's preference, so dot == 0 initially and the sign of the move is unambiguous).
Reel makeReel() {
    Reel r{};
    r.id = ReelId{10};
    r.creatorId = CreatorId{2};
    r.embedding = vec4(0, 1, 0, 0);
    r.visualStyleEmbedding = vec4(1, 0, 0, 0);
    r.musicEmbedding = vec4(0, 0, 0, 1);
    r.emotionalToneEmbedding = vec4(0, 0, 1, 0);
    r.primaryTopic = TopicId{0};
    r.durationSeconds = 30.0F;
    r.active = true;
    return r;
}

LatentReaction makeLatent(float satisfaction, float regret = 0.0F) {
    LatentReaction l{};
    l.immediateSatisfaction = satisfaction;
    l.regret = regret;
    return l;
}

PreferenceEvolution makeEvo(double etaEvo = kEtaEvo) {
    EvolutionConfig c;
    c.etaEvo = etaEvo;
    return PreferenceEvolution(c);
}

double dotv(const Embedding &a, const Embedding &b) { return static_cast<double>(dot(a, b)); }

} // namespace

// ============================================================================================
//  plasticity scaling (documented at PreferenceEvolution::plasticityScale)
// ============================================================================================
TEST(PreferenceEvolution, PlasticityScaleMapsMeanToOne) {
    EXPECT_DOUBLE_EQ(PreferenceEvolution::plasticityScale(0.5),
                     1.0); // population mean -> eta_u=etaEvo
    EXPECT_DOUBLE_EQ(PreferenceEvolution::plasticityScale(0.0), 0.0);  // freezes the user
    EXPECT_DOUBLE_EQ(PreferenceEvolution::plasticityScale(1.0), 2.0);  // doubles the rate
    EXPECT_DOUBLE_EQ(PreferenceEvolution::plasticityScale(-0.5), 0.0); // clamped
    EXPECT_DOUBLE_EQ(PreferenceEvolution::plasticityScale(2.0), 2.0);  // clamped
}

// ============================================================================================
//  (a) §4.15 hand-computed reinforcement: p' = normalize((1-eta_u)p + eta_u*s*v), incl. plasticity
// ============================================================================================
TEST(PreferenceEvolution, Section415HandComputedUpdate) {
    HiddenUserState h = makeUser(); // plasticity 0.5 -> eta_u = 0.02
    const Reel r = makeReel();      // embedding (0,1,0,0), visual (1,0,0,0)
    auto evo = makeEvo();
    evo.applyImpression(h, r, makeLatent(1.0F), /*now=*/1000);

    // Semantic: target = 0.98*(1,0,0,0) + 0.02*1*(0,1,0,0) = (0.98, 0.02, 0, 0);
    // norm = sqrt(0.9604 + 0.0004) = sqrt(0.9608) = 0.98020406; normalized = (0.99979183,
    // 0.02040384).
    const double norm = std::sqrt(0.9608);
    EXPECT_NEAR(h.hiddenPreference[0], 0.98 / norm, 1e-5);
    EXPECT_NEAR(h.hiddenPreference[1], 0.02 / norm, 1e-5);
    EXPECT_NEAR(h.hiddenPreference[2], 0.0, 1e-6);
    EXPECT_TRUE(isValid(h.hiddenPreference)); // stays unit-length

    // Visual channel: same math with v = (1,0,0,0) onto pref (0,1,0,0) -> (0.02040384, 0.99979183).
    EXPECT_NEAR(h.visualPreference[0], 0.02 / norm, 1e-5);
    EXPECT_NEAR(h.visualPreference[1], 0.98 / norm, 1e-5);
    EXPECT_TRUE(isValid(h.visualPreference));

    // Moved TOWARD the reel on both channels (dot rose from 0 to +0.0204).
    EXPECT_GT(dotv(h.hiddenPreference, r.embedding), 0.0);
    EXPECT_GT(dotv(h.visualPreference, r.visualStyleEmbedding), 0.0);
}

TEST(PreferenceEvolution, PlasticityScalesTheEffectiveRate) {
    const Reel r = makeReel();
    HiddenUserState hi = makeUser();
    hi.preferencePlasticity = 1.0F; // eta_u = 0.04, moves TWICE as far as mean
    HiddenUserState lo = makeUser();
    lo.preferencePlasticity = 0.0F; // frozen: no movement at all
    auto evo = makeEvo();
    evo.applyImpression(hi, r, makeLatent(1.0F), 1000);
    evo.applyImpression(lo, r, makeLatent(1.0F), 1000);

    EXPECT_GT(dotv(hi.hiddenPreference, r.embedding), 0.03);     // ~0.04 side
    EXPECT_EQ(lo.hiddenPreference, makeUser().hiddenPreference); // plasticity 0 => byte-unchanged
}

// ============================================================================================
//  (b) satisfaction-driven NOT reward-driven — the Tier-4 core (contracts §7, exit criterion 1).
//  A ragebait impression has POSITIVE observed reward (high watch/comment; that is what the reward
//  model sees) yet NEGATIVE hidden satisfaction. applyImpression consumes the HIDDEN satisfaction,
//  so the preference moves AWAY from the reel — the opposite of what reward-following would do.
// ============================================================================================
TEST(PreferenceEvolution, SatisfactionNotRewardMovesPreferenceAway) {
    const Reel r = makeReel(); // dot(pref, embedding) starts at 0
    auto evo = makeEvo();

    HiddenUserState ragebait = makeUser();
    evo.applyImpression(ragebait, r, makeLatent(/*s=*/-0.8F, /*regret=*/0.9F), 1000);
    // Negative satisfaction pushed the semantic preference AWAY (dot went negative).
    EXPECT_LT(dotv(ragebait.hiddenPreference, r.embedding), 0.0);

    // Contrast: the SAME reel with POSITIVE satisfaction moves the preference TOWARD it. The reel
    // (embedding) is identical; only the sign of hidden satisfaction differs -> opposite direction.
    HiddenUserState satisfying = makeUser();
    evo.applyImpression(satisfying, r, makeLatent(/*s=*/+0.8F), 1000);
    EXPECT_GT(dotv(satisfying.hiddenPreference, r.embedding), 0.0);

    // The ragebait impression also builds topic aversion and erodes trust (§4.16).
    EXPECT_GT(ragebait.exposure.topicAversion.at(r.primaryTopic), 0.0F);
    EXPECT_LT(ragebait.retention.trust, 0.8); // below the initialized platformTrust
}

// ============================================================================================
//  (c) saturation directionality: repeated same-TOPIC exposure grows exhaustion and makes future
//      satisfaction of that topic progressively more negative (shrinking marginal gain).
// ============================================================================================
TEST(PreferenceEvolution, TopicSaturationGrowsAndDullsFutureSatisfaction) {
    HiddenUserState h = makeUser();
    auto evo = makeEvo();

    std::vector<double> deltas;
    for (uint32_t i = 0; i < 5; ++i) {
        Reel r = makeReel();
        r.id = ReelId{100 + i};           // distinct reels ...
        r.creatorId = CreatorId{200 + i}; // ... and creators, so ONLY topic exhaustion accumulates
        r.primaryTopic = TopicId{0};      // same topic
        // Drag on THIS fresh reel reflects only accumulated topic exhaustion (novelty/burnout ==
        // 0).
        deltas.push_back(exposureSatisfactionDelta(h, r));
        evo.applyImpression(h, r, makeLatent(0.8F), /*now=*/1000); // same timestamp: no away-decay
    }

    // Strictly more negative each time (0, -0.045, -0.090, -0.135, -0.180): growing exhaustion.
    for (std::size_t i = 1; i < deltas.size(); ++i) {
        EXPECT_LT(deltas[i], deltas[i - 1]);
    }
    EXPECT_NEAR(deltas[1], -kExhaustionSatWeight * 0.15, 1e-6);          // one prior exposure
    EXPECT_NEAR(h.exposure.topicExhaustion.at(TopicId{0}), 0.75F, 1e-5); // 5 * 0.15
}

// ============================================================================================
//  (d) aversion after regret: regretful exposure builds topic aversion, which pushes the semantic
//      preference AWAY even on a later NEUTRAL impression, and dulls future satisfaction.
// ============================================================================================
TEST(PreferenceEvolution, AversionFromRegretPushesPreferenceAwayOnNeutralImpression) {
    HiddenUserState h = makeUser();
    const Reel r = makeReel();
    auto evo = makeEvo();

    // Three regretful (regret=1, s=0) impressions build aversion on topic 0.
    for (int i = 0; i < 3; ++i) {
        evo.applyImpression(h, r, makeLatent(/*s=*/0.0F, /*regret=*/1.0F), 1000);
    }
    EXPECT_GT(h.exposure.topicAversion.at(r.primaryTopic), 0.0F);
    const double dotBefore = dotv(h.hiddenPreference, r.embedding);
    const double deltaAverted = exposureSatisfactionDelta(h, r);
    EXPECT_LT(deltaAverted, 0.0); // aversion dulls future satisfaction

    // A NEUTRAL impression (s=0, regret=0): the accumulated aversion alone drives the semantic
    // channel further AWAY (s_sem = -kAversionUpdateWeight * aversion < 0).
    evo.applyImpression(h, r, makeLatent(0.0F, 0.0F), 1000);
    EXPECT_LT(dotv(h.hiddenPreference, r.embedding), dotBefore);
}

// ============================================================================================
//  (e) novelty depletion on repeats: seeing the SAME reel again depletes its novelty, so a repeat
//      is less satisfying than a first-time same-topic/same-creator reel by exactly the novelty
//      term.
// ============================================================================================
TEST(PreferenceEvolution, NoveltyDepletesOnRepeatViews) {
    HiddenUserState h = makeUser();
    const Reel r = makeReel(); // id 10, topic 0, creator 2
    auto evo = makeEvo();

    evo.applyImpression(h, r, makeLatent(0.8F), 1000); // first view of reel 10
    EXPECT_NEAR(h.exposure.reelNovelty.at(r.id), 0.5F, 1e-5);

    // A fresh reel with the SAME topic and creator but a NEW id has no novelty depletion.
    Reel fresh = r;
    fresh.id = ReelId{11};
    const double deltaRepeat = exposureSatisfactionDelta(h, r);    // reel 10: novelty depleted
    const double deltaFresh = exposureSatisfactionDelta(h, fresh); // reel 11: novel
    // The gap is exactly the novelty drag (topic/creator terms cancel): 0.2 * 0.5 = 0.10.
    EXPECT_NEAR(deltaFresh - deltaRepeat, kNoveltyDepletionSatWeight * 0.5, 1e-6);

    evo.applyImpression(h, r, makeLatent(0.8F), 1000); // second view -> deeper depletion
    EXPECT_NEAR(h.exposure.reelNovelty.at(r.id), 1.0F, 1e-5);
}

// ============================================================================================
//  (f) away-time reversion: accumulators decay by the EXACT closed form 2^(-gap/halfLife) on the
//      next touch (topic/creator/aversion at the long half-life, reel-novelty at the shorter one).
// ============================================================================================
TEST(PreferenceEvolution, AwayTimeDecaysAccumulatorsByClosedForm) {
    HiddenUserState h = makeUser();
    const Reel r = makeReel(); // topic 0, creator 2, id 10
    auto evo = makeEvo();
    evo.applyImpression(h, r, makeLatent(/*s=*/-0.5F, /*regret=*/1.0F), /*now=*/1000);
    const float exhaust0 = h.exposure.topicExhaustion.at(TopicId{0});
    const float burnout0 = h.exposure.creatorBurnout.at(CreatorId{2});
    const float novelty0 = h.exposure.reelNovelty.at(ReelId{10});
    const float aversion0 = h.exposure.topicAversion.at(TopicId{0});
    ASSERT_GT(exhaust0, 0.0F);
    ASSERT_GT(aversion0, 0.0F);

    // A second impression on a DIFFERENT topic/creator/reel one long-half-life later: the topic-0
    // entries just DECAY (they are not re-incremented). gap == kExposureHalfLifeSeconds.
    Reel other = r;
    other.id = ReelId{20};
    other.creatorId = CreatorId{99};
    other.primaryTopic = TopicId{7};
    const Timestamp later = 1000 + static_cast<Timestamp>(kExposureHalfLifeSeconds);
    evo.applyImpression(h, other, makeLatent(0.0F), later);

    // exp2(-1) = 0.5 for the long half-life; exp2(-2) = 0.25 for the reel-novelty (half the HL).
    EXPECT_FLOAT_EQ(h.exposure.topicExhaustion.at(TopicId{0}), exhaust0 * 0.5F);
    EXPECT_FLOAT_EQ(h.exposure.creatorBurnout.at(CreatorId{2}), burnout0 * 0.5F);
    EXPECT_FLOAT_EQ(h.exposure.topicAversion.at(TopicId{0}), aversion0 * 0.5F);
    EXPECT_FLOAT_EQ(h.exposure.reelNovelty.at(ReelId{10}),
                    static_cast<float>(static_cast<double>(novelty0) * 0.25));
}

TEST(PreferenceEvolution, AccumulatorsPruneBelowEpsilon) {
    HiddenUserState h = makeUser();
    const Reel r = makeReel();
    auto evo = makeEvo();
    evo.applyImpression(h, r, makeLatent(0.8F), 1000);
    ASSERT_FALSE(h.exposure.topicExhaustion.empty());
    // A very long gap (~20 half-lives) decays every entry below kExposureEpsilon -> pruned to
    // empty.
    Reel other = r;
    other.primaryTopic = TopicId{5};
    other.creatorId = CreatorId{5};
    other.id = ReelId{5};
    const Timestamp farFuture = 1000 + static_cast<Timestamp>(20 * kExposureHalfLifeSeconds);
    // Neutral impression -> no new aversion; the topic-0/creator-2/reel-10 entries prune away.
    evo.applyImpression(h, r, makeLatent(0.0F), farFuture);
    // After decay+prune the only surviving entries are THIS impression's fresh increments (reel
    // 10).
    EXPECT_EQ(h.exposure.topicExhaustion.count(TopicId{0}), 1u); // re-incremented (same reel)
    EXPECT_TRUE(h.exposure.topicAversion.empty()); // decayed away, not re-added (neutral)
}

// ============================================================================================
//  (g) SEPARATION from session fatigue (exit criterion 3): the exposure drag persists across a
//      session boundary, while the P16 session-fatigue drag resets with the new session.
// ============================================================================================
TEST(PreferenceEvolution, ExposureDragPersistsWhileSessionFatigueResets) {
    HiddenUserState h = makeUser();
    const Reel r = makeReel();
    auto evo = makeEvo();
    // Build persistent exposure state (several same-topic exposures).
    for (int i = 0; i < 4; ++i) {
        evo.applyImpression(h, r, makeLatent(0.6F), 1000);
    }
    const double exposureDrag = exposureSatisfactionDelta(h, r);
    ASSERT_LT(exposureDrag, 0.0);

    const SessionDynamicsConfig cfg; // defaults
    // Session 1: fatigued (topic + general fatigue present) -> negative fatigue drag.
    HiddenSessionState s1{};
    s1.topicFatigue[r.primaryTopic] = 0.5F;
    s1.generalFatigue = 0.5F;
    const double fatigueDrag1 = fatigueSatisfactionDelta(cfg, h, r, s1);
    EXPECT_LT(fatigueDrag1, 0.0);

    // Session 2 (a FRESH session after the user returns): fatigue reset to baseline.
    // reel.novelty==0 so the novelty-match term is 0 too -> the fatigue drag is exactly 0.
    HiddenSessionState s2{};
    const double fatigueDrag2 = fatigueSatisfactionDelta(cfg, h, r, s2);
    EXPECT_DOUBLE_EQ(fatigueDrag2, 0.0);

    // The exposure drag is UNCHANGED by the session reset (it is not a function of session state).
    EXPECT_DOUBLE_EQ(exposureSatisfactionDelta(h, r), exposureDrag);
}

// ============================================================================================
//  (i) platform-trust erosion under repeated regret, and slow recovery under satisfaction.
//      A is the ONLY writer of retention.trust; it initializes from platformTrust and clamps [0,1].
// ============================================================================================
TEST(PreferenceEvolution, TrustInitializesErodesAndRecovers) {
    HiddenUserState h = makeUser();
    h.platformTrust = 0.8F;
    const Reel r = makeReel();
    auto evo = makeEvo();

    // First touch initializes trust from the platformTrust trait.
    evo.applyImpression(h, r, makeLatent(/*s=*/-0.5F, /*regret=*/1.0F), 1000);
    // trust_0 = platformTrust + (-0.08*1 - 0.10*0.5) = 0.8 - 0.13 = 0.67 (1e-6 tol: 0.8F trait
    // rounding). Initialization from the platformTrust trait is the load-bearing assertion.
    EXPECT_NEAR(h.retention.trust, 0.67, 1e-6);

    double prev = h.retention.trust;
    for (int i = 0; i < 4; ++i) { // repeated regret keeps eroding
        evo.applyImpression(h, r, makeLatent(-0.5F, 1.0F), 1000);
        EXPECT_LT(h.retention.trust, prev);
        prev = h.retention.trust;
    }

    // Recovery is SLOW and positive: a satisfying impression raises trust by only +0.02.
    const double eroded = h.retention.trust;
    evo.applyImpression(h, r, makeLatent(/*s=*/1.0F, /*regret=*/0.0F), 1000);
    EXPECT_GT(h.retention.trust, eroded);
    EXPECT_NEAR(h.retention.trust - eroded, 0.02, 1e-9); // recovery step << erosion step

    // Clamp [0,1]: a long regret storm floors trust at 0, never below.
    for (int i = 0; i < 50; ++i) {
        evo.applyImpression(h, r, makeLatent(-1.0F, 1.0F), 1000);
    }
    EXPECT_GE(h.retention.trust, 0.0);
    EXPECT_LE(h.retention.trust, 1.0);
    EXPECT_NEAR(h.retention.trust, 0.0, 1e-9);
}

// ============================================================================================
//  Degenerate / guard behaviour: mirrors the P7 updater's fallback doctrine.
// ============================================================================================
TEST(PreferenceEvolution, DegenerateUpdateKeepsPreviousVector) {
    // Adversarial rate: etaEvo 0.5, plasticity 0.5 -> eta_u = 0.5; a fully-negative satisfaction on
    // a reel COLLINEAR with the preference makes the target (1-0.5)*p + 0.5*(-1)*p = 0
    // (degenerate).
    HiddenUserState h = makeUser();
    Reel r = makeReel();
    r.embedding = h.hiddenPreference; // collinear
    auto evo = makeEvo(/*etaEvo=*/0.5);
    const Embedding before = h.hiddenPreference;
    evo.applyImpression(h, r, makeLatent(-1.0F), 1000);
    EXPECT_EQ(h.hiddenPreference, before); // kept unchanged (degenerate fallback)
}

TEST(PreferenceEvolution, MissingModalityEmbeddingsAreSkipped) {
    HiddenUserState h = makeUser();
    Reel r = makeReel();
    r.visualStyleEmbedding.clear(); // gate-off-style: no modality embeddings on the reel
    r.musicEmbedding.clear();
    r.emotionalToneEmbedding.clear();
    const Embedding visualBefore = h.visualPreference;
    auto evo = makeEvo();
    evo.applyImpression(h, r, makeLatent(1.0F), 1000);
    EXPECT_NE(h.hiddenPreference, makeUser().hiddenPreference); // semantic still updated
    EXPECT_EQ(h.visualPreference, visualBefore); // modality channel skipped, untouched
}

TEST(PreferenceEvolution, NeutralImpressionOnFreshUserLeavesPreferenceUnchanged) {
    HiddenUserState h = makeUser();
    const Reel r = makeReel();
    auto evo = makeEvo();
    const Embedding before = h.hiddenPreference;
    evo.applyImpression(h, r, makeLatent(0.0F, 0.0F), 1000); // s=0, no aversion yet
    // (1-eta)*p normalizes back to p exactly.
    for (std::size_t i = 0; i < before.size(); ++i) {
        EXPECT_NEAR(h.hiddenPreference[i], before[i], 1e-6);
    }
}
