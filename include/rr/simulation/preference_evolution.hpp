#pragma once

#include "rr/domain/reel.hpp"
#include "rr/infrastructure/clock.hpp"
#include "rr/infrastructure/config.hpp"
#include "rr/simulation/hidden/hidden_user_state.hpp"
#include "rr/simulation/hidden/latent_reaction.hpp"

namespace rr {

// Exposure-driven preference evolution (V2 TDD §4.15/§4.16, Phase 20), owned SOLELY by the
// simulator side (D18) and applied by BOTH runners once per impression AFTER stepV2 when
// realism.preference_evolution is on. PACKAGE-A OWNERSHIP, FROZEN SIGNATURES: the scaffold ships
// this interface plus a no-op stub (preference_evolution.cpp); package A implements the real update
// there and owns the saturation/aversion modulation in behaviour_model_v2.cpp. The constructor and
// applyImpression signatures must NOT change (both runners call them under the gate); package A
// owns everything else in the file.
class PreferenceEvolution {
  public:
    PreferenceEvolution(const EvolutionConfig &cfg /* eta_evo */);
    // Applied once per impression AFTER BehaviourModelV2/stepV2, gate-on only. Deterministic:
    // draws ZERO rng values (the "preference-evolution" stream stays reserved-unused, documented).
    // Mutates: hidden preference channels (semantic + the three modality preferences) per §4.15
    //   p' = normalize((1-eta_u)p + eta_u * s * v) with s = latent.satisfaction (HIDDEN, not
    //   reward), eta_u = cfg.etaEvo * plasticityScale(hidden.preferencePlasticity);
    //   updates exposure accumulators (§4.16 exhaustion/burnout/novelty/aversion, incl. their
    //   away-decay via (now - lastTouched) closed form); erodes/recovers retention.trust.
    void applyImpression(HiddenUserState &hidden, const Reel &reel, const LatentReaction &latent,
                         Timestamp now);

    // Per-user scaling of the base reinforcement rate by the P13 preferencePlasticity trait
    // ([0,1], userTraitsV2). Documented at the definition (preference_evolution.cpp):
    // plasticityScale = 2·clamp01(plasticity), so the population MEAN 0.5 gives factor 1.0 (η_u =
    // cfg.etaEvo), plasticity 0 FREEZES the user (no evolution), plasticity 1 doubles the rate.
    // Public + static so the hand-computed §4.15 test asserts the exact effective rate η_u =
    // etaEvo·plasticityScale.
    static double plasticityScale(double plasticity);

  private:
    EvolutionConfig config_;
};

// Exposure-state drag on latent immediateSatisfaction (V2 TDD §4.16) — the PERSISTENT long-term
// analogue of the P16 session fatigueSatisfactionDelta: exhausted topics, burnt-out creators,
// novelty-depleted repeats, and averted topics all make the SAME reel genuinely LESS satisfying
// before the observable sampling. Pure arithmetic, rng/clock-free (draws NOTHING). Returns <= 0.
// BehaviourModelV2::simulate adds this to the latent's immediateSatisfaction BEFORE sampling,
// guarded by hidden.exposure.initialized (gate-on only), exactly mirroring how fatigueSatisfaction
// Delta plugs in. Reads the accumulators AS OF the previous impression's applyImpression (like the
// fatigue delta, simulate() carries no clock; the away-decay is realized on the next
// applyImpression — documented at the definition).
double exposureSatisfactionDelta(const HiddenUserState &hidden, const Reel &reel);

} // namespace rr
