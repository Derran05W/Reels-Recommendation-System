#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "rr/domain/candidate.hpp"
#include "rr/domain/recommendation.hpp"
#include "rr/domain/reel.hpp"
#include "rr/domain/user.hpp"
#include "rr/infrastructure/random.hpp"
#include "rr/recommendation/candidate_generator.hpp"

namespace rr {

// TDD 12.7 exploration candidate source: epsilon-greedy discovery, exposed as a CandidateGenerator
// for the orchestrated pipeline (TDD 13). It is the direct antidote to the frozen-arm collapse
// observed in Phase 7 (a static query mines out its neighbourhood): with probability epsilon per
// feed slot it injects reels the exploitation sources would never surface.
//
// RNG. The source holds a NON-OWNING Rng* — the OWNING recommender's per-request "recommender"
// stream (D8). The pointee must outlive the source (the recommender owns both; member order there
// keeps the rng alive first). All randomness is this rr::Rng only (D8); nothing here uses
// std::*_distribution.
//
// PER-REQUEST RNG DRAW-ORDER CONTRACT (fixed and documented so the recommender stream's shape is
// stable and the epsilon=0 no-op is exact):
//   1. If !request.enableExploration, return {} and consume NO rng (the harness sets this flag
//      from config.exploration.enabled; a disabled run must not perturb the stream). lastFiredSlots
//      is 0.
//   2. Otherwise draw EXACTLY request.feedSize bernoulli(epsilon) gates, in slot order — always
//      feedSize draws regardless of how many fire, so the gate stream shape is independent of
//      epsilon and of the outcomes. k = number of gates that fired (recorded as lastFiredSlots).
//   3. If k == 0, return {} (no further draws). This makes epsilon = 0 a STRUCTURAL no-op: no gate
//      can fire, so the source is empty and consumes only the fixed feedSize gate draws.
//   4. If k > 0, build up to `poolCap` (= RecommendationConfig.explorationCandidates) candidates
//      across the three TDD 12.7 modes. The candidate COUNT does not scale with k — k>0 means
//      "explore this request" and yields a full exploration pool; k feeds only the orchestrator's
//      guaranteed-slot rule (min(k, guaranteedSlots, ...)). The pool budget is split, in this fixed
//      draw order:
//        a. RANDOM-FRESH (the ONLY mode that draws rng): sample without replacement up to
//           poolCap/3 reels from the fresh pool (active, embeddable, createdAt within
//           freshWindowSeconds, in ascending ReelId order) via a partial Fisher-Yates — exactly
//           min(budget, |freshPool|) rng.uniformInt draws.
//        b. UNDEREXPOSED (deterministic, no rng): the reels with the lowest global impressionCount,
//           ties by ascending ReelId, up to poolCap/3, skipping reels already chosen.
//        c. UNCERTAIN-TOPIC (deterministic, no rng): the reels whose embedding is most DISTANT from
//           effectivePreference(user) (lowest cosine, ties by ascending ReelId), taking the
//           remainder of poolCap, skipping reels already chosen.
//      Selection uses pure ordering (lowest / most-distant N), so there are no magic quantile
//      thresholds. The result is deduplicated across modes and never exceeds poolCap.
//
// lastFiredSlots() returns k from the MOST RECENT generate() (0 before the first call, 0 on a
// disabled request). It is plain per-call state — safe because the simulation core is
// single-threaded (D13); the orchestrator reads it right after running this source.
//
// FILTERING: skips inactive reels and empty embeddings; dedup / seen / pool-cap across sources are
// the Orchestrator's job (TDD 13). CANDIDATE FIELDS are filled exactly like the other non-vector
// sources (reelId, source=Exploration, genuine retrievalSimilarity, D3-inverse distance).
class ExplorationCandidateSource final : public CandidateGenerator {
  public:
    ExplorationCandidateSource(const std::vector<Reel> &reels, double epsilon, uint32_t poolCap,
                               double freshWindowSeconds, Rng *rng, double enableAtDay = -1.0);

    std::vector<Candidate> generate(const User &user,
                                    const RecommendationRequest &request) override;

    // Number of per-slot epsilon gates that fired on the most recent generate() (see the draw-order
    // contract). Consumed by the Orchestrator's guaranteed-exploration-slot rule.
    std::size_t lastFiredSlots() const { return lastFiredSlots_; }

  private:
    const std::vector<Reel> &reels_;
    double epsilon_;
    // Phase 21 exploration time gate (contracts §1, config.exploration.enable_at_day). -1.0 (the
    // default) disables the gate — epsilon_ is used verbatim, byte-identical to pre-P21. When >= 0,
    // generate() uses EFFECTIVE epsilon = 0 for any request whose day floor(requestTime / 86400) is
    // strictly before enableAtDay_, else epsilon_. The per-slot bernoulli gates ALWAYS draw
    // feedSize times (contract step 2), and bernoulli(0) consumes the same uniform01 as
    // bernoulli(epsilon_), so switching the effective epsilon changes only the gate outcomes — the
    // draw count and the recommender-stream alignment are IDENTICAL to a run without the gate, and
    // the effective-0 window reproduces today's epsilon=0 behaviour exactly.
    double enableAtDay_;
    uint32_t poolCap_;
    double freshWindowSeconds_;
    Rng *rng_; // non-owning; the owning recommender's per-request stream (D8)
    std::size_t lastFiredSlots_ = 0;
};

} // namespace rr
