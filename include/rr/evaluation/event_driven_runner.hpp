#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <vector>

#include "rr/core/embedding.hpp"
#include "rr/evaluation/experiment_runner.hpp"
#include "rr/infrastructure/clock.hpp"
#include "rr/infrastructure/config.hpp"
#include "rr/infrastructure/random.hpp"
#include "rr/simulation/event_queue.hpp" // EventType

namespace rr {

// Within-timestamp processing phase (V2 §4.14 / D20 equal-timestamp snapshot). The event runner
// processes each timestamp in ASCENDING phase so that every feed request at a timestamp observes
// the same prior global popularity/trending state, regardless of pop order:
//   0 = arrivals (OpenApp / ReturnToApp) — come online, spawn a RequestFeed at the same time
//   1 = RequestFeed                       — READ global state, rank a feed, spawn a StartReel
//   2 = consumption + the rest (StartReel / FinishReel / Interaction / ExitApp / ...) — WRITE
//   counters
// Because arrivals spawn only phase-1 events, requests spawn only phase-2 events, and consumption
// spawns only FUTURE events (plus at most a same-time ExitApp), the phase cascade is a strict DAG
// within a timestamp: ALL RequestFeeds run before ANY consumption at that timestamp. Exposed so the
// group-ordering unit test can assert this "RequestFeed-first-within-group" contract that package
// C's equal-timestamp tie-break test relies on.
int eventProcessingPhase(EventType type);

// Baseline return-delay draw (V2 §4.12, SchedulingConfig; stream "scheduling", D19). After an exit,
// a user returns after max(60, round(gaussian(mean/max(0.25, baselineDailyUsage), rel*mean)))
// simulated seconds — the mean is scaled DOWN by the hidden baselineDailyUsage trait so heavy users
// return sooner, floored at 60s. Pure given the rng (consumes exactly one gaussian() draw); exposed
// for package A's return-delay bounds/determinism unit test.
Timestamp baselineReturnDelay(Rng &rng, const SchedulingConfig &scheduling,
                              double baselineDailyUsage);

// Phase 18 event-log entry (D20 determinism tripwire): one row per event in the deterministic
// processing order (queued events at pop time; the collapsed FinishReel/Interaction facets right
// after their StartReel). Folded into ExperimentResult::eventMode.eventLogDigest. Package C commits
// the digest golden and reads the digest + count in-process from the result.
struct EventLogEntry {
    Timestamp time = 0;
    uint64_t tieBreaker = 0;
    uint32_t userId = 0;
    uint8_t type = 0; // EventType value (rr::EventType)
    uint64_t seq = 0; // perUserSeq the tie-breaker was derived from
};

// Pure, order-sensitive SplitMix64 fold over the event log (definition + exact mixing documented in
// event_driven_runner.cpp). Same seed + config => identical log => identical digest (the D20 "same
// seed produces identical event sequence" tripwire); permuting user initialization /
// queue-insertion order leaves the log — and therefore this digest — unchanged (order invariance).
// Exposed for package A's digest-fold-stability unit test and package C's golden.
uint64_t foldEventLog(const std::vector<EventLogEntry> &log);

// ================================================================================================
// Phase 19 serving-strategy PURE decision helpers (V2 §4.13). The serving logic lives inside
// EventDrivenRunner::run(), but these three primitives are factored out as free functions so the
// exact decision math is unit-testable in isolation (serving_strategy_test.cpp). Each is documented
// at its definition in event_driven_runner.cpp.
// ================================================================================================

// Threshold refill (task 2): the client fires the next RequestFeed when its remaining prefetched
// inventory has fallen to `threshold` or below AND no feed request is already in flight for the
// user
// (`requestPending` — the outstanding-request guard that prevents double-requesting). threshold 0
// reproduces the Phase 18 refill-when-empty exactly (remaining 0 <= 0), so the default serving path
// is byte-identical.
bool servingShouldRefill(std::size_t remaining, uint32_t threshold, bool requestPending);

// Observable session-intent swing (task 3): the cosine between the user's CURRENT
// session-preference vector and the snapshot taken when the front prefetched item was RANKED.
// Returns 1.0 ("no swing") when either vector is empty or (near) zero-norm — intent is then
// undefined, so there is nothing to invalidate against. This is a purely OBSERVABLE signal (a real
// client could compute it); scheduled drift is captured through the same estimate swing it
// eventually induces (D18: the serving layer never reads hidden state).
double intentSwingCosine(const Embedding &current, const Embedding &rankedTime);

// Invalidation decision (task 3): drop the remaining feed when the intent-swing cosine has fallen
// strictly BELOW the configured threshold. Strict `<` so a threshold of 1.0 fires on any real
// movement yet leaves an unmoved feed (cos == 1.0) intact; preserve-downloaded (invalidation OFF)
// is the default and never calls this.
bool servingShouldInvalidate(double intentCosine, double threshold);

// Adaptation delay after drift (task 4, P10-style but on hidden SATISFACTION and indexed by
// per-user interactions rather than rounds): given a user's per-impression immediate-satisfaction
// sequence and the 0-based interaction index at which their drift fired, return the number of
// post-drift interactions until the trailing-`window` mean satisfaction first recovers to
// `recoverFraction` of the pre-drift trailing-`window` mean. Returns -1 when there is insufficient
// pre-drift history (driftIndex < window), no post-drift history, or recovery never occurs within
// the sequence. Pure; exposed for direct unit testing. Window/fraction are documented named
// constants in the runner.
long adaptationDelayInteractions(const std::vector<double> &satisfactionSeq, std::size_t driftIndex,
                                 std::size_t window, double recoverFraction);

// Phase 18 event-driven runner (V2 TDD 4.11/4.12/4.14; D20 is the binding determinism
// contract): users open, scroll, exit, and return on INDEPENDENT timelines over a
// deterministic (time, pinned-tie-breaker) priority queue (rr::EventQueue), single-threaded
// simulated concurrency. Selected by simulation.scheduler == "event_queue"; the legacy
// round-robin ExperimentRunner is retained permanently as the default and the D17 golden path.
// Requires the full V2 gate stack (content_v2 + latent_reactions + session_dynamics — exits and
// returns schedule against the P16 session machinery; validated at construction, fail-fast
// D10).
//
// Determinism contract (D20, test-enforced by the Phase 18 suite):
// - Same seed + config => identical event sequence (a committed event-log digest is the golden
//   tripwire) and byte-identical metric CSVs.
// - Permuting user initialization/queue-insertion order => identical event log and metrics.
// - Equal-timestamp events: global popularity/trending state is SNAPSHOTTED per unique
//   timestamp; all events at time T read state as of the end of T-epsilon (updates from events
//   at T become visible only to strictly later timestamps). Two equal-time RequestFeeds
//   therefore observe identical global state in either pop order.
//
// PACKAGE-A OWNERSHIP, FROZEN SIGNATURES: package A implements this class (currently a stub
// that throws); the constructor and run() must not change (ExperimentRunner::run() dispatches
// here under the event_queue scheduler). The returned ExperimentResult reuses the existing
// result/writer machinery (all four V2 metric groups, D22) with event-mode session-health
// additions (sessions per simulated day, concurrent-online occupancy, return delays).
class EventDrivenRunner {
  public:
    EventDrivenRunner(ExperimentConfig config, std::filesystem::path outputRoot,
                      BuildProvenance provenance);

    ExperimentResult run();

  private:
    ExperimentConfig config_;
    std::filesystem::path outputRoot_;
    BuildProvenance provenance_;
};

} // namespace rr
