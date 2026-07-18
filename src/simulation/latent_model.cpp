#include "rr/simulation/latent_model.hpp"

#include <algorithm>
#include <cmath>

#include "rr/core/embedding.hpp"
#include "rr/simulation/cohort_hash.hpp"

namespace rr {
namespace {

// ================================================================================================
// Multi-channel latent-reaction model (V2 TDD 4.3/4.4, Phase 14). The V1 single-affinity score
// (V1 TDD 10.2: z = alpha*a + beta*Q + gamma*C - delta*D + eps) is replaced, under the
// realism.latent_reactions gate, by a multi-channel base utility conditioned on the reel's hidden
// archetype. The recommender never sees any of this (D18).
//
// --- BASE UTILITY (latentBaseUtility, noise-free, archetype-free) ------------------------------
//   utility_base =
//       topicWeight     * <hiddenPreference,   reel.embedding>              (topic channel)
//     + visualWeight    * <visualPreference,    reel.visualStyleEmbedding>  (visual channel)
//     + musicWeight     * <musicPreference,     reel.musicEmbedding>        (music channel)
//     + emotionalWeight * <emotionalPreference, reel.emotionalToneEmbedding>(emotional channel)
//     + usefulnessWeight * reel.usefulness * usefulnessPreference           (>= 0, a bonus)
//     + humourWeight     * reel.humour     * humourPreference               (>= 0, a bonus)
//     + noveltyWeight    * reel.novelty    * noveltySeeking                 (>= 0, a bonus)
//     - informationDensityWeight * max(0, informationDensity - informationTolerance)  (overload)
//     + controversyTerm                                                     (see below)
//     + languageTerm                                                        (see below)
//     + creatorAttachmentWeight * <hiddenPreference, creator.styleEmbedding>(V1 10.2 C term)
//
//   The four channel dots are cosine similarities (unit embeddings, D3), in [-1, 1]. The three
//   scalar content terms are products of two [0,1] quantities, so each only ever ADDS utility (a
//   user is never hurt by usefulness/humour/novelty they do not care about — the term is 0 there);
//   the penalties live in the hinges below.
//
//   Information-overload hinge: dense content beyond what the user tolerates is taxing. Fires ONLY
//   when informationDensity > informationTolerance (mirrors the controversy hinge). The POSITIVE
//   informational payoff is carried by the usefulness term and the informationalValue latent
//   output, not here — this term is a pure penalty.
//
//   Controversy: penalty proportional to how far controversy exceeds the user's tolerance, plus a
//   SMALL thrill boost for high-tolerance users only:
//       controversyTerm = controversyBoost - controversyPenaltyWeight * max(0, controversy - tol)
//       controversyBoost = (tol > kControversyHighToleranceThreshold)
//                          ? controversyBoostWeight * controversy : 0
//   So an intolerant user is strongly penalized by ragebait's high controversy while a
//   high-tolerance user gets a mild positive kick from it (V2 TDD 4.3).
//
//   Language: a flat penalty when the reel's language differs from the user's primary language,
//   relieved by the user's mismatch tolerance:
//       languageTerm = (reel.language != user.primaryLanguage)
//                      ? -languageMismatchPenalty * (1 - languageMismatchTolerance) : 0
//
// --- FULL UTILITY (inside computeLatentReaction) -----------------------------------------------
//   utility = utility_base + latentNoiseStd * gaussian()  [THE SINGLE DRAW] + nicheCohortAdjust
//   Niche-treasure boost/penalty is folded into utility so it flows to satisfaction AND desire.
//
// --- ARCHETYPE CONDITIONING (the six LatentReaction fields) -------------------------------------
//   susceptibilityFactor = clamp(1 + kSusceptibilityGain * clickbaitSusceptibility
//                                  - kSusceptToleranceRelief * controversyTolerance,
//                                kSusceptMin, kSusceptMax)
//     — how much MORE an adverse archetype hurts this user (up with clickbaitSusceptibility, down
//       with controversyTolerance). Applied to the NEGATIVE satisfaction bias and the POSITIVE
//       regret bias only (V2 TDD 4.4 ragebait/clickbait wording).
//
//   immediateSatisfaction = tanh(utility + kSatBiasScale * effectiveSatBias)          in [-1, 1]
//     effectiveSatBias = satisfactionBias >= 0 ? satisfactionBias
//                                              : satisfactionBias * susceptibilityFactor
//     — ragebait's negative bias is amplified for susceptible/intolerant users and damped for
//       tolerant ones; positive-bias archetypes (useful, niche, ...) are unscaled.
//
//   regret = clamp01(kRegretFromNegUtility * max(0, -utility) + effectiveRegretBias)   in [0, 1]
//     effectiveRegretBias = regretBias > 0 ? regretBias * susceptibilityFactor : regretBias
//     — regret rises when utility is negative and from the archetype's regret bias (clickbait/
//       ragebait/polished have positive bias, amplified by susceptibility).
//
//   informationalValue = clamp01(kInfoDensityUsefulness * (informationDensity * usefulness)
//                                 * (kInfoTolLo + kInfoTolGain * informationTolerance)
//                                 + kInfoSatBias * max(0, satisfactionBias))            in [0, 1]
//     — driven by dense useful content the user can absorb; the useful/satisfying archetypes'
//       positive satisfactionBias lifts it too (V2 TDD 4.4 "useful -> high informationalValue").
//
//   emotionalValue = clamp01(kEmoIntensity * emotionalIntensity
//                             + kEmoHumour * reel.humour * humourPreference
//                             + kEmoEmoChannel   * max(0, emotionalMatch)
//                             + kEmoMusicChannel * max(0, musicMatch))                  in [0, 1]
//     — amusement/inspiration/connection: emotional intensity, humour match, and the emotional
//       AND music channel matches. The music term is why a background_music reel keeps positive
//       emotionalValue at weak topic match (V2 TDD 4.4 "music-driven completion").
//
//   desireForSimilarContent = clamp(kDesireSat * immediateSatisfaction
//                                    + kDesireInfoEmo * (0.5*(informationalValue+emotionalValue)
//                                                        - kDesireNeutral)
//                                    + kComfortDesire * comfortReturnBonus
//                                    - kDesireRegret * regret, -1, 1)                   in [-1, 1]
//     — tracks satisfaction and lasting (informational/emotional) value, plus the comfort
//       archetype's return bonus, minus regret. Polished-irrelevant (near-zero satisfactionBias,
//       low usefulness/informationalValue, no comfort bonus) therefore lands LOW even though its
//       production polish is high; comfort lands HIGH (verified in the statistical suite).
//
//   fatigueDelta = clamp01(kFatigueBase + kFatigueEmoIntensity * emotionalIntensity
//                           + kFatigueControversy * controversy)                        in [0, 1]
//     — a base per-impression scrolling cost plus more from emotionally intense / controversial
//       content. Consumed by P16 session fatigue; emitted sensibly here.
// ================================================================================================

// --- Base-utility shape constants (D24: behavioural constants no planned experiment varies stay
//     named constants in the .cpp; the config already exposes every channel/scalar WEIGHT). ------

// Controversy thrill boost fires only for users whose controversyTolerance exceeds this.
constexpr double kControversyHighToleranceThreshold = 0.6;

// --- Archetype-conditioning constants ---------------------------------------------------------

// Scaling of the archetype satisfaction bias before it is added to the (pre-tanh) utility.
constexpr double kSatBiasScale = 1.2;

// Susceptibility multiplier on adverse (negative-satisfaction / positive-regret) archetype biases.
constexpr double kSusceptibilityGain = 1.0;     // up with clickbaitSusceptibility
constexpr double kSusceptToleranceRelief = 0.6; // down with controversyTolerance
constexpr double kSusceptMin = 0.3;             // clamp floor (a very tolerant user)
constexpr double kSusceptMax = 2.0;             // clamp ceil (a very susceptible user)

// Regret contribution from a negative utility.
constexpr double kRegretFromNegUtility = 0.5;

// informationalValue shape.
constexpr double kInfoDensityUsefulness = 1.4; // gain on (informationDensity * usefulness)
constexpr double kInfoTolLo = 0.5;             // floor of the tolerance gate (tol = 0 still counts)
constexpr double kInfoTolGain = 0.5;           // slope of the tolerance gate
constexpr double kInfoSatBias = 0.3;           // lift from a positive satisfaction bias

// emotionalValue shape.
constexpr double kEmoIntensity = 0.5;
constexpr double kEmoHumour = 0.4;
constexpr double kEmoEmoChannel = 0.8;
constexpr double kEmoMusicChannel = 0.8;

// desireForSimilarContent shape.
constexpr double kDesireSat = 0.7;
constexpr double kDesireInfoEmo = 0.4;
constexpr double kDesireNeutral = 0.3; // (info+emo)/2 above this raises desire, below lowers it
constexpr double kComfortDesire = 0.8;
constexpr double kDesireRegret = 0.3;

// Niche-treasure satisfaction adjust (pre-squash utility units).
constexpr double kNicheInsideBoost = 0.8;     // strong boost for the hidden cohort
constexpr double kNicheOutsidePenalty = 0.45; // mild penalty for everyone else

// fatigueDelta shape.
constexpr double kFatigueBase = 0.05;
constexpr double kFatigueEmoIntensity = 0.25;
constexpr double kFatigueControversy = 0.10;

double clamp01(double v) { return std::clamp(v, 0.0, 1.0); }

// Dot product that treats an empty embedding as a 0 contribution (a channel the caller left
// unpopulated, e.g. a unit test isolating another channel). Two non-empty embeddings of different
// size still throw via rr::dot — that is a genuine dimension bug, not a silent 0.
double channelDot(const Embedding &a, const Embedding &b) {
    if (a.empty() || b.empty()) {
        return 0.0;
    }
    return static_cast<double>(dot(a, b));
}

} // namespace

double latentBaseUtility(const BehaviourV2Config &config, const HiddenUserState &user,
                         const Reel &reel, const Creator &creator) {
    // Channel matches (cosine similarities of unit embeddings, D3).
    const double topicMatch = channelDot(user.hiddenPreference, reel.embedding);
    const double visualMatch = channelDot(user.visualPreference, reel.visualStyleEmbedding);
    const double musicMatch = channelDot(user.musicPreference, reel.musicEmbedding);
    const double emotionalMatch = channelDot(user.emotionalPreference, reel.emotionalToneEmbedding);

    double utility = config.topicWeight * topicMatch + config.visualWeight * visualMatch +
                     config.musicWeight * musicMatch + config.emotionalWeight * emotionalMatch;

    // Scalar content-value bonuses (each a product of two [0,1] quantities; never negative).
    utility += config.usefulnessWeight * static_cast<double>(reel.usefulness) *
               static_cast<double>(user.usefulnessPreference);
    utility += config.humourWeight * static_cast<double>(reel.humour) *
               static_cast<double>(user.humourPreference);
    utility += config.noveltyWeight * static_cast<double>(reel.novelty) *
               static_cast<double>(user.noveltySeeking);

    // Information-overload hinge: penalty only when density exceeds the user's tolerance.
    const double infoOverflow = std::max(0.0, static_cast<double>(reel.informationDensity) -
                                                  static_cast<double>(user.informationTolerance));
    utility -= config.informationDensityWeight * infoOverflow;

    // Controversy: penalty beyond tolerance + a small thrill boost for high-tolerance users only.
    const double tol = static_cast<double>(user.controversyTolerance);
    const double controversy = static_cast<double>(reel.controversy);
    const double controversyPenalty =
        config.controversyPenaltyWeight * std::max(0.0, controversy - tol);
    const double controversyBoost = (tol > kControversyHighToleranceThreshold)
                                        ? config.controversyBoostWeight * controversy
                                        : 0.0;
    utility += controversyBoost - controversyPenalty;

    // Language mismatch penalty (relieved by the user's mismatch tolerance).
    if (reel.language != user.primaryLanguage) {
        utility -= config.languageMismatchPenalty *
                   (1.0 - static_cast<double>(user.languageMismatchTolerance));
    }

    // Creator attachment — the V1 TDD 10.2 C term, hidden side.
    utility +=
        config.creatorAttachmentWeight * channelDot(user.hiddenPreference, creator.styleEmbedding);

    return utility;
}

double nicheCohortAdjust(const HiddenReelState &hiddenReel, UserId userId) {
    // Width 0 (or negative) => not a niche reel => no adjustment.
    if (!(hiddenReel.nicheCohortWidth > 0.0f)) {
        return 0.0;
    }
    // PINNED Phase 10 cohort hash (cohort_hash.hpp). No wraparound: a band running off [0,1) is
    // simply truncated (centre in [0,1) by construction; documented in the header). Boundary is
    // inclusive: distance == width counts as INSIDE.
    const double h = cohortHash01(userId);
    const double distance = std::abs(h - static_cast<double>(hiddenReel.nicheCohortCentre));
    return (distance <= static_cast<double>(hiddenReel.nicheCohortWidth)) ? kNicheInsideBoost
                                                                          : -kNicheOutsidePenalty;
}

LatentReaction computeLatentReaction(const BehaviourV2Config &config, const HiddenUserState &user,
                                     const Reel &reel, const HiddenReelState &hiddenReel,
                                     const Creator &creator, Rng &satisfactionRng) {
    // THE SINGLE DRAW (fixed count, unconditional — see the header contract). Drawn even when
    // latentNoiseStd == 0 so the "satisfaction" stream advances identically for every impression
    // regardless of archetype/branch.
    const double noise = config.latentNoiseStd * satisfactionRng.gaussian();

    const double base = latentBaseUtility(config, user, reel, creator);
    const double utility = base + noise + nicheCohortAdjust(hiddenReel, user.userId);

    // Susceptibility multiplier for adverse archetype biases.
    const double susceptibilityFactor =
        std::clamp(1.0 + kSusceptibilityGain * static_cast<double>(user.clickbaitSusceptibility) -
                       kSusceptToleranceRelief * static_cast<double>(user.controversyTolerance),
                   kSusceptMin, kSusceptMax);

    const double satBias = static_cast<double>(hiddenReel.satisfactionBias);
    const double effectiveSatBias = (satBias >= 0.0) ? satBias : satBias * susceptibilityFactor;

    const double regretBias = static_cast<double>(hiddenReel.regretBias);
    const double effectiveRegretBias =
        (regretBias > 0.0) ? regretBias * susceptibilityFactor : regretBias;

    // Precompute the reused reel/user quantities.
    const double emotionalMatch = channelDot(user.emotionalPreference, reel.emotionalToneEmbedding);
    const double musicMatch = channelDot(user.musicPreference, reel.musicEmbedding);
    const double informationDensity = static_cast<double>(reel.informationDensity);
    const double usefulness = static_cast<double>(reel.usefulness);
    const double emotionalIntensity = static_cast<double>(reel.emotionalIntensity);

    LatentReaction reaction;

    // immediateSatisfaction in [-1, 1].
    reaction.immediateSatisfaction =
        static_cast<float>(std::tanh(utility + kSatBiasScale * effectiveSatBias));

    // regret in [0, 1].
    reaction.regret = static_cast<float>(
        clamp01(kRegretFromNegUtility * std::max(0.0, -utility) + effectiveRegretBias));

    // informationalValue in [0, 1].
    const double infoTolGate =
        kInfoTolLo + kInfoTolGain * static_cast<double>(user.informationTolerance);
    reaction.informationalValue = static_cast<float>(
        clamp01(kInfoDensityUsefulness * (informationDensity * usefulness) * infoTolGate +
                kInfoSatBias * std::max(0.0, satBias)));

    // emotionalValue in [0, 1].
    reaction.emotionalValue = static_cast<float>(clamp01(
        kEmoIntensity * emotionalIntensity +
        kEmoHumour * static_cast<double>(reel.humour) * static_cast<double>(user.humourPreference) +
        kEmoEmoChannel * std::max(0.0, emotionalMatch) +
        kEmoMusicChannel * std::max(0.0, musicMatch)));

    // desireForSimilarContent in [-1, 1].
    const double infoEmoMean = 0.5 * (static_cast<double>(reaction.informationalValue) +
                                      static_cast<double>(reaction.emotionalValue));
    reaction.desireForSimilarContent = static_cast<float>(
        std::clamp(kDesireSat * static_cast<double>(reaction.immediateSatisfaction) +
                       kDesireInfoEmo * (infoEmoMean - kDesireNeutral) +
                       kComfortDesire * static_cast<double>(hiddenReel.comfortReturnBonus) -
                       kDesireRegret * static_cast<double>(reaction.regret),
                   -1.0, 1.0));

    // fatigueDelta in [0, 1] (consumed by P16).
    reaction.fatigueDelta =
        static_cast<float>(clamp01(kFatigueBase + kFatigueEmoIntensity * emotionalIntensity +
                                   kFatigueControversy * static_cast<double>(reel.controversy)));

    return reaction;
}

} // namespace rr
