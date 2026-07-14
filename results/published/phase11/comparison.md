# Phase 11 — Load, latency, and scale

Measured throughput and tail latency of the full recommender (complete initial system: six
candidate sources, weighted ranker, diversity reranker, ε=0.05 exploration with guaranteed slots)
under concurrency and at large corpus sizes, plus completion of the TDD §17.3 HNSW benchmark grid.

**Hardware/provenance:** Apple M5, 10 cores, 24 GB RAM, macOS (Darwin 25.5.0), AppleClang,
Release (-O3). All published runs executed **serially on an otherwise-idle machine** — unlike
Phases 7–10, no run overlapped any other work, so absolute latencies are uncontaminated (this was
a deliberate Phase 11 methodology decision; see "Methodology" below). Runs executed at merge
commit `537d883`, whose tree is byte-identical to the squashed `Phase 11:` commit that publishes
them; the `metadata.json` files therefore reference a SHA that the squash removes from history —
the content mapping is recorded here (same provenance class as the `reel_rank_dirty` notes of
Phases 1/4–10). Seed 42 throughout. TSan concurrency verdict: `concurrency/VERDICT.md`.
Profiling: `profiling/BOTTLENECKS.md`.

## TDD §27 targets — pass/fail

| Benchmark | Target | Measured | Verdict |
|---|---|---|---|
| Small (10k reels, 1k users, 64d) | e2e p95 < 10 ms | 0.53 ms (T=1) … 1.26 ms (T=10) | **PASS** (8–19×) |
| Small | HNSW Recall@10 > 90% | **0.934** @ default M16/efC200/ef64, clustered data (0.965 @ef256) | **PASS** |
| Medium (100k reels, 10k users, 64d) | HNSW p95 retrieval < 10 ms | pure HNSW search p95 **0.26 ms** (clustered) / 0.84 ms (random) at the k=500 operating point; full candidate-generation *stage* p95 3.9 ms (T=1) … **11.4 ms (T=10)** | **PASS** on the HNSW reading at every point; the six-source stage crosses 10 ms at T≥8 — attributable to the O(catalog) scans, not HNSW (see bottlenecks) |
| Medium | e2e p95 < 25 ms | 4.2 ms (T=1) … 12.0 ms (T=10) | **PASS** (2–6×) |
| Medium | HNSW Recall@10 > 90% | **0.919** @ default ef64, clustered 100k×64d (0.958 at 128d). Pipeline fetches k=500 ⇒ beam 500 > 256, so effective recall@10 ≥ the ef256 value **0.932**. Isotropic random: 0.47 @ef64 / 0.67 @ef128 (documented near-worst-case distribution, Phase 1 finding) | **PASS** on production-like data; random-vector worst case reported alongside |
| Large stretch (1M reels, 100k users) | none — "establish after measuring" | see below | measured |

## Load matrix (full recommender, closed-loop, per-thread pipelines + one shared frozen index)

Selected rows (full data: `load/*/load_metrics.csv`, 4 cell-groups × T ∈ {1,2,4,8,10}):

| Corpus | dim | T | RPS | e2e p50/p95/p99 (ms) | retrieval p95 | CPU% | peak RSS |
|---:|---:|---:|---:|---|---:|---:|---:|
| 10k | 64 | 1 | 2,583 | 0.31 / 0.53 / 0.55 | 0.42 | 100 | 40 MB |
| 10k | 64 | 10 | 15,814 | 0.55 / 1.26 / 1.33 | 0.98 | 959 | 40 MB |
| 100k | 64 | 1 | 349 | 2.06 / 4.16 / 4.36 | 3.88 | 100 | 287 MB |
| 100k | 64 | 10 | 1,821 | 4.97 / 12.01 / 13.20 | 11.44 | 975 | 287 MB |
| 100k | 128 | 10 | 1,385 | 5.36 / 15.48 / 18.03 | 14.97 | 976 | 352 MB |
| 1M | 64 | 1 | 37 | 15.26 / 43.89 / 44.73 | 43.58 | 100 | 2,042 MB |
| 1M | 64 | 10 | 144 | 42.64 / 125.31 / 134.55 | 124.62 | 978 | 2,042 MB |

**Scaling:** 1→10-thread throughput scales 6.1× at 10k, 5.2× at 100k×64d, 5.4× at 100k×128d,
3.9× at 1M — the ceiling tightens as the working set grows, the signature of a
memory-bandwidth-bound workload (CPU% is ~975% at T=10 in every cell: cores are busy, waiting on
DRAM). Ranking p95 ≤ 0.58 ms and reranking p95 ≤ 0.08 ms everywhere — the pipeline's cost lives
almost entirely in candidate generation.

**Corpus scaling is linear in catalog size:** e2e p99 at T=10 goes 13.2 → 134.6 ms for 100k → 1M
(10.2× for 10×), because the popular/trending/fresh/exploration sources scan the whole catalog
per request. The pure HNSW search at 1M is p95 ≤ 1.8 ms even at M32/ef256 — ANN is NOT the
scaling problem; the linear scans are.

## Profiled bottlenecks (plan task 5 — named, not optimized)

From `sample` profiles at 100k×64d (T=1 and T=8), `profiling/BOTTLENECKS.md`:
1. **O(catalog) candidate-source scans — ~69–72% of self time** (Popular + Trending + Fresh +
   Exploration + CreatorAffinity; `rr::dot` alone ~22–25%). Cause of the linear corpus scaling
   and the bandwidth-bound thread ceiling. Optimization candidate (recorded, deliberately NOT
   implemented per plan): the deterministic every-K-requests leaderboard cache designed in
   Phase 6.
2. **HNSW search — ~11–13%** (searchLayer + SIMD Euclidean).
3. **Tail: ranking/diversity/merge ~7%, `exp2` decay math ~5–6%, per-request allocation churn
   ~5%.** Feature extraction — the pre-phase suspect — is negligible.

## Concurrency (D13, plan task 1)

Concurrent const `search()` on a frozen `HNSWVectorIndex` is **data-race-free**: zero
ThreadSanitizer findings on an 8-thread × 500-search probe AND on the full load driver under
TSan; static inspection concurs (per-call local visited buffer, no mutable members, rng touched
only by insert). Decision: **one shared read-only index + per-thread pipeline state** (sources
hold per-instance scratch; exploration holds per-instance rng) — no replicas, no mutex.
`insert()`/`setEfSearch()` remain single-threaded-only operations.

## §17.3 grid completion (retrieval benchmarks)

Full data under `retrieval/` (100k-full: dims {64,128} × M {8,16,32} × efC {50,100,200,400} ×
ef {16..256} × k {10..500}, 1000 queries; 10k-dims: dims {32,64,128,256}; 1M: dim64 M {16,32} +
dim128 M16, efC 200, 300 queries; clustered 100k + 10k at the M16/efC200 operating point;
distance-count pass). Highlights:

- **Clustered (production-like) data is the recall story:** 100k×64d recall@10 = 0.919 @ef64 vs
  0.473 on isotropic random at identical parameters. Clustered 128d BEATS 64d (0.958 vs 0.919
  @ef64) — topic-clustered data keeps low intrinsic dimensionality, reversing the
  distance-concentration penalty that crushes random 128d (0.37 @ef128).
- **efConstruction has diminishing returns past 200** (64d, M16, ef128: 0.573/0.633/0.666/0.675
  for efC 50/100/200/400 at 1.8/3/5.5/10× build cost) — the shipped efC=200 default is justified.
- **1M stretch:** build 640 s (M16, 1,563 inserts/s) / 1,150 s (M32), peak RSS 1.2/1.7 GB (64d),
  1.65 GB (128d M16). Pure HNSW p95 0.36 ms (M16/ef64) – 1.8 ms (M32/ef256). Recall@10 on
  isotropic random collapses at 1M (0.19 @M16/ef64, 0.77 @M32/ef256) — the known worst-case
  distribution at 10× the distractor density; a clustered 1M run was not executed (time budget)
  and is recorded as future work. No §27 recall target exists at 1M.
- **Distance computations per query** (new counter, decorator-injected, proven bit-identical
  on/off): HNSW at 0.97 recall@10 does ~10.8k comps vs 100k brute force (9.3×); comps scale
  ~linearly with ef and sublinearly with M. Counting rows are flagged
  (`distance_comps_per_query >= 0`) and their latency is excluded from clean-latency claims.

## Methodology

Phases 7–10 ran experiment arms concurrently and every entry carries a latency-contamination
known-issue. Phase 11's claims are absolute (RPS/p99 on documented hardware), so: packages
developed and smoke-validated in parallel worktrees, but every published number above came from
a single serialized run chain (`scripts/run_phase11_retrieval.sh` then
`scripts/run_phase11_load.sh`) on an idle machine — total 78 min. The load driver synthesizes
warm user/reel state (est↔hidden cosine calibrated to 0.449 vs Phase 7's published 0.4245 final
alignment; ~100 seen reels; popularity-shaped counters) instead of paying 2M simulated
interactions per cell; the synthesis is documented in `apps/benchmark_recommender.cpp` and each
run's `summary.json`. The 10k clustered recall cell was run separately after the sweep (same
binary/seed; recall is timing-independent) to complete the §27 small verdict.
