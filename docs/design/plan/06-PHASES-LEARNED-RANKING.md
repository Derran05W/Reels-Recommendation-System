# ReelRank Plan — Part 6: Learned Ranking and Completion (Phases 22–24)

Realism V2, Release E + close-out (V2 TDD Tier 5, §10). One phase = one Fable session (plan →
parallel opus/sonnet subagents → integrate → verify → commit). Read `plan/00-DESIGN-DECISIONS.md`,
`plan/00-DESIGN-DECISIONS-V2.md`, `commit.md`, and the referenced V2 TDD sections
(`REELS-SIMULATION-V2.md`) before starting. The D17 standing golden-baseline exit criterion and
D18 guard apply to every phase (see Part 4 header). Tier 5 deliberately lands after the hidden
world is stable (V2 §9 Release E rationale). Do not start a phase whose prerequisites are
unticked in `commit.md`.

---

## Phase 22 — Training-data logging and offline learned models

**V2 TDD refs:** §4.19, §4.20 (items 1–2), §4.22, §7 (logged-features purity test), §5
(InteractionEvent/User). D21 is the binding model-scope contract.
**Prerequisites:** Phases 18 and 20 (stable event-driven hidden world per V2 §9; Phase 21
recommended).

### Objective
A leak-proof logged-training pipeline — eligibility through impression with positions and
exploration provenance — plus in-house logistic/linear models for the V2 §4.19 targets, evaluated
offline against baselines on held-out data from identical worlds.

### Tasks
1. Impression logging pipeline (V2 §4.22) behind `learning_v2.training_log`: per request, record
   eligible → retrieved → ranked → shown candidate sets with positions, per-candidate features
   AS SERVED (the serving-time feature vector, versioned schema), exploration probability +
   exploration/provenance flags, request/user/session ids and timestamps; outcomes joined from
   InteractionEvents (incl. `observedExitAfterImpression`). Sampled/rotating file strategy for
   size (config; document what full-fidelity mode costs). Format: CSV or compact binary +
   schema JSON (D6 discipline).
2. **Purity enforcement (V2 §7, D18):** schema-level allowlist audit test — the training log
   contains no hidden field (latent, archetype, hidden traits, trust, session hidden state);
   designated evaluation outputs are excluded from feature files by construction (separate
   label tables). The audit runs on real emitted files, not just structs.
3. Explicit-feedback survey (V2 §4.19): sampled noisy satisfaction survey on
   `"explicit-feedback"` (rate + noise config-driven, e.g. Likert-quantized noisy
   `immediateSatisfaction`), emitted to a separate labeled table — the ONLY sanctioned
   hidden-derived training signal, clearly marked; off by default.
4. In-house learners (D21): deterministic mini-batch SGD logistic regression for completion /
   like / share / follow / not-interested / session-exit; linear regression for watch ratio;
   satisfaction-proxy regressor trained only on the survey subset when enabled. Streams
   `"training-split"` (temporal + user-disjoint split options) and `"model-init"`. Feature
   scaling documented; JSON model serialization round-trip (D6/D21).
5. Offline evaluation: held-out AUC/log-loss (binary), RMSE + calibration (watch ratio),
   vs baselines — global frequency, per-source frequency, and the hand-tuned `WeightedRanker`
   score used as a ranking-only predictor; `training_eval.csv` + report. Identical-worlds
   discipline: log generation configs and seeds pinned per D8/D17 so model comparisons share
   worlds (Tier 5 acceptance).
6. Tests: purity audit (mandated); split determinism (same seed ⇒ identical splits/batches);
   learner convergence on synthetic separable data (unit); learned-beats-frequency-baselines
   offline (statistical, per target where signal exists — report honestly any target where it
   doesn't, e.g. rare follows); serialization round-trip; golden regression (logging off ⇒
   byte-identical).

### Exit criteria
- [ ] Position/exposure/eligibility metadata persisted end-to-end (Tier 5 acceptance 4).
- [ ] Training data provably hidden-free (acceptance 2; audit on emitted files).
- [ ] Learned models beat frequency baselines on held-out observable outcomes offline
      (acceptance 1, offline half; honest per-target reporting).
- [ ] `results/published/phase22/` — offline eval report + calibration plots.

### Out of scope
Serving with learned models (23); GBT/neural (D21 stretch, only post-baselines and likely
deferred); position-bias correction methods (log supports future study — V2 §4.22 — not built).

### Suggested package split
A (opus): logging pipeline + schema + purity audit + survey mechanism.
B (opus): learners + splits + offline evaluation harness.
C (sonnet): baseline comparisons, calibration plots, report.
Fable: config surface (`learning_v2.*`), frozen log-schema header, integration.

### Session kickoff prompt
> Read plan/00-DESIGN-DECISIONS.md, plan/00-DESIGN-DECISIONS-V2.md (D21 especially),
> plan/06-PHASES-LEARNED-RANKING.md (Phase 22), commit.md, V2 TDD §§4.19–4.20, 4.22, 7.
> Implement the eligibility→impression training log with positions/exploration provenance and
> the emitted-file purity audit, the optional noisy survey table, and in-house logistic/linear
> learners with deterministic splits, test-first; publish the offline evaluation vs baselines.
> Update commit.md per protocol.

---

## Phase 23 — Learned multi-objective ranking in the loop

**V2 TDD refs:** §4.21, Tier 5 acceptance (closed-loop half), §6, §10 item 8.
**Prerequisites:** Phase 22.

### Objective
A `LearnedRanker` serving the V2 §4.21 multi-objective value function from the P22 models,
retrained periodically inside the simulation, compared closed-loop against the hand-tuned system
on identical worlds — and a weight-vector sweep exposing the engagement/satisfaction/retention
trade-off frontier.

### Tasks
1. `LearnedRanker` implementing `Ranker` behind `learning_v2.learned_ranker`: value =
   w_watch·predictedWatch + w_share·predictedShare + w_follow·predictedFollow +
   w_satisfaction·predictedSatisfactionProxy − w_exit·predictedExit − w_regret·(designed
   observable regret proxy — document the mapping from §4.21's predictedRegret given regret is
   hidden; e.g. the survey-trained proxy or an early-abandon/not-interested composite; the
   choice is a recorded design decision). Weight vector config-driven; per-candidate predicted
   components stored in `featureContributions`-style explanation output (V1 §14.4 parity).
2. In-loop retraining schedule: deterministic — retrain every K simulated hours on the log so
   far (windowing config), model versions stamped into results; cold-start fallback to
   `WeightedRanker` until minimum-data threshold; retraining cost accounted (wall-clock outside
   simulated time, documented).
3. Closed-loop experiments on identical worlds/streams (Tier 5 acceptance): (a) learned vs
   hand-tuned `WeightedRanker` vs semantic baseline — all four §6 groups + long-term metrics;
   (b) weight-vector sweep (w_satisfaction × w_watch grid, w_exit on/off) → the multi-objective
   frontier (engagement vs welfare vs retention); (c) survey-on vs survey-off arm — does sampled
   explicit feedback rescue the satisfaction axis?
4. Offline-vs-closed-loop gap analysis (acceptance 5): per-model offline metrics vs the same
   models' closed-loop outcomes; document divergences (feedback-loop effects on their own
   training distribution).
5. Tests: learned arm beats fixed baselines on held-out observable outcomes AND is competitive
   closed-loop on engagement at defaults (statistical); serving-path purity (D18 guard covers;
   an explicit test that LearnedRanker features == logged serving-time features); retraining
   determinism (same seed ⇒ identical model sequence); explanation output well-formed; golden
   regression.

### Exit criteria
- [ ] Tier 5 acceptance complete: offline AND closed-loop reporting; identical-world
      comparisons; training purity held through serving (V2 §10 item 8).
- [ ] Trade-off frontier published: at least one weight vector demonstrably trades engagement
      for satisfaction+retention vs the pure-engagement vector.
- [ ] `results/published/phase23/` complete with frontier plots + explanation example.

### Out of scope
GBT/neural upgrades (documented future work unless trivially in-budget post-baselines — D21);
off-policy correction/counterfactual estimators (future work; the log supports them).

### Suggested package split
A (opus): `LearnedRanker` + retraining schedule + explanation output.
B (opus): closed-loop experiment matrix + gap analysis.
C (sonnet): frontier/gap plots + comparison.md.
Fable: config, weight-vector presets review, integration.

### Session kickoff prompt
> Read plan/00-DESIGN-DECISIONS.md, plan/00-DESIGN-DECISIONS-V2.md, plan/06-PHASES-LEARNED-
> RANKING.md (Phase 23), commit.md, V2 TDD §4.21 + Tier 5 acceptance, §6. Implement LearnedRanker
> over the Phase 22 models with deterministic in-loop retraining, test-first; run the learned-vs-
> hand-tuned closed-loop comparison, the weight-vector frontier sweep, and the survey arm on
> identical worlds; publish with offline-vs-closed-loop gap analysis. Update commit.md per
> protocol.

---

## Phase 24 — V2 documentation, reproducibility, and completion audit

**V2 TDD refs:** §10 (all ten items), §6. V1 TDD §§26, 32–34 conventions; V1 Phase 12 is the
template.
**Prerequisites:** Phases 13–23 (gaps allowed if honestly listed as limitations).

### Objective
The realism upgrade is presentable, reproducible, and audited: every V2 §10 completion item is
demonstrated with committed evidence or honestly recorded as a limitation, and the public repo
tells the V2 story.

### Tasks
1. **§10 completion audit** — the phase's spine: a ten-row evidence table (claim → test name /
   published experiment / figure) in `docs/RESULTS-V2.md`; items that fall short are recorded in
   LIMITATIONS with what's missing, not argued around.
2. Docs: README V2 section (what changed vs V1, gate map, event-mode quickstart);
   `docs/RESULTS-V2.md` (the engagement-vs-satisfaction story, session health, batch frontier,
   ecosystem verdicts, learned-ranking frontier — numbers with hardware context);
   `docs/LIMITATIONS.md` V2 additions (incl. anything deferred: GBT/neural, position-bias
   correction, clustered-1M, modality retrieval); `docs/RESUME.md` V2 bullets kept accurate.
3. Figures: canonical V2 set regenerated deterministically from published CSVs
   (`results/published/figures-v2/` + index) — engagement-vs-welfare scatter, exit taxonomy,
   fatigue/cohort curves, batch frontier, preference-divergence, retention curves, ecosystem
   headline plots, learned frontier.
4. Demo: extend `scripts/demo.sh` (or `demo_v2.sh`) — the two-worlds demo: same user, same seed,
   engagement-preset vs satisfaction-preset feed side by side with welfare/session outcome
   printout; budget < 2 minutes.
5. Reproducibility audit (V1 Phase 12 protocol): clean clone, build from README only, full test
   suite, re-run one published V2 experiment (pick a Phase 16 or 20 arm) and byte-diff the
   deterministic CSVs; regenerate figures byte-identically; fix any drift found and re-audit.
6. Fold V2 planning material into `reel-rank/docs/design/` (PHASE-HISTORY V2 addendum from
   commit.md entries; V2 TDD + Part 4–6 plans + D17–D25), mirroring the V1 publication
   convention (D25). Push; CI green across the matrix.
7. Final commit.md close-out: V2 checklist complete or honestly annotated; carried-issues ledger
   zeroed or explicitly moved to LIMITATIONS.

### Exit criteria
- [ ] All ten V2 §10 items demonstrated with evidence links, or listed as limitations with
      reasons — no unsupported claims.
- [ ] Clean-clone reproduction verified (build, tests, one published experiment byte-identical,
      figures byte-identical).
- [ ] Public repo updated: docs/figures/demo/design-folding committed, CI green.

### Out of scope
New mechanisms or experiments beyond small audit-driven fixes; anything that belongs in a future
V3 TDD.

### Suggested package split
A (sonnet): RESULTS-V2 + completion-audit table drafting (Fable verifies every evidence link).
B (sonnet): figures + demo.
C (opus): reproducibility audit executor (clean clone, re-runs, diffs).
D (sonnet): design-folding + PHASE-HISTORY addendum.
Fable: audit verdicts, LIMITATIONS honesty pass, final commits/push. Mostly non-C++ —
same-checkout parallelism fine.

### Session kickoff prompt
> Read plan/00-DESIGN-DECISIONS.md, plan/00-DESIGN-DECISIONS-V2.md, plan/06-PHASES-LEARNED-
> RANKING.md (Phase 24), commit.md, V2 TDD §10 and §6, and V1 TDD §§26, 32–34. Produce the
> ten-item completion-audit evidence table, write RESULTS-V2/LIMITATIONS/README updates,
> regenerate the V2 figure set, build the two-worlds demo, run the clean-clone reproducibility
> audit, fold V2 planning into docs/design, and push with CI green. Update commit.md per
> protocol and mark the V2 upgrade complete or list what remains.
