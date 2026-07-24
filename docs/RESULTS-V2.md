# ReelRank — Results (Realism V2)

Companion to [`RESULTS.md`](RESULTS.md) (V1, Phases 0–12, which remains authoritative for the base
retrieval/ranking system). This document reports **Realism V2** (Phases 13–23: multi-factor content
and hidden channels, latent reactions, session dynamics, event-driven simulation, exposure-driven
preferences and retention, the ecosystem failure-mode suite, and learned multi-objective ranking)
against the V2 TDD's [§10 "Definition of Completion"](design/TECHNICAL-DESIGN-V2.md) — ten
completion items, audited below as an evidence table — and its [§6 four-metric-group evaluation
framework](design/TECHNICAL-DESIGN-V2.md) (engagement / hidden welfare / session health /
recommendation quality — never blended into one score, decision D22).

Every claim below points to a committed artifact under
[`../results/published/`](../results/published/) or a named test under
[`../tests/`](../tests/); per-phase deviations and known issues are in
[`design/PHASE-HISTORY-V2.md`](design/PHASE-HISTORY-V2.md) (condensed from the planning repo's
`commit.md`, Phases 13–23). Figures are the canonical V2 set under
[`../results/published/figures-v2/`](../results/published/figures-v2/) where one exists for the
claim; otherwise the per-phase figure under `results/published/phaseNN/figures/` is cited.

## Hardware and measurement context

Every number below comes from the same machine as V1's Phase 11 reference: **Apple M5, 10 cores,
24 GiB RAM, macOS (Darwin 25.5.0, arm64), AppleClang 21.0.0.21000101, C++20, Release (`-O3`)**,
single-threaded simulation core (D13), seed 42 unless a table says otherwise. Provenance for every
run is its committed `metadata.json`. **Every V2-phase run's `metadata.json` (Phases 15–23) records
the identical `vector_db_sha` `17e434a3e741f702ffb8e0f00b0484676d988198`** — byte-for-byte V1's
Phase 11 pin — direct evidence that vector-db was never touched across the realism upgrade (decision
D23; §10 item 10 below). As in V1, deterministic CSVs (welfare/session-health/long-term/ecosystem/
training-eval metrics and `summary.json`'s non-timing fields) are byte-reproducible under a fixed
seed; only wall-clock timing fields vary run to run, and several V2 phases ran their arms
**concurrently** (contention noted per phase in `comparison.md`'s own notes) — cross-arm comparisons
within a phase remain like-for-like regardless.

At HEAD `8458213` (Phase 23): **961/961 tests green, CI green across the full matrix**
(Debug/Release × macOS/Ubuntu + the pinned clang-format 22.1.8).

---

## §10 completion audit — the ten-item evidence table

The V2 TDD's own words (§10): *"The realism upgrade is complete when ReelRank can demonstrate all of
the following."* One row per item. Status is one of **DEMONSTRATED** / **PARTIAL** / **LIMITATION**;
nothing here is argued around — every nuance (a flat metric where a naive reading would expect
movement, a trade-off that turned out to be a dominance instead, a target with no learnable signal)
is stated plainly in the evidence, not smoothed over. All ten items are **DEMONSTRATED**; the honest
texture is in *how*.

| # | V2 §10 claim | Status | Evidence |
|---|---|---|---|
| 1 | High engagement can coexist with low satisfaction | **DEMONSTRATED** | `V2SignatureStatistical.RagebaitHighEngagementNegativeSatisfaction`, `.WatchSatisfactionCorrelationImperfectButPositive` (`tests/property/v2_signature_statistical_test.cpp`). Published: P15's engagement preset beats semantic on reward/impression (+7.5%), likes, and comments while *losing* hidden satisfaction (0.328 vs semantic's 0.355) and serving 2× the ragebait exposure (20.1% vs semantic 10.3%; proxy 0%) — [`phase15/comparison.md`](../results/published/phase15/comparison.md). Reproduced ecosystem-wide in P21: engagement serves 69.0% ragebait vs proxy's 0.0%, satisfaction −0.559 vs −0.156, test-enforced by `RagebaitAmplificationStatisticalTest.EngagementOverservesRagebaitAndDegradesWelfare` — [`phase21/ECOSYSTEM.md`](../results/published/phase21/ECOSYSTEM.md), [`figures-v2/p21_archetype_share_by_day_ragebait_engagement.png`](../results/published/figures-v2/p21_archetype_share_by_day_ragebait_engagement.png). |
| 2 | High satisfaction can occur with modest observable engagement | **DEMONSTRATED** | `V2SignatureStatistical.UsefulHighSatisfactionWeakEngagement`; `V2ArmStatisticalTest.EngagementBelowProxyOnHiddenSatisfaction`, `.OracleUpperBoundsAllArmsOnHiddenSatisfaction` (`tests/property/v2_arm_statistical_test.cpp`). Published: P15's satisfaction-proxy preset has *lower* watch time/impression (56.5 s) than both engagement (64.1 s) and semantic (69.1 s), yet reaches mean hidden satisfaction 0.653 — nearly double engagement's 0.328 (+98.9%) and semantic's 0.355 (+83.8%) — at comparable reward (0.535 vs 0.542/0.504) and ~9.6× lower regret — [`phase15/comparison.md`](../results/published/phase15/comparison.md), [`figures-v2/p15_engagement_vs_welfare.png`](../results/published/figures-v2/p15_engagement_vs_welfare.png). |
| 3 | Poor recommendation sequences cause early failure exits | **DEMONSTRATED** | `SessionExitStatisticalTest.BadFeedIncreasesEarlyFailureExits` (`tests/property/session_exit_statistical_test.cpp`; comment self-cites "V2 TDD §4.8, §10 item 3"). Published: P16 session-health panel — engagement-preset early-failure-exit rate 0.327 vs satisfaction-proxy's 0.026 (12.8×), U_s −1.638 vs +1.575, regret/min ~61× higher — [`phase16/comparison.md`](../results/published/phase16/comparison.md), [`figures-v2/p16_session_health_panel.png`](../results/published/figures-v2/p16_session_health_panel.png). |
| 4 | Repetition affects users differently according to hidden traits | **DEMONSTRATED** | `FatigueHeterogeneityStatistical.FocusedDeclinesSlowerThanFastCohorts` (comment self-cites "V2 TDD 10-item-4"; ≥0.05 decline-rate margin, ≥0.10 mean-satisfaction margin on **identical** repetition-heavy feeds), `.NoveltyToleranceGeneralFatigueLeverLowersSatisfaction`; `ToleranceEstimatorTest.EstimateCorrelatesWithHiddenRepetitionTolerance` (r=0.525, observables-only estimator vs hidden trait); `PersonalizedVsFixedStatisticalTest.PersonalizedBeatsFixedOnUsForFocused` / `.PersonalizedBeatsFixedOnUsForEasilyFatigued`. Published: P17's four-cohort panel — personalized diversity beats fixed on U_s for **all four** named cohorts (focused +15.4%, novelty_seeker +14.6%, creator_loyal +1.9%, easily_fatigued +15.8%) — [`phase17/comparison.md`](../results/published/phase17/comparison.md), [`figures-v2/p17_cohort_panel.png`](../results/published/figures-v2/p17_cohort_panel.png). |
| 5 | Users operate on independent timelines | **DEMONSTRATED** | `EventRunnerPipelineTest.UsersRunOnIndependentTimelines` (`tests/integration/event_runner_pipeline_test.cpp` — a staggered-open world shows concurrent-online occupancy strictly between 0 and 1 and *lower* than a synchronized-open control; multi-session exits/returns happen mid-run); `BaselineReturnDelayTest.HeavyUsersReturnSoonerOnAverage`; the full D20 suite (`EventQueueTest.*`, `EventDeterminismTest.*`). Published: the pinned tiny full-gate event-mode fixture ([`../tests/golden/event-digest/config.json`](../tests/golden/event-digest/config.json), 200 users/2,000 reels/6 simulated hours) reproduces committed digest `1533553118870293663` over 5,602 events — `docs/design/PHASE-HISTORY-V2.md` Phase 18 entry. |
| 6 | Feed batch depth produces a measurable adaptation trade-off | **DEMONSTRATED** | `BatchAdaptationStatisticalTest.Batch1AdaptsNoSlowerThanBatch20` (`tests/property/batch_adaptation_statistical_test.cpp` — the V2 §7-mandated property, live and green). Published: P19's prefetch-depth ∈ {1,3,10,20} sweep — depth 20 cuts serving cost 79.4% (ranking computations 1.01B → 208M; feed requests 2.03M → 417k) while adaptation delay after drift stays flat (4.06 vs 4.08 interactions, +0.5% — exactly the mandated no-slower property); the freshness price shows up on the staleness axes instead (stale-impression rate 0 → 0.794, mean staleness 0 → 3.05 updater applications, satisfaction lost before refresh 0 → 538k) — [`phase19/comparison.md`](../results/published/phase19/comparison.md), [`figures-v2/p19_freshness_cost_frontier.png`](../results/published/figures-v2/p19_freshness_cost_frontier.png). |
| 7 | Ranking policy changes long-term satisfaction and retention | **DEMONSTRATED** | `PolicyDivergenceTest.EngagementVsProxyDivergesOncePreferencesMove` (`tests/unit/policy_divergence_test.cpp`). Published: P20 — 10,000 matched users, identical world/seed, only the ranking policy differs: mean hidden-preference divergence 0.0388 (median 0.0177, p90 0.106); churn 26.45% (engagement) vs 8.99% (proxy); retention_1d 0.627 vs 0.824; sessions/user/day 0.86 vs 1.71 — [`phase20/comparison.md`](../results/published/phase20/comparison.md). P21's satisfaction-vs-retention λ-sweep confirms the lever moves retention_7d by 2.7 pp and satisfaction by 0.36 — **honest nuance:** retention and welfare *co-move* (Kendall τ=+0.80) rather than trade off; pure engagement is *dominated* on every axis, not merely different, and P23 reproduces the same dominance (not trade-off) under learned ranking (row 8) — recorded as met-in-the-dominance-direction, not argued into a trade-off that isn't there — [`phase21/ECOSYSTEM.md`](../results/published/phase21/ECOSYSTEM.md) §7, [`phase23/comparison.md`](../results/published/phase23/comparison.md). |
| 8 | The recommender learns only from observable data | **DEMONSTRATED** | `LeakAuditTest.InteractionEventSchemaMatchesAllowlistExactly` / `.UserSchemaMatchesAllowlistExactly` / `.NoEmittedKeyContainsForbiddenHiddenStateSubstrings` (`tests/unit/leak_audit_test.cpp`); `TrainingLogPurityTest.EmittedFilesAreHiddenFreeAndSchemaConformant` / `.PoolShownPositionsAndProvenancePersisted` (`tests/integration/training_log_purity_test.cpp` — audits **emitted files**, not just structs); the D18 include-graph guard, ctest `hidden_isolation_guard` (`scripts/check_hidden_isolation.py`, registered in `tests/CMakeLists.txt`); `LearnedRankerPipelineTest.ServingPurityParity` (`tests/integration/learned_ranker_pipeline_test.cpp`, comment self-cites "V2 §10 item 8" — the ranker's serving-time `FeatureVector` row equals the logged `candidates.csv` row end to end). Published: P22's offline learners beat frequency baselines using only these observable features (completed AUC 0.714 vs 0.500); P23's closed-loop `LearnedRanker` serves on the identical feature set — [`phase22/offline_eval.md`](../results/published/phase22/offline_eval.md), [`phase23/comparison.md`](../results/published/phase23/comparison.md). |
| 9 | Experiment results remain deterministic and reproducible | **DEMONSTRATED** | D17 golden baseline ([`../tests/golden/v1-baseline/`](../tests/golden/v1-baseline/), `scripts/check_golden.py`) — gates-off byte-identity re-verified at every phase HEAD, medium-config drift arm mandatory at Phases 14/16/18/24; D20 event-log digest golden ([`../tests/golden/event-digest/`](../tests/golden/event-digest/), `scripts/check_event_digest.py`); `SimulatorDeterminismTest.IdenticalSeedProducesIdenticalStream`; `EventDeterminismTest.SameSeedIdenticalDigest` / `.OrderInvarianceDischarge` / `.EqualTimestampSemantics`; `EventQueueTest.TieBreakerGoldenValues` / `.InsertionOrderNeverMatters`; `TrainingLogDeterminismTest.SameSeedByteIdenticalLogTree`; `LearnedRankerPipelineTest.RetrainingDeterminism`. 961/961 tests green, CI green at HEAD `8458213` — `docs/design/PHASE-HISTORY-V2.md` Phase 23 entry. **Clean-clone reproducibility audit (Phase 24, V1-Phase-12 protocol): PASS** — clean clone at `8458213`, built from the README alone (Debug 114 s, Release 93 s, AppleClang 21), full suite **961/961**; the published Phase 20 `engagement-on` arm re-ran bit-equal (event-log digest `10007518107807239012`, all **936,637 events** reproduced; welfare/session CSVs byte-identical; the 8 longterm columns shared with the published pre-P21-schema file byte-identical, the 9th `mean_preference_entropy` column being P21's documented additive evolution); config regeneration sha256-identical; both goldens PASS in the clone; figure regeneration **pixel-identical** (max delta 0 over all 9 rasters — literal byte-identity blocked only by the matplotlib `3.9.2`→`3.9.4` version string in the PNG `Software` chunk, the pinned uv toolchain being absent in the audit environment). Audit-found documentation drift (stale V1 test count, golden-runtime overstatement, uv-free figure fallback) fixed in this phase's commit. |
| 10 | The existing vector database remains unchanged unless a measured ANN limitation justifies modifying it | **DEMONSTRATED** | Design decision D23 ([`design/plan/00-DESIGN-DECISIONS-V2.md`](design/plan/00-DESIGN-DECISIONS-V2.md) — "No vector-db modifications in Phases 13–24, period ... `/Users/derranw/vector-db` remains read-only for all V2 sessions"). Every V2 phase's published `metadata.json` (Phases 15–23) records the **identical** `vector_db_sha` `17e434a3e741f702ffb8e0f00b0484676d988198` — byte-for-byte V1's Phase 11 pin — [`phase15/semantic/metadata.json`](../results/published/phase15/semantic/metadata.json), [`phase23/learned/metadata.json`](../results/published/phase23/learned/metadata.json). No Phase 13–23 entry in `docs/design/PHASE-HISTORY-V2.md` records a vector-db-touching task; the new Phase 13 modality embeddings (visual/music/emotional) ride as ranker/behaviour features only and are never ANN-indexed — only `semanticEmbedding` is, unchanged from V1. |

<!-- Self-check: every test name above was grepped directly against tests/**/*.cpp; every path was
     ls'd on disk; the two digest/sha numbers were read verbatim from committed files
     (tests/golden/event-digest/digest.txt; results/published/phase{15,16,17,19,20,21,22,23}/**/metadata.json). -->

---

## Engagement vs. hidden satisfaction (Phase 15) and session-health framing (Phase 16)

The Tier-1 core experiment (V2 TDD §4.4): semantic-similarity, engagement-optimized, and
satisfaction-proxy rankers, plus an evaluation-only oracle upper bound, on identical worlds
(`configs/realism-medium.json` + presets, 2,000,000 impressions/arm, seed 42).

| arm | reward/impression | watch s/impression | mean hidden satisfaction | hidden regret | ragebait share |
|---|---|---|---|---|---|
| semantic | 0.504385 | 69.06 | 0.355321 | 0.132837 | 10.28% |
| engagement | 0.542119 | 64.10 | 0.328336 | 0.166913 | 20.05% |
| proxy | 0.534769 | 56.53 | **0.653003** | **0.017371** | 0.00% |
| oracle (eval-only) | 0.586053 | 78.60 | 0.806101 | 0.003989 | 0.03% |

The engagement preset wins every observable engagement metric (+7.5% reward, completion 0.867 vs
0.782, likes 0.297 vs 0.253, comments 0.259 vs 0.194) while *losing* hidden satisfaction and serving
double the ragebait exposure; the proxy preset gives up a little reward (0.535 vs 0.542, −1.4%) for
**nearly double the satisfaction** and ~9.6× lower regret. The oracle (scores directly from expected
hidden satisfaction, evaluation-only) upper-bounds all three, at 0.806 — confirming satisfying
content *also* engages when it's actually served. See
[`phase15/comparison.md`](../results/published/phase15/comparison.md),
[`figures-v2/p15_engagement_vs_welfare.png`](../results/published/figures-v2/p15_engagement_vs_welfare.png).

**Phase 16 reframes the same three arms once session length is an outcome, not an input** (V2 TDD
§4.9's explicit warning: *"a four-hour session should not automatically be considered better than a
focused twenty-minute session"*). Under `realism.session_dynamics`, engagement's satisfaction
collapses further (mean 0.005 — fatigue **compounds** the Phase-15 wedge) while proxy sessions run
**7.7× longer** (mean session duration 375,041 s vs 48,978 s) *and* are healthier on every
duration-normalized axis:

| arm | mean session duration (s) | early-failure-exit rate | U_s (session utility) | regret/min (session-scoped) |
|---|---|---|---|---|
| semantic | 145,427 | 0.202 | −0.919 | 2.69e-4 |
| engagement | 48,978 | **0.327** | **−1.638** | 9.97e-4 |
| proxy | **375,041** | 0.026 | **+1.575** | 1.63e-5 |
| oracle | 1,107,300 | 0.001 | +2.931 | 8.19e-7 |

Raw duration alone would call engagement's shorter sessions "healthier"; U_s and regret/minute say
the opposite — exactly the duration-normalization V2 TDD §4.9 mandates. (Legacy-runner session
durations use the shared round-robin clock, so absolute seconds are not wall-clock-realistic;
cross-arm comparisons are sound — see [`phase16/comparison.md`](../results/published/phase16/comparison.md).)

---

## Session dynamics and personalized diversity (Phases 16–17)

Phase 17 crosses **fixed vs. personalized** diversity re-ranking with four named trait cohorts
(focused, novelty_seeker, creator_loyal, easily_fatigued) on identical repetition-heavy worlds.
Personalized diversity beats fixed on session utility (U_s) for **all four cohorts**:

| cohort | U_s fixed | U_s personalized | Δ |
|---|---|---|---|
| focused | 0.4453 | 0.5137 | **+15.4%** |
| novelty_seeker | 0.6457 | 0.7401 | **+14.6%** |
| creator_loyal | 0.2463 | 0.2509 | **+1.9%** |
| easily_fatigued | −0.5402 | −0.4549 | **+15.8%** |

The observables-only `ToleranceEstimator` (EMA over completion runs, not-interested-after-repeat,
exit-after-repetition, comment/save cadence) correlates r=0.525 with the *hidden* repetition-tolerance
trait it never reads directly. Delivered feed length stays a documented Phase-9 hard-cap operating
point (~3.5–4.0/10 across every diversity arm at medium scale — personalization *relaxes* the
short-feed hazard for tolerant users, up to 10/10, and tightens it for intolerant users, ~4/10, but
does not eliminate it). See [`phase17/comparison.md`](../results/published/phase17/comparison.md),
[`figures-v2/p17_cohort_panel.png`](../results/published/figures-v2/p17_cohort_panel.png).

---

## Event-driven core and the batch-depth frontier (Phases 18–19)

Phase 18 replaces round-robin processing with a deterministic priority-queue runner (independent
per-user open/consume/exit/return timelines; `simulation.scheduler="event_queue"`, gated, default
off — the legacy round-robin runner remains the permanent D17 golden path). Phase 19 turns feed
*prefetch depth* into a measurable design axis (V2 TDD §4.13) under abrupt preference drift
(`configs/realism-medium-events-drift.json`, ~2.0M impressions/arm):

| prefetch depth | feed requests | ranking computations | adaptation delay (interactions) | stale-impression rate | satisfaction lost before refresh |
|---|---|---|---|---|---|
| 1 | 2,029,230 | 1,011,607,164 | 4.062 | 0 | 0 |
| 3 | 822,755 | 409,290,163 | 4.227 | 0.594 | 271,006 |
| 10 | 441,128 | 219,896,171 | 4.249 | 0.781 | 505,298 |
| 20 | 417,041 | 207,935,476 | 4.082 | 0.794 | 538,369 |

Depth 20 vs depth 1: **79.4% fewer ranking computations and feed requests, at flat adaptation delay**
(4.06 → 4.08 interactions, +0.5%) — satisfying the V2 §7-mandated "batch-size-one adapts no slower"
property exactly (`BatchAdaptationStatisticalTest.Batch1AdaptsNoSlowerThanBatch20`). The freshness
cost is real but lives on the staleness axes, not adaptation delay: stale-impression rate climbs
0 → 0.794 and satisfaction lost before refresh climbs to 538k events at depth 20. See
[`phase19/comparison.md`](../results/published/phase19/comparison.md),
[`figures-v2/p19_freshness_cost_frontier.png`](../results/published/figures-v2/p19_freshness_cost_frontier.png).

---

## Preference evolution and retention (Phase 20)

Exposure-driven preference reinforcement/saturation (V2 TDD §4.15–4.16) and a hazard-based
return/churn model (§4.17), isolated from Phase 10's exogenous scheduled drift (no `drift` block at
all in this config — endogenous evolution only). `engagement` and `proxy` ranking presets run with
evolution+retention on (`-on`) and off (`-off`, the matched counterfactual), event mode, 10k
users × 100k reels, 9 simulated days, seed 42:

| arm | retention_1d | retention_7d | sessions/user/day | churn rate | mean hidden satisfaction |
|---|---|---|---|---|---|
| engagement-on | 0.6269 | 0.9692 | 0.860 | **26.45%** | −0.482 |
| engagement-off | 0.8486 | 0.9943 | 2.917 | 3.47% | 0.036 |
| proxy-on | 0.8242 | 0.9960 | 1.706 | **8.99%** | −0.122 |
| proxy-off | 0.9586 | 0.9998 | 5.962 | 0.24% | 0.213 |

**Headline (Tier-4 acceptance item 1):** over the **same 10,000 matched users**, same world, same
seed, differing only in which ranking policy served them, mean hidden-preference divergence is
**0.0388** (median 0.0177, p90 0.106) — the recommender measurably *shapes* preferences, it does not
merely discover them. Per-policy distortion vs. each policy's own gate-off twin (a built-in
consistency check: with evolution off, cross-run distortion must equal the `-on` arm's own
shift-from-initial, and it does, exactly) is 0.0281 for engagement and 0.0546 for proxy — engagement
optimization shows a measurably worse retention/churn outcome (**26.45% vs 8.99% churn**) alongside
lower satisfaction, i.e. a **negative long-term consequence of pure engagement optimization**,
foreshadowing Phase 21's Tier-4 acceptance item 2. **Known limitation carried through P20/21/23:**
trust erodes to near-floor under both policies at these erosion constants (final trust 0.018
engagement vs 0.024 proxy, vs 0.704 on the static gate-off twins) — trust separates only weakly at
this operating point; retention/churn/sessions carry the policy separation instead (see
[`LIMITATIONS.md`](LIMITATIONS.md)). See
[`phase20/comparison.md`](../results/published/phase20/comparison.md),
[`figures-v2/p20_preference_divergence_hist.png`](../results/published/figures-v2/p20_preference_divergence_hist.png),
[`figures-v2/p20_retention_by_day.png`](../results/published/figures-v2/p20_retention_by_day.png).

---

## Ecosystem failure-mode verdicts (Phase 21)

Seven pre-registered scenarios (hypothesis → mechanism → expected signature → verdict criteria,
committed **before** any run), each control-vs-treatment on identical worlds. Full detail, method
notes, and per-scenario figures: [`phase21/ECOSYSTEM.md`](../results/published/phase21/ECOSYSTEM.md).

| Scenario | Verdict | Headline |
|---|---|---|
| Filter-bubble formation | **PARTIAL** | Niche starvation confirmed (−46% share); entropy collapse NOT observed (saturation counteracts narrowing); creator-tail concentration **INVERTED** (the similarity bubble is *less* concentrated than the popularity-chasing default) |
| Ragebait amplification | **SUPPORTED** (test-enforced) | Engagement serves 69.0% ragebait vs proxy 0.0%; satisfaction −0.559 vs −0.156; the ecosystem starves itself to ~1/5 daily impressions by late run |
| Popularity feedback loop | **PARTIAL** | Tail-creator starvation confirmed (2.7× lower share, every day); creator HHI moves the **wrong direction** (a broad popular head, not mega-creators, at this catalog size) |
| Niche-content starvation | **PARTIAL** | Whole-run decline confirmed but **reverses by day 8**; welfare direction contradicts the starvation hypothesis |
| Creator overconcentration | **CONFIRMED** | Uncapped HHI 0.0205 vs capped 0.0180 every day; U_s worse uncapped — diversity caps buy diversity *and* welfare together |
| Exploration recovery | **CONDITIONAL** (test-enforced at concentrated scale) | Not observed at medium scale (bubble already diffuse across 5,000 creators — an ε=0.5 probe rules out under-exploration); demonstrated at a concentrated 600u/3k-reel scale (tail share 0.753 → 0.779 at ε=0.30 / 0.782 at ε=0.60; creator HHI 0.0209 → 0.0193 / 0.0192) |
| Satisfaction-vs-retention conflict | **NO TRADE-OFF** (honest non-confirmation) | Retention and welfare **co-move** (Kendall τ=+0.80); pure engagement is *dominated* on every axis; §10 item 7 demonstrated regardless (policy moves retention_7d 2.7 pp, sessions 2×, satisfaction 0.36) |

**Method notes that matter for interpretation:** `mean_preference_entropy` rises in *every* arm at
this operating point (Phase-20 saturation/aversion push users off exhausted channels faster than any
policy narrows them), so entropy is not a usable bubble signal here — exposure-based readings
(niche/tail shares, HHI) carry the concentration verdicts instead. Trust separates only weakly across
every scenario (the Phase-20 trust-floor operating point). A genuine satisfaction-vs-retention
*conflict* would require satisfaction-decoupled return mechanics (e.g. compulsive habit) that the
simulator deliberately does not model — recorded as future work, not argued around (see
[`LIMITATIONS.md`](LIMITATIONS.md)).

---

## Learned ranking: offline (Phase 22) and closed-loop (Phase 23)

**Phase 22** builds a leak-proof eligibility→impression training log (positions, exploration
provenance, per-candidate features *as served*) and in-house deterministic SGD logistic/linear
learners for the V2 §4.19 targets, evaluated offline on a real 216 MB pinned log world
(~90k train / ~21k test impressions, both a temporal and a user-disjoint split):

| target | metric | learned | best baseline | verdict |
|---|---|---|---|---|
| completed | AUC | **0.714** (temporal) / 0.711 (user-disjoint) | 0.628 (per-source freq.) | beats all 3 baselines |
| not_interested | AUC | **0.714** / 0.740 | 0.651 / 0.677 | beats all 3 |
| session_exit | AUC | **0.664** / 0.688 | 0.630 / 0.643 | beats all 3 |
| liked | AUC | **0.604** / 0.601 | 0.588 / 0.585 | beats all 3 |
| shared | AUC | **0.563** / 0.582 | 0.556 / 0.572 | beats all 3 |
| satisfaction (survey) | RMSE | **1.176** / 1.095 | 1.167 / 1.129 | beats 1–2 of 2 baselines |
| followed | AUC | 0.501 / 0.512 | 0.523 / 0.523 | **honest no-signal** (base rate 1.5%) |
| watch_ratio | RMSE | 0.868 / 0.875 | 0.857 / 0.860 | **honest no-signal** (learned does not beat any baseline) |

Two targets are reported with **no learnable signal at this operating point** — `followed` (a
~1.5%-base-rate rare event) and `watch_ratio` (the frequency baselines already capture nearly all the
signal) — stated plainly per the V2 TDD's own honesty requirement, not smoothed into the headline.
`served_score` (the hand-tuned ranking score, used as a naive predictor) is *anti-predictive* on
several welfare-adjacent targets (0.39–0.43 AUC) — the ranking score optimizes ranking, not outcome
prediction. Full table (both splits, all 8 targets, calibration):
[`phase22/offline_eval.md`](../results/published/phase22/offline_eval.md),
[`figures-v2/p22_offline_auc.png`](../results/published/figures-v2/p22_offline_auc.png).

**Phase 23** serves a `LearnedRanker` implementing the V2 §4.21 multi-objective value function over
the Phase 22 models, retrained every 24 simulated hours in-loop, compared closed-loop against the
hand-tuned `WeightedRanker` and a semantic-similarity control on **identical worlds**:

| arm | reward/impression | mean hidden satisfaction | U_s | mean final trust |
|---|---|---|---|---|
| semantic | 0.249 | −0.399 | −3.325 | 0.013 |
| hand_tuned | 0.399 | −0.298 | −2.833 | 0.012 |
| **learned** (balanced weights) | **0.404** | **−0.202** | **−2.100** | **0.021** |
| learned, survey off | 0.401 | −0.217 | −2.236 | 0.017 |

**The learned multi-objective ranker dominates the hand-tuned system** on identical worlds — better
reward *and* better hidden satisfaction *and* better session utility *and* better trust
simultaneously — and beats the semantic control everywhere. The weight-vector sweep (satisfaction ↔
watch, 5 points) exposes a **monotone, not a trade-off, frontier**: `w_sat_100` (satisfaction-weight
0.6, watch-weight 0) is best on *every* axis including engagement itself (reward 0.421 vs the
pure-engagement vector's 0.374; satisfaction −0.149 vs −0.284; U_s −1.813 vs −2.620; trust 0.025 vs
0.014) — satisfaction-weighted serving is engagement-*positive* here because retention and session
volume are themselves satisfaction-driven (the Phase-21 mechanism finding, reproduced under learned
ranking). Survey feedback (the sanctioned noisy explicit-satisfaction signal) helps modestly
(satisfaction −0.202 with vs −0.217 without). Full 9-arm table, retraining log, and the served
explanation sample:
[`phase23/comparison.md`](../results/published/phase23/comparison.md),
[`figures-v2/p23_multiobjective_frontier.png`](../results/published/figures-v2/p23_multiobjective_frontier.png).

**Offline-vs-closed-loop gap analysis** (Tier-5 acceptance item 5) pairs each of the 8 offline
targets against its most-related closed-loop metric, per learned arm — 55 (arm, target) pairs total.
**33/55 (60%) are ALIGNED** (offline-predictive and the closed-loop metric improved); **3/55 show
outright DIVERGENCE** (offline-predictive but the closed-loop metric did *not* improve — all three on
the two watch-weighted arms, `w_watch_70` and `w_watch_100_noexit`: `completed`/`liked` offline
signal did not carry into reward/impression, a feedback-loop effect on the arm's own training
distribution); the remainder are loop-gain (5), partial (13), or consistent-null (1). This is the
honest acceptance-5 finding: offline predictiveness mostly transfers, but not always, and the
exceptions are named, not hidden. See
[`phase23/gap_analysis.md`](../results/published/phase23/gap_analysis.md) §3–4,
[`figures-v2/p23_offline_closedloop_gap.png`](../results/published/figures-v2/p23_offline_closedloop_gap.png).

<!-- The 33/7/5/3/6/1 flag breakdown above is this author's own count over
     results/published/phase23/gap_analysis.md's §3 table (55 rows); the source does not print the
     aggregate itself. Re-derivable by counting the "flag" column. -->

---

## Traceability

Every number above is copied from a committed artifact, not from memory: `comparison.md`/`.csv`,
`ECOSYSTEM.md`, `offline_eval.md`, `gap_analysis.md`, and `metadata.json` under
`results/published/phase{15,16,17,19,20,21,22,23}/`, cross-checked against
`docs/design/PHASE-HISTORY-V2.md`'s per-phase entries. Test names were verified against the actual
source files under `tests/` (not against memory of what a phase "should" have tested). Phase 18 has
no standalone published experiment directory (its deliverable is the event-driven core itself,
evidenced by the committed digest golden); Phase 21's per-scenario `comparison.md` files and figures
live under `results/published/phase21/<scenario>/`. Hardware/build/SHA provenance is in every run's
`metadata.json`, matching the TDD §27/D12 convention V1 established.
