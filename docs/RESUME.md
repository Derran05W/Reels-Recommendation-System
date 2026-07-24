# ReelRank — Portfolio Narrative and Resume Bullets

Portfolio-ready summary of the project (TDD §34), with every number traceable to a committed
artifact under [`../results/published/`](../results/published/). Nothing here is rounded in the
project's favour, and every latency/recall claim names its context (hardware, threading, data
regime).

## Narrative

The sanctioned one-paragraph description (TDD §34), which the measurements support as written:

> Designed and implemented a simulated short-form video recommendation platform backed by a custom
> C++20 vector database. Used HNSW approximate nearest-neighbour search for candidate generation,
> followed by feature-based ranking, online preference updates, exploration, and diversity-aware
> re-ranking. Benchmarked approximate retrieval against exhaustive search across recommendation
> quality, Recall@K, throughput, and p99 latency.

**Scope note (for accuracy).** The project emphasizes two distinct accomplishments (TDD §34):
(1) the low-level vector-search infrastructure — a custom C++20 HNSW vector database, which ReelRank
wraps, differentially validates against exact search, sweeps across its full parameter grid, and
verifies for concurrent-read safety — and (2) applying that infrastructure to a realistic
multi-stage recommendation workflow. All quality/reward/recall numbers are on a **synthetic** domain
with a known ground-truth hidden preference (which is what makes affinity, regret, and alignment
measurable); the objective is to demonstrate the retrieval and recommendation machinery end-to-end,
not to reproduce any real platform.

## Resume bullets

Grouped by the two accomplishments. Each bullet's evidence pointer is in the trailing comment.

**Vector-search infrastructure and performance engineering**

- Benchmarked HNSW approximate nearest-neighbour retrieval against exhaustive brute-force search in
  a C++20 recommendation simulator: matched exact-search recommendation quality within **1.5%** (mean
  true affinity) and **0.12%** (oracle regret) while cutting retrieval latency **48–62×**
  (p50 0.13 ms vs 6.33 ms) over 2,000,000 impressions.
  <!-- results/published/phase5/comparison.md; hnsw vs exact_vector, medium config, single-threaded -->

- Ran a full HNSW parameter grid (M × efConstruction × ef_search × dimension) on up to **1,000,000**
  vectors; achieved **0.92–0.93 Recall@10** at the shipped configuration on production-like
  topic-clustered data using **~10,800 distance computations per query vs 100,000 for brute force
  (9.3× fewer)**, and characterized the recall / latency / insertion-cost / memory trade-offs
  (incl. efConstruction diminishing returns past 200, and 128d beating 64d on clustered data).
  <!-- results/published/phase1/ + results/published/phase11/retrieval/ (clustered-100k, clustered-10k, dist-100k-d64) -->

- Verified data-race-free concurrent reads on a frozen HNSW index (**zero ThreadSanitizer
  findings**) and built a multi-threaded closed-loop load driver; measured throughput scaling
  **349 → 1,821 RPS from 1 to 10 threads at 100k reels** on an Apple M5, and profiled the pipeline to
  identify O(catalog) candidate-source scans (**~70% of request self time**, vs ~12% for HNSW) as the
  scaling bottleneck.
  <!-- results/published/phase11/comparison.md, concurrency/VERDICT.md, profiling/BOTTLENECKS.md; serialized/idle-machine runs -->

**Multi-stage recommendation workflow**

- Built a five-stage recommendation pipeline — HNSW candidate generation → feature-weighted ranking
  → online preference learning → ε-greedy exploration → diversity-aware re-ranking — lifting
  reward/impression **2.25× via second-stage ranking** and a further **3.17× via online preference
  learning**, while driving feed repetition to **exactly 0** and per-item topic diversity **+89%**
  under hard diversity constraints.
  <!-- ranking: results/published/phase6/; learning: results/published/phase7/; diversity+repetition: results/published/phase9/ -->

- Modeled scheduled preference drift and showed a session-aware online learner **dominates a
  long-term-only learner on preference alignment at every post-drift round** (final estimated↔hidden
  cosine **0.66 vs 0.47**) and on post-drift drifted-cohort reward (**+46%**), while a static model
  never adapts.
  <!-- results/published/phase10/comparison.md + comparison.csv; adaptation read via round-by-round dominance and slopes -->

**Engineering rigor (both)**

- Engineered fully deterministic, seed-reproducible experiments (portable in-house RNG samplers,
  byte-identical metric CSVs verified at 2M-impression scale) covered by **533 automated tests**
  across unit / integration / property / differential suites, and published every result with full
  hardware / build / SHA provenance (`metadata.json` per run).
  <!-- decision D8 (determinism); test count from phase 11 (533/533 green); metadata.json in every results/published/*/ run dir -->

**Realism upgrade — behavioral modeling, event-driven simulation, and learned ranking (V2, Phases
13–23)**

- Extended the simulator with a multi-channel hidden-satisfaction model decoupled from observable
  engagement (separate latent reaction vs. sampled behaviour) and showed the decoupling is
  measurable: an engagement-optimized ranking preset wins reward/impression (+7.5%) and serves **2×
  the ragebait exposure** of a semantic baseline while *losing* hidden satisfaction, whereas a
  satisfaction-proxy preset **nearly doubles hidden satisfaction** (+98.9%) at comparable reward and
  ~9.6× lower regret.
  <!-- results/published/phase15/comparison.md; oracle-bounded 4-arm experiment, 2M impressions/arm -->
- Built a probabilistic session-exit model (fatigue/regret/satisfaction-driven, five-way classified)
  and a personalized diversity re-ranker driven by an observables-only tolerance estimator
  (r=0.525 correlation with the hidden trait it never reads); personalized diversity beat fixed
  diversity on session utility for **all four** tested trait cohorts (+1.9% to +15.8%).
  <!-- results/published/phase16/, phase17/comparison.md; ToleranceEstimatorTest.EstimateCorrelatesWithHiddenRepetitionTolerance -->
- Replaced round-robin simulation with a deterministic event-driven runner (independent per-user
  timelines over a priority queue with a pinned tie-breaker hash) and used it to expose a
  feed-prefetch-depth frontier: batching 20 reels per request cuts ranking computations and feed
  requests **79.4%** versus batching 1, at **flat post-drift adaptation delay** (+0.5%), with the
  freshness cost showing up on staleness metrics instead.
  <!-- results/published/phase19/comparison.md; BatchAdaptationStatisticalTest.Batch1AdaptsNoSlowerThanBatch20 -->
- Modeled exposure-driven preference evolution and a hazard-based retention/churn model, then showed
  a ranking policy measurably reshapes long-term outcomes for the **same 10,000 matched users** on
  an identical world: hidden-preference divergence 0.0388 between policies, with the
  engagement-optimized policy driving **26.5% churn** versus a satisfaction-proxy policy's **9.0%**.
  <!-- results/published/phase20/comparison.md; PolicyDivergenceTest.EngagementVsProxyDivergesOncePreferencesMove -->
- Ran a seven-scenario ecosystem failure-mode suite (filter bubbles, ragebait amplification,
  popularity feedback, niche starvation, creator overconcentration, exploration recovery,
  satisfaction-vs-retention) with pre-registered hypotheses, publishing honest verdicts including two
  test-enforced confirmations, three partial/inverted findings, and one non-confirmation (retention
  and welfare co-move rather than trade off) — reported as found, not fit to expectations after the
  fact.
  <!-- results/published/phase21/ECOSYSTEM.md; RagebaitAmplificationStatisticalTest, ExplorationRecovery test suites -->
- Built a leak-proof training-data logging pipeline (positions, exploration provenance, an automated
  emitted-file purity audit) and in-house deterministic learners beating frequency baselines offline
  (completion AUC 0.714 vs. 0.500), then closed the loop with a learned multi-objective ranker that
  **dominates** the hand-tuned system on identical worlds — better reward, hidden satisfaction,
  session utility, and trust simultaneously (0.404 vs. 0.399 reward at −0.202 vs. −0.298
  satisfaction) — with a weight-vector sweep showing satisfaction-weighted serving is
  engagement-*positive*, not a trade-off, because retention is itself satisfaction-driven.
  <!-- results/published/phase22/offline_eval.md, phase23/comparison.md; LearnedRankerPipelineTest.ServingPurityParity -->
