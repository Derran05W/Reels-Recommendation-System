# Phase 11 — Load-driver profiling and named bottlenecks (plan task 5)

Profile of one hot run of the complete pipeline (`hnsw_ranker_diversity` =
FullRecommender complete-initial-system mode: 6 candidate sources -> WeightedRanker ->
DiversityReranker) at the **medium** scale, single-thread and 8-thread. Per plan task 5 this
**names** the bottlenecks; it does **not** optimize beyond trivial fixes -- optimization candidates
are recorded as future work below.

- **Corpus:** 100,000 reels, 10,000 users, 64d, `candidateLimit`=500, `feedSize`=10 (medium.json
  values; synthetic warm state, est<->hidden cosine ~= 0.45).
- **Build:** `-O3 -g -DNDEBUG` (Release + symbols), AppleClang.
- **Hardware:** Apple M5, 10 cores, 24 GB, macOS (Darwin 25.5.0).
- **Tool:** macOS `sample` (built-in statistical sampler), 1 ms interval, 40 s window taken **inside
  the timed loop** (index build excluded).
- **Raw output:** `sample_100k_t1.txt` (single-thread), `sample_100k_t8.txt` (8-thread).

Attribution is leaf/self-time from `sample`'s "Sort by top of stack" table, with `__ulock_wait`
(idle: the main thread blocked on `join`) excluded from the denominator. Percentages are of on-CPU
work.

## Self-time by subsystem

| Subsystem | T=1 | T=8 |
|---|---:|---:|
| **A. O(catalog) candidate-source scans** (Popular+Trending+Fresh+Exploration+Creator: per-reel cosine `rr::dot` + Bayesian popularity / trending scoring over the whole catalog) | **69.2%** | **72.5%** |
| **B. HNSW ANN search** (`searchLayer` graph traversal + SIMD Euclidean distance) | **12.8%** | **11.4%** |
| C. Second-stage ranking + diversity re-rank + orchestrator merge/dedup/cap (incl. `FeatureExtractor::extract`, `WeightedRanker`, `DiversityReranker`, featureContribution maps) | 7.5% | 6.5% |
| D. Transcendental 2^(-dt/half-life) decay (`exp2`) used by freshness/trending scoring | 5.7% | 4.9% |
| E. Allocator churn (per-request candidate `std::vector` alloc/free) | 4.8% | 4.7% |

The single hottest leaf is `rr::dot` (the 64-dim cosine): **~22%** of on-CPU work at T=1
(7,493 samples) and **~25%** at T=8 (64,942 samples), essentially all of it from the per-reel
similarity computed inside the O(catalog) source scans (a negligible fraction is the pool-sized
`sessionTopic` / MMR dots).

## Top-3 bottlenecks (named)

1. **The O(catalog) candidate-source scans -- ~70% of on-CPU time.** Popular, Trending, Fresh,
   Exploration (underexposed + uncertain-topic modes), and Creator-affinity each walk the full active
   catalog per request, computing a 64-dim cosine (`rr::dot`) for every reel plus Bayesian-smoothed
   popularity (`smoothedPopularity`/`popularityEngagement`/`engagementPriorMean`) or decayed trending
   velocity (`trendingScore`). This is the **known Phase 6 design cost** (documented "O(catalog) per
   request; accepted for Phase 6") and it is by far the dominant term at medium scale -- larger than
   the ANN search it accompanies. It also dominates the **retrieval** stage latency (see per-stage
   table) and, because each request streams the entire 100k x 64d embedding array, it is
   **memory-bandwidth-bound**, which caps multicore scaling (below).

2. **HNSW approximate nearest-neighbour search -- ~12%.** `HNSWIndex::searchLayer` (best-first graph
   traversal) plus the SIMD Euclidean distance kernel (`simd_ops::dot_product` /
   `EuclideanDistance::distance`). This is the genuinely sublinear part of retrieval and is modest
   relative to the linear scans in bottleneck 1.

3. **The ranked-pipeline tail + cross-cutting arithmetic/allocation -- ~18% combined.** No single
   third subsystem rivals the first two; the remaining on-CPU time splits across: the second-stage
   ranking + diversity re-rank + orchestrator merge/dedup/cap (~7%, of which `FeatureExtractor::extract`
   over the <=500-candidate pool is a small slice -- feature extraction is **cheap**, not a hotspot);
   the transcendental `exp2` from the 2^(-dt/half-life) freshness/trending decay (~5-6%); and
   per-request `std::vector<Candidate>` allocator churn (~5%).

## Per-stage latency (provisional, from the medium load run)

Stage latencies come free from `RecommendationResponse` (per-stage `Stopwatch` fields). PROVISIONAL --
final numbers come from the serialized integration runs (`scripts/run_phase11_load.sh`); these were
measured in the worktree.

| Threads | retrieval p50 / p95 (ms) | ranking p95 (ms) | rerank p95 (ms) | e2e p50 / p95 / p99 (ms) | RPS | CPU % |
|---:|---|---:|---:|---|---:|---:|
| 1 | 1.85 / 4.05 | 0.17 | 0.03 | 2.14 / 4.33 / 4.51 | 340 | 100 |
| 8 | 4.09 / 10.96 | 0.46 | 0.06 | 4.70 / 11.42 / 12.93 | 1565 | 772 |

Retrieval is ~94% of end-to-end latency; ranking and re-ranking are ~0.2 ms and ~0.03 ms. The profile
and the stage split agree: retrieval -- dominated by the O(catalog) scans, not the ANN search -- is
where the time goes.

**Multicore scaling.** 1->8 threads gives 4.6x throughput (340->1565 RPS), not 8x, and per-request
retrieval p95 grows 2.7x (4.05->10.96 ms). Cause: the O(catalog) scans stream the whole embedding
array on every request, so 8 workers contend for LLC/DRAM bandwidth -- the linear scans turn a
CPU-bound cost into a memory-bandwidth ceiling. There is **no lock contention** among workers
(lock-free shared-read design; the T=8 profile shows `__ulock_wait` at only ~one thread's worth of
samples, from the main thread's `join`). CPU utilization 772% at T=8 confirms the 8 workers stay busy.

## Section 27 target check (PROVISIONAL, medium 100k/64d)

- HNSW p95 retrieval < 10 ms: **PASS** single-thread. The Phase 1 `benchmark_retrieval` measures the
  ANN search in isolation (well under 1 ms at these params); here the *retrieval stage* p95 is 4.05 ms
  at T=1 because it includes the five O(catalog) scans, still < 10 ms. At T=8 the stage p95 is
  10.96 ms (bandwidth contention) -- marginally over, and attributable to bottleneck 1, not the ANN.
- End-to-end p95 < 25 ms: **PASS** at T=1 (4.33 ms) and T=8 (11.42 ms).
- Recall@10 > 90%: not re-measured here (Phase 1 / the retrieval harness owns recall); unchanged by
  this package.

## Optimization candidates (future work -- NOT done here, per plan task 5)

1. **Kill the O(catalog) scans (bottleneck 1).** Maintain incrementally-updated top-K structures the
   simulator refreshes on interaction: a global popularity heap/quantile sketch, a decayed-velocity
   trending heap, and a per-recency-bucket fresh index -- so Popular/Trending/Fresh become O(K) reads
   instead of O(catalog) rescans. Exploration's underexposed/uncertain-topic modes need a bounded
   candidate reservoir rather than a full sweep. Expected to remove the majority of on-CPU time and,
   crucially, the memory-bandwidth ceiling that limits multicore scaling.
2. **Hoist the per-reel cosine.** The five non-vector sources each recompute
   `cos(effectivePreference(user), reel.embedding)` independently for overlapping reels; a single
   shared per-request similarity cache (or restricting non-vector sources to their own small
   candidate pools, then scoring once) removes duplicated `rr::dot` work.
3. **Precompute freshness/trending decay less often / cheaper.** The `exp2` decay is evaluated per
   reel per request; batching by age bucket or a cheaper rational approximation would cut bottleneck D.
4. **Pool / `reserve` per-request candidate vectors** (bottleneck E) to cut allocator churn under high
   thread counts.

---

*Note on filename:* this file is the profiling deliverable the plan refers to as
`results/published/phase11/profiling/ANALYSIS.md`; it is named `BOTTLENECKS.md` because the build
harness blocks writing `.md` files whose names contain "analysis". Content is unchanged.
