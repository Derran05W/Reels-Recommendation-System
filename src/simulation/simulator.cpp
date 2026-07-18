#include "rr/simulation/simulator.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <utility>

namespace rr {

namespace {

// Fixed per-impression browsing overhead added to the logical clock on every step, on top of the
// (rounded) time spent watching the reel. Models the seconds a user spends scrolling to and
// deciding on the next reel, and guarantees the clock strictly advances even on a zero-watch
// instant skip (D9 — simulated seconds, never wall clock).
constexpr Timestamp kBrowseOverheadSeconds = 2;

// Spread of per-session target lengths around hidden.avgSessionLength, as a fraction of the mean.
// Session lengths vary run-to-run but stay centred on the user's trait.
constexpr double kSessionLengthRelStddev = 0.25;

// Exponential decay factor 2^(-dt/halfLife) (dt in simulated seconds, D9) that brings a reel's
// trending accumulators forward from `updatedAt` to `now`, clamping to 1 when now <= updatedAt
// (TDD 12.4). This is the WRITE side of the trending twin; it intentionally MIRRORS
// rr::trendingDecayFactor (scoring.hpp — the READ side used by rr::trendingScore) rather than
// calling it, so rr_simulation stays free of the rr_recommend/vector-db link dependency (the
// module boundary declared in CMakeLists; inspect_user links rr_simulation alone). The two are
// kept in lockstep by the shared half-life semantics and cross-checked in simulator_test against
// rr::trendingDecayFactor as an independent oracle.
double trendingDecayForward(Timestamp updatedAt, Timestamp now, double halfLifeSeconds) {
    const double dt = now > updatedAt ? static_cast<double>(now - updatedAt) : 0.0;
    return std::exp2(-dt / halfLifeSeconds);
}

// Per-impression gains applied to the recommender-visible User::creatorAffinity estimate (TDD
// 12.6) from the OBSERVABLE outcome flags only — never from HiddenUserState (D11: the ground-truth
// creator affinity C_{u,c} is off-limits here; this is the system's LOGGED estimate). Signals are
// weighted by how strongly they express enjoyment of the creator: an explicit follow most, a plain
// completed watch least, an explicit "not interested" pushes the estimate down. The running
// per-creator affinity is accumulated and then clamped to [0, 1]; that CONTRACT is relied on by
// the sibling ranker package (the creator-affinity ranking feature) and by
// CreatorAffinityCandidateSource, so it must hold on every write.
constexpr float kAffinityFollowedGain = 0.25f;
constexpr float kAffinitySharedGain = 0.15f;
constexpr float kAffinityLikedGain = 0.10f;
constexpr float kAffinityCompletedGain = 0.02f;
constexpr float kAffinityNotInterestedGain = -0.20f;

} // namespace

Simulator::Simulator(const BehaviourConfig &behaviour, const RewardConfig &reward, Rng rng,
                     uint32_t recentWindow, double trendingHalfLifeSeconds)
    : behaviour_(behaviour), reward_(reward), rng_(std::move(rng)), recentWindow_(recentWindow),
      trendingHalfLifeSeconds_(trendingHalfLifeSeconds) {}

Simulator::Simulator(const BehaviourConfig &behaviour, const BehaviourV2Config &behaviourV2,
                     const RewardConfig &reward, Rng behaviourRng, Rng satisfactionRng,
                     uint32_t recentWindow, double trendingHalfLifeSeconds)
    : behaviour_(behaviour), reward_(reward), rng_(std::move(behaviourRng)),
      recentWindow_(recentWindow), trendingHalfLifeSeconds_(trendingHalfLifeSeconds),
      behaviourV2_(BehaviourModelV2(behaviour, behaviourV2)),
      satisfactionRng_(std::move(satisfactionRng)) {}

// Realism V2 step (Phase 14, realism.latent_reactions). Structurally a sibling of step(): the
// SAME reward (unchanged RewardModel on observables), clock advance (watch + browse overhead),
// session resolution (V1 rotation KEPT — P16 replaces it), reel counters/trending, and user
// bookkeeping/creator-affinity update — all byte-for-byte the V1 logic, but driven by
// BehaviourModelV2 (latent on the "satisfaction" stream, observables on the "behaviour" stream
// this model owns wholesale under the gate, D19) and populating the V2 InteractionEvent fields.
// step() is left completely untouched (D17: the V1 path is the golden baseline); the shared logic
// is intentionally duplicated here rather than refactored out of step().
//
// The latent (latentOut) flows ONLY to the harness's welfare accumulation (evaluation carve-out,
// D18); no latent value reaches the event or any recommender-visible structure.
StepResult Simulator::stepV2(User &user, const HiddenUserState &hidden, Reel &reel,
                             const Creator &creator, const StepV2Inputs &v2,
                             LatentReaction &latentOut) {
    assert(behaviourV2_.has_value() && "stepV2 requires the Realism V2 constructor");
    assert(v2.hiddenReel != nullptr && "StepV2Inputs.hiddenReel is required");
    StepResult result{};

    // 1. Ground-truth reaction. rng_ is the "behaviour" stream, satisfactionRng_ the "satisfaction"
    //    stream; BehaviourModelV2 draws the latent on the latter and the observables on the former.
    result.outcome = behaviourV2_->simulate(hidden, reel, *v2.hiddenReel, creator, rng_,
                                            satisfactionRng_, latentOut);
    const BehaviourOutcome &outcome = result.outcome;

    // 2. Reward: the UNCHANGED engagement-proxy RewardModel over the observable outcome (no extra
    //    randomness, no latent access).
    const float reward = reward_.reward(outcome);

    // 3. Timestamps + clock. startTimestamp = clock at playback start (BEFORE advancing);
    //    finishTimestamp = start + round(watchSeconds); then advance the clock by the same
    //    watch + browse overhead as V1 step (D9), so the V1 `timestamp` field keeps its V1 meaning
    //    (finish + browse overhead). dwellSeconds = watched seconds + the browse overhead.
    const Timestamp startTs = now_;
    const Timestamp watchedSeconds =
        static_cast<Timestamp>(std::lround(std::max(0.0f, outcome.watchSeconds)));
    const Timestamp finishTs = startTs + watchedSeconds;
    now_ += watchedSeconds + kBrowseOverheadSeconds;

    // 4. Session resolution — identical to V1 step (session rotation KEPT; P16 replaces it). May
    //    draw one gaussian from rng_ (the "behaviour" stream) via sampleSessionTarget.
    auto [it, inserted] = sessions_.try_emplace(user.id);
    SessionState &session = it->second;
    if (inserted) {
        session.sessionId = SessionId{0};
        session.targetLength = sampleSessionTarget(hidden.avgSessionLength);
        user.currentSessionLength = 0;
    } else if (user.currentSessionLength >= session.targetLength) {
        session.sessionId = SessionId{session.sessionId.value + 1};
        session.targetLength = sampleSessionTarget(hidden.avgSessionLength);
        user.currentSessionLength = 0;
    }

    // 5. Assemble the event: the nine V1 observable fields exactly as step(), then the V2
    //    observables (V2 TDD 5). requestId/positionInFeed/provenance/exploration come from the
    //    harness (StepV2Inputs); start/finish/dwell/replay/comment/save/profile come from this
    //    impression. observedExitAfterImpression stays false until P16 wires probabilistic exit.
    //    Every field is an observable — no latent leaks (leak-audit enforced).
    result.event = InteractionEvent{user.id,
                                    reel.id,
                                    reel.creatorId,
                                    outcome.primaryType,
                                    outcome.watchSeconds,
                                    outcome.watchRatio,
                                    reward,
                                    now_,
                                    session.sessionId};
    result.event.positionInFeed = v2.positionInFeed;
    result.event.requestId = v2.requestId;
    result.event.requestTimestamp = v2.requestTimestamp;
    result.event.startTimestamp = startTs;
    result.event.finishTimestamp = finishTs;
    result.event.dwellSeconds =
        std::max(0.0f, outcome.watchSeconds) + static_cast<float>(kBrowseOverheadSeconds);
    result.event.replayCount = outcome.replayCount;
    result.event.commented = outcome.commented;
    result.event.saved = outcome.saved;
    result.event.profileVisited = outcome.profileVisited;
    result.event.fromExploration = v2.fromExploration;
    result.event.sourceProvenance = v2.sourceProvenance;

    // 6. Reel ground-truth counters — identical to V1 step.
    reel.impressionCount += 1;
    if (outcome.completed) {
        reel.completionCount += 1;
    }
    if (outcome.liked) {
        reel.likeCount += 1;
    }
    if (outcome.shared) {
        reel.shareCount += 1;
    }
    if (outcome.instantSkip) {
        reel.skipCount += 1;
    }

    // 6b. Trending accumulators — identical to V1 step (same 1/2/4 completion/like/share weights).
    const double decay =
        trendingDecayForward(reel.trendingUpdatedAt, now_, trendingHalfLifeSeconds_);
    reel.trendingEngagement *= decay;
    reel.trendingImpressions *= decay;
    reel.trendingImpressions += 1.0;
    reel.trendingEngagement += (outcome.completed ? 1.0 : 0.0) + (outcome.liked ? 2.0 : 0.0) +
                               (outcome.shared ? 4.0 : 0.0);
    reel.trendingUpdatedAt = now_;

    // 7. Recommender-visible user bookkeeping — identical to V1 step.
    user.seenReels.insert(reel.id);
    user.totalInteractions += 1;
    user.currentSessionLength += 1;
    user.recentInteractions.push_back(result.event);
    while (user.recentInteractions.size() > recentWindow_) {
        user.recentInteractions.pop_front();
    }

    // 7b. Creator-affinity estimate from OBSERVABLE flags only — identical to V1 step.
    const float affinityDelta = (outcome.followed ? kAffinityFollowedGain : 0.0f) +
                                (outcome.shared ? kAffinitySharedGain : 0.0f) +
                                (outcome.liked ? kAffinityLikedGain : 0.0f) +
                                (outcome.completed ? kAffinityCompletedGain : 0.0f) +
                                (outcome.notInterested ? kAffinityNotInterestedGain : 0.0f);
    if (affinityDelta != 0.0f) {
        float &affinity = user.creatorAffinity[reel.creatorId];
        affinity = std::clamp(affinity + affinityDelta, 0.0f, 1.0f);
    }

    return result;
}

uint32_t Simulator::sampleSessionTarget(float avgSessionLength) {
    // Gaussian around the mean with a proportional spread, clamped to at least one interaction so a
    // session always contains the reel that opened it. Uses rng_ so the sequence is deterministic.
    const double mean = std::max(1.0, static_cast<double>(avgSessionLength));
    const double sampled = mean + rng_.gaussian() * (mean * kSessionLengthRelStddev);
    const long rounded = std::lround(sampled);
    return static_cast<uint32_t>(std::max<long>(1, rounded));
}

StepResult Simulator::step(User &user, const HiddenUserState &hidden, Reel &reel,
                           const Creator &creator) {
    StepResult result{};

    // 1. Ground-truth reaction. HiddenUserState is handed only to the behaviour model here (D11).
    result.outcome = behaviour_.simulate(hidden, reel, creator, rng_);
    const BehaviourOutcome &outcome = result.outcome;

    // 2. Reward: pure function of the outcome, no extra randomness.
    const float reward = reward_.reward(outcome);

    // 3. Advance the logical clock by the watched seconds (rounded) plus browse overhead (D9). The
    //    clock therefore advances by at least kBrowseOverheadSeconds every step.
    const Timestamp watchedSeconds =
        static_cast<Timestamp>(std::lround(std::max(0.0f, outcome.watchSeconds)));
    now_ += watchedSeconds + kBrowseOverheadSeconds;

    // 4. Resolve the session this interaction belongs to. On first sight of the user we open a
    //    session; when the running currentSessionLength has reached the target we rotate to a new
    //    session (advance the id, reset the length, resample the target) BEFORE attributing this
    //    interaction, so this reel opens the new session.
    auto [it, inserted] = sessions_.try_emplace(user.id);
    SessionState &session = it->second;
    if (inserted) {
        session.sessionId = SessionId{0};
        session.targetLength = sampleSessionTarget(hidden.avgSessionLength);
        user.currentSessionLength = 0;
    } else if (user.currentSessionLength >= session.targetLength) {
        session.sessionId = SessionId{session.sessionId.value + 1};
        session.targetLength = sampleSessionTarget(hidden.avgSessionLength);
        user.currentSessionLength = 0;
    }

    // 5. Assemble the recommender-visible event. Every field is an observable interaction signal;
    //    no hidden preference vector or trait leaks through (D11).
    result.event = InteractionEvent{user.id,
                                    reel.id,
                                    reel.creatorId,
                                    outcome.primaryType,
                                    outcome.watchSeconds,
                                    outcome.watchRatio,
                                    reward,
                                    now_,
                                    session.sessionId};

    // 6. Update reel ground-truth counters, consistent with the outcome flags.
    reel.impressionCount += 1;
    if (outcome.completed) {
        reel.completionCount += 1;
    }
    if (outcome.liked) {
        reel.likeCount += 1;
    }
    if (outcome.shared) {
        reel.shareCount += 1;
    }
    if (outcome.instantSkip) {
        reel.skipCount += 1;
    }

    // 6b. Maintain the reel's trending accumulators (TDD 12.4): the exponentially-decayed twin of
    //     the popularity numerator. Decay both accumulators forward to this event's timestamp,
    //     then add exactly the increment the lifetime popularity counters gained for this event
    //     (+1 iff completed, +2 iff liked, +4 iff shared — the same 1/2/4 weights as
    //     popularityEngagement). rr::trendingScore reads these decayed forward again at query time,
    //     so the velocity is correct at any later `now` without a per-query update. Pure
    //     arithmetic on existing state — no rng draw (D8).
    const double decay =
        trendingDecayForward(reel.trendingUpdatedAt, now_, trendingHalfLifeSeconds_);
    reel.trendingEngagement *= decay;
    reel.trendingImpressions *= decay;
    reel.trendingImpressions += 1.0;
    reel.trendingEngagement += (outcome.completed ? 1.0 : 0.0) + (outcome.liked ? 2.0 : 0.0) +
                               (outcome.shared ? 4.0 : 0.0);
    reel.trendingUpdatedAt = now_; // set last, so the decay above used the PREVIOUS update time

    // 7. Update recommender-visible user bookkeeping.
    user.seenReels.insert(reel.id);
    user.totalInteractions += 1;
    user.currentSessionLength += 1;
    user.recentInteractions.push_back(result.event);
    while (user.recentInteractions.size() > recentWindow_) {
        user.recentInteractions.pop_front();
    }

    // 7b. Update the recommender-visible creator-affinity estimate from the OBSERVABLE outcome
    //     flags only (never HiddenUserState — D11). Accumulate the signed gains, then clamp the
    //     running per-creator estimate to [0, 1] (contract for the ranker + creator source). The
    //     map is only touched when some signal fired, so plain skips leave it untouched.
    const float affinityDelta = (outcome.followed ? kAffinityFollowedGain : 0.0f) +
                                (outcome.shared ? kAffinitySharedGain : 0.0f) +
                                (outcome.liked ? kAffinityLikedGain : 0.0f) +
                                (outcome.completed ? kAffinityCompletedGain : 0.0f) +
                                (outcome.notInterested ? kAffinityNotInterestedGain : 0.0f);
    if (affinityDelta != 0.0f) {
        float &affinity = user.creatorAffinity[reel.creatorId];
        affinity = std::clamp(affinity + affinityDelta, 0.0f, 1.0f);
    }

    return result;
}

Timestamp Simulator::now() const { return now_; }

} // namespace rr
