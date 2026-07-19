#include "rr/simulation/preference_evolution.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

#include "rr/core/embedding.hpp"

// PreferenceEvolution (V2 TDD §4.15/§4.16, Phase 20) — the simulation-side component that lets
// recommendations SHAPE hidden preferences. Applied once per impression AFTER BehaviourModelV2/
// stepV2, gate-on only (realism.preference_evolution). It mutates ONLY hidden simulator-owned state
// (D18): the four preference channels, the exposure accumulators, and retention.trust.
//
// STREAM DISCIPLINE (D19, contracts §3): applyImpression takes NO Rng and calls neither forkRng nor
// any global generator, so it draws ZERO rng from ANY stream. The reserved "preference-evolution"
// fork (D19) therefore stays RESERVED-UNUSED this phase — every mechanism below is deterministic
// closed-form arithmetic on (hidden, reel, latent, now). Documented again at applyImpression.
//
// The ONE config-surface knob is EvolutionConfig::etaEvo (the swept base rate, contracts §1). Every
// other §4.16 magnitude is a NAMED CONSTANT here (D24 no-premature-config), documented at
// definition. Degenerate-vector handling mirrors the P7 updater (online_user_state_updater.cpp).

namespace rr {

namespace {

// --- Degenerate-direction handling: mirrors the P7 updater's ε=1e-6 convention exactly ----------
// (online_user_state_updater.cpp). If an EMA target's L2 norm falls below this the direction is a
// near-perfect cancellation and is meaningless; the caller keeps the previous unit vector rather
// than normalizing. Comfortably above rr::normalize's 1e-12 hard floor.
constexpr double kDegenerateNormEpsilon = 1e-6;

// --- §4.16 away-decay half-lives (closed-form 2^(-gap/halfLife), gap in simulated seconds, D9) ---
// Topic exhaustion / creator burnout / aversion fade over DAYS away (the §4.16 "return to baseline
// after time away"). Novelty of a SPECIFIC reel recovers faster (you re-find it fresh sooner), so
// it gets a shorter half-life. Whole-state single-anchor decay: applyImpression decays every map by
// its half-life from (now - lastTouchedAt) before adding this impression's contribution.
constexpr double kExposureHalfLifeSeconds =
    172800.0;                                       // 2 simulated days (exhaustion/burnout/aversn)
constexpr double kNoveltyHalfLifeSeconds = 86400.0; // 1 simulated day (per-reel novelty depletion)

// Prune an accumulator entry once it decays below this (keeps the maps bounded, contracts §2/§4).
constexpr float kExposureEpsilon = 1e-4F;

// --- §4.16 per-impression accumulation increments -----------------------------------------------
// Each exposure raises the matching accumulator by a fixed step (the rise is bounded above by the
// decay: increment / (1 - 2^(-cadence/halfLife)) is the steady state under regular exposure).
constexpr float kTopicExhaustionIncrement = 0.15F; // per same-topic exposure
constexpr float kCreatorBurnoutIncrement = 0.12F;  // per same-creator exposure
constexpr float kReelNoveltyIncrement = 0.50F;     // per repeat view of the SAME reel
// Aversion is SATISFACTION-WEIGHTED (§4.16 "increased aversion following regret", "negative
// association after ragebait"): it rises only for regretful / negatively-satisfying impressions.
constexpr float kAversionRegretWeight = 0.50F; // × latent.regret          (regret drives aversion)
constexpr float kAversionNegSatWeight =
    0.70F; // × max(0, -satisfaction)  (ragebait drives aversion)

// --- §4.16 drags on FUTURE latent satisfaction (used by exposureSatisfactionDelta) --------------
// How strongly each accumulator dulls the satisfaction of a matching future reel. All subtract.
constexpr double kExhaustionSatWeight = 0.30;       // × topicExhaustion[topic]
constexpr double kBurnoutSatWeight = 0.25;          // × creatorBurnout[creator]
constexpr double kNoveltyDepletionSatWeight = 0.20; // × reelNovelty[reel]
constexpr double kAversionSatWeight = 0.50;         // × topicAversion[topic]

// --- §4.16 aversion's DIRECT push on the semantic §4.15 update -----------------------------------
// Beyond dulling satisfaction, accumulated topic aversion feeds an extra NEGATIVE term into the
// semantic channel's reinforcement (contracts §2: "feeds a negative term into the evolution update
// AND future latent satisfaction"), so a user with built-up aversion keeps drifting AWAY from that
// topic even on a neutral impression. Modest, and distinct from the satisfaction drag above.
constexpr double kAversionUpdateWeight =
    0.50; // × topicAversion[topic], subtracted from s (semantic)

// --- §4.16 platform-trust erosion / recovery (retention.trust; A is its ONLY writer) ------------
// Trust ∈ [0,1], initialized from the platformTrust trait on first touch. ASYMMETRIC: regret and
// ragebait erode it fast; satisfying exposure recovers it slowly (trust is hard to earn, easy to
// lose — §4.16). Net per impression = +recovery·max(0,s) − erosionRegret·regret − erosionNegSat·
// max(0,-s), clamped [0,1]. Package B READS this for its return-delay/churn math (never writes it).
constexpr double kTrustErosionRegretWeight = 0.08; // × regret
constexpr double kTrustErosionNegSatWeight = 0.10; // × max(0, -satisfaction)
constexpr double kTrustRecoveryWeight = 0.02;      // × max(0, satisfaction)  (slow, ~9× < erosion)

double clamp01(double v) { return std::clamp(v, 0.0, 1.0); }
double clampSat(double v) { return std::clamp(v, -1.0, 1.0); }

// Ordered-map lookup with a 0 default (operator[] would insert on a const map). std::map keeps the
// read portable/deterministic; a miss is baseline (no accumulated exposure).
template <class Map, class Key> float exposureLookup(const Map &m, const Key &k) {
    const auto it = m.find(k);
    return it == m.end() ? 0.0f : it->second;
}

// Decay every entry of an ordered accumulator map by `factor` and prune entries that fall below
// kExposureEpsilon (bounded containers). Order-independent: each entry decays alone, no reduction.
template <class Map> void decayAndPrune(Map &m, double factor) {
    for (auto it = m.begin(); it != m.end();) {
        const float decayed = static_cast<float>(static_cast<double>(it->second) * factor);
        if (decayed < kExposureEpsilon) {
            it = m.erase(it);
        } else {
            it->second = decayed;
            ++it;
        }
    }
}

// L2-normalize `v` in place iff its direction is well-defined; return false (leaving `v` untouched)
// on a degenerate near-cancellation, signalling the caller to keep the previous vector. Mirrors the
// P7 updater's tryNormalize exactly.
bool tryNormalize(Embedding &v) {
    double sumSq = 0.0;
    for (float x : v) {
        sumSq += static_cast<double>(x) * static_cast<double>(x);
    }
    if (std::sqrt(sumSq) < kDegenerateNormEpsilon) {
        return false;
    }
    normalize(v); // safe: norm >= 1e-6 >> 1e-12; inputs are finite
    return true;
}

// One §4.15 channel reinforcement: p' = normalize((1-eta)·p + eta·s·v). Skips (no-op) when the
// channel or reel embedding is absent or dimension-mismatched — the semantic channel is always
// present and same-dim (D5); modality channels are present under content_v2 (required by the gate),
// guarded exactly like the P7 modality updater. A degenerate EMA target keeps the previous vector.
void reinforceChannel(Embedding &p, const Embedding &v, double eta, double s) {
    if (p.empty() || v.empty() || p.size() != v.size()) {
        return;
    }
    const std::size_t dim = p.size();
    Embedding target(dim, 0.0f);
    for (std::size_t i = 0; i < dim; ++i) {
        const double pi = static_cast<double>(p[i]);
        const double vi = static_cast<double>(v[i]);
        target[i] = static_cast<float>((1.0 - eta) * pi + eta * s * vi);
    }
    if (tryNormalize(target)) {
        p = std::move(target);
    }
}

} // namespace

PreferenceEvolution::PreferenceEvolution(const EvolutionConfig &cfg) : config_(cfg) {}

// plasticityScale(p) = 2·clamp01(p): the [0,1] preferencePlasticity trait maps so the population
// MEAN 0.5 gives factor 1.0 (η_u = etaEvo), plasticity 0 freezes the user, plasticity 1 doubles the
// rate. Mirrors the P17 "factor 1.0 at the mean-0.5 trait" convention (behaviour_model_v2.cpp).
double PreferenceEvolution::plasticityScale(double plasticity) { return 2.0 * clamp01(plasticity); }

double exposureSatisfactionDelta(const HiddenUserState &hidden, const Reel &reel) {
    const HiddenExposureState &ex = hidden.exposure;
    // Each accumulator subtracts from the felt satisfaction of this (matching) reel. Reads are by
    // key (.find) so nothing depends on map iteration order. Returns <= 0.
    const double drag =
        kExhaustionSatWeight *
            static_cast<double>(exposureLookup(ex.topicExhaustion, reel.primaryTopic)) +
        kBurnoutSatWeight * static_cast<double>(exposureLookup(ex.creatorBurnout, reel.creatorId)) +
        kNoveltyDepletionSatWeight * static_cast<double>(exposureLookup(ex.reelNovelty, reel.id)) +
        kAversionSatWeight *
            static_cast<double>(exposureLookup(ex.topicAversion, reel.primaryTopic));
    return -drag;
}

// Applied once per impression AFTER stepV2, gate-on only. DRAWS ZERO RNG (no Rng parameter; the
// reserved "preference-evolution" stream stays unused, D19) and reads the clock only via the passed
// `now`. `latent.immediateSatisfaction` is the HIDDEN felt satisfaction (already fatigue- AND
// exposure-modulated by BehaviourModelV2 upstream), never observed reward — this is the §4.15 /
// Tier-4 core: reinforcement follows hidden satisfaction, so positive-reward-negative-satisfaction
// ragebait moves preferences AWAY from the reel.
void PreferenceEvolution::applyImpression(HiddenUserState &hidden, const Reel &reel,
                                          const LatentReaction &latent, Timestamp now) {
    HiddenExposureState &ex = hidden.exposure;
    HiddenRetentionState &ret = hidden.retention;

    // 1. First-touch init (gate-on). Anchor the away-decay clock and seed trust from the platform
    //    trust trait (A is the ONLY writer of retention.trust; -1.0 is the uninitialized sentinel).
    if (!ex.initialized) {
        ex.initialized = true;
        ex.lastTouchedAt = now;
    }
    if (ret.trust < 0.0) {
        ret.trust = clamp01(static_cast<double>(hidden.platformTrust));
    }

    // 2. Whole-state away-decay ON TOUCH (§4.16 return-to-baseline). gap = now - lastTouchedAt from
    //    the shared logical clock (0 on the first impression / same timestamp — no decay). Each map
    //    decays by 2^(-gap/halfLife) and prunes; then re-anchor. Novelty uses its shorter
    //    half-life.
    const double gap = static_cast<double>(now > ex.lastTouchedAt ? now - ex.lastTouchedAt : 0);
    if (gap > 0.0) {
        const double decayLong = std::exp2(-gap / kExposureHalfLifeSeconds);
        const double decayNovelty = std::exp2(-gap / kNoveltyHalfLifeSeconds);
        decayAndPrune(ex.topicExhaustion, decayLong);
        decayAndPrune(ex.creatorBurnout, decayLong);
        decayAndPrune(ex.topicAversion, decayLong);
        decayAndPrune(ex.reelNovelty, decayNovelty);
    }
    ex.lastTouchedAt = now;

    const double s = clampSat(static_cast<double>(latent.immediateSatisfaction));
    const double regret = clamp01(static_cast<double>(latent.regret));

    // 3. §4.15 per-channel reinforcement p' = normalize((1-η_u)·p + η_u·s·v), η_u scaled per user.
    //    The SEMANTIC channel additionally carries the accumulated-aversion push (read
    //    PRE-increment, so it reflects PAST aversion): s_sem = clamp(s -
    //    kAversionUpdateWeight·aversion, [-1,1]). Modality channels use s directly; a negative s
    //    already pushes every channel away from v.
    const double etaU =
        config_.etaEvo * plasticityScale(static_cast<double>(hidden.preferencePlasticity));
    const double aversionPre =
        static_cast<double>(exposureLookup(ex.topicAversion, reel.primaryTopic));
    const double sSem = clampSat(s - kAversionUpdateWeight * aversionPre);
    reinforceChannel(hidden.hiddenPreference, reel.embedding, etaU, sSem);
    reinforceChannel(hidden.visualPreference, reel.visualStyleEmbedding, etaU, s);
    reinforceChannel(hidden.musicPreference, reel.musicEmbedding, etaU, s);
    reinforceChannel(hidden.emotionalPreference, reel.emotionalToneEmbedding, etaU, s);

    // 4. §4.16 accumulate THIS impression's exposure (post-update, post-decay). Exhaustion/burnout
    //    rise on every exposure; reel-novelty rises per view (bites on the NEXT view of the same
    //    reel); aversion rises satisfaction-weighted (ragebait/regret only).
    ex.topicExhaustion[reel.primaryTopic] += kTopicExhaustionIncrement;
    ex.creatorBurnout[reel.creatorId] += kCreatorBurnoutIncrement;
    ex.reelNovelty[reel.id] += kReelNoveltyIncrement;
    const float aversionInc = kAversionRegretWeight * static_cast<float>(regret) +
                              kAversionNegSatWeight * static_cast<float>(std::max(0.0, -s));
    if (aversionInc > 0.0f) {
        ex.topicAversion[reel.primaryTopic] += aversionInc;
    }

    // 5. §4.16 platform-trust erosion/recovery (asymmetric; clamped [0,1]).
    const double trustDelta = kTrustRecoveryWeight * std::max(0.0, s) -
                              kTrustErosionRegretWeight * regret -
                              kTrustErosionNegSatWeight * std::max(0.0, -s);
    ret.trust = clamp01(ret.trust + trustDelta);
}

} // namespace rr
