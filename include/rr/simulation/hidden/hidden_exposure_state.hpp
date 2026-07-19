#pragma once

#include <map>

#include "rr/domain/ids.hpp"
#include "rr/infrastructure/clock.hpp"

namespace rr {

// Hidden long-term exposure state (V2 TDD §4.16, Phase 20), owned SOLELY by the simulator (D18):
// the persistent, decays-over-DAYS counterpart to the per-session HiddenSessionState fatigue. It
// accumulates long-term topic exhaustion, creator burnout, per-reel novelty depletion, and
// per-topic aversion as satisfying / over-exposed / ragebait impressions land, and reverts toward
// baseline with time AWAY via a closed-form 2^(-gap/halfLife) decay applied ON TOUCH (there is no
// session-start hook — the away-decay is computed from (now - lastTouchedAt) whenever
// PreferenceEvolution::applyImpression next writes the state; contracts §2/§4).
//
// OWNED BY PACKAGE A (contracts §8): no other package includes this header (B/C never touch it), so
// its shape is A's alone. Default-initialized on every HiddenUserState; a gate-off
// (realism.preference_evolution off) run never calls applyImpression, so `initialized` stays false,
// every map stays empty, and the state is byte-identical to a pre-Phase-20 run (D17).
//
// SEPARATION FROM SESSION FATIGUE (V2 TDD §4.16 requirement / exit-criterion 3): this state is
// updated ONLY by applyImpression (once per impression, gate-on) and is NEVER reset at a session
// boundary — unlike HiddenSessionState, which startSession() decays/reseeds per session. Two
// distinct mechanisms, two distinct structs.
//
// DETERMINISM / ITERATION ORDER (D8, contracts §4): the accumulators are std::map (ordered by key)
// so the decay+prune sweep iterates in a fixed, portable order. No result depends on iteration
// order regardless: each key is decayed/incremented independently and NO cross-key floating-point
// reduction is ever performed (exposureSatisfactionDelta reads keys by .find; applyImpression sums
// nothing across keys). Documented at preference_evolution.cpp.
struct HiddenExposureState {
    // Gate/lifecycle flag. false until applyImpression first touches this user (gate-on only). The
    // BehaviourModelV2 exposure-satisfaction modulation and applyImpression's away-decay both guard
    // on this, so gate-off runs (applyImpression never called) are inert and byte-identical (D17).
    bool initialized = false;

    // Logical-clock anchor (D9) for the whole-state away-decay: on each applyImpression the maps
    // below are multiplied by 2^(-(now - lastTouchedAt)/halfLife) and pruned BEFORE the new
    // impression's contributions are added, then this is set to `now`. 0 until first touch.
    Timestamp lastTouchedAt = 0;

    // §4.16 accumulators. All entries are >= 0, rise on exposure, and decay toward 0 (baseline)
    // with time away. Bounded: entries pruned once they decay below kExposureEpsilon
    // (preference_evolution .cpp). Keyed by the reel's serving-time ids (primaryTopic / creatorId /
    // id).
    std::map<TopicId, float> topicExhaustion;  // repeated topic exposure dulls its future marginal
                                               // satisfaction (topic saturation)
    std::map<CreatorId, float> creatorBurnout; // repeated same-creator exposure (creator burnout)
    std::map<ReelId, float> reelNovelty;       // repeat views of the SAME reel deplete its novelty
                                               // (bites only on the 2nd+ view; shorter half-life)
    std::map<TopicId, float> topicAversion; // satisfaction-weighted negative association built by
                                            // ragebait/regret exposure; pushes the semantic
                                            // preference away AND dulls future satisfaction
};

} // namespace rr
