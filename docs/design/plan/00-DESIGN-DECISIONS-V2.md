# ReelRank — Global Design Decisions, V2 Addendum (Realism Upgrade, Phases 13–24)

Binding addendum to `plan/00-DESIGN-DECISIONS.md`. **D1–D16 remain in force unchanged** (session
protocol, vector-db containment, metric strategy, ids, embeddings, JSON config, testing layout,
RNG discipline, logical time, style, module structure, output layout, threading, CI, Python
tooling, scope guards). Every V2 session reads BOTH decision files, its phase section in
`plan/04-PHASES-SATISFACTION-SESSIONS.md` / `plan/05-PHASES-EVENTS-LONGTERM.md` /
`plan/06-PHASES-LEARNED-RANKING.md`, and the referenced sections of the V2 TDD
(`/Users/derranw/Reels-Simulation/REELS-SIMULATION-V2.md`). The V1 TDD stays authoritative for
already-built subsystems; where the two conflict on gated-on V2 behaviour, V2 wins.

A session may deviate from a decision here only if it records the deviation and the reason in
`commit.md` under its phase entry.

---

## D17. Compatibility gates and the V1 golden baseline

Every V2 mechanism ships behind config surface whose **defaults preserve V1 semantics exactly**:

- `realism.content_v2` (P13), `realism.latent_reactions` (P14), `realism.session_dynamics` (P16),
  `realism.personalized_diversity` (P17), `simulation.scheduler = "round_robin" | "event_queue"`
  (P18, default `round_robin`), `serving.*` (P19), `realism.preference_evolution` + `retention.*`
  (P20), `learning_v2.*` (P22–23). All booleans default **false**. Gates that require earlier
  gates (e.g. `latent_reactions` requires `content_v2`) throw at config load if inconsistent
  (fail-fast per D10). Unknown-key rejection (D6) continues to apply — old configs stay valid,
  new keys are additive with defaults.
- V1 model classes are **retained, not edited into V2 shapes**: `BehaviourModel` stays; V2
  behaviour is a separate config-selected implementation. Same for the legacy `ExperimentRunner`
  loop vs the event-driven runner (D20). Shared code may be refactored only when the gate-off
  byte-identity proof below still passes.
- **Golden baseline, checked every phase:** at each phase start, capture (once, at Phase 13) a
  Release-build reference run of `configs/small.json` under `hnsw_ranker_diversity` and of the
  Phase 10 `session_aware` drift arm's deterministic CSV set; commit them under
  `tests/golden/v1-baseline/`. Every V2 phase's exit criteria include: gates-off re-run at phase
  HEAD reproduces the small-config golden **byte-identically** (deterministic CSVs; non-timing
  summary fields bit-equal). The medium-config drift-arm re-verification is mandatory at Phases
  14, 16, 18, and 24 (the phases that touch the behaviour/runner core), discretionary elsewhere.
  Golden diffs always compare same-build-type runs (Release vs Release — the main `build/` dir on
  this machine is Debug).
- **New randomness never perturbs old streams** (D8 corollary): V2 draws come exclusively from
  newly forked streams (D19). With gates ON, V1-era fields and V1-stream draw sequences remain
  identical; with gates OFF, zero new draws occur. Both are property-tested.

## D18. Hidden-state isolation, extended

The D11 structural guarantee extends to all new hidden types: `HiddenReelState` (archetype id +
archetype parameters, V2 §4.4/§5), `LatentReaction` (§4.3), `HiddenSessionState` (§4.6), hidden
tolerance/susceptibility/plasticity traits (§4.2/§5), and hidden retention state (trust, habit —
§4.17).

- All hidden types live in headers under `include/rr/simulation/hidden/`. Only `rr_simulation`
  and designated evaluation translation units may include them.
- **Enforcement is automated (V2 §7 requirement):** a test (script-driven, runs under ctest and
  CI) walks the include graph and fails if any file under `include/rr/recommendation`,
  `include/rr/candidate_sources`, `include/rr/learning`, `src/recommendation`,
  `src/candidate_sources`, or `src/learning` reaches a `simulation/hidden/` header. Established
  in Phase 13, maintained forever.
- **Evaluation carve-out** (Phase 4 oracle precedent): evaluation modules may read hidden state
  for metrics and for explicitly-labeled oracle arms only. Anything oracle-flavoured lives in
  `evaluation/`, is named `oracle*`, and is barred from being a trainable/serving policy.
- **Recommender-visible surfaces are explicit:** serving-time reel attributes (V2 §4.1 minus
  archetype params) live on `Reel`; recommender-side estimates (V2 §5 "User") live on `User`.
  `InteractionEvent` carries observables only — a leak-audit test asserts its serialized schema
  (and the P22 training log) contains no hidden field, by field-name allowlist.

## D19. RNG streams for V2

New named forks (D8 mechanism, exact strings pinned here so phases don't collide):
`"reels-v2"`, `"users-v2"`, `"archetypes"` (P13); `"satisfaction"` (P14 latent noise);
`"session-exit"`, `"external-interruption"` (P16); `"scheduling"` (P18 open/return times);
`"preference-evolution"` (P20); `"explicit-feedback"` (P22 survey sampling); `"training-split"`,
`"model-init"` (P22–23). Existing streams (`"topics"`, `"creators"`, `"reels"`, `"users"`,
`"behaviour"`, `"recommender"`, `"oracle"`, `"retrieval"`, injection streams) keep their V1
consumption patterns under V1 configs. A V2 path may draw from an existing stream only where it
replaces that stream's V1 consumer wholesale under its gate (e.g. BehaviourModelV2 owns
`"behaviour"` when `latent_reactions` is on), never interleaved with V1 consumers.

## D20. Event-driven simulation determinism

- Priority queue ordered by `(time, deterministicTieBreaker)`; the tie-breaker is a pinned
  SplitMix64-finalizer hash of `(userId, eventType, perUserSequenceNumber)` — golden-value
  tripwire tests, same treatment as Phase 10's cohort hash.
- **Equal-timestamp semantics (V2 §4.14):** global popularity/trending state is snapshotted per
  unique timestamp; all events at time T read the state as of the end of T−ε (i.e. updates from
  events at T become visible only to strictly later timestamps). This makes equal-time feed
  requests observe identical global state regardless of pop order, and is tested directly.
- **Order invariance:** permuting user initialization order (or queue insertion order of the
  initial OpenApp events) produces identical event logs and metrics under the same seed —
  test-enforced (V2 §7 "same seed produces identical event sequence" + Tier 3 acceptance).
- The legacy round-robin `ExperimentRunner` is retained permanently as
  `simulation.scheduler = "round_robin"` (default) — it is the D17 golden path and the
  apples-to-apples base for pre-P18 comparisons. The event runner is NOT required to reproduce
  round-robin output; a documented statistical-comparability check replaces byte-identity there.
- Wall-clock stays banned from simulation (D9). Away-time fatigue decay and return delays are
  functions of the logical clock.

## D21. Learned models: in-house, explainable-first

Tier 5 adds **zero new external dependencies**: logistic regression (binary outcomes) and linear
regression (watch ratio) implemented in-house in C++ with deterministic mini-batch SGD
(`rr::Rng` streams `"training-split"`/`"model-init"`; fixed iteration order; documented feature
scaling). Feature vectors contain serving-time-visible features only (D18 leak audit applies to
the training pipeline end-to-end). Gradient-boosted trees and small neural models (V2 §4.20
items 3–4) are **optional stretch, only after the linear baselines are published**, and only if
implementable in-house or via a trivially-vendorable header-only library; default is to skip and
record as future work (D16 spirit). Model files serialize as JSON (D6) with round-trip tests.

## D22. Metrics: four groups, additive output schema

Every V2 experiment reports all four V2 §6 groups — engagement, hidden user welfare, session
health, recommendation quality — as separate blocks/files; **no single aggregate score is ever
defined** (V2 §6 rule). New outputs are additive to the D12/§26 layout: `welfare_metrics.csv`,
`session_health.csv`, `longterm_metrics.csv`, `training_eval.csv`, plus `summary.json` blocks
(`welfare`, `session_health`, `long_term`, `learned_models`) — existing V1 files/columns are
unchanged and gate-off runs emit byte-identical V1 output (D17). Hidden-derived metrics are
computed only inside the D18 evaluation carve-out. Published experiments continue under
`results/published/phaseNN/` with full D12 metadata.

## D23. vector-db stays frozen through V2

No vector-db modifications in Phases 13–24, period (V2 §8 recommendation adopted as binding).
Only the semantic embedding is ANN-indexed; extra modality embeddings ride on `Reel` as ranker/
behaviour features. If a phase experimentally needs modality retrieval (P23 stretch at most), it
instantiates additional `HNSWVectorIndex` objects via the existing adapter — separate index
instances, no shared-schema assumption. If a measured ANN limitation appears (mutable embeddings,
filter-aware search), record the measurement in `docs/LIMITATIONS.md` as designed-future-work and
move on. `/Users/derranw/vector-db` remains read-only for all V2 sessions.

## D24. Config and archetype conventions

- V2 config lives in a `realism` block plus the named blocks in D17, all with explicit defaults
  and `from_json`/`to_json` per D6. New experiment configs: `configs/realism-small.json` and
  `configs/realism-medium.json` (introduced P14/P15), scenario configs under
  `configs/scenarios/` (P21). Every published run keeps writing its fully-resolved config.
- The archetype catalog (V2 §4.4) is config-driven data, not code: each archetype = a named set
  of attribute-distribution parameters + mixture weight; defaults transliterate the eight §4.4
  archetypes. Archetype identity and parameters are hidden (D18); the ranker sees only the
  resulting per-reel attribute values. Adding an archetype must not require recompiling.
- The no-premature-config convention from Phases 2–3 continues: behavioural constants that no
  planned experiment varies stay named constants in `.cpp` files, documented at definition.

## D25. Session protocol and phase→file map for V2

D1's session protocol applies verbatim to Phases 13–24 (work test-first in reel-rank; commit
`Phase N: <summary>`; tick + history entry in `commit.md`; commit Reels-Simulation
`Phase N complete`). The phase→file map extends to: 13–17 →
`plan/04-PHASES-SATISFACTION-SESSIONS.md`, 18–21 → `plan/05-PHASES-EVENTS-LONGTERM.md`,
22–24 → `plan/06-PHASES-LEARNED-RANKING.md`. Phases 13–24 read the **V2 TDD**
(`REELS-SIMULATION-V2.md`) for their "V2 TDD refs" and fall back to the V1 TDD only where a
phase section explicitly cites it. reel-rank is now public
(github.com/Derran05W/reelrank-recommender) with green CI: V2 phases keep CI green (push at phase
end; the pinned clang-format 22.1.8 is canonical), and Phase 24 folds V2 planning material into
`docs/design/` the way Phase 12 did for V1.
