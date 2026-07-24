# ReelRank — Phase History, V2 Addendum (Realism Upgrade, Phases 13–24)

Companion to [`PHASE-HISTORY.md`](PHASE-HISTORY.md) (Phases 0–12, V1). Folded into the public
repo at Phase 24 per `plan/00-DESIGN-DECISIONS-V2.md` D25 ("Phase 24 folds V2 planning material
into `docs/design/` the way Phase 12 did for V1").

**How this file differs from `PHASE-HISTORY.md`:** `PHASE-HISTORY.md` is a verbatim snapshot of
`commit.md`'s checklist and commit-history sections as they stood at Phase 12. This file is
instead a **condensed distillation** of `commit.md`'s "V2 Planning" and "Phase 13"–"Phase 23"
entries: narration is trimmed, but every headline number below is copied verbatim from the source
entry, never paraphrased, re-derived, or rounded differently. `commit.md` (planning repo,
`Reels-Simulation/commit.md`) remains the complete, uncondensed, canonical record — including the
full package-orchestration narrative, every named test, and every constant this file compresses.

V2 introduced nine binding decisions (D17–D25, [`plan/00-DESIGN-DECISIONS-V2.md`](plan/00-DESIGN-DECISIONS-V2.md))
on top of V1's D1–D16, all of which stayed in force unchanged throughout. Every V2 mechanism ships
behind a config gate that defaults to exactly V1's behaviour (D17); every phase's exit criteria
included a gates-off, byte-identical re-run of the committed golden baseline
(`tests/golden/v1-baseline/`, captured at Phase 13) and, from Phase 18 on, the event-log digest
golden (`tests/golden/event-digest/`). Full TDD: [`TECHNICAL-DESIGN-V2.md`](TECHNICAL-DESIGN-V2.md).

---

## Checklist (V2 portion)

- [x] **V2 Planning** — V2 TDD adaptation, decisions D17–D25, phase plans Parts 4–6
- [x] **Phase 13** — Multi-factor content and hidden user channels (`plan/04-PHASES-SATISFACTION-SESSIONS.md`)
- [x] **Phase 14** — Latent reactions and observable behaviour V2 (`plan/04-PHASES-SATISFACTION-SESSIONS.md`)
- [x] **Phase 15** — Welfare metrics and the engagement-vs-satisfaction experiment (`plan/04-PHASES-SATISFACTION-SESSIONS.md`)
- [x] **Phase 16** — Session dynamics: fatigue, probabilistic exit, session outcomes (`plan/04-PHASES-SATISFACTION-SESSIONS.md`)
- [x] **Phase 17** — Personalized diversity and fatigue heterogeneity (`plan/04-PHASES-SATISFACTION-SESSIONS.md`)
- [x] **Phase 18** — Event-driven simulation core (`plan/05-PHASES-EVENTS-LONGTERM.md`)
- [x] **Phase 19** — Feed prefetch, refresh, and the adaptation trade-off (`plan/05-PHASES-EVENTS-LONGTERM.md`)
- [x] **Phase 20** — Exposure-driven preferences and retention (`plan/05-PHASES-EVENTS-LONGTERM.md`)
- [x] **Phase 21** — Ecosystem failure-mode experiment suite (`plan/05-PHASES-EVENTS-LONGTERM.md`)
- [x] **Phase 22** — Training-data logging and offline learned models (`plan/06-PHASES-LEARNED-RANKING.md`)
- [x] **Phase 23** — Learned multi-objective ranking in the loop (`plan/06-PHASES-LEARNED-RANKING.md`)
- [ ] **Phase 24** — V2 documentation, reproducibility, and completion audit (`plan/06-PHASES-LEARNED-RANKING.md`) — this file is one of its deliverables (task 6, D25).

---

## Commit History

### V2 Planning — 2026-07-17
- **Repo:** Reels-Simulation
- **Summary:** Adapted the Realism V2 TDD into the project's phase machinery: V2 TDD saved
  verbatim as `REELS-SIMULATION-V2.md` (four markdown-mangled equation blocks in §4.7–4.9/§4.15
  restored; §4.5 gap preserved for stable section references), binding addendum
  `plan/00-DESIGN-DECISIONS-V2.md` (D17–D25), and 12 session-sized phases across three files:
  `plan/04-PHASES-SATISFACTION-SESSIONS.md` (13–17, Tiers 1–2), `plan/05-PHASES-EVENTS-
  LONGTERM.md` (18–21, Tiers 3–4), `plan/06-PHASES-LEARNED-RANKING.md` (22–24, Tier 5 +
  close-out). `/next-phase` phase→file map extended to 13–24.
- **Key decisions:** every V2 mechanism config-gated with V1-preserving defaults plus a committed
  golden baseline byte-diffed every phase (D17); hidden-state isolation extended to reel/latent/
  session/retention state under `simulation/hidden/` with an automated include-graph guard (D18);
  new RNG streams pinned by name so V1 streams never perturb (D19); event-queue determinism
  contract — pinned tie-breaker hash, per-timestamp global-state snapshots, legacy round-robin
  runner retained permanently as default (D20); Tier 5 learners in-house logistic/linear only,
  zero new dependencies (D21); V2 §6 four-metric-group reporting, no aggregate score ever (D22);
  vector-db frozen for all of V2 (D23); archetype catalog is config data, not code (D24).
- **Deviations:** none.
- **Known issues:** none. Phase 13's first task captures the D17 golden baseline BEFORE any V2
  code lands; Phase 18 flagged as V2's riskiest session (runner core), with mandatory medium
  golden re-verification at Phases 14, 16, 18, and 24.

### Phase 13 — Multi-factor content and hidden user channels — 2026-07-18
- **reel-rank commit(s):** bcb5ec3 `Phase 13: multi-factor content and hidden user channels —
  archetype catalog, modality spaces, V2 hidden traits, D17 golden baseline, D18 include guard`
- **Summary:** Built test-first across three parallel worktree packages plus Fable scaffolding,
  squashed at phase end. **D17 golden baseline captured FIRST at clean V1 HEAD `c8e032b`**
  (Release: small `hnsw_ranker_diversity` + Phase 10 `session_aware` drift arm), committed under
  `tests/golden/v1-baseline/`; the gates-off re-run at phase HEAD reproduces both arms
  **byte-identically** (5 deterministic CSVs). Behind `realism.content_v2` (default off): `Reel`
  gains 3 modality embeddings + 8 content scalars + `LanguageId`; a config-driven eight-archetype
  catalog (D24) shapes attributes on new streams `"archetypes"`/`"reels-v2"` (pinned per-reel draw
  contract); `HiddenReelState` (new) and `HiddenUserState` (moved into `simulation/hidden/`) gain
  the V2 channels plus forward traits for Phases 16–20 on `"users-v2"`. D18's automated
  include-graph guard is live in ctest + CI. `inspect_user --v2-summary`: useful archetype
  usefulness 0.847 vs polished_irrelevant 0.156, all 60k modality embeddings unit-norm.
  **586/586 tests green (48 new)**: 24-seed stream-discipline suite; 50k-reel archetype
  statistics (mixtures within max(0.01, 10% rel); ragebait controversy 0.838 vs 0.251 population;
  clickbait strength 0.878 vs 0.239; music-reel cosine-to-centre 0.994 vs 0.787); full-scale
  100k/10k gate-on generation deterministic in ~0.9 s.
- **Published results:** none (no experiment arms this phase) — the committed artifact is the D17
  golden baseline (`tests/golden/v1-baseline/` + regeneration README).
- **Deviations:** `HiddenUserState` moved from `include/rr/domain/` to `include/rr/simulation/
  hidden/` (16 includers updated) so the D18 guard protects it; `BehaviourOutcome` extracted to
  `domain/behaviour_outcome.hpp` (the guard caught a transitive include from `reward_model.hpp`, a
  type-level artifact rather than a semantic leak); `realism.content_v2` combined with
  mid-simulation injection (Phase 8 `new_users`/`new_reels`) throws at config load — deferred.
- **Known issues:** Phase 14 must extend the leak-audit allowlist in the same commit that extends
  `InteractionEvent`; the golden README overstates the small arm's re-run cost (quoted ~6 min vs
  actual ~3.4 s wall Release; drift-medium ~6.7 min) — fix opportunistically; CI GREEN across the
  full matrix (Debug/Release × macOS/Ubuntu, incl. the D18 guard ctest, pinned clang-format
  22.1.8).

### Phase 14 — Latent reactions and observable behaviour V2 — 2026-07-18
- **reel-rank commit(s):** 3d4397b `Phase 14: latent reactions and observable behaviour V2 —
  LatentReaction, BehaviourModelV2, V2 event schema, welfare plumbing, signature suite`
- **Summary:** Built test-first across three parallel worktree packages plus Fable scaffolding/
  calibration. Behind `realism.latent_reactions` (requires `content_v2`; V1 `BehaviourModel`
  untouched): every impression computes a hidden `LatentReaction` (multi-channel utility over the
  four preference channels plus scalar terms, archetype conditioning, niche gating; exactly one
  gaussian per call on new stream `"satisfaction"`), then `BehaviourModelV2` samples observables on
  `"behaviour"` (pinned 8-unconditional + 3-completed-gated draw order) — arousal-driven watch,
  clickbait opening-hook/decay, completed-because-short inflation, social-conformity likes,
  familiar-song replay loop, new comment/save/profile-visit events, satisfaction/regret-driven
  not-interested. `Simulator::stepV2` populates the V2 `InteractionEvent` fields; the leak-audit
  allowlist was extended in the same commit (13 new observable keys). **639/639 tests green
  (51 new)**: cohort table — ragebait satisfaction −0.37 with top emotionalValue 0.66; useful
  informationalValue 0.81; niche in-cohort 0.91 vs 0.43 out. `configs/realism-small.json` runs
  end-to-end: welfare mean satisfaction 0.479, mean regret 0.079 on ranked feeds. **D17 small +
  medium drift golden byte-identity verified gates-off (medium mandatory this phase) — both
  PASS.**
- **Published results:** none (the Tier-1 4-arm experiment is Phase 15's; committed evidence is
  the §7 signature suite plus the realism-small end-to-end run).
- **Deviations:** integration calibration reshaped the watch model — satisfaction's logit scale
  reduced 2.4→1.2, the reel's `emotionalIntensity` added to the watch logit (1.7), watch depth
  (0.30), and completed-band stretch (0.9); music-match completion term (0.8) and the
  multiplicative familiar-song replay loop (0.9, cap 0.8); useful archetype `satisfactionBias`
  raised 0.30→0.45. The music signature test was recalibrated to mechanism level (matched-vs-
  unmatched rewatch margin 0.15; a cohort-mean margin of 0.02 was judged structurally
  unreachable — only ~1/8 of users blend any given music centre). `notInterested` is an
  unconditional draw in V2 (V1 z-gated it).
- **Known issues:** the arousal-driven watch logit lifts unranked random-pair completion to ~0.68
  and ranked-feed completion to ~0.85 (realism-small) — well above V1's ~0.29/0.44; a tuning
  surface flagged for Phase 15's write-up. Package B was killed mid-phase by a transient API
  error and resumed from its transcript with no work lost. Ranked-feed welfare (satisfaction
  0.479 at reward/impression 0.535, hnsw_ranker) is the pre-Phase-15 baseline number. Carried:
  stale pre-Phase-13 agent worktrees (gitignored); golden README runtime overstatement.

### Phase 15 — Welfare metrics and the engagement-vs-satisfaction experiment — 2026-07-18
- **reel-rank commit(s):** c6839fd `Phase 15: welfare metrics and the engagement-vs-satisfaction
  experiment — four-group reporting, modality features, oracle ceiling, published 4-arm result`
- **Summary:** Built test-first across four parallel worktree packages (A metrics, B1 features/
  updater, B2 oracle + acceptance test, C execution tooling) plus Fable integration/calibration.
  Four-group V2 §6 reporting went live for every run (D22): `WelfareMetrics` emits
  `welfare_metrics.csv` + `welfare_archetype_metrics.csv`; ten new V2 ranking features with
  ZERO-default weights; `OracleSatisfactionRecommender` added in `evaluation/` (D18).
  **Published 4-arm experiment** (2M impressions/arm, realism-medium + presets): engagement
  preset wins every observable — completion 0.867 vs 0.782, likes 0.297 vs 0.253, comments 0.259
  vs 0.194, reward/impression +7.5% — while LOSING hidden satisfaction (0.328 vs semantic 0.355)
  with regret +26% and **2× ragebait exposure (20.0% vs 10.3%; proxy serves 0%)**;
  satisfaction-proxy nearly doubles satisfaction (0.653, regret 10× lower, satisfaction/min
  0.693) at comparable reward (0.535 vs 0.542); oracle bounds all arms (0.806; likes 0.321). V2
  §10 items 1–2 demonstrated with committed numbers. **695/695 tests green**; D17 goldens (small
  + drift-medium) byte-identical gates-off.
- **Published results:** `results/published/phase15/` — comparison.{md,csv}, figures/
  (`engagement_vs_welfare.png` headline scatter + reward/alignment/regret curves), per-arm
  artifacts.
- **Deviations:** pure semantic similarity turned out to be the watch-optimal policy under V2
  behaviour (the default V1 ranker loses ~25 watch-s to pure semantic) — the TDD's watch-time
  clause was restated to what it actually mandates ("outperform RANDOM on watch time," which
  holds: +0.9 s, margin 0.5). The engagement preset was redesigned at integration as an honest
  watch-maximizer (similarity 0.60, intensity 0.08, emotional-match 0.10, music 0.12, visual
  0.04, popularity 0.05, clickbait 0.0 — clickbaitStrength is watch-negative here).
- **Known issues:** ranked V2 arms run ~4× slower than semantic (556 s vs 146 s per 2M-impression
  arm); mean true affinity/alignment are LOW across all arms (~0.09/0.12) vs V1 phases — the V1
  semantic-affinity metric no longer summarizes quality under V2 behaviour; oracle arm wall time
  (125 s) shows expected-satisfaction ranking is cheap enough to use as a later ceiling; CI GREEN
  full matrix; carried: stale pre-Phase-13 worktrees, golden README runtime note.

### Phase 16 — Session dynamics: fatigue, probabilistic exit, session outcomes — 2026-07-18
- **reel-rank commit(s):** c887e7a `Phase 16: session dynamics — hidden session state, fatigue,
  probabilistic classified exits, session health published`
- **Summary:** Session length became an OUTCOME of feed quality, built test-first across three
  worktree packages (A simulation, B evaluation, C tooling) plus Fable integration. Behind
  `realism.session_dynamics` (requires `latent_reactions`): `HiddenSessionState` lifecycle
  (reset on session start, away-time fatigue decay, starting-satisfaction carry-over);
  per-impression fatigue accumulation across topic/creator/format/music/intensity/general
  channels, modulating latent satisfaction inside `BehaviourModelV2`, tolerance-trait-modulated.
  Probabilistic exit per impression on new streams `"session-exit"`/`"external-interruption"`
  (five-way classification: failure/satisfied/fatigue/external/regret). `SessionHealthMetrics` +
  U_s, `session_health.csv`, `metric_groups.session_health` LIVE. **742/742 tests green.**
  **Published `results/published/phase16/` (4 session arms): U_s engagement −1.64 vs proxy
  +1.58; regret/min 60×; early-failure-exit rate 0.327 vs 0.026; engagement in-session
  satisfaction collapses to 0.005 (fatigue COMPOUNDS the Phase 15 wedge); proxy sessions run
  7.7× longer AND healthier** — V2 §10 item 3 demonstrated. D17 goldens (small + drift-medium,
  medium MANDATORY this phase) byte-identical gates-off.
- **Published results:** `results/published/phase16/` — comparison.{md,csv} (session-health
  panel + the four-hour-vs-twenty-minute framing), figures/, per-arm artifacts.
- **Deviations:** two private scratch fields added to `HiddenSessionState` (format/music repeat
  detectors), documented in-header; satisfied-share margin calibrated to the demonstrated
  deterministic gap (0.015 vs a 0.05 placeholder — satisfied exits are rare at the shipped exit
  coefficients); run-end drain (`Simulator::drainOpenSessions`) implemented Fable-direct.
- **Known issues:** legacy-runner session-duration semantics span the shared round-robin clock
  (absolute durations ~1.4e5 s) — cross-arm comparisons remain sound, but durations should not be
  quoted as wall-clock realism (real per-user timelines arrive with Phase 18). Ranked sessions
  arms took ~12.9 min each (vs Phase 15's 9.3 min). Package agent wall-times ballooned to ~9.5 h
  across a machine-sleep window mid-phase; both completed on resume. CI GREEN full matrix;
  carried: stale pre-Phase-13 worktrees, golden README runtime note.

### Phase 17 — Personalized diversity and fatigue heterogeneity — 2026-07-18
- **reel-rank commit(s):** a2a1d5f `Phase 17: personalized diversity and fatigue heterogeneity —
  trait cohorts, tolerance estimator, personalized reranker, fixed-vs-personalized published` +
  563d4fa `CI: fix GCC -Wmaybe-uninitialized in tolerance_estimator computeSat`
- **Summary:** Release B (Tier 2) closer, built test-first across three worktree packages plus
  Fable scaffolding/integration/calibration. Behind `realism.personalized_diversity` (requires
  `session_dynamics`): trait-cohort generation (`realism.cohort_mix`, four V2 §4.10 named
  cohorts; empty mix byte-identical to Phase 13); heterogeneity PROVEN on identical
  repetition-heavy feeds (isolated-lever gap 0.10 — V2 §10 item 4); the observables-only
  `ToleranceEstimator` (hidden-trait correlation r=0.525 end-to-end); `PersonalizedDiversity
  Reranker` (neutral-estimate ⇒ byte-identical to fixed, tested). **Published
  `results/published/phase17/` (8 arms): personalized beats fixed on U_s for ALL FOUR cohorts —
  focused +15.4%, novelty_seeker +14.6%, creator_loyal +1.9%, easily_fatigued +15.8%.** Delivered
  feed length at medium scale is ~3.5–4.0/10 across ALL diversity arms under hard caps
  (personalization relaxes the hazard up to 10/10 for tolerant users, tightens to ~4/10 for
  intolerant). **792/792 tests green**; goldens (small + drift-medium) byte-identical gates-off.
- **Published results:** `results/published/phase17/` — comparison.{md,csv} (per-cohort
  fixed-vs-personalized panels + deltas + session-health), per-arm artifacts.
- **Deviations:** the first published run showed personalization LOSING U_s for easily_fatigued
  (−15.8%) and novelty_seeker (−32.3%) at medium scale despite passing the reduced-scale
  acceptance test — a ranking-side repetition-penalty doubling ([0,2]) stacked with the
  reranker's tighter caps; bounding the ranking-side multiplier to [0.6,1.4] and easing
  `capScaleMin` 0.5→0.7 flipped every cohort positive. Recorded as the phase's headline
  engineering lesson: personalization interventions are scale-dependent.
- **Known issues:** delivered feed length ~3.5–4/10 persists across ALL medium diversity arms
  (fixed AND personalized) — the Phase 9 hard-cap short-feed operating point remains a known
  product-tuning gap. The two new statistical suites add ~5 min to the Debug ctest wall (792
  tests ≈ 8.6 min). CI `a2a1d5f` FAILED on ubuntu (GCC `-Wmaybe-uninitialized` on an
  enumerator-exhaustive switch Clang accepts), fixed by `563d4fa` (GREEN full matrix). Carried:
  stale pre-Phase-13 worktrees, golden README runtime note.

### Phase 18 — Event-driven simulation core — 2026-07-18
- **reel-rank commit(s):** 0693663 `Phase 18: event-driven simulation core — deterministic queue,
  independent timelines, EventDrivenRunner, D20 suite`
- **Summary:** V2's riskiest phase landed clean: a deterministic event-queue runner behind
  `simulation.scheduler="event_queue"` (legacy round-robin untouched, remains default, the
  permanent D17 golden path). `EventQueue` (pinned SplitMix64 tie-breaker over (userId,
  eventType, perUserSeq), golden-tripwired); the entire `EventDrivenRunner` (independent open/
  exit/return timelines on new stream `"scheduling"`; StartReel/FinishReel/Interaction as
  same-timestamp log facets; depth-1 refill; three-phase equal-timestamp processing discharging
  the §4.14 snapshot contract); the D20 determinism suite (12 pinned tie-breaker goldens,
  720-permutation queue order-invariance, same-seed digest + byte-identical CSVs).
  **822/822 tests green with the whole determinism suite live — zero skip-guards remained and no
  comparability margin needed recalibration.** Committed digest golden
  `tests/golden/event-digest/` (**digest 1533553118870293663, 5602 events**). Tiny event run
  (200u/2000r/6h): **5.03 s, 12,380 impressions, 3,432 closed + 17 open sessions, mean
  concurrent-online 0.119.** D17 goldens (small + drift-medium, medium mandatory) byte-identical
  gates-off.
- **Published results:** none (the batch-depth experiment is Phase 19's) — the committed artifact
  is the event-digest golden plus regeneration tooling.
- **Deviations:** none from D20/plan. Within mandate: consumption-collapse design (atomic
  `stepV2`), the additive `syncClock` event-clock bridge, `PreferenceDrift`/`ReelPublished` enums
  reserved (drift keeps the verbatim interaction-count keying; injection is config-blocked under
  `content_v2`).
- **Known issues:** round/interaction semantics differ by mode BY DESIGN — event-mode "rounds"
  are simulated days and `interactions_per_user` becomes an outcome; cross-mode comparisons use
  documented comparability bands, never byte-identity. `coldStart`/`adaptation` report blocks
  stay unconfigured in event mode (Phase 19 defines its own adaptation measure). Carried: stale
  pre-Phase-13 worktrees, golden README runtime note.

### Phase 19 — Feed prefetch, refresh, and the adaptation trade-off — 2026-07-19
- **reel-rank commit(s):** 4555d1d `Phase 19: feed prefetch/refresh serving strategies — prefetch
  depth, threshold refill, intent invalidation on the event runner; cost/staleness
  instrumentation; batch-depth freshness-vs-cost frontier under drift published`
- **Summary:** Serving strategy became a measurable design axis, built test-first across two
  parallel worktree packages (A: serving strategies + cost/staleness instrumentation; C:
  experiment matrix/comparison/frontier-plot tooling) plus Fable scaffolding/integration. Behind
  `serving.*` (event-mode only; defaults byte-preserve Phase 18 — proven by the committed
  event-digest golden): prefetch depth, threshold refill, intent invalidation, preserve-
  downloaded semantics — all pure decision helpers, unit-tested in isolation. D22 instrumentation
  (feed requests, ranking computations, per-impression staleness, stale-impression rate,
  satisfaction lost before refresh, drift adaptation delay). **852/852 tests green**; the V2 §7
  mandated `Batch1AdaptsNoSlowerThanBatch20` test runs LIVE and passes; D17 and event-digest
  goldens byte-identical.
- **Published results:** `results/published/phase19/` — 4 arms (`serving.prefetch_depth` ∈
  {1,3,10,20}, event-mode realism-medium, horizon 302400 s ≈ 3.5 simulated days, ~2.0M
  impressions/arm) + figures (`freshness_cost_frontier.png` headline). **Headline: depth 20 cuts
  serving cost 79.4% vs depth 1 (ranking computations 1.01B → 208M; feed requests 2.03M → 417k)
  while adaptation delay after drift stays flat (4.06 vs 4.08 interactions); the freshness price
  lives on the staleness axes: stale-impression rate 0 → 0.794, mean staleness 0 → 3.05 updater
  applications, satisfaction lost before refresh 0 → 538k, U_s −0.693 → −0.717.**
- **Deviations:** the suggested A/B/C split executed as A+C with B (instrumentation) folded into
  A (orchestration-level only; every planned task shipped); all arms fixed
  `refill_threshold=0` so prefetch depth was the single free variable; adaptation delay was
  measured via package A's per-user satisfaction-recovery machinery rather than Phase 10's
  round-based `AdaptationReport` (round windows don't exist in event mode — anticipated by the
  Phase 18 entry).
- **Known issues:** adaptation delay is flat across depths at `refill_threshold=0`
  (4.06–4.25 interactions) — dominated by learning dynamics rather than depth, with a small
  non-monotone wobble at depth 20; batch1 arm wall ≈100 min vs batch20 ≈34 min (budget
  accordingly for later multi-arm phases). CI GREEN full matrix (~37 min). Carried: stale
  pre-Phase-13 worktrees, golden README runtime note.

### Phase 20 — Exposure-driven preferences and retention — 2026-07-19
- **reel-rank commit(s):** b757115 `Phase 20: exposure-driven preferences and retention —
  satisfaction-driven evolution + saturation/aversion/trust erosion, hazard-based retention/churn
  model, long-term metrics group, policy-influence experiment published (10k matched-user
  divergence 0.039)`
- **Summary:** Recommendations started shaping the world, built test-first across three parallel
  worktree packages plus a new orchestration device: a Fable-authored frozen-contracts file
  (`docs/design/P20-CONTRACTS.md`), which eliminated Phase 19's schema-guess integration cost.
  Behind `realism.preference_evolution` (requires `session_dynamics`):
  `PreferenceEvolution::applyImpression` — p′=normalize((1−η_u)p+η_u·s·v) on semantic + three
  modality channels, driven by HIDDEN satisfaction (never reward), ZERO rng draws; §4.16 exposure
  state (topic exhaustion, creator burnout, per-reel novelty depletion, satisfaction-weighted
  aversion); trust erosion/recovery (asymmetric 0.08–0.10 erode vs 0.02 recover). Behind
  `retention.enabled` (requires `session_dynamics` + event mode): `RetentionModel` (exponential
  waiting-time hazard, habit seed/strengthen/decay, churn); frozen `LongTermReport` populated
  into `longterm_metrics.csv` + per-user `hidden_preference_final.csv`. **901/901 tests green
  (49 new)**; D17 goldens and the Phase 18 event-digest golden byte-identical gates-off.
- **Published results:** `results/published/phase20/` — 4 arms (engagement/proxy ranking presets
  × evolution-on/counterfactual, event-mode 10k×100k, 9 simulated days, seed 42) + 7 figures.
  **Headline: the ranking policy measurably reshapes hidden preferences — engagement-on vs
  proxy-on matched-user divergence mean 0.0388 (median 0.0177, p90 0.106, n=10,000, same world/
  seed); per-policy distortion vs its evolution-off twin: proxy 0.0546 vs engagement 0.0281.
  Engagement optimization shows measurable negative long-term outcomes: churn 26.5% vs proxy
  9.0%, retention_1d 0.627 vs 0.824, sessions/user/day 0.86 vs 1.71, U_s −3.94 vs −1.54, mean
  hidden satisfaction −0.48 vs −0.12. Trust erodes to near-floor under BOTH policies at these
  constants (0.018/0.024 vs 0.704 static twins).**
- **Deviations:** the counterfactual-twin design was integration-corrected — `-off` twins run
  evolution-OFF/retention-ON (not both gates off), preserving the export the distortion measure
  reads and making the pair differ ONLY in evolution (verified exactly: cross-run distortion
  provably equals the -on arm's shift-from-initial — the load-bearing Tier-4 evidence). Evolution
  draws ZERO rng; the D19-pinned `"preference-evolution"` stream is reserved-unused.
- **Known issues:** trust-floor operating point — at the shipped erosion constants, 9 days of
  EITHER policy drives mean trust to ~0.02 (proxy consistently above engagement, both near
  floor), so trust separates policies only weakly at this horizon; retention/churn/sessions carry
  the separation instead. Evolution-on arms show LOWER mean hidden satisfaction than their
  off-twins (engagement −0.48 vs +0.04) — saturation/aversion dominate reinforcement at this
  operating point, a finding rather than a defect. Per-user `hidden_preference_final.csv` (10k
  rows/arm) is NOT committed (regenerates deterministically from configs + seed 42). CI GREEN
  (~40 min).

### Phase 21 — Ecosystem failure-mode experiment suite — 2026-07-19
- **reel-rank commit(s):** e8023f7 `Phase 21: ecosystem failure-mode suite — seven pre-registered
  scenarios vs identical-world controls, per-day ecosystem metrics (creator HHI / archetype
  shares / niche match), exploration.enable_at_day time gate, ragebait + exploration-recovery
  harms test-enforced, ECOSYSTEM.md verdicts published` + 3d50033 `CI: replace eps-monotonicity
  assert with same-regime band in ExplorationRecovery`
- **Summary:** The V2 flagship results set, built across a scaffold plus four parallel packages
  (A filter-bubble/exploration-recovery, worktree; B ragebait/frontier, worktree; C popularity/
  creator/niche trio, main checkout; D consolidation/plots, main checkout) under a frozen
  contracts file (`docs/design/P21-CONTRACTS.md`). Per-day `ecosystem_metrics.csv` + `ecosystem`
  summary block (creator HHI, tail-creator share, eight archetype exposure shares, niche
  in-cohort match rate); `exploration.enable_at_day` time gate. **Nineteen full-scale arms**
  (10k×100k, event mode, 9 days, seed 42, identical worlds per scenario). **906/906 tests green
  (5 new)**; D17 small + event-digest goldens byte-identical (drift-medium skipped — product C++
  unchanged after the scaffold, which verified both).
- **Published results:** `results/published/phase21/` — `ECOSYSTEM.md` (the seven-verdict
  headline document) + per-scenario comparison.{md,csv}, figures, lean per-arm artifacts.
  **Verdicts: ragebait amplification SUPPORTED** (test-enforced: engagement serves 69.0%
  ragebait vs proxy 0.0%, satisfaction −0.56 vs −0.16, regret exits 0.59% vs 0); **creator
  overconcentration CONFIRMED** (uncapped affinity HHI 0.0205 vs capped 0.0180 on all 9 days);
  **exploration recovery CONDITIONAL** (absent at medium scale — the exploit bubble is already
  diffuse across 5k creators — but demonstrated + test-enforced in the concentrated regime);
  **filter-bubble PARTIAL** (niche starvation −46% confirmed; entropy collapse NOT observed;
  concentration INVERTED); **popularity feedback PARTIAL** (tail-creator starvation 2.7×
  confirmed every day; HHI wrong direction); **niche starvation PARTIAL** (decline confirmed but
  reverses by day 8); **satisfaction-vs-retention NO TRADE-OFF** (retention and welfare co-move,
  τ=+0.80; pure engagement dominated on every axis; knee λ≈0.25–0.5; policy moves retention_7d
  2.7 pp, sessions 2×, satisfaction 0.36). V2 §10 item 7 demonstrated regardless.
- **Deviations:** "susceptible-heavy population" for ragebait was implemented via a 2× ragebait
  archetype mixture (not `cohort_mix` — `controversyTolerance` isn't cohort-overridable), and
  pre-registered honestly as the content-mixture lever. Entropy was judged not a usable bubble
  signal at this operating point (rises everywhere from Phase 20 saturation/aversion) —
  concentration verdicts rest on exposure shares instead. Package A's first agent stalled before
  any work (API-level wedge) and was relaunched fresh on the same pristine worktree (~2 h wall
  lost, no work lost).
- **Known issues:** `ecosystem.creator_hhi_final_day` reads the literal last day-row, which is
  noise at a horizon that is an exact multiple of 86400 (both creator_overconcentration arms tie
  at 1.0) — all published analyses use day-8/whole-run readings instead. A true
  satisfaction-vs-retention conflict requires satisfaction-decoupled habit mechanics the
  simulator deliberately lacks (recorded as designed future work). CI `e8023f7` FAILED on ubuntu
  only (the eps-monotonicity assert — the Phase 0 libm-ulp note finally biting a statistical
  assert), fixed by `3d50033` (GREEN full matrix, ~40 min). Carried: stale pre-Phase-13
  worktrees, golden README runtime note, the Phase 20 trust-floor operating point.

### Phase 22 — Training-data logging and offline learned models — 2026-07-23
- **reel-rank commit(s):** 84323be `Phase 22: training-data logging and offline learned models —
  leak-proof eligibility→impression log with positions/exploration provenance, emitted-file
  purity audit, noisy survey table, in-house deterministic SGD logistic/linear learners, offline
  eval beats frequency baselines on held-out real logs (completed AUC 0.714 vs 0.500)` + 0de6f9a
  (CI fix: GCC `-Wrange-loop-construct`)
- **Summary:** Tier 5's foundation, built via scaffold plus three parallel packages under
  `docs/design/P22-CONTRACTS.md`. Behind `learning_v2.training_log` (event-mode-only): an
  additive null-default `RankingCapture` sink; `TrainingLogger` (a non-carve-out translation
  unit, structurally barred from hidden state by the extended D18 guard) streams
  `training_log/{schema.json,requests,candidates,outcomes,survey}.csv` with rotation, RNG-free
  request sampling via pinned hash01 salts. The V2 §7 purity audit runs on EMITTED FILES
  (frozen-header allowlists + forbidden-substring scan). The sanctioned survey
  (`survey.enabled`) draws exactly two per surveyed impression on the pinned
  `"explicit-feedback"` stream (Likert 1–5 quantized noisy `immediateSatisfaction`). In-house
  learners per D21 (zero new deps): deterministic mini-batch SGD logistic + linear regression,
  `apps/train_models` (temporal + user-disjoint pinned-hash splits). **951/951 tests green
  (43 new)**; D17 small + drift-medium + event-digest goldens byte-identical at phase HEAD.
- **Published results:** `results/published/phase22/` — `offline_eval.md` (per-target × both
  splits), calibration/ + figures, lean logworld + model artifacts. **Headline (REAL 216 MB log
  from the pinned seed-42 log world, ~90k train / 21k test impressions): learned models beat
  BOTH frequency baselines AND the served-score predictor on every high-signal target in both
  splits — completed AUC 0.714 vs 0.500 (global-freq) / 0.628 (per-source) / 0.597 (served
  score); liked 0.714–0.740; not_interested 0.664–0.688; session-exit 0.604; the
  survey-satisfaction regressor beats the mean baseline (RMSE 1.09–1.18 vs 1.26–1.29).** Honest
  per-target reporting: the ~1.5%-base-rate target shows no learnable signal (learned ≈0.50–0.51
  AUC), and served_score is anti-predictive on several welfare-adjacent targets (0.39–0.43).
  Tier-5 acceptance 1 (offline half), 2, and 4 demonstrated.
- **Deviations:** the scaffold gates BOTH `training_log` and `survey.enabled` on event mode (the
  contracts named only `training_log`; the survey hook is physically event-runner-only). Feature
  column names followed the `FeatureVector` struct (`repetition`, `impression_count`), overriding
  the contracts' prose naming — the frozen schema header is the single source of truth.
  `++requestCount` moved before `recommend()` in the event runner (needed as the logging join
  key; digest/goldens prove byte-identity). Package A was killed by the WEEKLY API LIMIT one step
  from done and resumed 4 days later from its preserved transcript (verified 925/925 in its
  worktree, zero work lost).
- **Known issues:** pre-run log-size estimates diverged (~1 GB vs ~170 MB extrapolations); the
  real run landed at 216 MB / ~111k logged shown impressions — size taken from real runs, not
  extrapolations. served_score anti-predictivity on welfare-adjacent targets is a finding for
  Phase 23's multi-objective value function. CI `84323be` FAILED on ubuntu only (GCC
  `-Wrange-loop-construct` in the new statistical test), fixed by `0de6f9a` (GREEN full matrix,
  ~60 min). Carried: stale pre-Phase-13 worktrees, golden README runtime note, the Phase 20
  trust-floor operating point.

### Phase 23 — Learned multi-objective ranking in the loop — 2026-07-23
- **reel-rank commit(s):** 8458213 `Phase 23: learned multi-objective ranking in the loop —
  LearnedRanker over the P22 models with deterministic in-loop retraining, closed-loop matrix on
  identical worlds (learned dominates hand-tuned: reward 0.404 vs 0.399 at satisfaction -0.20 vs
  -0.30), monotone frontier published (w_sat_100 best on every axis), offline-vs-closed-loop gap
  analysis`
- **Summary:** Tier 5's payoff, built under `docs/design/P23-CONTRACTS.md` with a simplified
  orchestration (one C++ writer, no worktrees needed — A=C++, B=matrix/gap tooling, C=plots/
  renderer, all concurrent in the main checkout with disjoint files). Behind
  `learning_v2.learned_ranker` (requires `training_log`): `LearnedRanker` serving the V2 §4.21
  value over the Phase 22 models — pWatch = clamped watch-ratio linear/1.5; pSatisfaction =
  survey regressor (Likert−1)/4 (contributing 0 when untrained); **pRegret := the not_interested
  model** (regret is hidden; not-interested is its sanctioned observable correlate) — full
  §14.4-parity explanation output. Deterministic in-loop retraining (per-version
  SplitMix64-derived seeds, retrain at simulated-hour boundaries, cold-start byte-exact
  `WeightedRanker` fallback below `min_training_rows`). **961/961 tests green (10 new)** incl.
  serving-purity parity (the ranker's `FeatureVector` row == the logged `candidates.csv` row
  end-to-end, V2 §10 item 8); D17 small + drift-medium + event-digest goldens byte-identical.
- **Published results:** `results/published/phase23/` — comparison.{md,csv} (9 identical-world
  arms), `gap_analysis.{md,csv}`, 10 figures, per-arm + offline re-eval artifacts, explanation
  sample. **Headline: the learned multi-objective ranker DOMINATES the hand-tuned system on
  identical worlds — reward/impression 0.404 vs 0.399 WITH hidden satisfaction −0.202 vs −0.298,
  U_s −2.10 vs −2.83, trust 0.021 vs 0.012 — and beats the semantic control everywhere (0.249
  reward). The weight sweep exposes a MONOTONE frontier: w_sat_100 (satisfaction-only) is best on
  EVERY axis including engagement (0.421 reward, −0.149 satisfaction), and the pure-engagement
  vector is strictly dominated (0.374, −0.284). Survey feedback helps modestly (satisfaction
  −0.202 with vs −0.217 without).** V2 §10 item 8 demonstrated; Tier-5 acceptance complete
  (offline Phase 22 half + closed-loop here, identical worlds, purity held through serving).
- **Deviations:** exit criterion 2 asked for a weight vector that trades engagement for
  satisfaction+retention vs pure-engagement; the honest result is DOMINANCE, not a trade
  (w_sat_100 wins satisfaction +0.135, U_s +0.81, trust +0.011 AND reward +0.047;
  `retention_7d` saturates at 0.994 across all arms at this horizon) — recorded as met in the
  dominance direction, with a genuine trade-off requiring satisfaction-decoupled habit mechanics
  (future work since Phase 21). B's semantic-arm extraction found the literal Phase 15 diff
  degenerate (Phase 15's semantic arm differed by algorithm, not weights) and implemented the
  contracts' explicit zero-all-but-similarity prescription instead. Only the six §4.21-value
  targets retrain in-loop (completed/liked stay offline-only, from Phase 22).
- **Known issues:** `retention_7d` saturates (0.994) on this config/horizon — the retention axis
  of the frontier is carried by sessions/user/day and welfare instead; a discriminating retention
  frontier would need a harsher churn threshold or longer horizon. Stale-ctest-registration
  gotcha: `ctest` run without a fresh build invoked 951 of the 961 registered tests (always
  `cmake --build` before a final ctest count). CI GREEN full matrix on the first run (~45 min).
  Carried: stale pre-Phase-13 worktrees, golden README runtime note, the Phase 20 trust-floor
  operating point (visible again here: all trust values ≈0.01–0.03).

---

Phase 24 (V2 documentation, reproducibility, and completion audit) is in progress as of this
writing; its own commit-history entry belongs in `commit.md` at close-out, not here — this file
covers only the phases complete at the time it was written (V2 Planning through Phase 23, HEAD
`8458213`). For the uncondensed record — full deviation prose, every named test, and the
package-orchestration detail this file compresses — see `Reels-Simulation/commit.md`.
