# Phase 20 — Frozen Cross-Package Contracts

Binding integration contract for Phase 20 (exposure-driven preferences and retention), written by
the orchestrator BEFORE package launch so no package guesses another's schema (the P19 lesson:
guessed field names cost an integration session). Packages A/B/C and the scaffold implement
EXACTLY these surfaces; proposed changes go in the package's final report, not into shared files.

Plan: `Reels-Simulation/plan/05-PHASES-EVENTS-LONGTERM.md` (Phase 20). TDD: V2 §4.15–4.17, §6.
Decisions: D17–D25 apply (gates default off; byte-identical gates-off output; D18 hidden isolation;
D19 streams; D22 additive metrics).

## 1. Config surface (scaffold owns; nobody else edits config.{hpp,cpp})

- `realism.preference_evolution` : bool, default false. Load-validation: requires
  `realism.session_dynamics`.
- `evolution.eta_evo` : double, default 0.02. Per-impression base reinforcement rate (§4.15 η),
  scaled per user by `preferencePlasticity` (see §3). All other §4.16 constants are NAMED CONSTANTS
  in `preference_evolution.cpp` (D24 no-premature-config).
- `retention.enabled` : bool, default false. Load-validation: requires `realism.session_dynamics`
  AND `simulation.scheduler == "event_queue"`.
- `retention.churn_delay_threshold_seconds` : double, default 604800 (7 simulated days). A computed
  next-return delay strictly greater than this marks the user churned (no ReturnToApp scheduled).
- `retention.hazard_floor` : double, default 0.02. Minimum effective per-day return hazard for a
  non-churned user (bounds delays away from infinity; exact use documented in retention_model.cpp).
- Unknown-key rejection (D6) continues; both blocks are additive.

## 2. Hidden state (simulation/hidden/, D18)

- `include/rr/simulation/hidden/hidden_exposure_state.hpp` — `struct HiddenExposureState`.
  OWNED BY PACKAGE A after scaffold (B/C never include it). Scaffold ships a minimal compiling
  shape; A may reshape freely. Semantics: long-term topic exhaustion, creator burnout, novelty
  depletion, per-channel aversion; all decay toward baseline with time away (closed-form
  `2^(-gap/halfLife)` decay-on-touch — see §4, no session-start hook exists).
- `include/rr/simulation/hidden/hidden_retention_state.hpp` — `struct HiddenRetentionState`
  (OWNED BY PACKAGE B after scaffold), minimum frozen fields:
  ```cpp
  struct HiddenRetentionState {
      double trust = -1.0;              // accumulated platform trust in [0,1]; -1 = uninitialized
                                        // (initialized to hidden.platformTrust on first gate-on use)
      double habitStrength = 0.0;       // [0,1], strengthens with satisfying sessions, decays away
      bool churned = false;             // set by RetentionModel when delay > churn threshold
      double lastSessionSatisfaction = 0.0;
      double lastSessionRegret = 0.0;
      rr::Timestamp lastExitAt = 0;
      uint32_t sessionsCompleted = 0;
  };
  ```
  WRITE DISCIPLINE: Package A's evolution component is the ONLY writer of `trust`
  (satisfaction/regret-weighted erosion and slow recovery, clamped [0,1], initializing from
  `platformTrust` on first touch). Package B reads `trust` and owns every other field.
- `HiddenUserState` gains `HiddenExposureState exposure;` and `HiddenRetentionState retention;`
  (scaffold edit, default-initialized — NO generator changes, NO new draws; gate-off runs never
  touch them).

## 3. Package A component — `rr::PreferenceEvolution`

`include/rr/simulation/preference_evolution.hpp` (scaffold freezes the interface; A implements):

```cpp
class PreferenceEvolution {
  public:
    PreferenceEvolution(const EvolutionConfig &cfg /* eta_evo */);
    // Applied once per impression AFTER BehaviourModelV2/stepV2, gate-on only. Deterministic:
    // draws ZERO rng values (the "preference-evolution" stream stays reserved-unused, documented).
    // Mutates: hidden preference channels (semantic + the three modality preferences) per §4.15
    //   p' = normalize((1-eta_u)p + eta_u * s * v) with s = latent.satisfaction (HIDDEN, not
    //   reward), eta_u = cfg.etaEvo * plasticityScale(hidden.preferencePlasticity);
    //   updates exposure accumulators (§4.16 exhaustion/burnout/novelty/aversion, incl. their
    //   away-decay via (now - lastTouched) closed form); erodes/recovers retention.trust.
    void applyImpression(HiddenUserState &hidden, const Reel &reel,
                         const LatentReaction &latent, Timestamp now);
};
```

Saturation/aversion ALSO modulate future latent satisfaction: A owns `behaviour_model_v2.cpp` this
phase and adds an exposure-state adjustment inside the latent computation, mirroring the P16
fatigue-modulation pattern (`fatigueSatisfactionDelta`) — session fatigue (resets between
sessions) and exposure state (persists, decays over days) stay structurally separate (§4.16 /
exit-criterion 3).

Interaction with scheduled drift (P10): drift retargets `preferredTopics`/preference exogenously at
its interaction mark; evolution keeps operating on the retargeted vector afterward. Both may
coexist; A documents order (drift applies before the step, evolution after) and covers it with a
determinism test.

## 4. Package B component — `rr::RetentionModel`

`include/rr/simulation/retention_model.hpp` (scaffold freezes; B implements):

```cpp
class RetentionModel {
  public:
    RetentionModel(const RetentionConfig &cfg);
    // Called at each session end (event runner exit path), BEFORE nextReturnDelay.
    void onSessionEnd(HiddenUserState &hidden, double sessionMeanSatisfaction,
                      double sessionMeanRegret, Timestamp exitAt);
    // Replaces P18's computeReturnDelay under the gate. Draws on the "scheduling" stream at the
    // SAME call site (wholesale replacement of the P18 return-delay consumer under retention.enabled
    // — the D19 replacement clause; gate-off draw sequence unchanged). Inputs per §4.17:
    // last-session satisfaction/regret, habitStrength, baselineDailyUsage, time-of-day curve
    // (named-constant curve over now % 86400), retention.trust. May set retention.churned
    // (delay > cfg churn threshold) — caller then schedules NO ReturnToApp.
    Timestamp nextReturnDelay(HiddenUserState &hidden, Timestamp now, Rng &schedulingRng);
    double churnProbability(const HiddenUserState &hidden) const; // model-implied, for metrics
};
```

Runner wiring (B owns `src/evaluation/event_driven_runner.cpp` this phase): scaffold marks the two
call sites (session-end block ~where drainOpenSessions/exit handling records sessions; the
`computeReturnDelay` call) with `// P20-HOOK` comments; B implements the gate switch, churn skip,
and per-day long-term accumulation. Retention monotonicity test: better last-session welfare ⇒
stochastically sooner return.

## 5. Long-term metrics schema (FROZEN — B implements, C consumes)

`ExperimentResult` gains `LongTermReport longTerm;` (scaffold adds the struct + member to
`include/rr/evaluation/experiment_runner.hpp`; zeros/false when unconfigured):

```cpp
struct LongTermReport {
    bool configured = false;           // true iff preference_evolution || retention.enabled
    bool retentionConfigured = false;  // true iff retention.enabled (event mode)
    double retention1d = 0.0;          // fraction of users with >=1 session STARTING in
                                       // (userFirstDayEnd, +1 day]; userFirstDayEnd = end of the
                                       // simulated day containing the user's first session
    double retention7d = 0.0;          // same with +7 days
    double sessionsPerUserPerDay = 0.0;
    double satisfactionWeightedRetention = 0.0; // sum_u(retained7d_u * satbar_u)/sum_u(satbar_u),
                                                // satbar_u = max(0, user mean session satisfaction)
    double churnRate = 0.0;                     // churned users / users
    double meanChurnProbability = 0.0;          // mean of model churnProbability at run end
    double meanFinalTrust = 0.0;
    double meanFinalHabit = 0.0;
    double meanPreferenceShiftFromInitial = 0.0; // mean_u (1 - cos(p_u(0), p_u(T))), semantic
    double meanFinalPreferenceEntropy = 0.0;     // mean_u entropy of softmax over topic-centre
                                                 // cosine similarities (documented in impl)
    std::vector<LongTermDayPoint> byDay;         // longterm_metrics.csv rows
};
struct LongTermDayPoint {
    uint32_t day;                 // simulated day index from run start
    uint64_t sessions;
    uint64_t activeUsers;         // users with >=1 session that day
    double sessionsPerActiveUser;
    double meanSessionSatisfaction;
    double meanTrust;             // mean over ALL users at day end (uninitialized trust reads
                                  // as the user's platformTrust trait)
    uint64_t cumulativeChurned;
    double meanPreferenceShiftFromInitial; // as-of day end
};
```

results_writer (B): summary.json top-level block `long_term` mirroring the struct with snake_case
keys exactly (`retention_1d`, `retention_7d`, `sessions_per_user_per_day`,
`satisfaction_weighted_retention`, `churn_rate`, `mean_churn_probability`, `mean_final_trust`,
`mean_final_habit`, `mean_preference_shift_from_initial`, `mean_final_preference_entropy`, plus a
`note` documenting the counterfactual-distortion method below); `longterm_metrics.csv` with header
`day,sessions,active_users,sessions_per_active_user,mean_session_satisfaction,mean_trust,`
`cumulative_churned,mean_pref_shift_from_initial` (fixed precision, deterministic, written only
when `configured`). Welfare trust column goes LIVE (P15 placeholder): `welfare` summary block and
`welfare_metrics.csv`'s trust field emit mean trust as defined for `meanTrust` above.

Per-user export for the distortion measure (B, gate-on only, evaluation carve-out):
`hidden_preference_final.csv` with header
`user_id,plasticity,churned,sem_shift,visual_shift,music_shift,emotional_shift,sem_v0..sem_v{D-1}`
(shift = 1 - cos(channel p(0), p(T)); rows ascending user_id; fixed precision).

**Preference-distortion measure (documented method, computed by C's tooling):** run the SAME
config+seed twice, once with `realism.preference_evolution=false` (the counterfactual world);
per-user distortion = 1 - cos(sem_final_on, sem_final_off) between MATCHED user rows of the two
runs' `hidden_preference_final.csv`. In-run `mean_preference_shift_from_initial` is the
within-world view; the cross-run number is the headline (exit criterion 1).

## 6. Package C — experiment + tooling (no C++)

- `configs/realism-medium-retention.json`: derived from `configs/realism-medium-events-drift.json`
  minus the `drift` block (isolate endogenous evolution; drift interaction is documented, not
  swept), plus `realism.preference_evolution=true`, `retention.enabled=true`,
  `serving.prefetch_depth=10` (P19: near-flat adaptation delay at 4.6x lower cost),
  `simulation.horizon_seconds=777600` (9 simulated days — covers `retention_7d` with margin).
- `scripts/run_phase20_experiment.sh` (P19 script's structure/JSON-patch pattern): FOUR arms —
  `engagement-on`, `engagement-off`, `proxy-on`, `proxy-off`; policy = P15's engagement /
  satisfaction-proxy ranking-weight presets (copy the weight patches from
  `results/published/phase15/` arm configs); `-off` twins set `realism.preference_evolution=false`
  AND `retention.enabled=false` (full gate-off counterfactual world for distortion AND a
  fixed-schedule baseline for retention comparison), same seed 42.
- `scripts/phase20_comparison.py`: policy divergence = per-user matched comparison
  engagement-on vs proxy-on (distribution + mean of 1-cos between matched users' final semantic
  preferences); per-policy distortion vs its `-off` twin (method in §5); retention/churn/trust/
  session-health table across arms; comparison.{md,csv} to `results/published/phase20`.
- `scripts/plot_results.py` additions: preference-divergence histogram (matched-user 1-cos, one
  series per policy pair), retention curve (retention_1d..7d or per-day active share from
  `longterm_metrics.csv`), trust trajectory (mean_trust by day, one line per arm). Read ONLY the
  frozen §5 key names — no candidate-path guessing this phase.

## 7. Tests (task 6 split)

- A (`tests/unit/preference_evolution_test.cpp`, `tests/property/evolution_*.cpp`): §4.15 update
  hand-computed (incl. normalize + plasticity scaling); satisfaction-driven-not-reward-driven
  (constructed ragebait case: positive reward, negative satisfaction ⇒ preference moves AWAY —
  directional assert); saturation directionality; aversion-after-regret; novelty depletion;
  away-time baseline reversion (closed form); separation from session fatigue AND from scheduled
  drift (coexistence determinism); trust erosion under repeated regret; 24-seed gate-off
  byte-identity/stream-discipline property suite (P13–P17 pattern).
- B (`tests/unit/retention_model_test.cpp`, `tests/property/retention_*.cpp`): retention
  monotonicity (statistical, model-level); habit strengthen/decay unit; churn determinism (same
  seed ⇒ identical churn set; threshold semantics exact); longterm CSV/summary determinism +
  schema; hidden_preference_final export correctness; trust column live; policy-divergence
  statistical test (reduced-scale in-process, engagement vs proxy presets, matched-user divergence
  margin) — in B's worktree A's evolution is a no-op stub, so follow the EXPECTED-FAIL/SKIP
  pending-integration protocol (P19 precedent), keyed on a runtime probe of whether evolution
  moved any preference, NOT on schema guessing.
- Golden obligations at integration: D17 small + drift-medium goldens byte-identical gates-off;
  P18 event-digest golden PASS (its config leaves both new gates absent).

## 8. Ownership map (disjoint files after scaffold)

- Scaffold: config.{hpp,cpp}; both hidden headers (minimal); both component headers + no-op .cpps;
  HiddenUserState members; `// P20-HOOK` call-site marks + gate plumbs in event_driven_runner.cpp
  and the legacy runner's evolution hook; LongTermReport struct + member; results_writer stub
  (empty long_term when unconfigured); CMake additions; this file.
- A: `src/simulation/preference_evolution.cpp`, `hidden_exposure_state.hpp`,
  `behaviour_model_v2.cpp`, A's test files.
- B: `src/simulation/retention_model.cpp`, `hidden_retention_state.hpp`,
  `src/evaluation/event_driven_runner.cpp`, `src/evaluation/results_writer.cpp` (+ its hpp if
  needed), welfare trust wiring, B's test files.
- C: `configs/realism-medium-retention.json`, `scripts/run_phase20_experiment.sh`,
  `scripts/phase20_comparison.py`, `scripts/plot_results.py`, no C++.
- Contended-by-declaration (report, don't edit): config files, CMakeLists, any file not listed.
