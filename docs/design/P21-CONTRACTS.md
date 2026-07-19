# Phase 21 — Frozen Contracts (Ecosystem Failure-Mode Suite)

Binding integration contract for Phase 21, written by the orchestrator before package launch
(P20 precedent). Plan: `Reels-Simulation/plan/05-PHASES-EVENTS-LONGTERM.md` Phase 21. TDD: V2
§4.18, Tier 4 acceptance 2–3, §6. D17–D25 apply. Scenarios use EXISTING mechanics; the only new
C++ is the scaffold's metric additions + one minor knob + the two mandated statistical tests.

## 1. New config surface (scaffold owns config.{hpp,cpp}; nobody else edits them)

- `evaluation.ecosystem_metrics` : bool, default false. Emits `ecosystem_metrics.csv` + the
  `ecosystem` summary block. Load-validation: requires `simulation.scheduler == "event_queue"`
  (day semantics). Default-off ⇒ byte-identical output for all existing runs (D17).
- `exploration.enable_at_day` : double, default −1.0 (no time gating — behaviour exactly as
  today). When ≥ 0: for any request with timestamp t where floor(t/86400) < enable_at_day, the
  per-slot exploration gates use EFFECTIVE ε = 0; at/after that day, the configured ε. The
  per-slot gates already draw bernoulli(ε) unconditionally for every slot
  (src/candidate_sources/exploration_candidate_source.cpp ~line 50), so the draw COUNT and
  stream alignment are unchanged — only outcomes flip, and ε=0 outcomes reproduce the
  established ε=0 behaviour exactly. Implemented inside ExplorationCandidateSource using the
  request timestamp; document at the ε member.

## 2. Frozen `ecosystem_metrics.csv` (per simulated day; emitted when ecosystem_metrics on)

Header, EXACTLY:
`day,impressions,creator_hhi,tail_creator_share,arch_genuinely_satisfying,arch_useful,arch_ragebait,arch_clickbait,arch_comfort,arch_polished_irrelevant,arch_niche_treasure,arch_background_music,niche_in_cohort_match_rate`

- `creator_hhi`: Σ_c (that-day impressions of creator c / that-day total impressions)².
- `tail_creator_share`: that day's impression share of creators OUTSIDE the top decile by
  CUMULATIVE impressions as of end-of-day (documented small/new-creator proxy — no creator
  injection exists in event mode).
- `arch_<name>`: that day's impression share by hidden archetype, all eight catalog names in
  index order (evaluation carve-out via HiddenReelState.archetypeIndex).
- `niche_in_cohort_match_rate`: among that day's `niche_treasure` impressions, the share where
  `rr::cohortHash01(userId)` falls in the reel's hidden [centre−width, centre+width] niche band;
  0 when the day has no niche impressions (with the impressions column disambiguating).
- Zero-impression days emit a row with zeros (day index continuity).

Summary block `ecosystem` (top-level, additive): `creator_hhi_final_day`,
`creator_hhi_whole_run`, `tail_creator_share_whole_run`, `arch_share_whole_run` (object keyed by
the eight names), `niche_in_cohort_match_rate_whole_run`, plus a `note` naming this contract.

## 3. `longterm_metrics.csv` addition

`LongTermDayPoint` gains `meanPreferenceEntropy` (per-day mean per-user softmax topic-similarity
entropy — same formula as the run-end `mean_final_preference_entropy`, same temperature
constant); emitted as a TRAILING CSV column `mean_preference_entropy` (appended last so existing
readers keep working). Gate: whenever long_term is configured (unchanged).

## 4. Scenario conventions (packages A/B/C)

- One config per scenario under `configs/scenarios/<scenario>.json`, derived from
  `configs/realism-medium-retention.json` (evolution+retention+serving depth 10, drift absent)
  unless the scenario's pre-registration says otherwise. Every scenario config carries a
  `description` STRING field with the PRE-REGISTERED block, written BEFORE any run:
  "HYPOTHESIS: … MECHANISM: … EXPECTED SIGNATURE: … VERDICT CRITERIA: …" (unknown keys are
  config-load errors — `description` must be added to the allowlist by the scaffold as a
  top-level ignored/documented field; scaffold adds `ExperimentConfig.description`, additive,
  echoed into the resolved config output).
- Every scenario sets `evaluation.ecosystem_metrics=true`, event mode, seed 42, and runs each
  arm with its own `--out` root under `results/phase21/<scenario>/<arm>/`.
- Arms per scenario: a control + 1–4 treatment arms on IDENTICAL worlds (same seed; only ranking
  weights / exploration / cohort_mix / archetype mixture differ, per the plan task list).
- Concurrency cap: at most 2 simulate processes per package at any time.
- Per-scenario notes: each package writes `results/phase21/<scenario>/NOTES.md` — the
  pre-registered block verbatim, the arm list with resolved dirs, the headline numbers pulled
  from summaries (python one-liners, not hand-typed), and the package's HONEST verdict
  recommendation incl. "effect not observed" where true. The integrator renders the final
  comparison/verdicts with package D's tooling.

## 5. Statistical tests (the two mandated effects; new disjoint files)

- Package A: `tests/property/exploration_recovery_statistical_test.cpp` — reduced-scale
  in-process event runs: neglected-interest world (exploit-heavy weights, ε=0 vs
  ε>0-from-day-K via exploration.enable_at_day); assert the recovery arm's late-window
  mean_preference_entropy (and/or neglected-channel exposure) exceeds the ε=0 control's with a
  margin calibrated at the demonstrated operating point (P20 convention: mechanism-alive
  tripwire, documented at the assert).
- Package B: `tests/property/ragebait_amplification_statistical_test.cpp` — reduced-scale:
  engagement-preset + susceptibility-heavy `realism.cohort_mix` vs satisfaction-proxy control on
  the same world; assert ragebait whole-run exposure share is higher AND (mean hidden
  satisfaction lower OR mean final trust lower) with calibrated margins (Tier 4 acceptance 2).
- Both tests read ONLY frozen names from §2/§3 and existing P15/P20 report fields.

## 6. Package D — consolidation tooling (non-C++)

- `scripts/phase21_scenarios.py`: renders per-scenario `comparison.{md,csv}` from an arm list
  (reads the four §6 groups + `long_term` + `ecosystem` via frozen keys; verdict cell filled
  from a `--verdict` argument or left "integrator judgement"); `--self-test` with synthetic
  fixtures.
- `scripts/plot_results.py` additions (append-only): `plot_creator_hhi_by_day`,
  `plot_archetype_share_by_day` (stacked or multi-line, 8 archetypes),
  `plot_entropy_by_day`, `plot_retention_welfare_frontier` (scatter: one point per arm,
  x=retention_7d, y=mean hidden satisfaction). Each warn-skips on absent inputs; synthetic
  fixture self-render then delete.
- `results/published/phase21/ECOSYSTEM.md` TEMPLATE (committed by D): seven scenario sections
  (hypothesis / design / headline numbers / verdict placeholder), intro, method notes,
  no-aggregate-score note. Integrator fills verdicts at publication.

## 7. Ownership map

- Scaffold: config surface + validation + `description` field; ExplorationCandidateSource time
  gate; ecosystem accumulators + writer emission; LongTermDayPoint entropy column; this file.
- A (worktree, C++ test + scenarios): `configs/scenarios/filter_bubble.json`,
  `configs/scenarios/exploration_recovery.json`, its statistical test file, its runs/NOTES.
- B (worktree, C++ test + scenarios): `configs/scenarios/ragebait_amplification.json`,
  `configs/scenarios/satisfaction_vs_retention.json` (weight-sweep arms), its statistical test
  file, its runs/NOTES.
- C (main checkout, non-C++): `configs/scenarios/popularity_feedback.json`,
  `configs/scenarios/creator_overconcentration.json`, `configs/scenarios/niche_starvation.json`,
  runs/NOTES for the trio.
- D (main checkout, non-C++): `scripts/phase21_scenarios.py`, plot additions, ECOSYSTEM.md
  template.
- Contended-by-declaration: config.{hpp,cpp}, CMakeLists, runners, results_writer — scaffold
  only; anything else unlisted → report, don't edit.
