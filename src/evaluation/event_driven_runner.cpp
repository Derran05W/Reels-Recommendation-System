#include "rr/evaluation/event_driven_runner.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <deque>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "rr/core/embedding.hpp"
#include "rr/domain/creator.hpp"
#include "rr/domain/recommendation.hpp"
#include "rr/domain/reel.hpp"
#include "rr/domain/user.hpp"
#include "rr/evaluation/cold_start.hpp"
#include "rr/evaluation/diversity_metrics.hpp"
#include "rr/evaluation/metrics_collector.hpp"
#include "rr/evaluation/oracle.hpp"
#include "rr/evaluation/oracle_satisfaction_recommender.hpp"
#include "rr/evaluation/results_writer.hpp"
#include "rr/evaluation/retrieval_evaluator.hpp"
#include "rr/evaluation/session_health_metrics.hpp"
#include "rr/evaluation/welfare_metrics.hpp"
#include "rr/infrastructure/clock.hpp"
#include "rr/infrastructure/random.hpp"
#include "rr/learning/online_user_state_updater.hpp"
#include "rr/learning/tolerance_estimator.hpp"
#include "rr/recommendation/effective_preference.hpp"
#include "rr/recommendation/recommender.hpp"
#include "rr/recommendation/recommender_factory.hpp"
#include "rr/recommendation/vector_index.hpp"
#include "rr/simulation/dataset_generator.hpp"
#include "rr/simulation/drift_scheduler.hpp"
#include "rr/simulation/event_queue.hpp"
#include "rr/simulation/simulator.hpp"

namespace rr {

namespace {

// One simulated day in seconds (V2 §4.12 "multiple sessions per day"): the event runner reports its
// four §6 metric groups per SIMULATED DAY rather than per round (round_robin has no rounds), so
// per-day CSVs stay meaningful. A named constant, not a config knob (D24).
constexpr Timestamp kSecondsPerSimulatedDay = 86400;

// Baseline return-delay model bounds (V2 §4.12, SchedulingConfig; the tuning surface is config).
// A user never returns in under a minute; baselineDailyUsage below this floor would blow the mean
// up unboundedly, so it is clamped (heavy users still return sooner, light users not arbitrarily
// later). Named constants at their definition per D24.
constexpr Timestamp kMinReturnDelaySeconds = 60;
constexpr double kMinBaselineUsage = 0.25;

// Phase 19 serving-strategy constants (V2 §4.13). No planned experiment varies these, so they stay
// named constants here rather than config knobs (D24, the no-premature-config convention).
//
// Intent-swing refetch latency: when invalidate_on_intent_change drops a stale feed, the fresh
// RequestFeed is scheduled 1 simulated second later (NOT at the current instant). A strictly-future
// timestamp keeps the equal-timestamp snapshot invariant intact — the runner's within-timestamp
// phase cascade (arrivals -> requests -> consumption) forbids spawning a phase-1 RequestFeed into
// the CURRENT timestamp's phase-2 consumption, so the refetch lands in a later timestamp group
// where it reads global state as of the end of this one (D20/§4.14). One second is negligible
// simulated time and models the client noticing the swing and re-fetching on the next tick.
constexpr Timestamp kIntentRefetchDelaySeconds = 1;

// Adaptation delay after drift (task 4): trailing-window mean SATISFACTION recovery, the P10
// adaptation idea moved onto hidden satisfaction and indexed by per-user interactions. The window
// smooths the noisy per-impression satisfaction signal; recovery is the first post-drift
// interaction whose trailing-window mean reaches this fraction of the pre-drift trailing-window
// mean. 0.9 (not P10's 0.95) is chosen because per-USER satisfaction is noisier than P10's
// cohort-mean reward, so a slightly looser bar avoids spurious "never recovered" verdicts; both are
// documented for package C.
constexpr std::size_t kAdaptationWindow = 10;
constexpr double kAdaptationRecoverFraction = 0.9;

// yyyymmddThhmmss experiment-id stamp (D12), identical to the legacy runner's helper. Wall-clock is
// permitted here: it only labels the output directory for provenance and never feeds the simulation
// (D9). Duplicated (not shared) because the legacy copy is a file-local anonymous-namespace helper.
std::string experimentTimestamp() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y%m%dT%H%M%S", &tm);
    return buf;
}

} // namespace

// Event-log digest (D20 "same seed produces identical event sequence" tripwire). A pure,
// order-sensitive fold: start from the FNV-1a 64-bit offset basis, then for each entry in
// processing order SplitMix64-mix its four identity components (time, tie-breaker, packed
// userId|type, perUserSeq) into the accumulator one at a time. SplitMix64's finalizer avalanches
// every input bit, so any reordering or field change flips the digest; it depends ONLY on the
// event stream (not on queue-insertion order), so the order-invariance property carries straight
// through. Implemented here (not std::hash / a library) so the value is pinned across
// platforms/compilers exactly like the tie-breaker and cohort hashes (D8).
uint64_t foldEventLog(const std::vector<EventLogEntry> &log) {
    uint64_t acc = 14695981039346656037ULL; // FNV-1a 64-bit offset basis (seed constant)
    for (const EventLogEntry &e : log) {
        acc = splitmix64(acc ^ e.time);
        acc = splitmix64(acc ^ e.tieBreaker);
        acc = splitmix64(acc ^ ((static_cast<uint64_t>(e.userId) << 8) | e.type));
        acc = splitmix64(acc ^ e.seq);
    }
    return acc;
}

// ================================================================================================
// Phase 19 serving-strategy pure decision helpers (V2 §4.13). Declared in the header; factored out
// so the exact decision math is unit-testable in isolation (serving_strategy_test.cpp).
// ================================================================================================

bool servingShouldRefill(std::size_t remaining, uint32_t threshold, bool requestPending) {
    // Fire the next feed request once the remaining prefetched inventory has fallen to `threshold`
    // or below, unless a request is already in flight (the outstanding-request guard prevents
    // double-requesting the same user). threshold 0 => fires only at remaining 0 (the P18
    // refill-when-empty, byte-identical default).
    return !requestPending && remaining <= static_cast<std::size_t>(threshold);
}

double intentSwingCosine(const Embedding &current, const Embedding &rankedTime) {
    // Cosine between the CURRENT session-preference vector and the snapshot taken when the front
    // prefetched item was ranked. Undefined (empty / different length / (near) zero norm on either
    // side) => 1.0 "no swing": with no measurable intent there is nothing to invalidate against.
    if (current.empty() || current.size() != rankedTime.size()) {
        return 1.0;
    }
    double d = 0.0;
    double nc = 0.0;
    double nr = 0.0;
    for (std::size_t i = 0; i < current.size(); ++i) {
        const double a = static_cast<double>(current[i]);
        const double b = static_cast<double>(rankedTime[i]);
        d += a * b;
        nc += a * a;
        nr += b * b;
    }
    constexpr double kNormEps = 1e-12;
    if (nc < kNormEps || nr < kNormEps) {
        return 1.0;
    }
    return d / std::sqrt(nc * nr);
}

bool servingShouldInvalidate(double intentCosine, double threshold) {
    // Drop the remaining feed once intent has swung strictly BELOW the threshold. Strict `<` leaves
    // an unmoved feed (cos == 1.0) intact even at threshold 1.0.
    return intentCosine < threshold;
}

long adaptationDelayInteractions(const std::vector<double> &satisfactionSeq, std::size_t driftIndex,
                                 std::size_t window, double recoverFraction) {
    // Pre-drift baseline = mean of the `window` satisfactions ending just before driftIndex; needs
    // driftIndex >= window of history. Recovery = the first interaction k >= driftIndex whose
    // trailing-`window` mean (satisfactions (k-window+1 .. k)) reaches recoverFraction * baseline;
    // the returned delay is (k - driftIndex + 1) — the count of post-drift interactions consumed
    // through recovery. -1 when history is too short, there is no post-drift interaction, or the
    // bar is never re-crossed within the sequence.
    if (window == 0 || driftIndex < window || driftIndex >= satisfactionSeq.size()) {
        return -1;
    }
    double preSum = 0.0;
    for (std::size_t i = driftIndex - window; i < driftIndex; ++i) {
        preSum += satisfactionSeq[i];
    }
    const double preMean = preSum / static_cast<double>(window);
    const double bar = recoverFraction * preMean;
    for (std::size_t k = driftIndex; k < satisfactionSeq.size(); ++k) {
        // Trailing-window mean ending at k. k >= driftIndex >= window, so k-window+1 >= 1 > 0.
        double sum = 0.0;
        for (std::size_t i = k + 1 - window; i <= k; ++i) {
            sum += satisfactionSeq[i];
        }
        if (sum / static_cast<double>(window) >= bar) {
            return static_cast<long>(k - driftIndex + 1);
        }
    }
    return -1;
}

int eventProcessingPhase(EventType type) {
    switch (type) {
    case EventType::OpenApp:
    case EventType::ReturnToApp:
        return 0; // arrivals: come online, spawn a RequestFeed at the same time
    case EventType::RequestFeed:
        return 1; // read global state, rank a feed, spawn a StartReel
    case EventType::StartReel:
    case EventType::FinishReel:
    case EventType::Interaction:
    case EventType::ExitApp:
    case EventType::PreferenceDrift:
    case EventType::ReelPublished:
        return 2; // consumption WRITES counters, plus the log/exit/reserved events
    }
    return 2;
}

Timestamp baselineReturnDelay(Rng &rng, const SchedulingConfig &scheduling,
                              double baselineDailyUsage) {
    // Mean scaled DOWN by baselineDailyUsage (heavy users return sooner), clamped so a near-zero
    // usage cannot blow the mean up unboundedly. One gaussian() draw, floored at the minimum, and
    // rounded to an integer Timestamp (D9). See the header for the full formula.
    const double usage = std::max(kMinBaselineUsage, baselineDailyUsage);
    const double meanScaled = scheduling.returnDelayMeanSeconds / usage;
    const double sd = scheduling.returnDelaySpreadRel * meanScaled;
    const double draw = meanScaled + rng.gaussian() * sd;
    return static_cast<Timestamp>(
        std::max(static_cast<double>(kMinReturnDelaySeconds), std::round(draw)));
}

EventDrivenRunner::EventDrivenRunner(ExperimentConfig config, std::filesystem::path outputRoot,
                                     BuildProvenance provenance)
    : config_(std::move(config)), outputRoot_(std::move(outputRoot)),
      provenance_(std::move(provenance)) {}

// ================================================================================================
// Phase 18 event-driven runner (V2 §4.11/4.12/4.14; D20). Users open, scroll, exit, and return on
// INDEPENDENT timelines over a deterministic (time, pinned-tie-breaker) priority queue. The legacy
// round-robin ExperimentRunner is UNTOUCHED and remains the default/golden path (D17/D20); this
// runner mirrors its dataset/recommender/updater/estimator/metric construction so the shared
// ResultsWriter serializes the four §6 groups (D22) unchanged, and ADDS the event-mode-only
// session-health numbers (sessions/simulated-day, concurrent-online occupancy, return delays).
//
// KEY DESIGN DECISIONS (documented at their use below):
//  - Event-clock bridge: stepV2 owns clock advance in round-robin; here the queue owns time, so the
//    runner Simulator::syncClock(eventTime) before each stepV2 (the ONLY simulator addition).
//  - Consumption collapse: BehaviourModelV2 samples watch/exit atomically inside stepV2, so
//    StartReel does the whole impression (metrics/welfare/session/updater/estimator + the exit
//    draw) and emits FinishReel/Interaction as LOG-ONLY facets — the event log still carries the
//    full §4.11 vocabulary for package C's digest.
//  - Equal-timestamp snapshot (D20/§4.14): within one timestamp, ALL RequestFeeds run before ANY
//    consumption, so equal-time feed requests observe identical global popularity/trending state
//    regardless of pop order (see processTimestampGroup).
//  - Horizon: events beyond horizonSeconds are DROPPED at schedule time; per-user interaction
//    counts are an OUTCOME (interactions_per_user is ignored in event mode).
// ================================================================================================
ExperimentResult EventDrivenRunner::run() {
    Stopwatch wall; // total wall time (D9: provenance only, confined to summary.timing).
    const uint64_t seed = config_.simulation.seed;

    // Event mode REQUIRES the full V2 gate stack (validated at config load: event_queue =>
    // session_dynamics => latent_reactions => content_v2). Re-checked here for fail-fast safety on
    // directly-constructed configs that bypass from_json (D10). These locals mirror the legacy
    // runner.
    if (!(config_.realism.contentV2 && config_.realism.latentReactions &&
          config_.realism.sessionDynamics)) {
        throw std::invalid_argument(
            "EventDrivenRunner requires realism.content_v2 + latent_reactions + session_dynamics");
    }
    const bool latentReactions = config_.realism.latentReactions;
    const bool sessionDynamics = config_.realism.sessionDynamics;
    const bool learningEnabled = config_.learning.enabled;
    const bool personalizedDiversity = config_.realism.personalizedDiversity;

    // 1. Dataset + FROZEN cold-start prior — identical to the legacy runner (TDD 11.1). Mid-run
    //    injection is BLOCKED under content_v2 (the P13 config guard) and event mode requires the
    //    full stack, so ReelPublished is reserved/no-op this phase (documented at its dispatch).
    GeneratedDataset ds = generateDataset(config_.simulation, config_.realism, seed);
    const Embedding coldStartPrior = globalAveragePreference(ds.hiddenStates);
    applyColdStart(ds.users, coldStartPrior);

    // Stream-neutral post-step appliers, constructed exactly as the legacy runner.
    const OnlineUserStateUpdater updater(ds.reels, config_.learning, config_.realism.contentV2);
    const ToleranceEstimator toleranceEstimator(ds.reels, config_.diversity);
    // Scheduled drift: kept on the EXACT legacy mechanism — maybeApply keyed on
    // user.totalInteractions BEFORE each step (see the StartReel handler). No PreferenceDrift queue
    // events are emitted this phase; the interaction-count keying is preserved verbatim (the enum
    // stays for P20). Construction validates the drift config (fail-fast, D10); unconfigured =>
    // maybeApply is a guaranteed no-op. The round-based AdaptationReport is NOT emitted in event
    // mode (rounds are days here, not feedSize buckets), so drift still moves hidden preferences
    // but adaptation.configured stays false (documented at assembly).
    const DriftScheduler drift(config_.drift, ds.topics);

    // 2. Simulator (P16 session-dynamics ctor — the gate stack is validated at load), recommender,
    //    oracle, retrieval, and the NEW "scheduling" stream (D19). forkRng derives each stream from
    //    (seed, name) independently of fork ORDER, so adding "scheduling" never perturbs the
    //    behaviour/satisfaction/session-exit/external-interruption/recommender/oracle/retrieval
    //    streams — the same names the legacy runner forks keep their per-stream determinism.
    Simulator sim(config_.behaviour, config_.behaviourV2, config_.sessionDynamics, config_.reward,
                  forkRng(seed, "behaviour"), forkRng(seed, "satisfaction"),
                  forkRng(seed, "session-exit"), forkRng(seed, "external-interruption"),
                  config_.learning.recentWindow, config_.ranking.trendingHalfLifeSeconds);
    RecommenderDeps deps{ds.reels, ds.users, config_};
    // Oracle-satisfaction arm reads hidden state, so it is built here under the evaluation
    // carve-out (D18); everything else goes through the factory — mirrors the legacy special-case
    // exactly.
    std::unique_ptr<Recommender> recommender =
        config_.algorithm == RecommendationAlgorithm::OracleSatisfaction
            ? std::make_unique<OracleSatisfactionRecommender>(
                  config_, ds.reels, ds.users, ds.creators, ds.hiddenStates, ds.hiddenReelStates,
                  forkRng(seed, "recommender"))
            : makeRecommender(config_.algorithm, deps, forkRng(seed, "recommender"));
    Rng oracleRng = forkRng(seed, "oracle");
    Rng retrievalRng = forkRng(seed, "retrieval");
    Rng schedulingRng = forkRng(seed, "scheduling"); // NEW (D19): open/return draws only.

    const VectorIndex *annIndex = recommender->retrievalIndex();
    const bool retrievalApplicable = annIndex != nullptr;
    std::optional<RetrievalEvaluator> retrievalEval;
    if (retrievalApplicable && config_.evaluation.retrievalSampleRate > 0.0) {
        retrievalEval.emplace(config_.simulation.dimensions, ds.reels);
    }

    // 3. Horizon + per-day metric buckets. horizon floors horizonSeconds to an integer Timestamp
    //    (D9). numDays = the number of 86400s buckets an event at t<=horizon can land in.
    const Timestamp horizon = static_cast<Timestamp>(config_.simulation.horizonSeconds);
    const std::size_t numDays = static_cast<std::size_t>(horizon / kSecondsPerSimulatedDay) + 1;
    const std::size_t feedSize = config_.recommendation.feedSize;
    const std::size_t userCount = ds.users.size();

    // Phase 19 serving strategy (V2 §4.13). `prefetchDepth` is how many ranked reels each
    // RequestFeed lands in the deque: prefetch_depth 0 resolves to feedSize (the P18 baseline,
    // byte-identical); a depth > feedSize simply models a DEEPER client cache (the request just
    // asks the orchestrator for more items — candidateLimit / the O(catalog) source scans are
    // unchanged, so the ranking COST per request barely moves; the cost axis is the NUMBER of
    // requests, which deeper prefetch reduces). Refill threshold and invalidation are read straight
    // off the config.
    const ServingConfig &serving = config_.serving;
    const std::size_t prefetchDepth =
        serving.prefetchDepth != 0 ? static_cast<std::size_t>(serving.prefetchDepth) : feedSize;

    // Metric collectors, sized to numDays and constructed exactly as the legacy runner's per-round
    // collectors (so ResultsWriter serializes the four §6 groups unchanged, D22).
    MetricsCollector metrics;
    struct RegretAcc {
        std::size_t sampled = 0;
        double sum = 0.0;
    };
    std::vector<RegretAcc> regretByDay(numDays);
    struct RetrievalAcc {
        std::size_t samples = 0;
        double recall10Sum = 0.0;
        double recall50Sum = 0.0;
        double distErrorSum = 0.0;
    };
    std::vector<RetrievalAcc> retrievalByDay(numDays);
    WelfareMetrics welfareMetrics(numDays, config_.realism.archetypes.size());
    SessionHealthMetrics sessionHealth(numDays, config_.sessionDynamics);
    DiversityAccumulator diversityByDay(numDays);
    std::vector<double> alignmentByDay(numDays, 0.0);

    std::vector<double> latencies;
    std::vector<double> retrievalLatencies;
    std::vector<double> rankingLatencies;
    std::vector<double> rerankingLatencies;

    std::size_t requestCount = 0;
    std::size_t impressionCount = 0;

    // 4. Per-user timeline state (V2 §4.11) + runner-side feed context. `timelines[u]` owns the
    //    online flag, the prefetched-feed deque, and the monotone per-user seq feeding the
    //    tie-breaker. Each prefetched item carries a PARALLEL stamp (per-item, in `feedStamps[u]`,
    //    kept in lockstep with `timelines[u].prefetchedFeed`) so the frozen UserTimeline surface is
    //    not widened. Per-item (not per-user) is REQUIRED once preserve-downloaded threshold
    //    refills APPEND a newer feed behind an older surviving one: the two batches were ranked at
    //    different times, so each item must remember its own request id/serve-time and its own
    //    staleness base.
    struct PrefetchStamp {
        uint64_t requestId = 0; // the RequestFeed that produced this item (== requestCount then)
        Timestamp requestTime =
            0; // that request's serve time (feeds StepV2Inputs.requestTimestamp)
        uint64_t rankApplyCount = 0; // applyCount[u] when this item was ranked (the staleness base)
        Embedding rankSessionPref;   // sessionPreference snapshot at ranking (ONLY populated when
                                     // invalidate_on_intent_change is on; empty otherwise, so the
                                     // preserve-downloaded default pays nothing)
    };
    std::vector<UserTimeline> timelines(userCount);
    std::vector<std::deque<PrefetchStamp>> feedStamps(userCount);
    // Per-user updater-apply counter (the staleness clock): bumped once per updater.apply.
    // Staleness of a served impression = applyCount[u] at serving − the front stamp's
    // rankApplyCount. With learning off there are no applies, so staleness is 0 everywhere (nothing
    // is stale if the model never moves).
    std::vector<uint64_t> applyCount(userCount, 0);
    // Outstanding-request guard (avoid double-requests, task 2): true from when a RequestFeed is
    // scheduled for a user until it is dispatched. A threshold refill only fires when this is
    // false.
    std::vector<bool> outstandingRequest(userCount, false);

    // Event-mode-only accumulators: the deterministic event log (folded into the digest), the
    // baseline return delays, and the concurrent-online occupancy samples.
    std::vector<EventLogEntry> eventLog;
    std::vector<double> returnDelays;
    double onlineFractionSum = 0.0;
    std::size_t occupancySamples = 0;
    std::size_t onlineNow = 0; // running count of online users (O(1) occupancy).

    // Phase 19 serving/cost/staleness instrumentation (V2 §4.13, D22). Run-level totals plus a
    // per-simulated-day breakdown for serving_metrics.csv (package C's per-day view).
    // Fresh-vs-stale satisfaction is bucketed per day so satisfaction-lost can compare each stale
    // impression against that day's fresh-serving mean.
    std::uint64_t rankingComputations = 0; // Σ candidatesRanked over all feed requests (the COST)
    std::uint64_t stalenessSum = 0;        // Σ per-impression staleness (for meanStaleness)
    std::size_t staleImpressionCount = 0;  // impressions with staleness > 0
    std::size_t feedInvalidationCount = 0; // intent-swing invalidations
    struct ServingDayAcc {
        std::size_t feedRequests = 0;
        std::uint64_t rankingComputations = 0;
        std::size_t impressions = 0;
        std::size_t staleImpressions = 0;
        std::uint64_t stalenessSum = 0;
        double freshSatSum = 0.0; // satisfaction of THIS day's staleness-0 impressions
        std::size_t freshSatCount = 0;
        std::vector<double> staleSatisfactions; // per stale impression, for the clamped-gap sum
    };
    std::vector<ServingDayAcc> servingByDay(numDays);
    // Per-user immediate-satisfaction sequence, collected ONLY when drift is configured (adaptation
    // delay is drift-only). Kept empty otherwise so the default/no-drift path allocates nothing.
    const bool driftConfigured = drift.configured();
    std::vector<std::vector<double>> userSatSeq(driftConfigured ? userCount : 0);

    EventQueue queue;

    // --- helpers (lambdas capturing the run state) --------------------------------------------

    // Schedule one queued event for `uid` at `time`. HORIZON: events beyond the horizon are DROPPED
    // at schedule time (documented choice) — the pop-time check below is then a pure defensive
    // assert. The per-user seq is bumped on every SCHEDULED event (the scaffold's UserTimeline
    // contract), pinning the tie-breaker so two events for one user can never tie.
    auto scheduleEvent = [&](EventType type, UserId uid, Timestamp time) {
        if (time > horizon) {
            return;
        }
        const uint64_t seq = timelines[uid.value].nextSeq++;
        SimulationEvent ev;
        ev.time = time;
        ev.deterministicTieBreaker = eventTieBreaker(uid, type, seq);
        ev.userId = uid;
        ev.type = type;
        ev.perUserSeq = seq;
        queue.push(ev);
    };

    // Schedule a RequestFeed AND raise the outstanding-request flag (task 2's double-request
    // guard). Every RequestFeed in the runner goes through here so the invariant
    // "outstandingRequest[u] == a RequestFeed is scheduled-but-not-yet-dispatched" holds globally;
    // the RequestFeed handler lowers it on dispatch. Emits exactly the same queued event as
    // scheduleEvent(RequestFeed, ...), so the event log — and the digest — is unchanged for any
    // given schedule sequence.
    auto scheduleRequestFeed = [&](UserId uid, Timestamp time) {
        outstandingRequest[uid.value] = true;
        scheduleEvent(EventType::RequestFeed, uid, time);
    };

    // Append one row to the event log (queued events at pop time; the collapsed facets from the
    // consumption handler). The log order IS the deterministic processing order.
    auto pushLog = [&](Timestamp time, uint64_t tieBreaker, UserId uid, EventType type,
                       uint64_t seq) {
        eventLog.push_back(
            EventLogEntry{time, tieBreaker, uid.value, static_cast<uint8_t>(type), seq});
    };

    // Estimate<->hidden alignment snapshot (TDD 18.2 evaluation carve-out): mean over all users of
    // cos(estimatedPreference, hiddenPreference) — both unit-length, so dot == cosine. Snapshotted
    // at each simulated-day boundary below to fill the per-day learning curve.
    auto computeAlignment = [&]() -> double {
        if (userCount == 0) {
            return 0.0;
        }
        double sum = 0.0;
        for (std::size_t u = 0; u < userCount; ++u) {
            sum += dot(ds.users[u].estimatedPreference, ds.hiddenStates[u].hiddenPreference);
        }
        return sum / static_cast<double>(userCount);
    };

    // --- the single event dispatcher (one impression's worth of work per event) ---------------
    auto dispatch = [&](const SimulationEvent &e, std::size_t dayIdx) {
        pushLog(e.time, e.deterministicTieBreaker, e.userId, e.type, e.perUserSeq);
        const uint32_t u = e.userId.value;
        UserTimeline &tl = timelines[u];
        User &user = ds.users[u];

        switch (e.type) {
        // OpenApp / ReturnToApp: the user comes online and immediately requests a feed. A new
        // session begins implicitly on the first StartReel — the P16 Simulator starts/away-decays
        // the hidden session inside stepV2 (impressionsThisSession == 0 => startSession), so there
        // is nothing to open here beyond marking the timeline online and firing the feed request.
        case EventType::OpenApp:
        case EventType::ReturnToApp: {
            if (!tl.online) {
                tl.online = true;
                ++onlineNow;
            }
            scheduleRequestFeed(e.userId, e.time); // fresh session at this instant
            break;
        }

        // RequestFeed: build the request EXACTLY like the legacy loop (requestTime = event time),
        // rank a feed of `prefetchDepth` reels (Phase 19; feedSize by default), measure it
        // (diversity/oracle/retrieval on the SAME streams as legacy), and land it in the deque.
        // This handler runs FIRST-within-timestamp (phase 1), so it always reads global state as of
        // the end of the prior timestamp (the equal-timestamp snapshot, D20/§4.14).
        case EventType::RequestFeed: {
            outstandingRequest[u] = false; // this pending request is now being served (task 2)
            const bool wasEmpty = tl.prefetchedFeed.empty();

            RecommendationRequest req{};
            req.userId = user.id;
            req.sessionId = user.recentInteractions.empty()
                                ? SessionId{0}
                                : user.recentInteractions.back().sessionId;
            req.feedSize = prefetchDepth; // Phase 19: depth reels per request (== feedSize default)
            req.candidateLimit = config_.recommendation.vectorCandidates; // UNCHANGED (task 1)
            req.enableExploration = config_.exploration.enabled;
            req.enableDiversity = config_.diversity.enabled;
            req.requestTime = e.time;

            Stopwatch sw; // time ONLY recommend() (D9 wall-clock carve-out).
            RecommendationResponse resp = recommender->recommend(req);
            latencies.push_back(sw.elapsedMs());
            retrievalLatencies.push_back(resp.retrievalLatencyMs);
            rankingLatencies.push_back(resp.rankingLatencyMs);
            rerankingLatencies.push_back(resp.rerankingLatencyMs);
            ++requestCount;

            // Cost instrumentation (task 4): candidatesRanked is the pool the orchestrator actually
            // scored for this request. Summed across requests it is the "ranking computations" cost
            // axis — deeper prefetch => fewer requests => lower total (the freshness-vs-cost
            // trade).
            rankingComputations += static_cast<std::uint64_t>(resp.candidatesRanked);
            servingByDay[dayIdx].feedRequests += 1;
            servingByDay[dayIdx].rankingComputations +=
                static_cast<std::uint64_t>(resp.candidatesRanked);

            std::vector<ReelId> feedReelIds;
            feedReelIds.reserve(resp.reels.size());
            for (const RankedReel &ranked : resp.reels) {
                feedReelIds.push_back(ranked.reelId);
            }

            // Per-feed diversity, measured on the pre-consumption seen-set (as of presentation).
            diversityByDay.add(dayIdx, feedReelIds, ds.reels, user.seenReels);

            // Oracle regret on a Bernoulli(oracleSampleRate) subset — same "oracle" stream/order
            // semantics as legacy (one draw per request keeps the stream aligned). seenReels is the
            // pre-feed snapshot the oracle scores against. `feedSize` (not depth) stays the regret
            // top-k so the oracle baseline is depth-invariant (the DEFAULT path is byte-identical:
            // depth == feedSize there).
            if (oracleRng.bernoulli(config_.evaluation.oracleSampleRate)) {
                const OracleResult oracle =
                    computeOracleRegret(ds.hiddenStates[u].hiddenPreference, ds.reels,
                                        user.seenReels, feedReelIds, feedSize);
                regretByDay[dayIdx].sampled += 1;
                regretByDay[dayIdx].sum += oracle.regret;
            }

            // Live retrieval sampling (TDD 18.1) — one draw per request on the "retrieval" stream.
            if (retrievalRng.bernoulli(config_.evaluation.retrievalSampleRate) &&
                retrievalEval.has_value()) {
                const RetrievalSample rs =
                    retrievalEval->evaluate(*annIndex, effectivePreference(user));
                RetrievalAcc &acc = retrievalByDay[dayIdx];
                acc.samples += 1;
                acc.recall10Sum += rs.recallAt10;
                acc.recall50Sum += rs.recallAt50;
                acc.distErrorSum += rs.distanceError;
            }

            // Land the ranked feed in the deque. PRESERVE-DOWNLOADED (V2 §4.13, the default): a
            // threshold refill APPENDS behind whatever survives, so already-downloaded reels are
            // kept (the realistic client cache); the surviving items keep their OWN older stamps.
            // When the deque was empty (session start, depth-1 refill, or a just-invalidated feed)
            // append == assign, which is the byte-identical P18 behaviour. Each item gets a
            // per-item stamp; the ranking-time sessionPreference snapshot is taken ONLY when
            // invalidation is on.
            const std::uint64_t rankApply = applyCount[u];
            for (const RankedReel &ranked : resp.reels) {
                tl.prefetchedFeed.push_back(ranked);
                PrefetchStamp st;
                st.requestId = static_cast<uint64_t>(requestCount);
                st.requestTime = e.time;
                st.rankApplyCount = rankApply;
                if (serving.invalidateOnIntentChange) {
                    st.rankSessionPref = user.sessionPreference;
                }
                feedStamps[u].push_back(std::move(st));
            }
            // Only OPEN a consumption chain if none is running (the deque was empty). A threshold
            // refill that appended behind a live chain must NOT schedule a second StartReel — the
            // in-flight chain will reach the appended reels — else the user would double-consume.
            if (wasEmpty && !tl.prefetchedFeed.empty()) {
                scheduleEvent(EventType::StartReel, e.userId, e.time);
            }
            break;
        }

        // StartReel: the collapsed consumption event (StartReel+FinishReel+Interaction). Runs the
        // per-impression block in the SAME order as the legacy loop: drift.maybeApply BEFORE the
        // step, updater + estimator AFTER, welfare/session/metrics collection, then schedules the
        // next event off the impression's outcome.
        case EventType::StartReel: {
            if (tl.prefetchedFeed.empty()) {
                // Defensive: StartReel is only scheduled with a non-empty feed; if somehow empty,
                // request a refill rather than stranding the user.
                feedStamps[u].clear();
                scheduleRequestFeed(e.userId, e.time);
                break;
            }

            // Invalidation on major session-intent change (task 3, gated). BEFORE consuming,
            // compare the user's CURRENT session-preference to the snapshot from when the front
            // item was ranked; if intent has swung strictly below the cosine threshold, the
            // downloaded feed no longer matches what the user wants, so DROP the whole remainder
            // and re-fetch. This is the OBSERVABLE session-intent swing (a real client could
            // compute it); scheduled drift is captured through the same swing it induces in the
            // estimate (D18 — the serving layer never reads hidden state). Preserve-downloaded
            // (invalidation OFF) is the default and skips this entirely. The refetch lands +1s
            // later so it stays a strictly-future timestamp (the equal-timestamp phase cascade
            // forbids a phase-1 request inside this phase-2 consumption; see
            // kIntentRefetchDelaySeconds).
            if (serving.invalidateOnIntentChange) {
                const double swing = intentSwingCosine(user.sessionPreference,
                                                       feedStamps[u].front().rankSessionPref);
                if (servingShouldInvalidate(swing, serving.intentSwingCosineThreshold)) {
                    tl.prefetchedFeed.clear();
                    feedStamps[u].clear();
                    ++feedInvalidationCount;
                    if (!outstandingRequest[u]) {
                        scheduleRequestFeed(e.userId, e.time + kIntentRefetchDelaySeconds);
                    }
                    break; // the stale impression is not consumed; the fresh feed will serve
                }
            }

            const RankedReel ranked = tl.prefetchedFeed.front();
            const PrefetchStamp stamp = feedStamps[u].front();
            tl.prefetchedFeed.pop_front();
            feedStamps[u].pop_front();
            // Staleness (task 4): updater applies on this user since the feed was ranked, measured
            // BEFORE this impression's own apply (its own apply must not count toward its
            // staleness).
            const std::uint64_t staleness = applyCount[u] - stamp.rankApplyCount;
            const ReelId reelId = ranked.reelId;
            Reel &reel = ds.reels[reelId.value];
            const Creator &creator = ds.creators[reel.creatorId.value];

            // Scheduled drift keyed on totalInteractions, BEFORE the step — the EXACT legacy
            // mechanism (Simulator increments totalInteractions by 1 per impression, so each event
            // fires once per cohort user). rng/clock-free => inert & stream-neutral when
            // unconfigured.
            drift.maybeApply(ds.hiddenStates[u], static_cast<uint32_t>(user.totalInteractions));

            // Event-clock bridge: align the logical clock to this impression's start time so
            // startSession's away-gap and the start/finish timestamps track event time.
            sim.syncClock(e.time);

            StepV2Inputs v2;
            v2.hiddenReel = &ds.hiddenReelStates[reelId.value];
            v2.positionInFeed = static_cast<uint32_t>(ranked.rank);
            v2.requestId = stamp.requestId;
            v2.requestTimestamp =
                stamp.requestTime; // this item's feed serve time (<= startTimestamp)
            // Provenance election, identical rule to the legacy loop: Exploration wins if present,
            // else the first (highest-priority) source; empty => the benign VectorHNSW default.
            v2.fromExploration = false;
            v2.sourceProvenance =
                ranked.sources.empty() ? CandidateSource::VectorHNSW : ranked.sources[0];
            for (const CandidateSource s : ranked.sources) {
                if (s == CandidateSource::Exploration) {
                    v2.fromExploration = true;
                    v2.sourceProvenance = CandidateSource::Exploration;
                }
            }

            LatentReaction latent;
            SessionRecord closedRec;
            const StepResult step =
                sim.stepV2(user, ds.hiddenStates[u], reel, creator, v2, latent, &closedRec);

            // Welfare-group feed (evaluation carve-out, D18): hidden latent + observable watch +
            // hidden archetype index. None reach a recommender-visible surface.
            WelfareImpression wi;
            wi.immediateSatisfaction = latent.immediateSatisfaction;
            wi.regret = latent.regret;
            wi.watchSeconds = step.outcome.watchSeconds;
            wi.archetypeIndex = ds.hiddenReelStates[reelId.value].archetypeIndex;
            welfareMetrics.add(dayIdx, wi);

            if (learningEnabled) {
                updater.apply(user, reel, step.event);
                ++applyCount[u]; // the staleness clock: one apply advances this user's serving
                                 // state
            }
            if (personalizedDiversity) {
                toleranceEstimator.apply(user, reel, step.event);
            }

            // Serving/staleness instrumentation (task 4). staleness > 0 means the feed's ranking is
            // out of date by that many updater applies. satisfaction-lost buckets each impression's
            // hidden immediate satisfaction into per-day FRESH (staleness 0) vs STALE pools; the
            // clamped fresh-minus-stale gap is summed at reduction. Adaptation delay needs the
            // per-user satisfaction sequence, collected only under configured drift.
            stalenessSum += staleness;
            ServingDayAcc &sd = servingByDay[dayIdx];
            sd.impressions += 1;
            sd.stalenessSum += staleness;
            if (staleness > 0) {
                ++staleImpressionCount;
                sd.staleImpressions += 1;
                sd.staleSatisfactions.push_back(latent.immediateSatisfaction);
            } else {
                sd.freshSatSum += latent.immediateSatisfaction;
                sd.freshSatCount += 1;
            }
            if (driftConfigured) {
                userSatSeq[u].push_back(latent.immediateSatisfaction);
            }

            ImpressionSample s;
            s.watchRatio = step.event.watchRatio;
            s.watchSeconds = step.event.watchSeconds;
            s.instantSkip = step.outcome.instantSkip;
            s.completed = step.outcome.completed;
            s.liked = step.outcome.liked;
            s.shared = step.outcome.shared;
            s.followed = step.outcome.followed;
            s.commented = step.outcome.commented;
            s.saved = step.outcome.saved;
            s.profileVisited = step.outcome.profileVisited;
            s.reward = step.event.reward;
            s.trueAffinity = dot(ds.hiddenStates[u].hiddenPreference, reel.embedding);
            s.userId = user.id.value;
            s.sessionId = step.event.sessionId.value;
            metrics.add(dayIdx, s);
            ++impressionCount;

            // LOG-ONLY facets: FinishReel then Interaction at the SAME timestamp, sharing this
            // StartReel's perUserSeq (the EventType distinguishes their tie-breakers). The handling
            // is atomic, but the event stream carries the full §4.11 vocabulary for package C's
            // digest — nextSeq stays the queued-event counter the scaffold documents.
            pushLog(e.time, eventTieBreaker(e.userId, EventType::FinishReel, e.perUserSeq),
                    e.userId, EventType::FinishReel, e.perUserSeq);
            pushLog(e.time, eventTieBreaker(e.userId, EventType::Interaction, e.perUserSeq),
                    e.userId, EventType::Interaction, e.perUserSeq);

            // Follow-on. finishTs = start + round(watch); the next start is at sim.now() (==
            // finishTs + the browse overhead stepV2 already advanced — dwell = watch + browse), so
            // the clock strictly advances and integer Timestamps are used throughout (D9).
            const Timestamp finishTs = step.event.finishTimestamp;
            const Timestamp nextTs = sim.now();
            if (step.event.observedExitAfterImpression) {
                // Collect the closed session INLINE, exactly like the legacy runner's consume loop
                // (sessionHealth.add right where the exit fires), attributed to the impression's
                // day — NOT deferred to the ExitApp handler. Deferring would LOSE the record
                // whenever finishTs spills past the horizon (the ExitApp event would be dropped);
                // collecting here records every fired exit and keeps the day attribution identical
                // to legacy.
                sessionHealth.add(dayIdx, closedRec);
                scheduleEvent(EventType::ExitApp, e.userId, finishTs);
            } else {
                // Phase 19 unified refill/continue. Threshold refill (task 2): once the remaining
                // prefetched inventory has fallen to refill_threshold or below (and no request is
                // in flight), fetch the next feed at nextTs — the browse-completed swipe time,
                // which is strictly future (dwell > 0) so it never spawns a phase-1 request inside
                // this phase-2 consumption, and is the moment the client would act on a low buffer.
                // refill_threshold 0 makes this fire exactly when the deque empties — the
                // byte-identical P18 depth-1 refill (remaining 0 <= 0), scheduled at the same
                // nextTs.
                const std::size_t remaining = tl.prefetchedFeed.size();
                if (servingShouldRefill(remaining, serving.refillThreshold,
                                        outstandingRequest[u])) {
                    scheduleRequestFeed(e.userId, nextTs);
                }
                // Keep consuming the downloaded feed while anything remains. When it is empty the
                // refill just scheduled (or one already in flight) opens the next chain on landing,
                // so no StartReel is scheduled here.
                if (remaining > 0) {
                    scheduleEvent(EventType::StartReel, e.userId, nextTs);
                }
            }
            break;
        }

        // ExitApp: the user goes offline and schedules the baseline return. The closed
        // SessionRecord was already collected inline in the consumption handler (see StartReel), so
        // this handler owns only the offline transition + the return draw. If finishTs spilled past
        // the horizon this event is dropped — correctly leaving the user "online" for the window's
        // tail (they were still watching at the horizon) and scheduling no (beyond-horizon) return.
        case EventType::ExitApp: {
            if (tl.online) {
                tl.online = false;
                --onlineNow;
            }
            tl.prefetchedFeed.clear(); // an exit abandons the remainder of the feed
            feedStamps[u].clear();     // ...and its parallel stamps (kept in lockstep)
            outstandingRequest[u] =
                false; // clean slate: the next session re-requests from ReturnToApp

            // Baseline return-delay (stream "scheduling", D19). Reading the hidden
            // baselineDailyUsage trait is a documented evaluation-side read of simulation state;
            // the draw is dropped if the return lands beyond the horizon.
            const Timestamp returnDelay =
                baselineReturnDelay(schedulingRng, config_.scheduling,
                                    static_cast<double>(ds.hiddenStates[u].baselineDailyUsage));
            returnDelays.push_back(static_cast<double>(returnDelay));
            scheduleEvent(EventType::ReturnToApp, e.userId, e.time + returnDelay);
            break;
        }

        // Not queued in Phase 18 (documented): FinishReel/Interaction are log-only facets emitted
        // above; PreferenceDrift keying stays on totalInteractions (no queue events — the enum is
        // reserved for P20); ReelPublished is reserved/no-op (mid-run injection is blocked under
        // content_v2, which event mode requires). Reaching here would be a scheduling bug.
        case EventType::FinishReel:
        case EventType::Interaction:
        case EventType::PreferenceDrift:
        case EventType::ReelPublished:
            break;
        }
    };

    // Handle every queued event at time `t` in the given processing phase, in deterministic pop
    // order, INCLUDING any same-`t` events the handlers spawn (they are popped as the loop
    // continues); events of a LATER phase are set aside and re-queued. The heap re-orders
    // everything, so the order within a phase is always (tie-breaker, userId, type) — never
    // insertion order.
    auto handlePhaseAt = [&](Timestamp t, std::size_t dayIdx, int phase) {
        std::vector<SimulationEvent> deferred;
        while (!queue.empty() && queue.nextTime() == t) {
            const SimulationEvent ev = queue.pop();
            if (eventProcessingPhase(ev.type) == phase) {
                dispatch(ev, dayIdx);
            } else {
                deferred.push_back(ev);
            }
        }
        for (const SimulationEvent &ev : deferred) {
            queue.push(ev);
        }
    };

    // 5. Seed the queue: one OpenApp per user at uniform(0, openStaggerSeconds) from "scheduling",
    //    drawn IN ASCENDING UserId ORDER. Pinning the draw order to the user index (not the
    //    iteration/insertion order) is what makes the whole run order-invariant: the initial event
    //    set is identical however users are enumerated, and every later draw keys off the
    //    deterministic pop order, so permuting user init leaves the event log and metrics identical
    //    (D20). Opens beyond the horizon are dropped (that user simply never participates).
    for (std::size_t u = 0; u < userCount; ++u) {
        const double t = schedulingRng.uniform(0.0, config_.scheduling.openStaggerSeconds);
        scheduleEvent(EventType::OpenApp, ds.users[u].id, static_cast<Timestamp>(std::llround(t)));
    }

    // 6. Main loop: process the queue one TIMESTAMP GROUP at a time (V2 §4.14 / D20 equal-timestamp
    //    snapshot). Within a timestamp we drain in three phases so that ALL feed requests observe
    //    the same prior global state:
    //      phase 0 (arrivals): OpenApp/ReturnToApp  -> spawn RequestFeed at t
    //      phase 1 (requests):  RequestFeed          -> read popularity/trending, spawn StartReel
    //      at t phase 2 (the rest):  StartReel (consumption WRITES counters), ExitApp, ...
    //    Because arrivals only spawn phase-1 events, requests only spawn phase-2 events, and
    //    consumption only spawns FUTURE events (dwell >= browse overhead > 0) plus at most an
    //    ExitApp at the same t, the phase cascade is a strict DAG within t: no RequestFeed is ever
    //    handled AFTER a consumption at the same t, so equal-time requests are snapshot-consistent
    //    regardless of pop order. This is the "process equal-timestamp requests from a snapshot"
    //    option in §4.14 done the CHEAP way: instead of buffering every counter mutation for a
    //    whole-state snapshot per timestamp (the full-buffer alternative — correct but O(catalog)
    //    memory per timestamp and a second write-back pass), we only need the ONE ordering
    //    invariant "requests before writes", which the phase split guarantees with no extra state.
    //    Package C's equal-timestamp tie-break contract test can rely on this ordering. The phases
    //    are rr::eventProcessingPhase (0 arrivals, 1 requests, 2 the rest).
    std::size_t processedDay = 0;
    bool anyProcessed = false;
    while (!queue.empty()) {
        const Timestamp t = queue.nextTime();
        if (t > horizon) {
            break; // defensive: events beyond the horizon are dropped at schedule time.
        }
        const std::size_t rawDay = static_cast<std::size_t>(t / kSecondsPerSimulatedDay);
        const std::size_t dayIdx = rawDay < numDays ? rawDay : numDays - 1;

        // Day-boundary alignment snapshot: when the day advances, snapshot the est<->hidden
        // alignment once and assign it to the day(s) that just ended (empty intervening days share
        // the same end-of-window snapshot).
        if (anyProcessed && dayIdx != processedDay) {
            const double a = computeAlignment();
            for (std::size_t d = processedDay; d < dayIdx && d < numDays; ++d) {
                alignmentByDay[d] = a;
            }
        }
        processedDay = dayIdx;
        anyProcessed = true;

        handlePhaseAt(t, dayIdx, /*phase=*/0); // arrivals
        handlePhaseAt(t, dayIdx, /*phase=*/1); // request feeds (all see the same prior state)
        handlePhaseAt(t, dayIdx, /*phase=*/2); // consumption + the rest

        // Concurrent-online occupancy: fraction of users online after this timestamp's events
        // resolved, sampled once per processed event timestamp (documented definition).
        onlineFractionSum +=
            userCount == 0 ? 0.0 : static_cast<double>(onlineNow) / static_cast<double>(userCount);
        ++occupancySamples;
    }
    // Final alignment snapshot fills the last active day and any trailing empty days.
    {
        const double a = computeAlignment();
        for (std::size_t d = (anyProcessed ? processedDay : 0); d < numDays; ++d) {
            alignmentByDay[d] = a;
        }
    }

    // 7. Run-end drain: sessions still open at the horizon become RunEnded records (excluded from
    //    exit-rate denominators), attributed to the last day — the collected-where-closed
    //    convention, mirroring the legacy runner's run-end drain.
    if (sessionDynamics && numDays > 0) {
        for (const SessionRecord &openRec : sim.drainOpenSessions()) {
            sessionHealth.add(numDays - 1, openRec);
        }
    }

    // 8. Assemble the ExperimentResult, mirroring the legacy runner's RESULT ASSEMBLY so
    //    ResultsWriter serializes the four §6 groups unchanged (D22). "Rounds" are simulated DAYS.
    ExperimentResult result;
    result.config = config_;
    result.seed = seed;
    result.experimentId = std::string(toString(config_.algorithm)) + "-seed" +
                          std::to_string(seed) + "-" + experimentTimestamp();
    result.directory = outputRoot_ / result.experimentId;
    result.userCount = userCount;
    result.reelCount = ds.reels.size();
    result.requestCount = requestCount;
    result.impressionCount = impressionCount;
    result.overall = metrics.overall();
    result.oracleSampleRate = config_.evaluation.oracleSampleRate;
    result.learningEnabled = learningEnabled;
    result.retrievalApplicable = retrievalApplicable;
    result.retrievalSampleRate = config_.evaluation.retrievalSampleRate;

    double cumulativeRegret = 0.0;
    std::size_t totalSampled = 0;
    double totalRegretSum = 0.0;
    std::size_t totalRetrievalSamples = 0;
    double totalRecall10Sum = 0.0;
    double totalRecall50Sum = 0.0;
    double totalDistErrorSum = 0.0;
    result.rounds.reserve(numDays);
    for (std::size_t r = 0; r < numDays; ++r) {
        RoundMetrics rm;
        rm.round = r;
        rm.metrics = (r < metrics.roundCount()) ? metrics.roundSummary(r) : MetricsSummary{};
        rm.sampledRequests = regretByDay[r].sampled;
        rm.meanRegret = regretByDay[r].sampled > 0
                            ? regretByDay[r].sum / static_cast<double>(regretByDay[r].sampled)
                            : 0.0;
        cumulativeRegret += regretByDay[r].sum;
        rm.cumulativeRegret = cumulativeRegret;

        const RetrievalAcc &racc = retrievalByDay[r];
        rm.retrievalSamples = racc.samples;
        if (racc.samples > 0) {
            const double n = static_cast<double>(racc.samples);
            rm.meanRecallAt10 = racc.recall10Sum / n;
            rm.meanRecallAt50 = racc.recall50Sum / n;
            rm.meanDistanceError = racc.distErrorSum / n;
        }
        rm.meanEstimatedHiddenCosine = alignmentByDay[r];

        const DiversitySummary dsum = diversityByDay.roundSummary(r);
        rm.diversityFeeds = dsum.feeds;
        rm.meanUniqueTopics = dsum.meanUniqueTopics;
        rm.meanUniqueCreators = dsum.meanUniqueCreators;
        rm.meanIntraListSimilarity = dsum.meanIntraListSimilarity;
        rm.meanTopicConcentration = dsum.meanTopicConcentration;
        rm.meanCreatorConcentration = dsum.meanCreatorConcentration;
        rm.repetitionCount = dsum.totalRepeats;
        rm.repetitionRate = dsum.repetitionRate;
        // Drift cohort columns stay 0: the round-based adaptation split is not emitted in event
        // mode (rounds are days, not feedSize buckets), so adaptation.configured stays false below.

        result.rounds.push_back(rm);

        totalSampled += regretByDay[r].sampled;
        totalRegretSum += regretByDay[r].sum;
        totalRetrievalSamples += racc.samples;
        totalRecall10Sum += racc.recall10Sum;
        totalRecall50Sum += racc.recall50Sum;
        totalDistErrorSum += racc.distErrorSum;
    }
    result.sampledRequestCount = totalSampled;
    result.meanRegret = totalSampled > 0 ? totalRegretSum / static_cast<double>(totalSampled) : 0.0;
    result.cumulativeRegret = totalRegretSum;
    result.finalEstimatedHiddenCosine =
        result.rounds.empty() ? 0.0 : result.rounds.back().meanEstimatedHiddenCosine;

    // Hidden-user-welfare reduction (Phase 15) — populated under latent_reactions (always on in
    // event mode). Identical to the legacy assembly.
    result.welfare.configured = latentReactions;
    if (latentReactions) {
        std::vector<std::string> archetypeNames;
        archetypeNames.reserve(config_.realism.archetypes.size());
        for (const ArchetypeSpec &a : config_.realism.archetypes) {
            archetypeNames.push_back(a.name);
        }
        result.welfare = welfareMetrics.reduce(archetypeNames);
        result.welfare.configured = true;
    }

    // Session-health reduction (Phase 16) — populated under session_dynamics (always on in event
    // mode); also realizes the welfare group's harmful_fatigue column, exactly as the legacy
    // runner.
    result.sessionHealth.configured = sessionDynamics;
    if (sessionDynamics) {
        result.sessionHealth = sessionHealth.reduce();
        result.sessionHealth.configured = true;
        result.welfare.harmfulFatigue = result.sessionHealth.harmfulFatigueMean;
        for (std::size_t r = 0; r < result.welfare.byRound.size(); ++r) {
            if (r < result.sessionHealth.byRound.size()) {
                result.welfare.byRound[r].harmfulFatigue =
                    result.sessionHealth.byRound[r].harmfulFatigueMean;
            }
        }
    }

    result.retrievalSampleCount = totalRetrievalSamples;
    if (totalRetrievalSamples > 0) {
        const double n = static_cast<double>(totalRetrievalSamples);
        result.retrievalRecallAt10 = totalRecall10Sum / n;
        result.retrievalRecallAt50 = totalRecall50Sum / n;
        result.retrievalDistanceError = totalDistErrorSum / n;
    }

    const DiversitySummary diversityOverall = diversityByDay.overall();
    result.diversityFeedCount = diversityOverall.feeds;
    result.meanUniqueTopics = diversityOverall.meanUniqueTopics;
    result.meanUniqueCreators = diversityOverall.meanUniqueCreators;
    result.meanIntraListSimilarity = diversityOverall.meanIntraListSimilarity;
    result.meanTopicConcentration = diversityOverall.meanTopicConcentration;
    result.meanCreatorConcentration = diversityOverall.meanCreatorConcentration;
    result.totalRepetitions = diversityOverall.totalRepeats;
    result.repetitionRate = diversityOverall.repetitionRate;

    result.latency = latencyStats(latencies);
    result.retrievalLatency = latencyStats(retrievalLatencies);
    result.rankingLatency = latencyStats(rankingLatencies);
    result.rerankingLatency = latencyStats(rerankingLatencies);
    result.totalWallSeconds = wall.elapsedMs() / 1000.0;

    // Injection (Phase 8) and the round-based drift-adaptation report (Phase 10) are NOT emitted in
    // event mode: mid-run injection is blocked under content_v2 (which event mode requires), and
    // the adaptation windows are defined over feedSize rounds that do not exist here. Both stay
    // unconfigured (no keys written), while drift still moves hidden preferences via maybeApply.
    result.coldStart.configured = false;
    result.adaptation.configured = false;

    // Event-mode additions (D20/D22): the event-log digest + count (package C's golden tripwire)
    // and the session-health numbers only the event runner can produce.
    result.eventMode.configured = true;
    result.eventMode.eventLogDigest = foldEventLog(eventLog);
    result.eventMode.eventCount = eventLog.size();
    result.eventMode.simulatedDays =
        config_.simulation.horizonSeconds / static_cast<double>(kSecondsPerSimulatedDay);
    result.eventMode.sessionsPerSimulatedDay =
        result.eventMode.simulatedDays > 0.0
            ? static_cast<double>(result.sessionHealth.sessions) / result.eventMode.simulatedDays
            : 0.0;
    result.eventMode.meanConcurrentOnline =
        occupancySamples > 0 ? onlineFractionSum / static_cast<double>(occupancySamples) : 0.0;
    result.eventMode.returnCount = returnDelays.size();
    if (!returnDelays.empty()) {
        double sum = 0.0;
        for (const double d : returnDelays) {
            sum += d;
        }
        result.eventMode.returnDelayMeanSeconds = sum / static_cast<double>(returnDelays.size());
        result.eventMode.returnDelayMedianSeconds = medianOf(returnDelays);
    }

    // Phase 19 serving / cost / staleness reduction (V2 §4.13, D22). The effective serving strategy
    // is echoed so package C can label each frontier point; the run-level cost/staleness follow,
    // then the per-day rows for serving_metrics.csv, then the drift-only adaptation delay.
    result.eventMode.servingPrefetchDepth = static_cast<uint32_t>(prefetchDepth);
    result.eventMode.servingRefillThreshold = serving.refillThreshold;
    result.eventMode.servingInvalidateOnIntentChange = serving.invalidateOnIntentChange;
    result.eventMode.feedRequestCount = requestCount;
    result.eventMode.rankingComputations = rankingComputations;
    result.eventMode.staleImpressionCount = staleImpressionCount;
    result.eventMode.feedInvalidationCount = feedInvalidationCount;
    if (impressionCount > 0) {
        result.eventMode.meanStaleness =
            static_cast<double>(stalenessSum) / static_cast<double>(impressionCount);
        result.eventMode.staleImpressionRate =
            static_cast<double>(staleImpressionCount) / static_cast<double>(impressionCount);
    }

    // Satisfaction lost before refresh: for each day, the fresh-serving reference is that day's
    // mean immediate satisfaction over its staleness-0 impressions; each stale impression
    // contributes max(0, freshMean − itsSatisfaction). A day with no fresh impressions has no
    // reference, so its stale impressions contribute 0 (documented — nothing to compare against).
    // Summed over days.
    double satisfactionLost = 0.0;
    result.eventMode.servingByDay.reserve(numDays);
    for (std::size_t d = 0; d < numDays; ++d) {
        const ServingDayAcc &acc = servingByDay[d];
        double dayLost = 0.0;
        if (acc.freshSatCount > 0) {
            const double freshMean = acc.freshSatSum / static_cast<double>(acc.freshSatCount);
            for (const double staleSat : acc.staleSatisfactions) {
                dayLost += std::max(0.0, freshMean - staleSat);
            }
        }
        satisfactionLost += dayLost;

        ServingDayPoint pt;
        pt.day = d;
        pt.feedRequests = acc.feedRequests;
        pt.rankingComputations = acc.rankingComputations;
        pt.impressions = acc.impressions;
        pt.staleImpressions = acc.staleImpressions;
        pt.staleImpressionRate = acc.impressions > 0 ? static_cast<double>(acc.staleImpressions) /
                                                           static_cast<double>(acc.impressions)
                                                     : 0.0;
        pt.meanStaleness = acc.impressions > 0 ? static_cast<double>(acc.stalenessSum) /
                                                     static_cast<double>(acc.impressions)
                                               : 0.0;
        pt.satisfactionLost = dayLost;
        result.eventMode.servingByDay.push_back(pt);
    }
    result.eventMode.satisfactionLostBeforeRefresh = satisfactionLost;

    // Adaptation delay after drift (task 4, drift-only). For each drifted-cohort user with enough
    // pre-drift history, find the interactions-until-recovery of their trailing-window mean
    // satisfaction; aggregate mean/median over the users that recovered within the horizon. The
    // drift interaction index is the scheduler's earliest configured atInteraction (whole-cohort,
    // mirroring the P10 AdaptationReport anchor). Users who never recover are counted but excluded
    // from the delay means (a right-censored "did not recover within the run", reported honestly).
    result.eventMode.adaptationConfigured = driftConfigured;
    if (driftConfigured) {
        const std::size_t driftIdx = drift.firstDriftInteraction();
        std::vector<double> recoveredDelays;
        std::size_t driftedUsers = 0;
        for (std::size_t uu = 0; uu < userCount; ++uu) {
            if (!drift.everApplies(ds.users[uu].id)) {
                continue;
            }
            // Needs enough pre-drift history AND a post-drift interaction to say anything.
            if (userSatSeq[uu].size() <= driftIdx || driftIdx < kAdaptationWindow) {
                continue;
            }
            ++driftedUsers;
            const long delay = adaptationDelayInteractions(
                userSatSeq[uu], driftIdx, kAdaptationWindow, kAdaptationRecoverFraction);
            if (delay >= 0) {
                recoveredDelays.push_back(static_cast<double>(delay));
            }
        }
        result.eventMode.adaptationDriftedUsers = driftedUsers;
        result.eventMode.adaptationRecoveredUsers = recoveredDelays.size();
        if (!recoveredDelays.empty()) {
            double s = 0.0;
            for (const double d : recoveredDelays) {
                s += d;
            }
            result.eventMode.meanAdaptationDelayInteractions =
                s / static_cast<double>(recoveredDelays.size());
            result.eventMode.medianAdaptationDelayInteractions = medianOf(recoveredDelays);
        }
    }

    // 9. Write the §26 output layout (ResultsWriter emits the event_mode block only when
    // configured).
    std::filesystem::create_directories(result.directory);
    RunMetadata meta = collectRunMetadata(provenance_, result.experimentId);
    meta.userCount = ds.users.size();
    meta.reelCount = ds.reels.size();
    meta.creatorCount = ds.creators.size();
    meta.topicCount = ds.topics.size();
    meta.dimensions = config_.simulation.dimensions;
    ResultsWriter::writeAll(result, meta);

    return result;
}

} // namespace rr
