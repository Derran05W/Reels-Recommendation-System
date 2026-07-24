# Design artifacts

The process documents behind ReelRank, preserved as written during development (July 2026):
V1 (Phases 0–12, the base recommender) and the V2 realism upgrade (Phases 13–24).

## V1 — Foundation (Phases 0–12)

- [`TECHNICAL-DESIGN.md`](TECHNICAL-DESIGN.md) — the technical design document (§1–34), authored
  **before any code**: domain model, algorithms, metrics, testing strategy, performance targets,
  and the 13 implementation phases.
- [`plan/00-DESIGN-DECISIONS.md`](plan/00-DESIGN-DECISIONS.md) — the 16 binding top-level design
  decisions (D1–D16): vector-db integration strategy, distance-metric equivalence, determinism
  rules, hidden-state isolation, threading, scope guards.
- [`plan/01–03-PHASES-*.md`](plan/) — session-sized implementation plans for phases 0–12, each
  with explicit prerequisites and exit criteria.
- [`PHASE-HISTORY.md`](PHASE-HISTORY.md) — the phase-by-phase tracker: per-phase commit hashes,
  result summaries, **every deviation from plan with its reason, and every known issue carried
  forward** — the honest development record.

## V2 — Realism Upgrade (Phases 13–24)

Satisfaction-vs-engagement, session dynamics, an event-driven world, exposure-driven preference
evolution/retention, and learned multi-objective ranking. Folded into the public repo at Phase 24
per `plan/00-DESIGN-DECISIONS-V2.md` D25, mirroring the V1 convention above.

- [`TECHNICAL-DESIGN-V2.md`](TECHNICAL-DESIGN-V2.md) — the V2 technical design document (§1–10),
  a companion to `TECHNICAL-DESIGN.md`: the tiered plan (Tiers 1–5 / Releases A–E), the reel/user
  factor model, session and event-queue semantics, long-term dynamics, and learned ranking.
- [`plan/00-DESIGN-DECISIONS-V2.md`](plan/00-DESIGN-DECISIONS-V2.md) — the binding V2 addendum
  (D17–D25): compatibility gates over a byte-identical V1 golden baseline, extended hidden-state
  isolation, new RNG streams, event-driven determinism, in-house-only learned models, additive
  four-group metrics, vector-db staying frozen, archetype/config conventions, and the V2 session
  protocol.
- [`plan/04-PHASES-SATISFACTION-SESSIONS.md`](plan/04-PHASES-SATISFACTION-SESSIONS.md),
  [`plan/05-PHASES-EVENTS-LONGTERM.md`](plan/05-PHASES-EVENTS-LONGTERM.md),
  [`plan/06-PHASES-LEARNED-RANKING.md`](plan/06-PHASES-LEARNED-RANKING.md) — session-sized
  implementation plans for phases 13–24 (Releases A–E), each with explicit prerequisites and exit
  criteria.
- [`PHASE-HISTORY-V2.md`](PHASE-HISTORY-V2.md) — the V2 phase-by-phase record, condensed from
  `commit.md`'s "V2 Planning" and Phase 13–23 entries (see that file's header for how condensation
  was done): commit hashes, headline experiment numbers, deviations, and known issues carried
  forward.
- [`P20-CONTRACTS.md`](P20-CONTRACTS.md) — frozen cross-package integration contract for Phase 20,
  an orchestration artifact.
- [`P21-CONTRACTS.md`](P21-CONTRACTS.md) — frozen cross-package integration contract for Phase 21,
  an orchestration artifact.
- [`P22-CONTRACTS.md`](P22-CONTRACTS.md) — frozen cross-package integration contract for Phase 22,
  an orchestration artifact.
- [`P23-CONTRACTS.md`](P23-CONTRACTS.md) — frozen cross-package integration contract for Phase 23,
  an orchestration artifact.

These are historical artifacts: file paths inside them reference the original local development
layout (sibling checkouts, absolute paths) and are preserved as written rather than rewritten
after the fact. `PHASE-HISTORY-V2.md` is the one exception noted above (condensed rather than
verbatim); everything else here — V1 and V2 alike — is copied byte-for-byte from the planning
repo.
