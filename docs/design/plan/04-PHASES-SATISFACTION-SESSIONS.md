# ReelRank Plan — Part 4: Satisfaction and Session Realism (Phases 13–17)

Realism V2, Releases A–B (V2 TDD Tiers 1–2). One phase = one Fable session: Fable plans the
parallel split, executes with opus subagents (sonnet for light/mechanical packages), integrates,
verifies, commits. Before starting a phase, read `plan/00-DESIGN-DECISIONS.md` (D1–D16, binding),
`plan/00-DESIGN-DECISIONS-V2.md` (D17–D25, binding), `commit.md`, and the **V2 TDD**
(`REELS-SIMULATION-V2.md`) sections referenced below. V1 TDD sections are cited explicitly where
still relevant. Do not start a phase whose prerequisites are unticked in `commit.md`.

Standing exit criterion for every V2 phase (D17, not repeated below): gates-off re-run at phase
HEAD reproduces the committed V1 golden baseline byte-identically (deterministic CSVs; non-timing
summary fields bit-equal; Release build), and the D18 include-graph guard passes.

The "Suggested package split" sections are starting points for Step 3 of `/next-phase`, not
mandates — worktree isolation is required whenever two packages both touch C++ (one test binary;
see the orchestration memory), and packages owning only disjoint non-C++ files may share the main
checkout.

---

## Phase 13 — Multi-factor content and hidden user channels

**V2 TDD refs:** §3.1, §4.1, §4.2, §4.4, §5 (Reel + HiddenUserState), §7. V1 TDD §9 for the
generators being extended.
**Prerequisites:** Phases 0–12 (V1 complete).

### Objective
Reels and users carry the full V2 factor model — modality embeddings, content-value scalars,
archetypes, preference channels, susceptibility traits — generated deterministically behind
`realism.content_v2`, with the hidden/visible split structurally enforced and V1 output untouched.

### Tasks
1. **Golden baseline capture (first, at current HEAD):** Release-build reference runs per D17
   (`configs/small.json` `hnsw_ranker_diversity`; Phase 10 `session_aware` drift arm), committed
   under `tests/golden/v1-baseline/` with regeneration instructions. This is the byte-identity
   anchor for all of V2.
2. `Reel` gains serving-time-visible V2 attributes (V2 §4.1): `visualStyleEmbedding`,
   `musicEmbedding`, `emotionalToneEmbedding` (dim from config, L2-normalized per D3/D5),
   scalars `usefulness`, `humour`, `novelty`, `productionQuality` (alias/reconcile with existing
   `quality` — document), `controversy`, `clickbaitStrength`, `informationDensity`,
   `emotionalIntensity` (all [0,1]), `LanguageId language` (small config-driven language set with
   a skewed global distribution). Semantic embedding stays the only ANN-indexed vector (D23).
3. `HiddenReelState` (new, `include/rr/simulation/hidden/`): archetype id + archetype parameters
   (satisfaction bias, regret bias, opening-hook/decay retention shape for clickbait, niche
   cohort centre/width for niche-treasure, comfort return-bonus) — simulator-only (D18).
4. Archetype system (V2 §4.4, D24): config-driven catalog of the eight archetypes as
   distribution parameters + mixture weights; `ReelGeneratorV2` path samples archetype from
   stream `"archetypes"`, then attributes conditionally from `"reels-v2"` — archetypes shape
   feature distributions probabilistically; no label reaches recommender-visible state.
5. `HiddenUserState` gains the V2 channels (§4.2) and the §5 forward-traits: modality preference
   embeddings (visual/music/emotional; topic = existing hidden preference), scalar preferences
   (`usefulnessPreference`, `humourPreference`, `controversyTolerance`, `noveltySeeking`,
   `clickbaitSusceptibility`, `informationTolerance`), language affinity, plus — generated now,
   consumed by later phases, each with documented `[lo, hi]` ranges and its consuming phase
   named: repetition/novelty/creator-loyalty tolerance traits (P16–17), `habitStrength`,
   `platformTrust`, `baselineDailyUsage` (P20), `preferencePlasticity` (P20). Drawn from
   `"users-v2"` in `UserGeneratorV2` path.
6. Stream discipline (D17/D19): gate off ⇒ zero V2 draws, structures default-initialized; gate
   on ⇒ V1 fields byte-identical (new draws only from new streams). Both property-tested across
   ≥20 seeds, plus regenerating-V2-fields-never-changes-V1-fields (Phase 2 precedent test).
7. **D18 include-graph guard** (script + ctest registration + CI): recommender/candidate-source/
   learning modules cannot reach `simulation/hidden/` headers. Add the `InteractionEvent`/`User`
   field-name leak-audit scaffolding (allowlist asserted in a test) for later phases to extend.
8. `inspect_user` extension: V2 attribute/preference distribution dump; per-archetype attribute
   summary (hidden data — output labeled as simulator-side inspection, evaluation carve-out).
9. Tests: unit (ranges, normalization, language distribution, catalog parsing incl. unknown-key
   rejection, mixture-weight validation); property (determinism ≥20 seeds; stream independence);
   statistical (archetype mixture proportions match config; archetype signature distributions
   differ where designed — e.g. ragebait controversy ≫ population mean); golden regression.

### Exit criteria
- [ ] Full V2 dataset (100k reels / 10k users) generates deterministically with gate on; all
      embeddings valid; archetype mixtures statistically match config (tests).
- [ ] Gate-off byte-identity to the newly committed golden baseline; gate-on leaves all V1
      fields/streams identical (tests).
- [ ] D18 guard live in ctest+CI and failing-by-construction demo documented (temporarily add an
      illegal include locally to prove it fires; not committed).
- [ ] Distributions inspectable via `inspect_user`.

### Out of scope
Any behaviour-model consumption of the new fields (Phase 14); ranker features (Phase 15); no new
recommender-visible estimates yet.

### Suggested package split
A (opus): reel side — attributes, archetype catalog + generator path, `HiddenReelState`.
B (opus): user side — channels, traits, generator path, stream-discipline property tests.
C (sonnet): golden-baseline capture/wiring, include-graph guard script, `inspect_user` extension.
Fable: config surface (`realism` block), frozen headers, integration, golden verification.

### Session kickoff prompt
> Read plan/00-DESIGN-DECISIONS.md, plan/00-DESIGN-DECISIONS-V2.md, plan/04-PHASES-SATISFACTION-
> SESSIONS.md (Phase 13), commit.md, V2 TDD §§3.1, 4.1–4.4, 5, 7, and V1 TDD §9. First capture
> and commit the D17 golden baseline at current HEAD. Then implement the V2 content/user factor
> model and archetype catalog test-first behind `realism.content_v2`, on new rng streams only,
> with the D18 include-graph guard. Prove gate-off byte-identity and gate-on V1-field stability.
> Update commit.md per protocol.

---

## Phase 14 — Latent reactions and observable behaviour V2

**V2 TDD refs:** §3.1–3.3, §4.3, §4.4 (behaviour signatures), §5 (InteractionEvent), §7. V1 TDD
§10 for the model being superseded.
**Prerequisites:** Phase 13.

### Objective
Every impression produces a hidden `LatentReaction` (satisfaction, regret, informational/emotional
value, desire-for-similar, fatigue delta) from which observable events are sampled conditionally —
breaking engagement ≡ enjoyment while keeping the recommender blind to the difference.

### Tasks
1. `LatentReaction` struct (V2 §4.3) in `simulation/hidden/`; per-impression computation in a new
   `BehaviourModelV2` (config-selected when `realism.latent_reactions`; V1 `BehaviourModel`
   untouched, D17): multi-channel base utility = weighted combination of topic/visual/music/
   emotional channel matches (dot products), usefulness×usefulnessPreference,
   humour×humourPreference, novelty×noveltySeeking, controversy vs `controversyTolerance`
   (penalty beyond tolerance, small boost within for high-tolerance users), language mismatch
   penalty, creator attachment (hidden style affinity, V1 §10.2's C term), information density vs
   `informationTolerance`. Channel weights config-driven; document the formula at definition.
2. Archetype-conditioned latent + observable signatures (V2 §4.4 + hidden params from P13):
   ragebait → elevated watch/comment probability with negative `immediateSatisfaction` and
   positive `regret` scaled by `clickbaitSusceptibility`/`controversyTolerance`; clickbait →
   opening-hook retention then early abandonment + regret; useful → high
   `informationalValue`/satisfaction, damped like probability; music reel → completion/rewatch
   driven by music-channel match even at weak topic match; comfort → moderate engagement,
   positive `desireForSimilarContent`; niche treasure → high satisfaction inside the hidden
   cohort only; polished-irrelevant → strong initial retention, low lasting value (low
   desire-for-similar); short-duration completion inflation (completed-because-short, V2 §3.2).
3. Observable sampling conditional on the latent (V2 §4.3): watch/completion/rewatch, like/share/
   follow (satisfaction-correlated but noisy; social-conformity like term from visible popularity
   counters), **new events** comment / save / profile-visit, not-interested. Draw order
   documented and pinned; streams: `"behaviour"` (owned wholesale by V2 under the gate, D19) +
   `"satisfaction"` for latent noise.
4. `InteractionEvent` V2 fields (V2 §5): position in feed, feed/request id, request timestamp,
   start/finish timestamps, dwell, replay count, comment/save/profile-visit flags,
   `observedExitAfterImpression` placeholder (wired for real in P16). Exploration flag +
   candidate-source provenance already exist — verify and document. Extend the D18 leak-audit
   allowlist test: no latent field on the event, ever.
5. Welfare accumulation plumbing (evaluation carve-out): per-impression `LatentReaction` streamed
   to the metrics side (satisfaction/regret accumulators per user/round) without touching any
   recommender-visible structure. (Full §6 metric group formalization is Phase 15 — here, enough
   for this phase's statistical tests.)
6. `RewardModel`/updater unchanged: V1 reward is already observable-only — it becomes explicitly
   an *engagement proxy* (document in `reward_model.hpp`); hidden satisfaction is the new,
   separate ground truth. `configs/realism-small.json` introduced (small.json + V2 gates on).
7. Tests (V2 §7 mandated set): statistical — ragebait cohort yields above-population watch+
   comment with negative mean satisfaction; useful cohort yields top-quartile satisfaction with
   below-population like rate; clickbait shows opening retention + early-abandonment signature +
   regret; watch↔satisfaction correlation positive but inside a documented imperfect band (e.g.
   Spearman ∈ [0.2, 0.8] at defaults); music-reel rewatch at weak topic match. Unit (each channel
   term, signature math, draw order); determinism (bit-identical event+latent streams per seed);
   leak audit; golden regression (this phase touches the behaviour core ⇒ D17 medium
   re-verification mandatory).

### Exit criteria
- [ ] Tier 1 acceptance (V2 §4.4) minus the ranker experiment: imperfect-but-positive
      watch↔satisfaction correlation; ragebait high-engagement/negative-satisfaction and
      useful high-satisfaction/weak-engagement both demonstrated by committed tests.
- [ ] Latent values structurally inaccessible to recommendation code (D18 guard + leak audit).
- [ ] V2 event schema populated end-to-end on `realism-small.json`; D17 small + medium golden
      byte-identity with gates off.

### Out of scope
Metric-group formalization and the 4-arm experiment (Phase 15); session exit behaviour (16);
fatigue dynamics beyond emitting `fatigueDelta` (16).

### Suggested package split
A (opus): `BehaviourModelV2` core — channel utility, latent computation, archetype signatures.
B (opus): observable sampling + event schema V2 + welfare plumbing + leak audit.
C (sonnet): statistical signature test suite + `realism-small.json` + golden re-verification runs.
Fable: config gating, frozen `latent_reaction.hpp`/model interface, integration, medium golden.

### Session kickoff prompt
> Read plan/00-DESIGN-DECISIONS.md, plan/00-DESIGN-DECISIONS-V2.md, plan/04-PHASES-SATISFACTION-
> SESSIONS.md (Phase 14), commit.md, V2 TDD §§3, 4.3–4.4, 5, 7, and V1 TDD §10. Implement
> LatentReaction + BehaviourModelV2 behind `realism.latent_reactions` test-first (V1 model
> untouched), sample observables conditionally incl. comment/save/profile-visit, extend
> InteractionEvent per V2 §5 with the leak audit, and land the §7 statistical signature tests.
> Prove small+medium gate-off byte-identity. Update commit.md per protocol.

---

## Phase 15 — Welfare metrics and the engagement-vs-satisfaction experiment

**V2 TDD refs:** §4.4 (core experiment), §6, §10 items 1–2. V1 TDD §14 (ranker being extended),
§18 (metrics being extended).
**Prerequisites:** Phase 14.

### Objective
The four V2 §6 metric groups become first-class experiment output, the ranker/updater gain V2
attribute awareness, and the Tier 1 core experiment — semantic vs engagement-optimized vs
satisfaction-proxy vs oracle — is published with engagement and welfare diverging measurably.

### Tasks
1. Metrics framework V2 (§6, D22): `WelfareMetrics` (immediate satisfaction, regret,
   satisfaction/min, per-archetype-exposure breakdown; harmful-fatigue and trust columns emitted
   as not-yet-modeled placeholders documented until P16/P20) via the evaluation carve-out;
   engagement group extended with comment/save/profile-visit; recommendation-quality group keeps
   V1 affinity/alignment/diversity. New `welfare_metrics.csv` + `summary.json` blocks; existing
   V1 outputs unchanged.
2. `OnlineUserStateUpdater` V2 extension (gated): estimated modality preferences on `User`
   (V2 §5) — per-modality EMA updates mirroring the V1 §11.2 rule, driven by the same observable
   reward, applied to the reel's modality embeddings; blend into candidate query stays
   semantic-only (D23) but modality estimates feed ranking features. Tests mirror Phase 7's
   (convergence toward hidden modality preference under positive feedback; hidden untouched).
3. `FeatureExtractor` V2 features (visible-only): modality-match features (estimated modality
   prefs × reel modality embeddings), `clickbaitStrength`, `emotionalIntensity`, `usefulness`,
   `productionQuality`, `informationDensity`, language match, comment/save-derived popularity
   refinements. Documented [0,1] normalizations per V1 §14.3 conventions.
4. Experiment arms (V2 §4.4 core): (1) semantic-similarity = existing `hnsw`; (2)
   engagement-optimized = `WeightedRanker` weight preset leaning on watch-correlated features
   (clickbait, emotional intensity, popularity, modality match) — preset shipped as
   `configs/presets/engagement.json` fragment or documented weight block; (3) satisfaction-proxy
   = hand-designed observable proxy (e.g. completion+rewatch+save+follow weighted, clickbait and
   early-abandon-associated features negatively weighted — design documented against §3.2's
   misleading-engagement list); (4) `OracleSatisfactionRanker` in `evaluation/` (D18: explicitly
   oracle, evaluation-only arm, reads hidden satisfaction expectation; barred from non-experiment
   use).
5. Run the 4-arm experiment on `configs/realism-medium.json` (introduce: medium + gates on):
   report all four groups (session-health group limited to what exists pre-P16 — note it);
   per-archetype exposure shares per arm (does the engagement arm over-serve ragebait/clickbait?).
   Publish `results/published/phase15/` with comparison.md + plots (`scripts/plot_results.py`
   extension: engagement-vs-welfare scatter per arm).
6. Tests: statistical — engagement arm beats random and semantic on watch time while scoring
   below satisfaction-proxy on mean hidden satisfaction (Tier 1 acceptance); oracle bounds all
   arms on satisfaction; unit (each new feature normalization; proxy formula; preset plumbing);
   determinism; golden regression.

### Exit criteria
- [ ] Tier 1 acceptance criteria fully met, incl. the watch-time-ranker clause (test + published).
- [ ] V2 §10 items 1–2 demonstrated with committed numbers (high engagement + low satisfaction
      coexist; high satisfaction + modest engagement).
- [ ] Four-group §6 reporting live for every V2 run; no aggregate score anywhere (D22).
- [ ] `results/published/phase15/` complete per D12.

### Out of scope
Session exits/fatigue (16); learned prediction models (22–23) — arms here are hand-weighted
presets by design.

### Suggested package split
A (opus): metrics framework + welfare CSV/summary wiring.
B (opus): updater modality estimates + V2 features + arm presets + oracle ranker.
C (sonnet): experiment execution (per-arm `--out` roots; concurrency contention note per the
orchestration memory), comparison.md, plots.
Fable: config surface, arm/preset review, integration, publication review.

### Session kickoff prompt
> Read plan/00-DESIGN-DECISIONS.md, plan/00-DESIGN-DECISIONS-V2.md, plan/04-PHASES-SATISFACTION-
> SESSIONS.md (Phase 15), commit.md, V2 TDD §§4.4 (core experiment), 6, 10, and V1 TDD §§14, 18.
> Build the four-group metrics framework, modality-aware updater/features, the four ranker arms
> (oracle in evaluation/ only), test-first; run and publish the engagement-vs-satisfaction
> experiment on realism-medium. Update commit.md per protocol.

---

## Phase 16 — Session dynamics: fatigue, probabilistic exit, session outcomes

**V2 TDD refs:** §4.6–4.9, §6 (session health), §7. V1 TDD §10 (session rotation being
superseded under the gate).
**Prerequisites:** Phase 15.

### Objective
Session length becomes an outcome of feed quality: hidden per-session state accumulates fatigue
and regret, exit is probabilistic and classified, session utility replaces time-on-app, and the
Phase 15 arms re-run shows poor feeds causing early failure exits.

### Tasks
1. `HiddenSessionState` (V2 §4.6) in `simulation/hidden/`, owned per active session:
   `currentSatisfaction` (EMA), `accumulatedRegret`, `generalFatigue`, `noveltyNeed`, `boredom`,
   `remainingAttention`, `topicFatigue`/`creatorFatigue` maps plus format/music/emotional-
   intensity fatigue (V2 §4.7 list). Reset on session start; long-term preferences persist
   (Tier 2 acceptance).
2. Fatigue dynamics (V2 §4.7): per-impression accumulation (driven by `fatigueDelta` +
   repetition detectors), effective-utility modulation inside `BehaviourModelV2`
   (base − α·topicFatigue − β·creatorFatigue + γ·noveltyMatch; coefficients config-driven,
   modulated by the P13 per-user tolerance traits — heterogeneity is measured in P17); decay
   during away time as a function of logical-clock gap between sessions (V1 loop's inter-session
   gap now; event-driven gaps arrive in P18 unchanged).
3. Probabilistic exit (V2 §4.8): after every impression under `realism.session_dynamics`,
   P(exit) = σ(b0 + b1·fatigue + b2·recentRegret + b3·consecutivePoorReels −
   b4·recentSatisfaction + b5·externalInterruption); coefficients config-driven; draws on
   `"session-exit"`; external interruptions on `"external-interruption"` (independent hazard).
   Replaces `avgSessionLength` rotation under the gate (V1 path untouched). Fires
   `observedExitAfterImpression` on the last event (recommender-visible observable, P14 field).
4. Exit classification (V2 §4.8 taxonomy): simulator-side labeling rules (failure / satisfied /
   fatigue / external / regret) with documented thresholds; hidden labels flow only to
   evaluation.
5. Session outcomes (V2 §4.9): U_s = Σ satisfaction − λ1·Σ regret − λ2·harmfulFatigue −
   λ3·earlyFailureExit (λs config-driven); `session_health.csv` + summary block: time-before-
   exit, satisfaction/min, regret/min, early-failure-exit rate, natural-completion rate,
   next-session starting satisfaction; harmful-fatigue placeholder from P15 now real. The
   four-hour-vs-twenty-minute framing goes in comparison.md.
6. Re-run the Phase 15 four arms with session dynamics on (`realism-medium`): session-health
   group now differentiates arms — engagement arm's regret exits vs proxy arm's satisfied exits;
   publish `results/published/phase16/`.
7. Tests (V2 §7 mandated): fatigue increases per exposure and decays with away time (unit +
   property); each exit-model coefficient's monotone effect; deterministic exit sampling;
   classification rules on constructed sessions; statistical — bottom-quality feeds (forced-bad
   recommender or adversarial preset) produce significantly higher early-failure-exit rates than
   the proxy arm; re-entering users keep long-term estimates but fresh session state; golden
   regression (behaviour core touched ⇒ medium re-verification mandatory).

### Exit criteria
- [ ] Tier 2 acceptance items: poor feeds measurably increase early failure exits; away time
      reduces fatigue; re-entry preserves long-term prefs with fresh session state (tests).
- [ ] Good-long vs addictive-high-regret sessions distinguishable in published metrics (U_s and
      regret/min separate the engagement arm from the proxy arm at comparable durations).
- [ ] V2 §10 item 3 demonstrated; `results/published/phase16/` complete.

### Out of scope
Per-user tolerance heterogeneity experiments and personalized diversity (17); independent
timelines/returns (18); retention coupling (20).

### Suggested package split
A (opus): `HiddenSessionState` + fatigue dynamics + behaviour-model integration.
B (opus): exit model + classification + session-health metrics/U_s.
C (sonnet): four-arm re-run, comparison.md, plots, golden re-verification.
Fable: config surface, frozen hidden-session header, integration.

### Session kickoff prompt
> Read plan/00-DESIGN-DECISIONS.md, plan/00-DESIGN-DECISIONS-V2.md, plan/04-PHASES-SATISFACTION-
> SESSIONS.md (Phase 16), commit.md, V2 TDD §§4.6–4.9, 6, 7. Implement hidden session state,
> fatigue accumulation/decay, the probabilistic classified exit model, and session utility
> metrics behind `realism.session_dynamics`, test-first; re-run the four arms and publish the
> session-health comparison. Prove small+medium gate-off byte-identity. Update commit.md per
> protocol.

---

## Phase 17 — Personalized diversity and fatigue heterogeneity

**V2 TDD refs:** §4.10, Tier 2 acceptance + core experiment, §5 (User estimates). V1 TDD §15
(rerankers being extended).
**Prerequisites:** Phase 16.

### Objective
Users differ in repetition tolerance and the system knows it two ways: the simulator's hidden
traits drive genuinely different fatigue responses, and the recommender estimates tolerances from
observable behaviour to personalize diversity — beating universal caps for the cohorts where it
matters.

### Tasks
1. Wire the P13 hidden tolerance traits into fatigue dynamics for real heterogeneity: focused
   users (slow topic-fatigue, concentration preference), novelty-seekers (fast fatigue, high
   `noveltySeeking`), creator-loyal (creator-fatigue immunity for favourites), easily-fatigued
   (fast general fatigue). Cohort generation config (generator mixture over trait profiles) —
   named cohorts for experiments, continuous traits by default.
2. Recommender-side estimation (V2 §4.10/§5, observables only): `ToleranceEstimator` producing
   `User` fields — estimated novelty tolerance, estimated topic/creator fatigue, recent-session
   summary (declining within-topic completion runs, not-interested after repeats, exit-after-
   repetition patterns, comment/save cadence). Unit-tested against constructed histories; an
   evaluation-only correlation check against hidden traits (carve-out, not a training signal).
3. `PersonalizedDiversityReranker` (extends P9 machinery): hard rules stay universal (no dup, no
   seen — V1 §15.1); per-creator/per-topic caps and MMR λ become functions of the estimates;
   repetition penalty in ranking scales with estimated tolerance. Composition and cap-scaling
   rules documented; `realism.personalized_diversity` gates it; fixed-diversity path untouched.
   Report feed-length effects explicitly (Phase 9's short-feed operating point is the known
   hazard — measure whether personalization relaxes or worsens it per cohort).
4. Tier 2 core experiment: fixed vs personalized diversity × four cohorts on `realism-medium`
   variants; all four metric groups + per-cohort session-health; publish
   `results/published/phase17/` with per-cohort plots.
5. Tests: statistical — identical repetition-heavy feeds produce measurably different fatigue/
   exit outcomes across cohorts (Tier 2 acceptance item 1); personalized ≥ fixed on U_s for
   focused and easily-fatigued cohorts with the mechanism documented (the designed effect);
   estimator sanity (no hidden reads — D18 guard covers structurally); determinism; golden
   regression.

### Exit criteria
- [ ] Repetition affects users differently per hidden traits (V2 §10 item 4; test + published).
- [ ] Personalized diversity beats fixed caps on session utility for at least the two designed
      cohorts, with feed-length impact reported honestly.
- [ ] `results/published/phase17/` complete; Release B (Tier 2) done.

### Out of scope
Event-driven timelines (18); any hidden-trait leakage into the estimator (forbidden, D18);
learned tolerance models (a P22+ candidate — hand-designed estimators here).

### Suggested package split
A (opus): trait wiring + cohort config + heterogeneity statistics.
B (opus): `ToleranceEstimator` + `PersonalizedDiversityReranker`.
C (sonnet): experiment matrix execution + comparison + plots.
Fable: config, frozen estimator interface, integration.

### Session kickoff prompt
> Read plan/00-DESIGN-DECISIONS.md, plan/00-DESIGN-DECISIONS-V2.md, plan/04-PHASES-SATISFACTION-
> SESSIONS.md (Phase 17), commit.md, V2 TDD §4.10 + Tier 2 acceptance/core experiment, §5, and
> V1 TDD §15. Wire hidden tolerance heterogeneity into fatigue, build the observable-only
> ToleranceEstimator and PersonalizedDiversityReranker test-first, then run and publish the
> fixed-vs-personalized × four-cohort experiment. Update commit.md per protocol.
