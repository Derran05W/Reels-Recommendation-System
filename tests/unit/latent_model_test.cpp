#include "rr/simulation/latent_model.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "rr/core/embedding.hpp"
#include "rr/domain/creator.hpp"
#include "rr/domain/ids.hpp"
#include "rr/domain/reel.hpp"
#include "rr/infrastructure/config.hpp"
#include "rr/infrastructure/random.hpp"
#include "rr/simulation/cohort_hash.hpp"
#include "rr/simulation/hidden/hidden_reel_state.hpp"
#include "rr/simulation/hidden/hidden_user_state.hpp"

using namespace rr;

namespace {

constexpr int kDim = 4;
const LanguageId kLangA{1};
const LanguageId kLangB{2};

// A unit vector along axis k (already L2-normalized).
Embedding axis(int k) {
    Embedding e(kDim, 0.0f);
    e[k] = 1.0f;
    return e;
}

// A pair of unit vectors whose dot product is exactly d (a on axis 0; b in the 0-1 plane).
std::pair<Embedding, Embedding> pairWithDot(double d) {
    Embedding a = axis(0);
    Embedding b(kDim, 0.0f);
    b[0] = static_cast<float>(d);
    b[1] = static_cast<float>(std::sqrt(1.0 - d * d));
    return {a, b};
}

// A fully neutral user: every channel empty, every scalar preference/susceptibility 0, primary
// language kLangA. latentBaseUtility of a neutral user against a neutral reel is exactly 0.
HiddenUserState neutralUser(UserId id = UserId{0}) {
    HiddenUserState u;
    u.userId = id;
    u.primaryLanguage = kLangA;
    return u; // all V2 scalar fields default to 0.0f, embeddings default empty
}

// A fully neutral reel: every embedding empty, every scalar 0, language kLangA (matches
// neutralUser so no language penalty).
Reel neutralReel() {
    Reel r{};
    r.language = kLangA;
    return r;
}

HiddenReelState neutralHiddenReel() { return HiddenReelState{}; }

Creator neutralCreator() {
    Creator c;
    c.id = CreatorId{0};
    return c; // styleEmbedding empty => creator-attachment term is 0
}

} // namespace

// --- Base-utility channel isolation -----------------------------------------------------------

TEST(LatentModelBaseUtility, TopicChannelAloneMovesUtilityByTopicWeightTimesDot) {
    BehaviourV2Config cfg;
    HiddenUserState u = neutralUser();
    Reel r = neutralReel();
    auto [a, b] = pairWithDot(0.5);
    u.hiddenPreference = a;
    r.embedding = b;
    EXPECT_NEAR(latentBaseUtility(cfg, u, r, neutralCreator()), cfg.topicWeight * 0.5, 1e-5);
}

TEST(LatentModelBaseUtility, VisualChannelAloneMovesUtilityByVisualWeightTimesDot) {
    BehaviourV2Config cfg;
    HiddenUserState u = neutralUser();
    Reel r = neutralReel();
    auto [a, b] = pairWithDot(0.5);
    u.visualPreference = a;
    r.visualStyleEmbedding = b;
    EXPECT_NEAR(latentBaseUtility(cfg, u, r, neutralCreator()), cfg.visualWeight * 0.5, 1e-5);
}

TEST(LatentModelBaseUtility, MusicChannelAloneMovesUtilityByMusicWeightTimesDot) {
    BehaviourV2Config cfg;
    HiddenUserState u = neutralUser();
    Reel r = neutralReel();
    auto [a, b] = pairWithDot(0.5);
    u.musicPreference = a;
    r.musicEmbedding = b;
    EXPECT_NEAR(latentBaseUtility(cfg, u, r, neutralCreator()), cfg.musicWeight * 0.5, 1e-5);
}

TEST(LatentModelBaseUtility, EmotionalChannelAloneMovesUtilityByEmotionalWeightTimesDot) {
    BehaviourV2Config cfg;
    HiddenUserState u = neutralUser();
    Reel r = neutralReel();
    auto [a, b] = pairWithDot(0.5);
    u.emotionalPreference = a;
    r.emotionalToneEmbedding = b;
    EXPECT_NEAR(latentBaseUtility(cfg, u, r, neutralCreator()), cfg.emotionalWeight * 0.5, 1e-5);
}

TEST(LatentModelBaseUtility, UsefulnessTermIsWeightTimesUsefulnessTimesPreference) {
    BehaviourV2Config cfg;
    HiddenUserState u = neutralUser();
    u.usefulnessPreference = 0.8f;
    Reel r = neutralReel();
    r.usefulness = 0.5f;
    EXPECT_NEAR(latentBaseUtility(cfg, u, r, neutralCreator()), cfg.usefulnessWeight * 0.5 * 0.8,
                1e-5);
}

TEST(LatentModelBaseUtility, HumourAndNoveltyTermsAreWeightTimesAttributeTimesPreference) {
    BehaviourV2Config cfg;
    HiddenUserState u = neutralUser();
    u.humourPreference = 0.6f;
    u.noveltySeeking = 0.4f;
    Reel r = neutralReel();
    r.humour = 0.5f;
    r.novelty = 0.25f;
    EXPECT_NEAR(latentBaseUtility(cfg, u, r, neutralCreator()),
                cfg.humourWeight * 0.5 * 0.6 + cfg.noveltyWeight * 0.25 * 0.4, 1e-5);
}

TEST(LatentModelBaseUtility, CreatorAttachmentIsWeightTimesHiddenPreferenceDotStyle) {
    BehaviourV2Config cfg;
    HiddenUserState u = neutralUser();
    Reel r = neutralReel(); // embedding empty => no topic term, isolates the creator term
    Creator c = neutralCreator();
    auto [a, b] = pairWithDot(0.5);
    u.hiddenPreference = a;
    c.styleEmbedding = b;
    EXPECT_NEAR(latentBaseUtility(cfg, u, r, c), cfg.creatorAttachmentWeight * 0.5, 1e-5);
}

// --- Controversy hinge: penalty only beyond tolerance, boost only for high-tolerance users -----

TEST(LatentModelBaseUtility, ControversyPenaltyFiresOnlyBeyondTolerance) {
    BehaviourV2Config cfg;
    Reel r = neutralReel();
    r.controversy = 0.8f;

    // Below tolerance: controversy < tolerance => no penalty, no boost (tolerance not "high").
    HiddenUserState within = neutralUser();
    within.controversyTolerance = 0.9f; // 0.8 < 0.9, and 0.9 > high threshold => only a boost
    // boost = controversyBoostWeight * controversy; no penalty.
    EXPECT_NEAR(latentBaseUtility(cfg, within, r, neutralCreator()),
                cfg.controversyBoostWeight * 0.8, 1e-5);

    // Beyond tolerance, low-tolerance user: penalty fires, no boost.
    HiddenUserState beyond = neutralUser();
    beyond.controversyTolerance = 0.2f; // 0.8 > 0.2 => penalty; 0.2 not high => no boost
    EXPECT_NEAR(latentBaseUtility(cfg, beyond, r, neutralCreator()),
                -cfg.controversyPenaltyWeight * (0.8 - 0.2), 1e-5);
}

TEST(LatentModelBaseUtility, ControversyBoostOnlyForHighToleranceUsers) {
    BehaviourV2Config cfg;
    Reel r = neutralReel();
    r.controversy = 0.5f;

    // High tolerance (> threshold) and controversy below it: pure boost.
    HiddenUserState high = neutralUser();
    high.controversyTolerance = 0.8f;
    EXPECT_NEAR(latentBaseUtility(cfg, high, r, neutralCreator()), cfg.controversyBoostWeight * 0.5,
                1e-5);

    // Mid tolerance (below the high threshold) and controversy == tolerance: no penalty, no boost.
    HiddenUserState mid = neutralUser();
    mid.controversyTolerance = 0.5f;
    EXPECT_NEAR(latentBaseUtility(cfg, mid, r, neutralCreator()), 0.0, 1e-5);
}

// --- Information-overload hinge ----------------------------------------------------------------

TEST(LatentModelBaseUtility, InformationOverloadPenaltyFiresOnlyBeyondTolerance) {
    BehaviourV2Config cfg;
    Reel r = neutralReel();
    r.informationDensity = 0.7f;

    HiddenUserState tolerant = neutralUser();
    tolerant.informationTolerance = 0.9f; // density < tolerance => no penalty
    EXPECT_NEAR(latentBaseUtility(cfg, tolerant, r, neutralCreator()), 0.0, 1e-5);

    HiddenUserState overwhelmed = neutralUser();
    overwhelmed.informationTolerance = 0.3f; // density > tolerance => penalty
    EXPECT_NEAR(latentBaseUtility(cfg, overwhelmed, r, neutralCreator()),
                -cfg.informationDensityWeight * (0.7 - 0.3), 1e-5);
}

// --- Language mismatch penalty -----------------------------------------------------------------

TEST(LatentModelBaseUtility, LanguagePenaltyOnlyOnMismatchAndRelievedByTolerance) {
    BehaviourV2Config cfg;
    Reel r = neutralReel();
    r.language = kLangB; // user primary is kLangA => mismatch

    HiddenUserState u = neutralUser();
    u.languageMismatchTolerance = 0.25f;
    EXPECT_NEAR(latentBaseUtility(cfg, u, r, neutralCreator()),
                -cfg.languageMismatchPenalty * (1.0 - 0.25), 1e-5);

    // Same language => no penalty regardless of tolerance.
    Reel same = neutralReel(); // language kLangA == user primary
    EXPECT_NEAR(latentBaseUtility(cfg, u, same, neutralCreator()), 0.0, 1e-5);
}

// --- Empty channels contribute nothing (gate-off / test-isolation safety) ----------------------

TEST(LatentModelBaseUtility, EmptyEmbeddingsContributeZero) {
    BehaviourV2Config cfg;
    // Everything neutral & empty => exactly zero utility (no throw on empty dot).
    EXPECT_NEAR(latentBaseUtility(cfg, neutralUser(), neutralReel(), neutralCreator()), 0.0, 1e-9);
}

// --- Archetype conditioning: biases and susceptibility scaling ---------------------------------

TEST(LatentModelConditioning, SatisfactionBiasShiftsSatisfaction) {
    BehaviourV2Config cfg;
    cfg.latentNoiseStd = 0.0; // deterministic
    Rng rng(1);
    HiddenReelState pos = neutralHiddenReel();
    pos.satisfactionBias = 0.5f;
    LatentReaction rp =
        computeLatentReaction(cfg, neutralUser(), neutralReel(), pos, neutralCreator(), rng);
    // base utility 0, neutral user => susceptibility factor 1, positive bias unscaled.
    EXPECT_NEAR(rp.immediateSatisfaction, std::tanh(1.2 * 0.5), 1e-5);

    HiddenReelState neg = neutralHiddenReel();
    neg.satisfactionBias = -0.5f;
    LatentReaction rn =
        computeLatentReaction(cfg, neutralUser(), neutralReel(), neg, neutralCreator(), rng);
    EXPECT_NEAR(rn.immediateSatisfaction, std::tanh(1.2 * -0.5), 1e-5);
}

TEST(LatentModelConditioning, NegativeBiasScaledUpBySusceptibilityDownByTolerance) {
    BehaviourV2Config cfg;
    cfg.latentNoiseStd = 0.0;
    Rng rng(1);
    HiddenReelState rage = neutralHiddenReel();
    rage.satisfactionBias = -0.5f;

    HiddenUserState neutral = neutralUser();
    HiddenUserState susceptible = neutralUser();
    susceptible.clickbaitSusceptibility = 1.0f; // factor -> 2.0
    HiddenUserState tolerant = neutralUser();
    tolerant.controversyTolerance = 1.0f; // factor -> 0.4

    float sNeutral = computeLatentReaction(cfg, neutral, neutralReel(), rage, neutralCreator(), rng)
                         .immediateSatisfaction;
    float sSusceptible =
        computeLatentReaction(cfg, susceptible, neutralReel(), rage, neutralCreator(), rng)
            .immediateSatisfaction;
    float sTolerant =
        computeLatentReaction(cfg, tolerant, neutralReel(), rage, neutralCreator(), rng)
            .immediateSatisfaction;

    // More susceptible => MORE negative; more tolerant => LESS negative.
    EXPECT_LT(sSusceptible, sNeutral);
    EXPECT_LT(sNeutral, sTolerant);
    EXPECT_NEAR(sSusceptible, std::tanh(1.2 * -0.5 * 2.0), 1e-5);
    EXPECT_NEAR(sTolerant, std::tanh(1.2 * -0.5 * 0.4), 1e-5);
}

TEST(LatentModelConditioning, RegretFromBiasAndScaledBySusceptibility) {
    BehaviourV2Config cfg;
    cfg.latentNoiseStd = 0.0;
    Rng rng(1);
    HiddenReelState clickbait = neutralHiddenReel();
    clickbait.regretBias = 0.4f;

    // Neutral user: factor 1, utility 0 => regret == regretBias.
    EXPECT_NEAR(
        computeLatentReaction(cfg, neutralUser(), neutralReel(), clickbait, neutralCreator(), rng)
            .regret,
        0.4, 1e-5);

    // Susceptible user: factor 2 => 0.8.
    HiddenUserState susceptible = neutralUser();
    susceptible.clickbaitSusceptibility = 1.0f;
    EXPECT_NEAR(
        computeLatentReaction(cfg, susceptible, neutralReel(), clickbait, neutralCreator(), rng)
            .regret,
        0.8, 1e-5);
}

// --- Niche-treasure cohort gating (exact boundary) ---------------------------------------------

TEST(LatentModelNiche, AdjustZeroForNonNicheReel) {
    HiddenReelState r = neutralHiddenReel(); // width 0
    EXPECT_EQ(nicheCohortAdjust(r, UserId{7}), 0.0);
}

TEST(LatentModelNiche, InsideCohortPositiveOutsideNegative) {
    const UserId u{12345};
    const double h = cohortHash01(u);
    HiddenReelState inside = neutralHiddenReel();
    inside.nicheCohortWidth = 0.1f;
    inside.nicheCohortCentre = static_cast<float>(h); // distance ~0 => inside
    EXPECT_GT(nicheCohortAdjust(inside, u), 0.0);

    HiddenReelState outside = neutralHiddenReel();
    outside.nicheCohortWidth = 0.1f;
    // Place the centre half the space away => clearly outside the 0.1 band.
    outside.nicheCohortCentre = static_cast<float>(h >= 0.5 ? h - 0.4 : h + 0.4);
    EXPECT_LT(nicheCohortAdjust(outside, u), 0.0);
}

TEST(LatentModelNiche, BoundaryIsInclusiveWithinFloatMargin) {
    const UserId u{999};
    const double h = cohortHash01(u);
    const double centre = 0.25;
    const double dist = std::abs(h - centre);
    ASSERT_GT(dist, 0.02); // need room around the boundary for the margin test

    HiddenReelState r = neutralHiddenReel();
    r.nicheCohortCentre = static_cast<float>(centre);

    // Just inside: width a hair larger than the distance => inside (adjust > 0).
    r.nicheCohortWidth = static_cast<float>(dist * 1.001);
    EXPECT_GT(nicheCohortAdjust(r, u), 0.0);

    // Just outside: width a hair smaller than the distance => outside (adjust < 0). This is the
    // "distance == width is INSIDE" boundary tested to 0.1% (float granularity of
    // nicheCohortWidth).
    r.nicheCohortWidth = static_cast<float>(dist * 0.999);
    EXPECT_LT(nicheCohortAdjust(r, u), 0.0);
}

TEST(LatentModelNiche, InsideUserGetsFarHigherSatisfactionThanOutsideUser) {
    BehaviourV2Config cfg;
    cfg.latentNoiseStd = 0.0;
    Rng rng(1);
    const UserId u{555};
    const double h = cohortHash01(u);

    HiddenReelState inside = neutralHiddenReel();
    inside.nicheCohortWidth = 0.1f;
    inside.nicheCohortCentre = static_cast<float>(h);
    HiddenReelState outside = neutralHiddenReel();
    outside.nicheCohortWidth = 0.1f;
    outside.nicheCohortCentre = static_cast<float>(h >= 0.5 ? h - 0.4 : h + 0.4);

    HiddenUserState user = neutralUser(u);
    float satInside = computeLatentReaction(cfg, user, neutralReel(), inside, neutralCreator(), rng)
                          .immediateSatisfaction;
    float satOutside =
        computeLatentReaction(cfg, user, neutralReel(), outside, neutralCreator(), rng)
            .immediateSatisfaction;
    EXPECT_GT(satInside, satOutside + 0.3f);
}

// --- Range invariants --------------------------------------------------------------------------

TEST(LatentModelRanges, AllFieldsWithinDeclaredRangesAcrossExtremes) {
    BehaviourV2Config cfg;
    Rng rng(42);
    const auto &catalog = RealismConfig{}.archetypes;

    // Sweep every archetype crossed with a spread of user/reel extremes.
    for (const auto &spec : catalog) {
        HiddenReelState hr;
        hr.satisfactionBias = static_cast<float>(spec.satisfactionBias);
        hr.regretBias = static_cast<float>(spec.regretBias);
        hr.comfortReturnBonus = static_cast<float>(spec.comfortReturnBonus);
        hr.nicheCohortWidth = static_cast<float>(spec.nicheCohortWidth);
        hr.nicheCohortCentre = 0.5f;

        for (int i = 0; i < 200; ++i) {
            HiddenUserState u = neutralUser(UserId{static_cast<uint32_t>(i)});
            u.usefulnessPreference = rng.uniform01();
            u.humourPreference = rng.uniform01();
            u.noveltySeeking = rng.uniform01();
            u.controversyTolerance = rng.uniform01();
            u.clickbaitSusceptibility = rng.uniform01();
            u.informationTolerance = rng.uniform01();
            u.languageMismatchTolerance = rng.uniform01();

            Reel r = neutralReel();
            r.usefulness = rng.uniform01();
            r.humour = rng.uniform01();
            r.novelty = rng.uniform01();
            r.controversy = rng.uniform01();
            r.informationDensity = rng.uniform01();
            r.emotionalIntensity = rng.uniform01();
            r.language = (rng.uniform01() < 0.5) ? kLangA : kLangB;

            LatentReaction x = computeLatentReaction(cfg, u, r, hr, neutralCreator(), rng);
            EXPECT_GE(x.immediateSatisfaction, -1.0f);
            EXPECT_LE(x.immediateSatisfaction, 1.0f);
            EXPECT_GE(x.regret, 0.0f);
            EXPECT_LE(x.regret, 1.0f);
            EXPECT_GE(x.informationalValue, 0.0f);
            EXPECT_LE(x.informationalValue, 1.0f);
            EXPECT_GE(x.emotionalValue, 0.0f);
            EXPECT_LE(x.emotionalValue, 1.0f);
            EXPECT_GE(x.desireForSimilarContent, -1.0f);
            EXPECT_LE(x.desireForSimilarContent, 1.0f);
            EXPECT_GE(x.fatigueDelta, 0.0f);
            EXPECT_LE(x.fatigueDelta, 1.0f);
        }
    }
}
