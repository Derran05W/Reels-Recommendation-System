#pragma once

namespace rr {

// The hidden per-impression reaction (V2 TDD 4.3, Phase 14), owned SOLELY by the simulator and
// the evaluation carve-out (D18): computed by rr::computeLatentReaction (latent_model.hpp) for
// every impression when realism.latent_reactions is on, consumed by the observable-sampling
// stage of BehaviourModelV2 and streamed to welfare accumulators. Observable behaviour is
// sampled CONDITIONALLY on these values but is never identical to them (V2 TDD 3.2: engagement
// is evidence, not truth) — and no field of this struct may ever reach an InteractionEvent, the
// User, or any recommender-visible surface (leak-audit + include-graph guard enforced).
struct LatentReaction {
    float immediateSatisfaction = 0.0f;   // enjoyment during viewing, roughly [-1, 1]
    float informationalValue = 0.0f;      // perceived usefulness, [0, 1]
    float emotionalValue = 0.0f;          // amusement / inspiration / connection, [0, 1]
    float regret = 0.0f;                  // wishes they had skipped, [0, 1]
    float desireForSimilarContent = 0.0f; // appetite for more like this, [-1, 1]
    float fatigueDelta = 0.0f;            // contribution to session fatigue (consumed P16), [0, 1]
};

} // namespace rr
