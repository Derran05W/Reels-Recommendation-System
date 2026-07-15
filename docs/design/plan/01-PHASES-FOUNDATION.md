# ReelRank Plan — Part 1: Foundation (Phases 0–4)

One phase = one Claude session. Before starting a phase, read
`plan/00-DESIGN-DECISIONS.md` (binding) and the TDD (`REELS-SIMULATION.md`) sections referenced
below. Follow the session protocol in D1. Do not start a phase whose prerequisites are unticked
in `commit.md`.

---

## Phase 0 — Project bootstrap and skeleton

**TDD refs:** §6, §21, §22, §23, §28 Phase 0, §30 (second output).
**Prerequisites:** none.

### Objective
A building, tested, CI-checked `reel-rank` repo that links vector-db and contains all interface
headers and config structures — no algorithm implementations.

### Tasks
1. `git init /Users/derranw/reel-rank`; `.gitignore` (build/, results/ except results/published/, .cache).
2. Top-level `CMakeLists.txt`: project `reel_rank`, C++20, warnings-as-errors on own targets; `REELRANK_VDB_DIR` cache var; `add_subdirectory` of vector-db with `VDB_BUILD_TESTS/BENCHMARKS/SERVER=OFF`; alias `vector_db::vdb_core` (D2). FetchContent GoogleTest v1.15.2 (D7).
3. Directory tree per D11/TDD §22 (`include/rr/{domain,simulation,recommendation,candidate_sources,learning,evaluation,infrastructure}`, `src/`, `src/vindex/`, `apps/`, `tests/{unit,integration,property,differential}`, `configs/`, `scripts/`, `results/`).
4. Infrastructure (implemented + unit-tested — this is the only real code in Phase 0):
   - `rr::Rng` with portable samplers + `forkRng` (D8). Tests: reproducibility across two instances with same seed, stream independence, sampler range/moment sanity.
   - `rr::Embedding` helpers: `normalize`, `dot`, `isValid` (D5). Tests: unit length, zero-vector handling (throws), NaN rejection.
   - Logger with levels INFO/DEBUG/TRACE/WARN/ERROR (TDD §25) — thin, `fprintf`-based, level from env/config. Test: level filtering.
   - `Timestamp` + latency `Stopwatch` (D9).
   - Config loading: `ExperimentConfig` + nested `HNSWConfig`, `RankingConfig`, `ExplorationConfig`, `DiversityConfig`, `LearningConfig`, `SimulationConfig` (TDD §20/§21) with nlohmann `from_json`/`to_json`, TDD default values, unknown-key rejection (D6). Tests: round-trip, defaults, unknown key throws, `configs/small.json` parses.
5. Interface headers only (pure virtual, no impls): `VectorIndex`, `CandidateGenerator`, `Ranker`, `Reranker`, `UserStateUpdater`, `Recommender` (TDD §12, §16, §23).
6. Domain structs: `Reel`, `User` (public state only), `HiddenUserState` (separate, per D11), `Creator`, `Topic`, `InteractionEvent`, `Candidate`, `RankedReel`, `RecommendationRequest/Response`, strong `Id` types with hash (TDD §8, D4).
7. One vector-db link-sanity test: construct an `HNSWIndex(8, 16, 200, 64)` directly, insert 3 vectors, search top-1 — proves the dependency links and runs.
8. `configs/{small,medium,large,benchmark}.json` transliterated from TDD §21.
9. `.clang-format`; GitHub Actions workflow (D14); `README.md` stub with build instructions; `scripts/` uv project stub (D15).

### Exit criteria (TDD §28 P0)
- [ ] Clean clone builds on macOS (and CI Ubuntu) in Debug and Release.
- [ ] `ctest` passes; at least the Rng/embedding/config/link-sanity tests exist and pass.
- [ ] vector-db links and its HNSWIndex is callable from a ReelRank test.

### Out of scope
Any generator, recommender, or simulation logic.

### Session kickoff prompt
> Read /Users/derranw/Reels-Simulation/plan/00-DESIGN-DECISIONS.md, plan/01-PHASES-FOUNDATION.md (Phase 0), commit.md, and TDD §§6, 21–23, 28, 30. Execute Phase 0 exactly as specified: bootstrap /Users/derranw/reel-rank, CMake + vector-db add_subdirectory link, GoogleTest, CI, and implement+test only the infrastructure layer (Rng, embedding helpers, logger, config loading) plus interface/domain headers. Test-first. When exit criteria pass, commit reel-rank, then update commit.md in Reels-Simulation per the session protocol.

---

## Phase 1 — Vector index adapters and vector-db validation

**TDD refs:** §12.1–12.2, §17, §23.1, §24.4, §28 Phase 1.
**Prerequisites:** Phase 0.
**Note:** vector-db hardening (TDD §17.1/17.2 fixes) is being delivered *separately* in the
vector-db repo. This phase validates from the consumer side and must not modify vector-db. If a
§17.2 defect (entry-point propagation, reciprocal pruning) is still present, document the measured
impact in commit.md and file it as a known issue — do not fix it here.

### Objective
`rr::VectorIndex` implementations backed by vector-db, plus a differential benchmark proving (or
precisely quantifying) HNSW recall against exact search.

### Tasks
1. `ExactVectorIndex` (own flat brute-force scan over stored embeddings — ~30 lines, exact by construction; KD-trees are useless at 64d). Unit tests: empty index, k=0, k>size, exact self-match, ordering.
2. `HNSWVectorIndex` adapter in `src/vindex/` wrapping `HNSWIndex` directly (D2): ctor takes `HNSWConfig` + dims + seed; `ReelId`↔string key conversion (D4); pre-insert validation (normalized, finite); `setEfSearch` passthrough; `similarityFromEuclidean` (D3). Unit tests mirroring TDD §17.1 from the consumer side: empty search, single element, duplicate id rejection, dimension mismatch, NaN rejection, very large k, k=0, determinism (same seed+order ⇒ identical results), search after 10k batch insertion.
3. Differential test suite (TDD §24.4): random normalized datasets (1k–10k × 32/64d), HNSW top-k vs exact top-k — overlap, distance error, no malformed ids, self-match. Property: recall\@10 must exceed a floor (start 0.85; if the un-hardened HNSW misses it, record actual and pin the floor just below as a regression tripwire).
4. `apps/benchmark_retrieval.cpp`: sweeps subset of TDD §17.3 grid (10k/100k vectors × 64d × M{8,16,32} × efSearch{16..256} × k{10,50,200,500}) measuring Recall\@K, build time, insert throughput, query p50/p95/p99, memory (rough RSS), graph level distribution (via `getLevelDistribution`). CSV output per D12.
5. Run the 10k and 100k sweeps on this machine; commit the report to `results/published/phase1/` with full `metadata.json`.

### Exit criteria (TDD §28 P1)
- [ ] Recall\@K measurable and reported HNSW-vs-exact on identical data.
- [ ] Adapter behaviour deterministic under fixed seed (test-enforced).
- [ ] Consumer-side correctness tests pass, or failures are documented with measurements in commit.md as known vector-db issues.
- [ ] Published benchmark CSVs exist for 10k and 100k vectors.

### Out of scope
Modifying vector-db; LSH adapter (optional stretch, only if trivial); 1M-vector sweep (Phase 11).

### Session kickoff prompt
> Read plan/00-DESIGN-DECISIONS.md, plan/01-PHASES-FOUNDATION.md (Phase 1), commit.md, TDD §§12.1–12.2, 17, 23.1, 24.4. First check whether the pending vector-db hardening changes have landed (git log in /Users/derranw/vector-db) and note the SHA. Then implement ExactVectorIndex and the HNSWVectorIndex adapter test-first, the differential suite, and benchmark_retrieval; run the 10k/100k sweeps and publish results. Do not modify vector-db. Update commit.md per protocol.

---

## Phase 2 — Synthetic domain generation

**TDD refs:** §8, §9, §28 Phase 2.
**Prerequisites:** Phase 0 (Phase 1 not required but typically done).

### Objective
Deterministic generators for topics, creators, reels, and users at 100k-reel/10k-user scale.

### Tasks
1. `TopicGenerator`: N normalized random topic centres (default 32) from stream `"topics"`.
2. `CreatorGenerator`: style embedding = normalized(topic mix + noise), 1–3 topic specialties, base quality (TDD §9.4).
3. `ReelGenerator` (TDD §9.2): embedding = normalize(w1·t1 + w2·t2 + creatorStyle·ws + ε); primary/secondary topic labels; duration from the 4-bucket distribution; quality ~ clamped gaussian around creator base; createdAt spread over a configurable window; zeroed counters.
4. `UserGenerator` (TDD §9.3): hidden preference = normalize(Σ aᵢtᵢ + ε) over 2–5 topics; per-user behavioural traits (explore-willingness, session length, like/share propensity, duration tolerance, preference stability) stored on `HiddenUserState`.
5. Each generator takes `(config, rr::Rng)` forked per stream name — regenerating reels never changes users (D8).
6. Tests: unit (all embeddings valid+normalized, topic labels consistent with construction weights, durations within buckets, trait ranges); property over ≥20 seeds (determinism: same seed ⇒ identical dataset; different streams independent); integration (generate full 100k/10k/5k-creator dataset < a few seconds, memory sane).
7. `apps/inspect_user.cpp` (first cut): dump a user/reel/topic distribution summary as JSON for eyeballing (nearest topic histogram, quality histogram).

### Exit criteria (TDD §28 P2)
- [ ] 100k reels + 10k users + 5k creators generate deterministically (same seed ⇒ identical, test-enforced).
- [ ] All embeddings normalized/valid (property test).
- [ ] Distributions inspectable via inspect_user.

### Out of scope
Any behaviour/interaction simulation; indexing the reels (Phase 4/5 does that where needed).

### Session kickoff prompt
> Read plan/00-DESIGN-DECISIONS.md, plan/01-PHASES-FOUNDATION.md (Phase 2), commit.md, TDD §§8–9. Implement TopicGenerator, CreatorGenerator, ReelGenerator, UserGenerator test-first with forked Rng streams, plus the inspect_user dump app. Determinism property tests across ≥20 seeds are mandatory. Update commit.md per protocol.

---

## Phase 3 — Behaviour simulation

**TDD refs:** §10, §24.6, §28 Phase 3.
**Prerequisites:** Phase 2.

### Objective
The synthetic "ground truth" user: given a shown reel, produce a stochastic-but-reproducible
interaction with watch time, events, and reward.

### Tasks
1. `BehaviourModel` (owned by the simulator; the only component that reads `HiddenUserState`):
   - base affinity `a = p_u·q_v` (TDD §10.1);
   - behaviour score `z = αa + βQ + γC − δD + ε` with configurable α..δ (TDD §10.2);
   - instant-skip and completion probabilities via sigmoid (TDD §10.3);
   - watch-ratio sampling conditioned on affinity bands, >100% = rewatch (TDD §10.4);
   - like/share/follow probabilities conditioned on affinity and completed watch (TDD §10.3), modulated by per-user traits (Phase 2);
   - `NotInterested` low-probability path for very negative z.
2. `RewardModel`: TDD §10.5 formula, weights config-driven, output clamped to [−1, 1] (normalization documented). Unit tests: each term's contribution, bounds, config override.
3. `InteractionEvent` assembly (type, watchSeconds, watchRatio, reward, timestamp, sessionId).
4. Statistical tests (seeded, tolerance-based, N large enough to be stable): mean watch ratio of top-affinity-decile reels > bottom decile by a margin; instant-skip rate monotonically decreasing across affinity quartiles; like rate higher for completed watches; identical seed ⇒ identical event stream (determinism, TDD §24.6).
5. Wire a minimal `Simulator::step(user, reel)` that advances the logical clock and updates reel counters (impressions, completions, likes, shares, skips) — needed by popularity sources in Phase 4.

### Exit criteria (TDD §28 P3)
- [ ] High-affinity ⇒ statistically stronger engagement; low-affinity ⇒ more skips (tests).
- [ ] Behaviour stochastic but reproducible under fixed seed (test).
- [ ] Reward bounded and configurable.

### Out of scope
Any recommender; user preference *estimation* updates (Phase 7 — hidden state drives behaviour, estimated state stays untouched here).

### Session kickoff prompt
> Read plan/00-DESIGN-DECISIONS.md, plan/01-PHASES-FOUNDATION.md (Phase 3), commit.md, TDD §10. Implement BehaviourModel + RewardModel + Simulator::step test-first, including the statistical monotonicity tests and determinism tests. Only the simulator touches HiddenUserState. Update commit.md per protocol.

---

## Phase 4 — Baseline recommenders and evaluation harness

**TDD refs:** §16.1–16.3, §18.1–18.3, §19, §20, §26, §28 Phase 4.
**Prerequisites:** Phases 2–3 (and Phase 1 for the exact index).

### Objective
End-to-end simulation loop: request → feed → simulated interactions → metrics on disk, for three
baselines.

### Tasks
1. `RandomRecommender` (unseen reels, uniform), `PopularityRecommender` (Bayesian-smoothed popularity score, TDD §12.3), `ExactVectorRecommender` (brute-force over estimated preference — the personalization ceiling; uses `effectivePreference`, which at this phase is just the cold-start estimated preference).
2. Cold-start initialization: `estimatedPreference = globalAveragePreference` over generated users (TDD §11.1). (No online updates yet — that's Phase 7; baselines run with static estimates, which is expected and must be noted in results.)
3. Seen-reel filtering shared utility (feeds never repeat items, TDD §13 items 5).
4. Evaluation harness (`ExperimentRunner`): loads config, generates dataset, builds needed indexes, runs `interactionsPerUser` rounds over all users interleaved, collects metrics, writes `results/<experiment-id>/` per D12/TDD §26.
5. Metrics (first tranche): behaviour metrics (watch ratio, skip/completion/like/share/follow rates, reward per impression/session — TDD §18.3); average true affinity of recommended items (evaluation-only hidden-state access, TDD §18.2); oracle regret (TDD §19: oracle = exhaustive true-affinity scoring on a sampled subset of requests to keep cost sane — sampling rate config-driven and recorded).
6. `apps/simulate.cpp` CLI: `--config path --algorithm name --seed N --out dir`, plus `--smoke` tiny config for CI (D14).
7. Tests: unit (popularity smoothing math, seen-filter); integration (end-to-end smoke run produces all output files; same seed twice ⇒ byte-identical CSVs); statistical (exact-vector baseline beats random on avg true affinity and reward at small scale).
8. Run the three baselines on `configs/small.json`; publish comparison to `results/published/phase4/`.

### Exit criteria (TDD §28 P4)
- [ ] All three baselines run end-to-end; metrics exported per §26 layout.
- [ ] Exact personalization outperforms random (test + published numbers).
- [ ] Full-run determinism test passes.

### Out of scope
HNSW in the loop (Phase 5), ranking (6), learning (7), exploration (8), diversity (9).

### Session kickoff prompt
> Read plan/00-DESIGN-DECISIONS.md, plan/01-PHASES-FOUNDATION.md (Phase 4), commit.md, TDD §§16.1–16.3, 18.1–18.3, 19, 20, 26. Build the ExperimentRunner + simulate app and the Random/Popularity/ExactVector recommenders test-first, wire behaviour metrics + regret with oracle sampling, run and publish the small-config baseline comparison. Update commit.md per protocol.
