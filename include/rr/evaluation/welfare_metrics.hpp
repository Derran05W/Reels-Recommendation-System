#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace rr {

// ===========================================================================================
// Hidden-user-welfare metric group (V2 TDD §6, Phase 15) — the evaluation carve-out module.
//
// V2 §6's four metric groups are reported as SEPARATE blocks and NO aggregate score is ever
// defined (D22). This header owns the "hidden user welfare" group: per-round and overall mean
// immediate satisfaction, mean regret, satisfaction per minute, and a per-archetype exposure
// breakdown (the direct "does the engagement arm over-serve ragebait/clickbait?" measurement)
// with per-archetype mean satisfaction/regret. Harmful-fatigue and platform-trust are emitted as
// documented NOT-YET-MODELED placeholders (constant 0; real in P16/P20) so the output schema is
// stable early (D22).
//
// Everything here is computed strictly inside the D18 EVALUATION CARVE-OUT: the per-impression
// LatentReaction and the reel's hidden archetype index never reach any recommender-visible
// structure. The module reduces the hidden per-impression stream the harness already produces; it
// is a pure reduction (no rng, no clock), so same-seed runs are byte-identical (D8).
//
// The engagement group's V2 additions (comment/save/profile-visit rates) live in MetricsCollector
// (the engagement group), NOT here — the welfare CSV writer joins the two per round.
// ===========================================================================================

// One hidden-welfare impression, reduced to just the fields the welfare group needs. Kept
// domain-type-free (no LatentReaction / HiddenReelState include) so the aggregation math is
// unit-testable with hand-built values, exactly like MetricsCollector's ImpressionSample. The
// ExperimentRunner fills it per impression from the latent stream + observable watch time + the
// reel's hidden archetype index (all evaluation carve-out reads).
struct WelfareImpression {
    double immediateSatisfaction = 0.0; // LatentReaction.immediateSatisfaction, roughly [-1, 1]
    double regret = 0.0;                // LatentReaction.regret, [0, 1]
    double watchSeconds = 0.0;          // observable watch time; the satisfaction-per-minute denom
    uint32_t archetypeIndex = 0;        // HiddenReelState.archetypeIndex (index into the catalog)
};

// Per-archetype exposure + welfare (V2 §6). `exposureShare` is this archetype's fraction of all
// welfare impressions in the bucket — the measurement task 5's experiment reads to detect an
// engagement arm over-serving ragebait/clickbait. Rows are emitted for EVERY catalog archetype in
// index order (even zero-impression ones) so the schema is stable and deterministic across arms.
struct ArchetypeWelfare {
    uint32_t archetypeIndex = 0;
    std::string name; // resolved from the catalog (config.realism.archetypes[i].name)
    std::size_t impressions = 0;
    double exposureShare = 0.0; // impressions / total welfare impressions (0 when none)
    double meanSatisfaction = 0.0;
    double meanRegret = 0.0;
};

// Per-round welfare point (extends the Phase 14 slice — the first four fields are unchanged so the
// Phase 14 welfare fingerprint keeps matching).
struct WelfareRoundPoint {
    std::size_t round = 0;
    std::size_t impressions = 0;
    double meanSatisfaction = 0.0; // mean LatentReaction.immediateSatisfaction over the round
    double meanRegret = 0.0;       // mean LatentReaction.regret over the round

    // Satisfaction per minute (V2 TDD §4.9/§6). DEFINITION: the sum of per-impression immediate
    // satisfaction over the round divided by the sum of SIMULATED watch-minutes (watchSeconds/60)
    // over the round — satisfaction accrued per minute of watch time, NOT a per-impression mean.
    // It can be negative (net dissatisfaction per minute). 0 when the round has no watch time.
    // `watchMinutes` is emitted alongside so the denominator (and thus the ratio) is reproducible.
    double satisfactionPerMinute = 0.0;
    double watchMinutes = 0.0;

    // NOT-YET-MODELED placeholders (V2 §6), constant 0. Harmful fatigue becomes real in P16
    // (session dynamics), platform trust in P20 (retention). Emitted now so the schema is stable
    // early (D22); the summary note flags them explicitly.
    double harmfulFatigue = 0.0;
    double platformTrust = 0.0;
};

// Overall hidden-user-welfare report (extends the Phase 14 slice). `configured` mirrors
// realism.latent_reactions: false leaves the whole block absent from output (byte-identical to a
// gate-off run, D17). The first four value fields are unchanged from Phase 14.
struct WelfareReport {
    bool configured = false;       // true iff realism.latent_reactions
    std::size_t impressions = 0;   // total impressions with a latent reaction
    double meanSatisfaction = 0.0; // over all impressions (0 if none)
    double meanRegret = 0.0;       // over all impressions (0 if none)

    // Overall satisfaction per minute (same definition as the per-round field) and the watch-minute
    // denominator. Placeholders as above.
    double satisfactionPerMinute = 0.0;
    double watchMinutes = 0.0;
    double harmfulFatigue = 0.0; // placeholder 0 (P16)
    double platformTrust = 0.0;  // placeholder 0 (P20)

    std::vector<WelfareRoundPoint> byRound;
    std::vector<ArchetypeWelfare> byArchetype; // one row per catalog archetype, index order
};

// The welfare-group accumulator (the module): fed one WelfareImpression per impression, reduced to
// a WelfareReport. Constructed with the round count and the archetype-catalog size so per-round and
// per-archetype buckets are pre-sized (deterministic layout, no rehash-order dependence). Pure
// reduction — no rng, no clock — so two same-seed runs produce byte-identical reports (D8).
class WelfareMetrics {
  public:
    WelfareMetrics(std::size_t rounds, std::size_t archetypeCount);

    // Accumulate one impression into round `round` and its archetype bucket. `round` must be in
    // range; an archetypeIndex past the catalog size is ignored (defensive — the generator keeps
    // indices in range, asserted in Debug).
    void add(std::size_t round, const WelfareImpression &imp);

    // Reduce to the report. `archetypeNames` resolves catalog indices to names in catalog order
    // (name i for archetype index i); a missing name falls back to "archetype_<i>". `configured`
    // is left default (false) — the caller sets it from the gate flag. Deterministic.
    WelfareReport reduce(const std::vector<std::string> &archetypeNames) const;

    std::size_t impressions() const { return impressions_; }

  private:
    struct RoundAcc {
        std::size_t impressions = 0;
        double satisfactionSum = 0.0;
        double regretSum = 0.0;
        double watchSecondsSum = 0.0;
    };
    struct ArchAcc {
        std::size_t impressions = 0;
        double satisfactionSum = 0.0;
        double regretSum = 0.0;
    };

    std::vector<RoundAcc> rounds_;
    std::vector<ArchAcc> archetypes_;
    std::size_t impressions_ = 0;
    double satisfactionSum_ = 0.0;
    double regretSum_ = 0.0;
    double watchSecondsSum_ = 0.0;
};

// Satisfaction per minute from a satisfaction sum and a watch-seconds sum (shared by the per-round
// and overall reductions and directly unit-testable). Returns 0 when there is no watch time
// (guards divide-by-zero → never NaN). watchMinutes = watchSeconds / 60.
double satisfactionPerMinute(double satisfactionSum, double watchSecondsSum);

} // namespace rr
