#pragma once

#include <vector>

#include "rr/domain/reel.hpp"
#include "rr/infrastructure/config.hpp"
#include "rr/learning/user_state_updater.hpp"

namespace rr {

// Online preference learning (TDD 11.2, 11.3, 8.3): the Phase 7 UserStateUpdater. Stateless and
// deterministic (D8: no rng; D9: no clock) - every apply() is a pure function of the User's
// recorded interaction history and the reel catalog.
//
// Call-site contract: apply() runs AFTER Simulator::step has recorded the interaction, so
// `interaction` is already the newest entry of user.recentInteractions and seenReels /
// creatorAffinity / interaction counters are Simulator-maintained (Phase 3/6 ownership, kept -
// recorded as a Phase 7 deviation from the TDD 23.4 wording). This class owns ONLY the three
// preference vectors:
//
//  1. longTermPreference <- normalize((1 - longTermRate) * u + longTermRate * reward * v)
//     (TDD 11.2; negative reward pushes away).
//  2. sessionPreference  <- normalize(sum over the recent-interaction window's CURRENT-session
//     events i of sessionLambda^(n-i) * reward_i * embedding_i) (TDD 11.3). Restricting the sum
//     to events with this interaction's sessionId makes the between-session reset (phase task 3)
//     implicit: rotation starts the sum over.
//  3. estimatedPreference <- normalize(longTermWeight * longTerm + sessionWeight * session)
//     (TDD 8.3). estimatedPreference is thus the CACHED effective preference, so
//     rr::effectivePreference(user) keeps returning it by const reference.
//
// Realism V2 (Phase 15, gated by `contentV2`): when contentV2 is true AND learning is enabled AND
// the reel carries a modality embedding, apply() ALSO maintains the three estimated modality
// preferences on User (visual/music/emotional, V2 TDD 5) with the SAME 11.2 rule at
// LearningConfig.modalityRate, driven by the SAME observable reward, applied to the reel's
// per-modality embeddings — est <- normalize((1 - eta_m) * est + eta_m * reward *
// modalityEmbedding). An EMPTY estimate cold-starts to the first observed reel's modality direction
// (mirroring 11.1's cold-start of the long-term vector to a prior; here the first observation,
// there being no modality prior). These estimates feed the V2 ranking features; the candidate QUERY
// stays semantic-only (D23). With contentV2 false (the default — every pre-Phase-15 call site) the
// three modality estimates are never written and behaviour is byte-identical to V1 (D17).
//
// All maintained vectors remain unit-length after every apply (property-tested). Degenerate
// normalizations (near-zero direction) fall back deterministically; the rules are documented at
// their definitions in online_user_state_updater.cpp.
//
// `reels` is the dense-id catalog (reels[i].id.value == i, the dataset-generator guarantee the
// whole pipeline relies on) used to look up embeddings of past in-window interactions.
class OnlineUserStateUpdater final : public UserStateUpdater {
  public:
    // `contentV2` (Phase 15, defaulted false to keep every pre-Phase-15 call site V1-identical):
    // gates the per-modality estimate maintenance above. The integrator flips the harness call site
    // (experiment_runner.cpp) to pass config.realism.contentV2.
    OnlineUserStateUpdater(const std::vector<Reel> &reels, const LearningConfig &config,
                           bool contentV2 = false);

    void apply(User &user, const Reel &reel, const InteractionEvent &interaction) const override;

  private:
    const std::vector<Reel> &reels_;
    LearningConfig config_;
    bool contentV2_ = false;
};

} // namespace rr
