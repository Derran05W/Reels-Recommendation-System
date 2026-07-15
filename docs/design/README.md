# Design artifacts

The process documents behind ReelRank, preserved as written during development (July 2026):

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

These are historical artifacts: file paths inside them reference the original local development
layout (sibling checkouts, absolute paths) and are preserved as written rather than rewritten
after the fact.
