#include "rr/simulation/retention_model.hpp"

#include <algorithm>
#include <cmath>

namespace rr {

namespace {

// ================================================================================================
// §4.17 retention shape constants (D24: behavioural constants no planned experiment varies stay
// NAMED CONSTANTS here, documented at definition — only the churn threshold + hazard floor are
// config surface). The model treats the delay until a user's next session as an EXPONENTIAL waiting
// time with a per-simulated-day return hazard h that RISES with better last-session welfare,
// stronger habit, higher trust, and heavier baseline usage — so better welfare => stochastically
// SOONER return (V2 §4.17). One uniform draw per call turns the hazard into a concrete delay.
// ================================================================================================

// --- Habit dynamics (onSessionEnd): retention.habitStrength in [0,1], seeded from the per-user
//     habitStrength TRAIT on the first session, decayed toward 0 while the user is away, and
//     strengthened by satisfying sessions / eroded by dissatisfying ones. ---
constexpr double kHabitHalfLifeSeconds = 259200.0; // 3 simulated days: habit halves per 3d away.
constexpr double kHabitGain = 0.15;      // strengthening fraction of the remaining headroom (1-h)
                                         // per unit positive session welfare.
constexpr double kHabitErodeGain = 0.10; // erosion fraction of the current habit per unit negative
                                         // session welfare.
constexpr double kRegretHabitWeight = 1.0; // session welfare = meanSatisfaction - w*meanRegret.

// --- Return-hazard coupling (nextReturnDelay / churnProbability). The per-day hazard is
//     h = usage * exp(welfareCoeff*welfareSignal + habitCoeff*habit + trustCoeff*(trust-pivot)),
//     optionally * the time-of-day factor, then floored at cfg.hazardFloor. All coeffs are in
//     natural-log space, so each term is a multiplicative push on the daily return rate. ---
constexpr double kWelfareHazardCoeff = 1.0; // last-session (satisfaction - regret) push.
constexpr double kRegretDelayWeight = 1.0;  // regret weight inside the welfare signal.
constexpr double kHabitHazardCoeff = 1.0;   // habit push (habit 0 => none, habit 1 => e^1 ~ 2.7x).
constexpr double kTrustHazardCoeff = 1.0;   // trust push about the pivot.
constexpr double kTrustPivot = 0.7;         // trust reference (mid of the [0.4,1.0] trait range):
                                            // above => sooner, below => later.
constexpr double kMinDailyUsage = 0.05;     // clamp so a ~0-usage trait keeps a meaningful core
                                            // hazard (the config hazard_floor is the final bound).

// --- Time-of-day rhythm (retentionTimeOfDayFactor): a pure cosine offset by 1, mean 1 over a day,
//     so it re-phases WHEN a user returns without changing the day-averaged rate. ---
constexpr double kTimeOfDayAmplitude = 0.3;   // hazard swings +/-30% over the day.
constexpr double kPeakSecondsOfDay = 72000.0; // 20:00 — the evening usage peak.
constexpr double kSecondsPerDay = 86400.0;

// --- Delay realization bounds. ---
constexpr double kMinReturnDelaySeconds = 60.0; // matches the P18 baseline floor (D9 integer time).
constexpr double kMaxReturnDelaySeconds = 1e15; // overflow guard for the exponential's tail (still
                                                // far above any churn threshold, so a capped draw
                                                // still reads as churn).

double clamp01(double x) { return std::clamp(x, 0.0, 1.0); }

// Trust the model READS: the -1 sentinel (uninitialized; package A initializes/writes trust) reads
// as the user's platformTrust trait (contract §2/§4).
double effectiveTrust(const HiddenUserState &h) {
    return h.retention.trust >= 0.0 ? h.retention.trust : static_cast<double>(h.platformTrust);
}

} // namespace

double retentionTimeOfDayFactor(double secondsOfDay) {
    // 1 + A*cos(2*pi*(s - peak)/86400): strictly positive (A < 1), 24h-periodic, mean 1, peaking at
    // kPeakSecondsOfDay. Defined for any real s (the caller passes now % 86400).
    constexpr double kTwoPi = 6.283185307179586;
    return 1.0 + kTimeOfDayAmplitude *
                     std::cos(kTwoPi * (secondsOfDay - kPeakSecondsOfDay) / kSecondsPerDay);
}

RetentionModel::RetentionModel(const RetentionConfig &cfg) : config_(cfg) {}

// Core per-day return hazard implied by the current hidden state, WITHOUT the time-of-day factor
// and WITHOUT the config hazard floor (the callers apply those). Rises with last-session welfare,
// habit, trust, and baseline usage.
double RetentionModel::baseDailyHazard(const HiddenUserState &hidden) const {
    const HiddenRetentionState &r = hidden.retention;
    const double usage = std::max(kMinDailyUsage, static_cast<double>(hidden.baselineDailyUsage));
    const double welfareSignal =
        r.lastSessionSatisfaction - kRegretDelayWeight * r.lastSessionRegret;
    const double logMultiplier = kWelfareHazardCoeff * welfareSignal +
                                 kHabitHazardCoeff * r.habitStrength +
                                 kTrustHazardCoeff * (effectiveTrust(hidden) - kTrustPivot);
    return usage * std::exp(logMultiplier);
}

// V2 §4.17: strengthen habit on satisfying sessions, decay it with time away, and remember the
// just-closed session for the next return-delay draw. Package B owns every retention field EXCEPT
// trust (package A). Called at each session end BEFORE nextReturnDelay.
void RetentionModel::onSessionEnd(HiddenUserState &hidden, double sessionMeanSatisfaction,
                                  double sessionMeanRegret, Timestamp exitAt) {
    HiddenRetentionState &r = hidden.retention;

    if (r.sessionsCompleted == 0) {
        // First session under the gate: seed the dynamic habit from the per-user trait (symmetric
        // with trust<-platformTrust; the frozen field's 0.0 default is untouched until this first
        // gate-on touch, so gate-off runs stay byte-identical, D17).
        r.habitStrength = clamp01(static_cast<double>(hidden.habitStrength));
    } else {
        // Decay for the time away since the previous session end — closed-form decay-on-touch
        // (2^(-gap/halfLife)), the same pattern the exposure accumulators use (contract §2). The
        // gap spans the away time plus this short session; away time dominates.
        const double gap =
            std::max(0.0, static_cast<double>(exitAt) - static_cast<double>(r.lastExitAt));
        r.habitStrength *= std::exp2(-gap / kHabitHalfLifeSeconds);
    }

    // Strengthen on net-positive session welfare (toward 1), erode on net-negative (toward 0).
    const double welfare = sessionMeanSatisfaction - kRegretHabitWeight * sessionMeanRegret;
    if (welfare > 0.0) {
        r.habitStrength += kHabitGain * welfare * (1.0 - r.habitStrength);
    } else {
        r.habitStrength += kHabitErodeGain * welfare * r.habitStrength;
    }
    r.habitStrength = clamp01(r.habitStrength);

    // Remember the session for the return-delay draw and the next away-decay.
    r.lastSessionSatisfaction = sessionMeanSatisfaction;
    r.lastSessionRegret = sessionMeanRegret;
    r.lastExitAt = exitAt;
    ++r.sessionsCompleted;
}

// V2 §4.17: draw the delay until the next session. The per-day hazard (baseDailyHazard) is scaled
// by the time-of-day rhythm at `now` and floored at cfg.hazardFloor, then inverted through the
// exponential CDF with ONE uniform draw: delay_days = -ln(1-U) / h. A computed delay strictly
// greater than cfg.churnDelayThresholdSeconds marks the user churned (the caller then schedules no
// ReturnToApp). Draw-count contract: EXACTLY ONE uniform01() draw on the passed "scheduling" stream
// per call — the wholesale replacement of P18's single-gaussian baseline consumer under the gate
// (D19); gate-off runs never reach here, so their scheduling-stream sequence is unchanged.
Timestamp RetentionModel::nextReturnDelay(HiddenUserState &hidden, Timestamp now,
                                          Rng &schedulingRng) {
    const double secondsOfDay =
        std::fmod(static_cast<double>(now), kSecondsPerDay); // now is non-negative logical time.
    const double hazard = std::max(config_.hazardFloor, baseDailyHazard(hidden) *
                                                            retentionTimeOfDayFactor(secondsOfDay));

    const double u = schedulingRng.uniform01(); // THE one scheduling-stream draw for this call.
    // Inverse exponential CDF; 1-u in (0,1], so -ln(1-u) in [0, +inf). hazard >= hazardFloor > 0.
    double delaySeconds = -std::log(1.0 - u) / hazard * kSecondsPerDay;
    delaySeconds = std::min(delaySeconds, kMaxReturnDelaySeconds);
    delaySeconds = std::max(delaySeconds, kMinReturnDelaySeconds);
    const Timestamp delay = static_cast<Timestamp>(std::llround(delaySeconds));

    // Churn: strictly greater than the threshold (contract §1 boundary semantics). Compared on the
    // realized integer delay so the boundary is exact and testable.
    if (static_cast<double>(delay) > config_.churnDelayThresholdSeconds) {
        hidden.retention.churned = true;
    }
    return delay;
}

// Model-implied probability that the NEXT return delay would exceed the churn threshold, for
// metrics only (no draw, no `now`). Uses the time-of-day-neutral hazard (the daily-mean factor is
// 1): for the exponential waiting time, P(delay_days > T_days) = exp(-h * T_days), with h the
// floored core hazard and T_days the threshold in days. Independent of the 60s floor (the threshold
// is far above it).
double RetentionModel::churnProbability(const HiddenUserState &hidden) const {
    const double hazard = std::max(config_.hazardFloor, baseDailyHazard(hidden));
    const double thresholdDays = config_.churnDelayThresholdSeconds / kSecondsPerDay;
    return std::exp(-hazard * thresholdDays);
}

} // namespace rr
