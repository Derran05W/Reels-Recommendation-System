#pragma once

#include "rr/infrastructure/clock.hpp"
#include "rr/infrastructure/config.hpp"
#include "rr/infrastructure/random.hpp"
#include "rr/simulation/hidden/hidden_user_state.hpp"

namespace rr {

// Retention / trust / churn model (V2 TDD §4.17, Phase 20), driven by the EVENT runner's exit path
// (evaluation carve-out, D18) when retention.enabled is on. PACKAGE-B OWNERSHIP, FROZEN SIGNATURES:
// the scaffold ships this interface plus a stub (retention_model.cpp) whose nextReturnDelay
// reproduces the P18 baseline return-delay EXACTLY (a single "scheduling"-stream gaussian draw at
// the same call site), so a retention-on run is behaviour-identical to gate-off until package B
// lands the real satisfaction-coupled model. The constructor and the three method signatures must
// NOT change (the event runner calls them under the gate); package B owns everything else.
class RetentionModel {
  public:
    RetentionModel(const RetentionConfig &cfg);
    // Called at each session end (event runner exit path), BEFORE nextReturnDelay.
    void onSessionEnd(HiddenUserState &hidden, double sessionMeanSatisfaction,
                      double sessionMeanRegret, Timestamp exitAt);
    // Replaces P18's computeReturnDelay under the gate. Draws on the "scheduling" stream at the
    // SAME call site (wholesale replacement of the P18 return-delay consumer under
    // retention.enabled — the D19 replacement clause; gate-off draw sequence unchanged). Inputs per
    // §4.17: last-session satisfaction/regret, habitStrength, baselineDailyUsage, time-of-day curve
    // (named-constant curve over now % 86400), retention.trust. May set retention.churned
    // (delay > cfg churn threshold) — caller then schedules NO ReturnToApp.
    Timestamp nextReturnDelay(HiddenUserState &hidden, Timestamp now, Rng &schedulingRng);
    double churnProbability(const HiddenUserState &hidden) const; // model-implied, for metrics

  private:
    // Per-day return hazard implied by the CURRENT hidden state WITHOUT the time-of-day factor
    // (the daily-mean rhythm multiplier is 1). Shared by nextReturnDelay (which multiplies in the
    // time-of-day factor before drawing) and churnProbability (which needs a `now`-independent
    // rate). Never below cfg.hazardFloor. Documented math at the definition in retention_model.cpp.
    double baseDailyHazard(const HiddenUserState &hidden) const;

    RetentionConfig config_;
};

// Named-constant daily-rhythm multiplier applied to the return hazard, evaluated at a time-of-day
// in seconds within [0, 86400). Strictly positive, periodic with a 24h period, and with mean 1 over
// a full day (a pure cosine offset by 1), so it re-phases WHEN a user returns without changing the
// day-averaged rate. Peaks at the evening usage window. Exposed as a free function so the
// determinism/shape unit test can pin it; the shape constants live in retention_model.cpp (D24).
double retentionTimeOfDayFactor(double secondsOfDay);

} // namespace rr
