# ReelRank Plan — Part 3: Experiments, Scale, and Presentation (Phases 10–12)

One phase = one Claude session. Read `plan/00-DESIGN-DECISIONS.md`, this file's phase section,
`commit.md`, and the referenced TDD sections before starting. Follow the D1 session protocol.

---

## Phase 10 — Preference drift and adaptation

**TDD refs:** §11.4, §18.6, §28 Phase 10.
**Prerequisites:** Phase 7 (session/long-term learning must exist); Phase 8–9 recommended.

### Objective
Scheduled hidden-preference changes with measured adaptation: how fast does each strategy recover?

### Tasks
1. Drift scheduler: config-driven list of `{atInteraction, newTopicMix}` events per user cohort (TDD §11.4 example: fitness→travel at interaction 500). Applied to `HiddenUserState` only, by the simulator, deterministically.
2. Adaptation metrics (TDD §18.6): reward drop at drift, recovery time (interactions to return to 95% of pre-drift reward), estimated↔hidden distance over time, cumulative regret during the adaptation window → `regret_curve.csv` + additions to `learning_curve.csv`.
3. Tests: unit (drift application math, event scheduling); integration (drift fires at the configured interaction, hidden state changes, recommender-visible state does not change instantaneously); statistical (post-drift reward drops then recovers with learning enabled; session-aware blend recovers faster than long-term-only — run both configurations); determinism.
4. Experiments: (a) drift recovery for session-aware (0.65/0.35) vs long-term-only (1.0/0.0) vs session-heavy (0.4/0.6); (b) learning-rate sensitivity (η_session ∈ {0.05, 0.15, 0.3}). Publish `results/published/phase10/` with recovery plots (scripts/plot_results.py).

### Exit criteria (TDD §28 P10)
- [ ] Drift experiments deterministic and config-driven.
- [ ] Session-aware models measurably adapt faster than static/long-term-only (published).
- [ ] Recovery curves plotted.

### Session kickoff prompt
> Read plan/00-DESIGN-DECISIONS.md, plan/03-PHASES-EXPERIMENTS.md (Phase 10), commit.md, TDD §§11.4, 18.6. Implement the drift scheduler and adaptation metrics test-first, run the session-weight and learning-rate drift experiments, and publish recovery curves. Update commit.md per protocol.

---

## Phase 11 — Load, latency, and scale

**TDD refs:** §17.3 (1M row), §18.7, §24.5, §27, §28 Phase 11; D13.
**Prerequisites:** Phases 5–9 (the full pipeline is what gets load-tested).

### Objective
Measured throughput and tail latency of the full recommender under concurrency and at large corpus
sizes, on documented hardware, with bottlenecks profiled.

### Tasks
1. **First task (D13):** verify `HNSWIndex::search() const` is safe for concurrent readers on a frozen index (inspect for mutable members/scratch state in the current vector-db source; write a ThreadSanitizer test). If unsafe: per-thread index replicas, decision recorded in commit.md.
2. `apps/benchmark_recommender.cpp`: multi-threaded closed-loop load driver — T client threads, each drawing distinct users, issuing full recommendation requests (no interaction simulation on the timed path); measures RPS, p50/p95/p99 end-to-end and per stage (retrieval/ranking/reranking), CPU%, peak RSS. T ∈ {1, 2, 4, 8, #cores}. Latency histograms via the D12 harness.
3. Corpus scaling: 10k / 100k / 1M reels (1M is the stretch: generate, build index — record build time and memory; run the medium benchmark suite against it). Dimension sweep 64/128 at 100k.
4. Complete the TDD §17.3 benchmark grid at 100k and (feasible subset at) 1M; extend `benchmark_retrieval` if gaps remain from Phase 1.
5. Profile one hot run (Instruments/`sample` on macOS) — name the top-3 bottlenecks in the report; do **not** optimize beyond trivial fixes (record optimization candidates as future work).
6. Compare results against TDD §27 targets (small: e2e p95 <10ms; medium: retrieval p95 <10ms, e2e p95 <25ms, Recall@10 >90%) — report pass/fail honestly per target.
7. Publish `results/published/phase11/` with full hardware/build metadata (D12) and throughput/latency plots.

### Exit criteria (TDD §28 P11)
- [ ] Throughput + p50/p95/p99 measured on documented hardware across thread counts and corpus sizes.
- [ ] Bottlenecks profiled and named.
- [ ] §27 targets evaluated pass/fail; no unsupported claims.

### Session kickoff prompt
> Read plan/00-DESIGN-DECISIONS.md, plan/03-PHASES-EXPERIMENTS.md (Phase 11), commit.md, TDD §§17.3, 18.7, 24.5, 27. First verify concurrent-read safety of HNSWIndex::search (TSan test). Then build the multi-threaded benchmark_recommender load driver, run the thread/corpus/dimension sweeps including the 1M stretch, profile, compare against §27 targets, and publish. Update commit.md per protocol.

---

## Phase 12 — Documentation and presentation

**TDD refs:** §26 (graphs), §28 Phase 12, §32–34.
**Prerequisites:** all previous phases (gaps allowed if unfinished phases are honestly listed as limitations).

### Objective
A portfolio-ready repo: another engineer can build, run, and reproduce every published number.

### Tasks
1. `README.md` (reel-rank): what/why, architecture diagram (mermaid or committed SVG mirroring TDD §7), build + run instructions verified from a clean clone, config reference, results summary tables with hardware context.
2. Results consolidation: regenerate the TDD §26 recommended graphs from published CSVs via `scripts/plot_results.py` (recall-vs-efSearch, recall-vs-latency, reward curves, cumulative regret, HNSW-vs-exact quality, diversity-vs-reward, cold-start, drift recovery, throughput-vs-clients, p99-vs-corpus). Commit plots to `results/published/figures/`.
3. `docs/RESULTS.md`: the core engineering question (TDD §3) answered with numbers — recall/latency/reward trade-off narrative; each secondary question from §3 answered in one paragraph with a pointer to its experiment.
4. `docs/LIMITATIONS.md` + future work (TDD §33): honest gaps, un-hit targets, deferred stretch goals (service split, Thompson sampling, 1M+ if not done).
5. Demo script: `scripts/demo.sh` — small config end-to-end + inspect_user showing a feed with ranking explanations, < 2 minutes.
6. Resume bullets (TDD §34 narrative, kept accurate to what was measured).
7. Reproducibility audit: clean-clone build on this machine following only the README; re-run one published experiment and confirm the numbers match; fix any drift.
8. MVP checklist audit against TDD §32 — every item ticked or listed as a limitation.

### Exit criteria (TDD §28 P12)
- [ ] Clean-clone reproduction verified.
- [ ] All claims traceable to committed results with hardware metadata.
- [ ] README/RESULTS/LIMITATIONS complete; demo runs.

### Session kickoff prompt
> Read plan/00-DESIGN-DECISIONS.md, plan/03-PHASES-EXPERIMENTS.md (Phase 12), commit.md, TDD §§26, 32–34. Write the README/RESULTS/LIMITATIONS docs, regenerate all recommended figures from published CSVs, build the demo script, then perform the clean-clone reproducibility audit and the §32 MVP audit. Update commit.md per protocol and mark the project MVP-complete or list what remains.
