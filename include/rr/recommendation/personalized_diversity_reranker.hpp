#pragma once

#include <vector>

#include "rr/domain/reel.hpp"
#include "rr/infrastructure/config.hpp"
#include "rr/recommendation/diversity_reranker.hpp"
#include "rr/recommendation/reranker.hpp"

namespace rr {

// Phase 17 personalized diversity (V2 TDD 4.10): the HARD rules stay universal (no duplicate,
// no seen — V1 TDD 15.1), but the per-creator/per-topic caps and the MMR lambda become
// per-user functions of the User's ESTIMATED tolerances (ToleranceEstimator), and the
// fixed-diversity path (DiversityReranker) is untouched (D17).
//
// NEUTRAL-EQUIVALENCE CONTRACT (the key regression guarantee, tested): a user whose estimates are
// all neutral — estimatedRepetitionTolerance == estimatedNoveltyTolerance == 0.5 AND empty
// topic/creator fatigue maps — gets the FIXED behaviour BYTE-FOR-BYTE, because rerank() delegates
// straight to the fixed DiversityReranker on that fast-path. So a gate-on run over a population
// that has accumulated no tolerance evidence yet is identical to fixed diversity, and the whole
// mechanism degrades gracefully to V1. The general (non-neutral) path is additionally CONTINUOUS
// with fixed at the neutral point (see the scaling rules below), so the transition is smooth.
//
// PER-USER SCALING (documented in full at the implementation; DiversityConfig.personalized*):
//   * maxPerTopicEff  = clamp(round(maxPerTopic  * capScale(repTol)),        1, feedSize)
//   * maxPerCreatorEff= clamp(round(maxPerCreator* capScale(repTol*(1-maxCreatorFatigue))), 1, fs)
//     where capScale is the piecewise-linear map through (0, capScaleMin), (0.5, 1.0),
//     (1, capScaleMax): a repetition-tolerant user (repTol -> 1) gets LOOSER caps (up to
//     capScaleMax), an intolerant user (repTol -> 0) TIGHTER caps (down to capScaleMin), and a
//     NEUTRAL user (0.5) gets EXACTLY the fixed cap (capScale(0.5) == 1.0). The creator cap is
//     additionally tightened by the user's peak estimated creator fatigue.
//   * Per-topic fatigue tightening: a topic the user is estimated-fatigued of gets an even tighter
//     cap capForTopic(t) = max(1, round(baseTopicCap * (1 - estimatedTopicFatigue[t]))), so a
//     high-fatigue topic is squeezed toward a single slot while unfatigued topics keep
//     baseTopicCap.
//   * Per-user MMR lambda: interpolates config.mmrLambda toward [personalizedLambdaMin,
//     personalizedLambdaMax] with estimatedNoveltyTolerance — novelty-tolerant (novTol -> 1) => a
//     LOWER lambda => MORE diversity; novelty-averse (novTol -> 0) => a higher lambda => more raw
//     relevance. novTol == 0.5 => exactly config.mmrLambda (continuity with fixed).
//
// The hard-rule greedy walk mirrors ConstraintReranker::selectFeed exactly (same no-dup / no-seen /
// out-of-range handling and relevance-order walk) but with the per-user effective caps above (a
// per-TOPIC cap needs the walk here rather than ConstraintReranker's single global cap); MMR
// ordering reuses MMRReranker with the per-user lambda; the constraints-only order reuses the
// consecutive-same-topic swap. Deterministic and exception-free (pure function of inputs; no
// rr::Rng, no wall clock), like the fixed reranker it extends.
//
// PACKAGE-B OWNERSHIP, FROZEN SIGNATURES.
class PersonalizedDiversityReranker final : public Reranker {
  public:
    PersonalizedDiversityReranker(const std::vector<Reel> &reels, const DiversityConfig &config);

    std::vector<RankedReel> rerank(const User &user, const std::vector<Candidate> &ranked,
                                   std::size_t feedSize) const override;

  private:
    const std::vector<Reel> &reels_;
    DiversityConfig config_;
    DiversityReranker fixed_; // the P9 machinery this class extends (and the neutral-path delegate)
};

} // namespace rr
