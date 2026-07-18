#pragma once

#include "rr/domain/creator.hpp"
#include "rr/domain/ids.hpp"
#include "rr/domain/reel.hpp"
#include "rr/infrastructure/config.hpp"
#include "rr/infrastructure/random.hpp"
#include "rr/simulation/hidden/hidden_reel_state.hpp"
#include "rr/simulation/hidden/hidden_user_state.hpp"
#include "rr/simulation/hidden/latent_reaction.hpp"

namespace rr {

// Hidden latent-reaction computation (V2 TDD 4.3, Phase 14) — the multi-channel replacement for
// the V1 single-affinity behaviour score, simulator-side only (D18).
//
// PACKAGE-A OWNERSHIP, FROZEN SIGNATURE: package A implements this in latent_model.cpp
// (currently a scaffolding stub); BehaviourModelV2 (package B) calls it as its first stage. The
// signature below must not change; package A owns everything else in this file and may add
// test-visible helpers (e.g. an exposed channel-utility function) beside it.
//
// Contract:
// - Computes the full LatentReaction for one impression from the hidden user channels (V2 TDD
//   4.2), the reel's serving-visible V2 attributes (4.1), the hidden archetype parameters
//   (HiddenReelState — satisfaction/regret biases, niche cohort via rr::cohortHash01, comfort
//   bonus), and the creator's hidden style affinity (the V1 TDD 10.2 C term:
//   dot(hiddenPreference, creator.styleEmbedding)).
// - The multi-channel base utility is the weighted combination documented at the definition:
//   channel matches (topic/visual/music/emotional dot products), usefulness x
//   usefulnessPreference, humour x humourPreference, novelty x noveltySeeking, controversy vs
//   controversyTolerance (penalty beyond tolerance, small boost within for high-tolerance
//   users), language mismatch penalty, information density vs informationTolerance — weights
//   from BehaviourV2Config.
// - Draws ONLY from `satisfactionRng` (the caller-forked "satisfaction" stream, D19), with a
//   FIXED documented per-call draw count independent of archetype and of every branch.
//   >>> DRAW COUNT: EXACTLY ONE satisfactionRng.gaussian() per call, unconditional. <<<
//   The single gaussian is the utility noise term (V2 TDD 4.3). It is drawn before any branch and
//   even when latentNoiseStd == 0 (its result is then scaled to 0), so the stream advances by the
//   same amount regardless of archetype, niche membership, language match, or any other branch —
//   package B may therefore rely on the "satisfaction" stream advancing identically per impression.
//   Everything else (niche cohort membership, biases, clamps) is deterministic given the inputs.
// - No clock, no forkRng, no mutation of any input.
LatentReaction computeLatentReaction(const BehaviourV2Config &config, const HiddenUserState &user,
                                     const Reel &reel, const HiddenReelState &hiddenReel,
                                     const Creator &creator, Rng &satisfactionRng);

// --- Test-visible helpers (package A owns these; not part of the frozen cross-package contract) --

// The noise-free, archetype-free multi-channel base utility: the weighted combination of channel
// matches (topic/visual/music/emotional), the scalar content-value terms
// (usefulness/humour/novelty), the information-overload and controversy hinges, the language
// mismatch penalty and the creator-attachment term (V1 TDD 10.2 C). Draws NOTHING (no rng): this
// is the deterministic core that computeLatentReaction perturbs with one gaussian and then
// conditions on the archetype. Exposed so unit tests can isolate each channel term. Modality/
// creator dot products of an empty embedding contribute 0 (lets tests zero a channel by leaving
// both sides empty); a size mismatch between two non-empty embeddings still throws (a real bug).
double latentBaseUtility(const BehaviourV2Config &config, const HiddenUserState &user,
                         const Reel &reel, const Creator &creator);

// The additive niche-treasure satisfaction adjustment applied (pre-squash) to the utility (V2 TDD
// 4.4): a strong POSITIVE boost for users inside the reel's hidden cohort band and a mild NEGATIVE
// penalty for everyone else, using the PINNED Phase 10 rr::cohortHash01. Returns exactly 0 for a
// non-niche reel (nicheCohortWidth <= 0). Membership is |cohortHash01(userId) - nicheCohortCentre|
// <= nicheCohortWidth, boundary INCLUSIVE, NO wraparound (a band that runs off [0,1) is simply
// truncated — documented at the definition). Exposed so unit tests can assert the boundary exactly.
double nicheCohortAdjust(const HiddenReelState &hiddenReel, UserId userId);

} // namespace rr
