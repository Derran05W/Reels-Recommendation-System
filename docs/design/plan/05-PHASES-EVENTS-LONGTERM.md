# ReelRank Plan — Part 5: Event-Driven World and Long-Term Dynamics (Phases 18–21)

Realism V2, Releases C–D (V2 TDD Tiers 3–4). One phase = one Fable session (plan → parallel
opus/sonnet subagents → integrate → verify → commit). Read `plan/00-DESIGN-DECISIONS.md`,
`plan/00-DESIGN-DECISIONS-V2.md`, `commit.md`, and the referenced V2 TDD sections
(`REELS-SIMULATION-V2.md`) before starting. The D17 standing golden-baseline exit criterion and
D18 guard apply to every phase (see Part 4 header). Do not start a phase whose prerequisites are
unticked in `commit.md`.

---

## Phase 18 — Event-driven simulation core

**V2 TDD refs:** §4.11, §4.12, §4.14, §7 (event determinism tests). D20 is the binding
determinism contract.
**Prerequisites:** Phase 16 (session dynamics must exist for exits/returns to schedule against;
Phase 17 recommended but not required).

### Objective
A deterministic event-queue runner where users open, scroll, exit, and return on independent
timelines in simulated time — coexisting with the untouched legacy round-robin runner, with
identical-event-sequence and order-invariance guarantees test-enforced.

### Tasks
1. Event core (V2 §4.11, D20): `SimulationEvent` (time, deterministicTieBreaker, userId, type),
   `EventType` enum (OpenApp, RequestFeed, StartReel, FinishReel, Interaction, ExitApp,
   ReturnToApp, PreferenceDrift, ReelPublished), priority queue ordered by (time, tieBreaker);
   tie-breaker = pinned hash of (userId, eventType, perUserSeq) with golden-value tests.
   `UserTimeline` (nextEventAt, online, activeSession, prefetchedFeed deque).
2. `EventDrivenRunner` selected by `simulation.scheduler = "event_queue"` (legacy
   `ExperimentRunner` untouched, remains default — D17/D20): handlers wiring the EXISTING
   pipeline — RequestFeed → orchestrated recommendation into the timeline's prefetch deque
   (depth = feedSize this phase; strategies in P19); StartReel/FinishReel → `BehaviourModelV2` +
   latent/welfare plumbing + updater apply; ExitApp ← P16 exit model verdicts; ReturnToApp ←
   baseline return-delay model (config distribution + per-user offset from `"scheduling"`;
   satisfaction-coupled retention arrives in P20); OpenApp schedule: staggered initial opens
   from `"scheduling"`; PreferenceDrift → existing `DriftScheduler` events mapped to timestamps;
   ReelPublished → maps the P8 injection machinery to publish times.
3. Global-state snapshot semantics (V2 §4.14, D20): popularity/trending accumulators keyed to
   event timestamps; all events at time T observe state as of T−ε; updates at T visible only
   strictly later. Direct test: two equal-timestamp RequestFeeds observe identical global state
   in either pop order.
4. Simulation horizon semantics: config moves from interactions-per-user to simulated-duration
   (`simulation.horizon_seconds`) for event mode, with per-user interaction counts becoming an
   outcome; both summarized. Time-of-day intent hook: OpenApp carries a time-of-day bucket
   (config-driven activity curve) — consumed as a session-intent modifier placeholder documented
   for P20 (kept minimal here).
5. Metrics continuity (D22): all four §6 groups emitted from the event runner; new session-health
   columns — sessions per simulated day, concurrent-online occupancy, return delays (baseline
   model). Determinism: byte-identical CSVs per seed as everywhere else.
6. Determinism suite (Tier 3 acceptance + §7): same seed ⇒ identical event log (golden event-log
   test artifact, compact digest committed); permuting user initialization/insertion order ⇒
   identical event log and metrics; equal-timestamp tie-break determinism; cross-runner sanity —
   event runner configured degenerate (all users open at t=0, no returns, feed depth = feedSize)
   is statistically comparable to round-robin on engagement/welfare means (documented
   comparability check, NOT byte-identity — D20); golden regression (runner core touched ⇒
   medium re-verification mandatory).

### Exit criteria
- [ ] Users on independent open/exit/return timelines; feeds requested by consumption (depth-1
      refill this phase), event order fully deterministic per seed (tests + committed digest).
- [ ] Order-invariance: user iteration/insertion order provably immaterial (test).
- [ ] Equal-timestamp semantics explicitly defined, documented, and tested (V2 §10 item 5 and
      Tier 3 acceptance minus the batch experiment).
- [ ] Legacy runner byte-identical behaviour preserved (D17 golden, small + medium).

### Out of scope
Prefetch depths/refresh strategies and the batch experiment (19); satisfaction-coupled retention
(20); any threading (the queue is single-threaded simulated concurrency, D13 unchanged).

### Suggested package split
A (opus): event core — queue, tie-breaker, timelines, runner skeleton, horizon semantics.
B (opus): handler integration — behaviour/session/updater/drift/injection mapping, snapshot
semantics, metrics continuity.
C (sonnet): determinism suite — event-log golden digest, order-permutation tests, cross-runner
comparability, golden re-verification.
Fable: config surface (`simulation.scheduler`, horizon, activity curve), frozen event/timeline
headers, integration. This is V2's riskiest phase — keep packages strictly off the legacy runner.

### Session kickoff prompt
> Read plan/00-DESIGN-DECISIONS.md, plan/00-DESIGN-DECISIONS-V2.md (D20 especially),
> plan/05-PHASES-EVENTS-LONGTERM.md (Phase 18), commit.md, V2 TDD §§4.11–4.12, 4.14, 7.
> Implement the event queue, user timelines, and EventDrivenRunner behind
> simulation.scheduler="event_queue" test-first, mapping the existing pipeline into handlers with
> snapshot semantics; land the identical-event-sequence and order-invariance determinism suite.
> Legacy runner untouched and byte-identical. Update commit.md per protocol.

---

## Phase 19 — Feed prefetch, refresh, and the adaptation trade-off

**V2 TDD refs:** §4.13, Tier 3 acceptance (feed requests) + core experiment, §7 (batch-size-one
test).
**Prerequisites:** Phase 18.

### Objective
Serving strategy becomes a measurable design axis: prefetch depth, refill thresholds, and
invalidation rules trade freshness against ranking cost, quantified under abrupt preference
drift.

### Tasks
1. Serving strategies (V2 §4.13) on the event runner (`serving.*` config): prefetch depth
   {1, 3, 10, 20}; refill when remaining ≤ threshold; invalidate-remaining on major session-
   intent change (documented trigger: session-vector swing beyond a cosine threshold, or drift
   event); preserve-downloaded semantics (default: already-prefetched reels survive preference-
   estimate changes — the realistic client cache). RequestFeed events fire from consumption;
   ranked feeds land in the timeline deque.
2. Cost + staleness instrumentation (D22 additions): feed-request count, ranking computations
   (candidates scored), per-impression staleness (updater applications between the impression's
   ranking time and serving time), stale-impression rate (staleness > 0), adaptation delay after
   drift (interactions to recover pre-drift satisfaction, from P10 machinery re-used on
   satisfaction), satisfaction lost before refresh (cumulative satisfaction gap during stale
   serving windows).
3. Tier 3 core experiment: batch sizes 1/3/10/20 under the P10 abrupt-drift schedule on event-
   mode `realism-medium`; report adaptation delay, satisfaction lost before refresh, request
   counts, ranking computation counts, stale-impression rate — the freshness-versus-cost
   frontier. Publish `results/published/phase19/` with frontier plots.
4. Tests: refill fires exactly at threshold (unit); invalidation trigger rules (unit);
   preserve-downloaded semantics (unit); §7 mandated — batch-size-1 adapts no slower than
   batch-20 under drift (statistical); serving strategies leave the no-scheduler path untouched
   (config validation: `serving.*` requires event mode); determinism; golden regression.

### Exit criteria
- [ ] Feed requests occur from consumption + thresholds (Tier 3 acceptance completed).
- [ ] Batch-depth adaptation trade-off published with cost columns (V2 §10 item 6).
- [ ] `results/published/phase19/` complete.

### Out of scope
Retention coupling (20); ranking-cost optimizations themselves (the O(catalog) source scans stay
as documented future work unless a target fails — V1 Phase 11 stance).

### Suggested package split
A (opus): serving strategies + invalidation semantics.
B (sonnet): instrumentation columns + config validation.
C (sonnet): experiment matrix + frontier plots + comparison.md.
Fable: config surface, integration, publication review. (Deliberately lighter phase after 18.)

### Session kickoff prompt
> Read plan/00-DESIGN-DECISIONS.md, plan/00-DESIGN-DECISIONS-V2.md, plan/05-PHASES-EVENTS-
> LONGTERM.md (Phase 19), commit.md, V2 TDD §4.13 + Tier 3 core experiment, §7. Implement
> prefetch/refill/invalidation serving strategies on the event runner test-first with staleness
> and cost instrumentation, then run and publish the batch-size × drift adaptation experiment
> incl. the batch-1-adapts-no-slower test. Update commit.md per protocol.

---

## Phase 20 — Exposure-driven preferences and retention

**V2 TDD refs:** §4.15–4.17, Tier 4 acceptance items 1 and 4, §6 (welfare: trust, distortion;
session health: retention). V1 TDD §11.4 (scheduled drift, which stays separate).
**Prerequisites:** Phase 18 (returns are events); Phase 19 recommended.

### Objective
Recommendations start shaping the world: satisfying exposure reinforces hidden preferences,
overexposure saturates them, ragebait leaves aversion and erodes trust, and the delay until a
user's next session depends on how the last one felt — with long-term metrics reported separately
from immediate engagement.

### Tasks
1. Preference reinforcement (V2 §4.15) behind `realism.preference_evolution`:
   p_{t+1} = normalize((1−η_evo)·p_t + η_evo·s_t·v_t) driven by **hidden satisfaction** s_t (not
   reward), per channel (topic + modalities), η_evo scaled by the P13 `preferencePlasticity`
   trait; draws (where noise is designed) on `"preference-evolution"`. Structurally separate
   from: scheduled drift (P10, exogenous — both may coexist; document interaction) and session
   fatigue (P16, resets between sessions — V2 §4.16 requirement).
2. Saturation and aversion (V2 §4.16): long-term topic exhaustion and creator burnout
   accumulators (distinct from session fatigue; decay over days away — return-to-baseline);
   negative association after ragebait/regret exposures (satisfaction-weighted aversion pushes
   the channel away and dents `platformTrust`); novelty depletion on repeats.
3. Retention model (V2 §4.17): next-session delay = f(last-session satisfaction, regret,
   `habitStrength`, `baselineDailyUsage`, external schedule/time-of-day curve, `platformTrust`);
   habit strengthening/decay dynamics; churn = configurable delay-threshold / hazard floor;
   feeds `ReturnToApp` scheduling (replacing P18's baseline model under the gate).
4. Long-term metrics (D22, `longterm_metrics.csv` + summary `long_term`): 1-day and 7-day
   simulated retention, sessions/user/day, satisfaction-weighted retention, churn probability,
   long-term interest diversity (hidden-preference entropy/spread over time — evaluation
   carve-out), preference-distortion measure = per-user hidden-preference shift vs the
   gate-off counterfactual world at same seed (documented method: same world, evolution off).
   Trust column goes live (P15 placeholder resolved).
5. Policy-influence experiment (Tier 4 acceptance 1): two policies (engagement preset vs
   satisfaction-proxy preset) on identical worlds/seeds, multi-day horizon — show hidden
   preference distributions diverge by policy (distortion metric + distribution plots) and
   retention/churn separate. Publish `results/published/phase20/`.
6. Tests: reinforcement math (unit, hand-computed); satisfaction-driven not reward-driven
   (constructed case where they disagree — ragebait: high reward, negative satisfaction ⇒
   preference moves AWAY); saturation/aversion directionality; away-time baseline reversion;
   retention monotonicity (better last-session welfare ⇒ stochastically sooner return);
   trust erosion under repeated regret; churn determinism; policy-divergence statistical test;
   golden regression.

### Exit criteria
- [ ] Recommender policy measurably alters future hidden preference distributions (Tier 4
      acceptance 1; test + published).
- [ ] Retention/churn/trust live and reported separately from engagement (acceptance 4).
- [ ] Content-induced preference change demonstrably separate from session fatigue and from
      scheduled drift (tests).
- [ ] `results/published/phase20/` complete.

### Out of scope
The full failure-mode scenario suite (21); learned ranking (22–23).

### Suggested package split
A (opus): preference evolution + saturation/aversion (simulation side).
B (opus): retention/trust/churn model + long-term metrics.
C (sonnet): policy-influence experiment + distortion-vs-counterfactual tooling + plots.
Fable: config surface, integration, docs of drift/evolution/fatigue separation.

### Session kickoff prompt
> Read plan/00-DESIGN-DECISIONS.md, plan/00-DESIGN-DECISIONS-V2.md, plan/05-PHASES-EVENTS-
> LONGTERM.md (Phase 20), commit.md, V2 TDD §§4.15–4.17, 6, and V1 TDD §11.4. Implement
> satisfaction-driven preference reinforcement, saturation/aversion, and the retention/trust/
> churn model behind realism.preference_evolution + retention.* test-first; land long-term
> metrics and publish the two-policy preference-divergence experiment. Update commit.md per
> protocol.

---

## Phase 21 — Ecosystem failure-mode experiment suite

**V2 TDD refs:** §4.18, Tier 4 acceptance items 2–3, §6 (all groups), §10 item 7.
**Prerequisites:** Phase 20 (Phases 17 and 19 recommended for cohort configs and serving
realism).

### Objective
The V2 flagship results set: seven designed scenarios demonstrating (or honestly failing to
demonstrate) the classic recommender pathologies — each with hypothesis, config, all four metric
groups plus long-term outcomes, plots, and a verdict.

### Tasks
1. Scenario harness: `configs/scenarios/*.json` + a runner script (uv, D15) that executes arms
   serially or with documented contention (orchestration memory: separate `--out` roots; long
   batches via nohup + Monitor). Each scenario documents hypothesis → mechanism → expected
   signature BEFORE running (committed in the scenario config's description block).
2. Scenarios (V2 §4.18), each vs a control arm on identical worlds:
   - **Filter-bubble formation:** exploit-only (ε=0, high personalization weight) vs shipped
     defaults, multi-day: interest-diversity collapse, distortion, niche starvation onset.
   - **Ragebait amplification:** engagement-optimized preset + susceptible-heavy population:
     ragebait exposure share over time, welfare/trust decline, regret exits (Tier 4 acceptance 2
     — engagement optimization creates measurable negative long-term outcomes).
   - **Popularity feedback loop:** popularity-heavy weights: creator concentration (HHI over
     time), new-creator exposure decay.
   - **Niche-content starvation:** niche-treasure archetype exposure/served-cohort match rate
     under mainstream-optimizing policies.
   - **Creator overconcentration:** creator-affinity-heavy weights vs caps: session creator-HHI
     and creator-fatigue exits.
   - **Exploration recovery:** neglected-interest world (evolution suppressed a channel) —
     ε/exploration sweep shows recovery of the neglected interest (Tier 4 acceptance 3).
   - **Satisfaction-vs-retention conflict:** weight sweep between engagement and
     satisfaction-proxy presets: the retention/welfare frontier (comfort-content dynamics
     surfaced).
3. Metric additions only where a scenario needs them (e.g. creator-HHI-over-time, per-archetype
   exposure share time series — small, documented; D22 additive).
4. Statistical tests committed for the two acceptance-mandated effects (ragebait amplification
   harm; exploration recovery); other scenarios report published numbers + verdicts without
   test-enforcement if inherently noisy — honesty per V1 conventions.
5. Publish `results/published/phase21/` — per-scenario directories + a consolidated
   `ECOSYSTEM.md` (the V2 headline document: seven verdicts with pointers).

### Exit criteria
- [ ] Tier 4 acceptance 2–3 test-enforced and published; V2 §10 item 7 fully demonstrated
      (policy changes long-term satisfaction and retention).
- [ ] All seven scenarios run with pre-registered hypotheses and honest verdicts (incl. any
      "effect not observed" outcomes) in `ECOSYSTEM.md`.
- [ ] All four §6 groups + long-term metrics reported per scenario; no aggregate score.

### Out of scope
New simulation mechanics (scenarios use existing knobs; a missing minor knob is a documented
config addition, not a new subsystem); learned ranking (22–23).

### Suggested package split
Scenario configs + runs parallelize naturally: A (opus): filter-bubble + exploration-recovery
(the two needing counterfactual care); B (opus): ragebait + satisfaction-vs-retention (the two
test-enforced/frontier ones); C (sonnet): popularity/creator/niche trio; D (sonnet): ECOSYSTEM.md
consolidation + plots. Fable: scenario review (pre-registration discipline), metric additions,
integration. Mostly non-C++ — same-checkout parallelism is fine (orchestration memory).

### Session kickoff prompt
> Read plan/00-DESIGN-DECISIONS.md, plan/00-DESIGN-DECISIONS-V2.md, plan/05-PHASES-EVENTS-
> LONGTERM.md (Phase 21), commit.md, V2 TDD §4.18 + Tier 4 acceptance, §6. Build the scenario
> harness and the seven pre-registered failure-mode scenarios, run them against controls on
> identical worlds, test-enforce ragebait-amplification and exploration-recovery, and publish
> per-scenario results + ECOSYSTEM.md with honest verdicts. Update commit.md per protocol.
