#pragma once

#include "rr/domain/behaviour_outcome.hpp"
#include "rr/infrastructure/config.hpp"

namespace rr {

// TDD 10.5 reward: a weighted sum of the outcome's engagement signals, clamped to [-1, 1] so
// preference updates (Phase 7) stay stable. Weights come from RewardConfig; the log-watch-seconds
// term's normalization is documented at the implementation.
//
// Realism V2 (Phase 14) reframing: this reward is, and always was, an ENGAGEMENT PROXY — it is
// computed from observable signals only and that is exactly why the online updater may consume
// it. Under realism.latent_reactions the separate hidden ground truth is the LatentReaction
// (immediateSatisfaction/regret, V2 TDD 4.3), which reaches only the welfare metrics through the
// D18 evaluation carve-out. Engagement is evidence, not truth (V2 TDD 3.2): this model stays
// unchanged, deliberately blind to the latent, so optimizing it can now diverge measurably from
// satisfaction — the wedge the Phase 15 experiment quantifies.
class RewardModel {
  public:
    explicit RewardModel(const RewardConfig &config);

    // Pure function of the outcome — no randomness, no hidden-state access.
    float reward(const BehaviourOutcome &outcome) const;

  private:
    RewardConfig config_;
};

} // namespace rr
