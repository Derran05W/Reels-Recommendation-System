#pragma once

#include "rr/domain/behaviour_outcome.hpp"
#include "rr/domain/creator.hpp"
#include "rr/domain/reel.hpp"
#include "rr/infrastructure/config.hpp"
#include "rr/infrastructure/random.hpp"
#include "rr/simulation/hidden/hidden_reel_state.hpp"
#include "rr/simulation/hidden/hidden_user_state.hpp"
#include "rr/simulation/hidden/latent_reaction.hpp"

namespace rr {

// The Realism V2 synthetic ground-truth user (V2 TDD 4.3/4.4, Phase 14): every impression first
// computes a hidden LatentReaction (rr::computeLatentReaction, stream "satisfaction"), then
// samples OBSERVABLE behaviour conditionally on it (stream "behaviour", which this model owns
// WHOLESALE under realism.latent_reactions — D19; the V1 BehaviourModel is untouched and serves
// all gate-off runs, D17). Engagement is evidence, not truth (V2 TDD 3.2): watch/likes/comments
// correlate with the latent but are noisy, archetype-shaped, and deliberately misleading in the
// documented ways (ragebait watch+comment on negative satisfaction; completed-because-short;
// social-conformity likes from visible popularity counters; clickbait opening-hook retention
// with early abandonment).
//
// PACKAGE-B OWNERSHIP, FROZEN SIGNATURES: package B implements this class in
// behaviour_model_v2.cpp (currently a scaffolding stub that delegates to the V1 model). The
// constructor and simulate() signatures must not change (the Simulator's V2 path calls them);
// package B owns everything else in the file. The pinned per-impression draw order on BOTH
// streams must be documented at the definition.
class BehaviourModelV2 {
  public:
    BehaviourModelV2(const BehaviourConfig &v1Config, const BehaviourV2Config &config);

    // Simulate one impression. Fills `latentOut` (hidden side — the caller routes it to welfare
    // accumulation via the evaluation carve-out; it must NEVER reach recommender-visible state)
    // and returns the observable outcome (V1 fields + the V2 comment/save/profile-visit/replay
    // signals). `behaviourRng` is the "behaviour" stream, `satisfactionRng` the "satisfaction"
    // stream, both caller-forked (D8/D19); never calls forkRng, never reads a clock.
    //
    // Two stages: (1) latentOut = rr::computeLatentReaction(...) on `satisfactionRng` (package A;
    // a neutral no-op stub in package B's tree), then (2) sampleObservables(...) on
    // `behaviourRng`. The pinned per-stream draw order is documented at sampleObservables in the
    // .cpp.
    BehaviourOutcome simulate(const HiddenUserState &hidden, const Reel &reel,
                              const HiddenReelState &hiddenReel, const Creator &creator,
                              Rng &behaviourRng, Rng &satisfactionRng,
                              LatentReaction &latentOut) const;

    // Sample the OBSERVABLE outcome conditionally on an EXPLICITLY supplied latent reaction,
    // drawing only from `behaviourRng` (the "behaviour" stream this model owns wholesale under the
    // gate, D19). This is stage (2) of simulate(): simulate() computes the latent via package A's
    // rr::computeLatentReaction and forwards it here. Exposed so unit tests can drive the
    // latent-conditioned sampling across a synthetic LatentReaction grid — package A's real
    // multi-channel latent is a stub (neutral zero) in package B's tree, so testing the wedges
    // (satisfaction->watch monotonicity, useful-underliked like damping, ragebait comments,
    // emotion-driven rewatch, ...) is only possible by constructing the latent directly. The
    // per-impression draw order on `behaviourRng` is FIXED and documented at the definition
    // (behaviour_model_v2.cpp). Never calls forkRng, never reads a clock, never mutates an input.
    BehaviourOutcome sampleObservables(const HiddenUserState &hidden, const Reel &reel,
                                       const HiddenReelState &hiddenReel, const Creator &creator,
                                       const LatentReaction &latent, Rng &behaviourRng) const;

  private:
    BehaviourConfig v1Config_;
    BehaviourV2Config config_;
};

} // namespace rr
