#pragma once

#include <vector>

#include "rr/domain/interaction.hpp"
#include "rr/domain/reel.hpp"
#include "rr/domain/user.hpp"
#include "rr/infrastructure/config.hpp"

namespace rr {

// Phase 17 observables-only tolerance estimation (V2 TDD 4.10/5): maintains the User's
// estimated repetition/novelty tolerances and per-topic/per-creator fatigue estimates from the
// interaction stream — declining within-topic completion runs, not-interested after repeats,
// exit-after-repetition patterns, comment/save cadence. Lives in learning/ (D18 guard: cannot
// reach simulation/hidden/); rng-free and clock-free (stream-neutral, D8); invoked by the
// harness after each Simulator step, only under realism.personalized_diversity.
//
// CALL-SITE CONTRACT (mirrors OnlineUserStateUpdater): apply() runs AFTER Simulator::stepV2 has
// recorded the interaction, so `interaction` is already the NEWEST entry of
// user.recentInteractions (== recentInteractions.back()). The "prior window" this estimator reads
// is therefore recentInteractions with that last entry excluded — the OBSERVABLE context the user
// experienced BEFORE reacting to `reel`.
//
// SIGNALS (all OBSERVABLE — the D18 guard structurally forbids any hidden read):
//   * The event `type` (complete/rewatch/like/share/follow => positive; instant-skip /
//     not-interested => negative; partial/impression => graded by watchRatio), folded into a
//     single per-event satisfaction proxy `sat` in [0,1].
//   * `interaction.commented / saved / profileVisited` — deep-engagement cadence: any of these
//     lifts `sat` to the positive extreme (documented at computeSat), so comment/save cadence is
//     mild positive evidence on the current topic (plan task (d)).
//   * `interaction.observedExitAfterImpression` — an exit fired after a repetitive recent window
//     (repTopic high) is strong intolerance evidence: it drives `sat` down (plan "exit-after-
//     repetition"), so the same unified path lowers repetition tolerance and raises topic fatigue.
//   * The recent-window REPETITION context of the current reel: repTopic = fraction of the prior
//     window sharing the current primary topic, repCreator = fraction sharing the current creator,
//     both in [0,1] (mirrors FeatureExtractor's `repetition` feature but split by channel).
//
// UPDATE RULES (EMA-style, bounded [0,1], rates are named constants per D24; documented in full at
// their definition in tolerance_estimator.cpp). All four fields start at the neutral 0.5 / empty
// (no evidence) and CONVERGE within ~recentWindow interactions of a consistent stream:
//   (b) estimatedRepetitionTolerance (global): EMA toward `sat`, the STEP SCALED BY repTopic, so
//       repetition-heavy completions raise it, not-interested/exit-after-repeat lower it, and
//       purely novel events (repTopic 0) leave it unchanged (they carry no repetition evidence).
//   (c) estimatedNoveltyTolerance (global): EMA toward `sat`, the STEP SCALED BY (1 - repTopic), so
//       completing/liking a NOVEL-topic item raises it and skipping one lowers it, while repeated
//       events barely move it.
//   (a) estimatedTopicFatigue[t] / estimatedCreatorFatigue[c] in [0,1] ("how fatigued of t / c the
//       user currently seems"): every event first DECAYS all live entries multiplicatively
//       (kFatigueDecayPerEvent) and prunes negligible ones (recovery when a topic/creator stops
//       recurring — the documented decay), then the CURRENT topic/creator EMA toward a fatigue
//       signal repChannel * (1 - sat): a repeated topic/creator met with a negative reaction rises
//       toward fatigued, a completion (even repeated) relaxes it, and a novel item barely moves it.
//
// PACKAGE-B OWNERSHIP, FROZEN SIGNATURES: package B implements this class and documents every
// signal and update rule at the definition (tolerance_estimator.cpp). Consumed by
// PersonalizedDiversityReranker (per-user caps / MMR lambda) and, under the same gate, by the
// WeightedRanker repetition-penalty scaling.
class ToleranceEstimator {
  public:
    ToleranceEstimator(const std::vector<Reel> &reels, const DiversityConfig &config);

    // Update the user's tolerance estimates from one completed interaction (called after
    // Simulator::stepV2 and OnlineUserStateUpdater::apply, mirroring their post-step contract).
    void apply(User &user, const Reel &reel, const InteractionEvent &interaction) const;

  private:
    // Dense-id reel catalog (reels[i].id.value == i) for looking up prior in-window events' topics;
    // creators come off the event itself. Non-owning; the owner keeps it alive for our lifetime.
    const std::vector<Reel> &reels_;
    DiversityConfig config_;
};

} // namespace rr
