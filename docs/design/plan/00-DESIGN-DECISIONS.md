# ReelRank — Global Design Decisions

This file is the binding set of top-level design choices for the ReelRank project.
**Every implementation session must read this file first**, then its phase section in
`plan/01-PHASES-FOUNDATION.md`, `plan/02-PHASES-PIPELINE.md`, or `plan/03-PHASES-EXPERIMENTS.md`,
plus the source-of-truth TDD at `/Users/derranw/Reels-Simulation/REELS-SIMULATION.md`.

A session may deviate from a decision here only if it records the deviation and the reason in
`commit.md` under its phase entry.

---

## D1. Repositories and workspace layout

| Repo | Path | Role |
|---|---|---|
| `Reels-Simulation` | `/Users/derranw/Reels-Simulation` | Planning + tracking repo. Holds the TDD, `plan/`, and the **canonical** `commit.md`. |
| `vector-db` | `/Users/derranw/vector-db` | Existing ANN library. **Out of scope for ReelRank sessions** — hardening changes (TDD §17) are being delivered separately. ReelRank sessions may *read* it to confirm signatures, never modify it. |
| `reel-rank` | `/Users/derranw/reel-rank` | New repo, created in Phase 0. All ReelRank code, tests, benchmarks, configs, results. |

**Session protocol (applies to every phase):**
1. Read `plan/00-DESIGN-DECISIONS.md`, your phase section, and `commit.md`.
2. Work test-first in `reel-rank` (TDD §30 implementation rule: tests → smallest passing impl → run → benchmark → refactor).
3. Commit in `reel-rank` with message `Phase N: <summary>`.
4. Update `/Users/derranw/Reels-Simulation/commit.md`: tick the phase checkbox, append the commit hash(es), a 3–6 line summary, deviations, and known issues. Commit `Reels-Simulation` with message `Phase N complete`.
5. If the phase's exit criteria cannot be met, do **not** tick the box — record what is blocking and stop.

## D2. vector-db integration

- **Consumption:** CMake `add_subdirectory` of a local checkout. Cache variable `REELRANK_VDB_DIR` (default `${CMAKE_SOURCE_DIR}/../vector-db`). Set `VDB_BUILD_TESTS=OFF`, `VDB_BUILD_BENCHMARKS=OFF`, `VDB_BUILD_SERVER=OFF` before the `add_subdirectory`. Link the bare target `vdb_core` and define our own alias `vector_db::vdb_core` (upstream exports none). No `find_package` — upstream has no install rules.
- **Use `HNSWIndex` directly, NOT the `VectorDatabase` facade.** Rationale (verified against the current source): the facade constructs HNSW as `HNSWIndex(dims, 10, 8, 8, metric)`, its `setApproximateAlgorithm("hnsw", p1, p2)` cannot set `ef_construction` and `ef_search` independently, and it exposes no `setEfSearch`. It also forces a global mutex, query cache, and persistence machinery we don't need. `HNSWIndex(dims, M, ef_construction, ef_search, metric, seed)` gives full parameter and seed control.
- **Containment:** vector-db symbols live in the **global namespace** (`Vector`, `HNSWIndex`, `DistanceMetric`, …). Only the adapter translation units under `src/vindex/` may include vector-db headers. The rest of ReelRank sees only our `rr::VectorIndex` abstraction (TDD §23.1).
- **Exceptions:** vector-db throws `std::invalid_argument` (dup key, dim mismatch, NaN/Inf) and `std::runtime_error`. Adapters catch nothing on the hot path — a throw during indexing is a bug and should surface; adapters validate inputs (normalized, finite) *before* insert so throws never happen in normal operation.
- **Immutability:** reel embeddings are immutable after indexing (TDD §17.2 — the vector-db update path may create stale index entries). Never call update/remove on indexed reels in v1.

## D3. Distance metric strategy

**All embeddings are L2-normalized at creation; all indexes use `EuclideanDistance`.**
For unit vectors, `d² = 2 − 2·cosθ`, so Euclidean ordering is exactly cosine ordering, and
`similarity = 1 − d²/2` recovers cosine similarity precisely. This avoids depending on the exact
semantics of vector-db's `CosineSimilarity` class (similarity vs. distance ambiguity), matches
HNSW's default metric, and makes exact-vs-ANN comparisons metric-identical. The conversion lives in
one function: `rr::similarityFromEuclidean(float d)`.

## D4. Identifiers

- Internal IDs are `uint32_t` strong types: `ReelId`, `UserId`, `CreatorId`, `TopicId`, `SessionId` (a `template <class Tag> struct Id { uint32_t value; }` with `==`, `<`, `std::hash`).
- vector-db requires `std::string` keys. The `vindex` adapters convert `ReelId` ↔ decimal string (`"12345"`) at the boundary. No other code touches string keys.

## D5. Embeddings

`using Embedding = std::vector<float>;` (plain std, per TDD §8.1 — conversion to vector-db's
`Vector` class happens only inside adapters). Dimension is config-driven; default **64**, must
support 32/64/128/256. `rr::normalize`, `rr::dot`, and validity checks (`finite`, unit-length
within 1e-4) live in `src/core/embedding.{hpp,cpp}` and are unit-tested first (Phase 0).

## D6. Configuration and serialization

**JSON via nlohmann/json** for config files and all report output. Rationale: it is already a
PUBLIC transitive dependency of `vdb_core` (v3.11.3) — zero new dependencies — and experiment
output (`summary.json`, `metadata.json`) needs JSON anyway. The TDD's §21 YAML example is
transliterated to `configs/{small,medium,large,benchmark}.json` with identical keys. Every config
struct has `from_json`/`to_json` and **explicit defaults matching the TDD's suggested values**.
Unknown keys are an error (catches typos); every experiment writes its fully-resolved config back
out.

## D7. Testing

- **Framework:** GoogleTest **v1.15.2** via FetchContent — same version vector-db pins, avoiding two gtest copies in one build tree.
- **Layout:** `tests/unit/`, `tests/integration/`, `tests/property/`, `tests/differential/` — one `reel_rank_tests` binary, gtest filters distinguish suites; CTest labels per directory.
- **Property tests** are parameterized/seed-swept gtest cases (many seeds per property, TDD §24.3) — no extra property-testing dependency.
- **Performance tests are never unit tests** (TDD §24.5): they are separate executables under `apps/`, excluded from `ctest`.
- TDD discipline per subsystem: write the test list from the phase plan first, watch them fail, implement, pass.

## D8. Determinism and randomness

- Single `uint64_t` master seed per experiment. Subsystems get independent streams via `rr::Rng forkRng(uint64_t masterSeed, std::string_view streamName)` — SplitMix64 over `masterSeed ^ fnv1a(streamName)` seeding a `std::mt19937_64`. Adding a new consumer never perturbs existing streams.
- **Portable samplers:** `std::uniform_*_distribution` / `std::normal_distribution` output is implementation-defined, so `rr::Rng` provides its own `uniform(lo,hi)`, `uniformInt(n)`, `gaussian()` (Box–Muller), `bernoulli(p)` implemented in-house. **Only `rr::Rng` may be used for simulation randomness** — `std::*_distribution` is banned outside it. Guarantees bit-identical runs across compilers/platforms.
- HNSW construction passes an explicit seed derived from the master seed.
- A determinism integration test (same seed twice ⇒ byte-identical metric CSVs) exists from Phase 2 onward and runs in CI.

## D9. Time

Simulation uses a **logical clock**: `using Timestamp = uint64_t;` in simulated seconds, advanced
by the simulator (never wall clock). Freshness/trending decay read this clock. Wall-clock
(`std::chrono::steady_clock`) is used *only* inside latency measurement. Anything needing "now"
takes a `Timestamp` parameter or an injected clock — nothing calls a global.

## D10. Language, style, error handling

- **C++20**, matching vector-db. Primary toolchain: AppleClang on this machine; CI adds GCC/Ubuntu.
- Everything in namespace **`rr`**. Files `snake_case.{hpp,cpp}`, types `PascalCase`, methods/fields `camelCase` (matches the TDD's struct sketches). `#pragma once`.
- `.clang-format` (LLVM base, 100 columns, 4-space indent) committed in Phase 0; CI checks formatting.
- **Exceptions for setup errors** (bad config, dimension mismatch, missing files) — fail fast. **No exceptions on the recommendation/simulation hot path**; hot-path invariant violations are `assert`s in Debug and avoided by construction in Release.
- Warnings: `-Wall -Wextra -Wpedantic`, warnings-as-errors on ReelRank targets (not on vendored code).

## D11. Module structure and interfaces

Directory layout follows TDD §22 verbatim (`include/rr/...` + `src/...` split, `apps/`, `tests/`,
`configs/`, `scripts/`, `results/`), with one addition: `src/vindex/` for the vector-db adapters.
The four abstract interfaces are exactly TDD §23 (`VectorIndex`, `Ranker`, `Reranker`,
`UserStateUpdater`), plus `CandidateGenerator` (§12) and `Recommender` (§16). Hidden user
preference is enforced by structure: `User::hiddenPreference` lives in a separate
`HiddenUserState` struct owned by the simulator, **not** on the `User` object the recommender
sees — the "recommender never accesses hidden preference" property becomes a compile-time
guarantee, and a property test asserts recommendation calls can't mutate it.

## D12. Benchmarks and experiment output

- Custom lightweight harness (same philosophy as vector-db's `bench/harness.hpp`): warmup + N timed iterations, p50/p95/p99 from stored samples. No Google Benchmark dependency.
- Every experiment writes `results/<experiment-id>/` exactly per TDD §26 (config.json, summary.json, CSVs, metadata.json). `experiment-id = <name>-seed<seed>-<yyyymmdd-hhmmss>`.
- `metadata.json` records: git SHA of reel-rank **and** vector-db, build type, compiler, CPU model, RAM, OS, thread count, dataset sizes, dimension (TDD §27 publication requirements).
- `results/` is gitignored except for curated, committed benchmark reports under `results/published/`.

## D13. Threading

Single-threaded simulation core through Phase 10. Phase 11 adds a load driver with one shared
read-only `HNSWIndex` and per-thread users. `HNSWIndex` has **no internal locking**; concurrent
`const` search on a frozen index must be verified safe (inspect for mutable scratch state) as the
first task of Phase 11 — if unsafe, per-thread index replicas are the fallback (documented, not
silently mutexed).

## D14. CI

GitHub Actions from Phase 0: macOS + Ubuntu, Debug + Release matrix, build + `ctest` +
clang-format check. Benchmarks are never run in CI (unstable timing); a `--smoke` mode of the
simulate app runs a tiny end-to-end config in CI from Phase 4 onward.

## D15. Python tooling

`scripts/` uses **uv** (`pyproject.toml`, pinned `pandas` + `matplotlib`). Scripts only *read*
result CSVs/JSON and produce plots/tables — no simulation logic in Python. Entry points per TDD
§22: `run_experiments.py`, `compare_results.py`, `plot_results.py`.

## D16. Scope guards (binding non-goals for v1)

Per TDD §5/§29: no Kafka/Redis/Kubernetes, no neural rankers, no REST/gRPC service, no frontend,
no Thompson sampling (epsilon-greedy only), no real media, no vector-db modifications. The
service-based deployment (TDD §6.3 "later approach") is explicitly deferred past Phase 12.
