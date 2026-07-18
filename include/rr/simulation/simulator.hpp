#pragma once

#include <optional>
#include <unordered_map>

#include "rr/domain/creator.hpp"
#include "rr/domain/interaction.hpp"
#include "rr/domain/reel.hpp"
#include "rr/domain/user.hpp"
#include "rr/infrastructure/clock.hpp"
#include "rr/infrastructure/config.hpp"
#include "rr/learning/reward_model.hpp"
#include "rr/simulation/behaviour_model.hpp"
#include "rr/simulation/behaviour_model_v2.hpp"
#include "rr/simulation/hidden/hidden_user_state.hpp"

namespace rr {

// One simulated impression: the ground-truth outcome plus the assembled event. The outcome is
// simulator/evaluation-side only (it derives from hidden state, TDD 18.2); recommenders may see
// the event but never the outcome.
struct StepResult {
    BehaviourOutcome outcome;
    InteractionEvent event;
};

// Per-impression V2 context (Phase 14), threaded by the harness from the feed-serving loop into
// stepV2 so the event can carry the V2 TDD 5 observables: which request served the item, where
// in the feed it sat, its candidate-source provenance, and the reel's hidden archetype state
// (simulator-side input for BehaviourModelV2 — never surfaced on the event).
struct StepV2Inputs {
    const HiddenReelState *hiddenReel = nullptr; // required (points into the dataset's vector)
    uint32_t positionInFeed = 0;
    uint64_t requestId = 0;
    Timestamp requestTimestamp = 0;
    bool fromExploration = false;
    CandidateSource sourceProvenance = CandidateSource::VectorHNSW;
};

// Drives the ground-truth interaction loop (TDD 10 + phase-3 task 5): given a user shown a reel,
// it simulates the reaction, computes the reward, assembles the InteractionEvent, advances the
// logical clock (D9 — simulated seconds, never wall clock), and updates reel counters
// (impressions/completions/likes/shares/skips) and recommender-visible user bookkeeping
// (seenReels, recentInteractions, totalInteractions, currentSessionLength, session rotation
// around hidden.avgSessionLength).
class Simulator {
  public:
    // `rng` is forked by the caller on the "behaviour" stream (D8); the Simulator owns it from
    // here on. `recentWindow` bounds User::recentInteractions (LearningConfig.recentWindow).
    // `trendingHalfLifeSeconds` is the half-life (RankingConfig.trendingHalfLifeSeconds) used to
    // decay each reel's trending accumulators forward on every impression (TDD 12.4).
    Simulator(const BehaviourConfig &behaviour, const RewardConfig &reward, Rng rng,
              uint32_t recentWindow, double trendingHalfLifeSeconds);

    // Realism V2 constructor (Phase 14, realism.latent_reactions): additionally owns the
    // caller-forked "satisfaction" stream and the BehaviourModelV2 parameters; step() keeps its
    // V1 behaviour byte-identically (D17 — the V1 model still serves it), while stepV2() runs
    // the latent-reaction pipeline. PACKAGE-B OWNERSHIP, FROZEN SIGNATURE (the harness calls
    // both constructors).
    Simulator(const BehaviourConfig &behaviour, const BehaviourV2Config &behaviourV2,
              const RewardConfig &reward, Rng behaviourRng, Rng satisfactionRng,
              uint32_t recentWindow, double trendingHalfLifeSeconds);

    // Simulate `user` being shown `reel`. Mutates `reel` counters and `user` bookkeeping;
    // advances the logical clock. HiddenUserState is read-only here and only ever handed to the
    // BehaviourModel (D11).
    StepResult step(User &user, const HiddenUserState &hidden, Reel &reel, const Creator &creator);

    // Realism V2 step (Phase 14): BehaviourModelV2 (latent on "satisfaction", observables on
    // "behaviour" — which V2 owns wholesale under the gate, D19), V2 event fields populated from
    // `v2` (position/request/provenance) and from the outcome (comment/save/profile-visit/
    // replay/dwell/start/finish). Fills `latentOut` for the harness's welfare accumulation
    // (evaluation carve-out); no latent value reaches the event or any recommender-visible
    // structure (leak-audit enforced). Requires the V2 constructor (asserts in Debug).
    // PACKAGE-B OWNERSHIP, FROZEN SIGNATURE.
    StepResult stepV2(User &user, const HiddenUserState &hidden, Reel &reel, const Creator &creator,
                      const StepV2Inputs &v2, LatentReaction &latentOut);

    // Current logical time in simulated seconds (starts at 0).
    Timestamp now() const;

  private:
    // Per-user session bookkeeping. The recommender-visible User carries currentSessionLength but
    // not the SessionId, so the Simulator owns the id and the (rng-sampled) target length that
    // triggers rotation. avgSessionLength (hidden state) is read here ONLY to derive targetLength;
    // it is never copied into any recommender-visible field (D11).
    struct SessionState {
        SessionId sessionId{0};
        uint32_t targetLength = 1;
    };

    // Draw a per-session target length from hidden.avgSessionLength using rng_ (deterministic, D8).
    uint32_t sampleSessionTarget(float avgSessionLength);

    BehaviourModel behaviour_;
    RewardModel reward_;
    Rng rng_;
    uint32_t recentWindow_;
    double trendingHalfLifeSeconds_;
    Timestamp now_ = 0;
    std::unordered_map<UserId, SessionState> sessions_;

    // Realism V2 state (Phase 14): engaged only via the V2 constructor; V1-constructed
    // simulators carry std::nullopt plus a never-drawn seed-0 placeholder rng (Rng has no
    // default constructor) and never touch either.
    std::optional<BehaviourModelV2> behaviourV2_;
    Rng satisfactionRng_{0};
};

} // namespace rr
