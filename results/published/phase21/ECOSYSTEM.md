# ReelRank V2 — Ecosystem Failure-Mode Suite (Phase 21)

Seven designed scenarios probing the classic recommender pathologies (V2 TDD §4.18), each run as
control-vs-treatment on **identical worlds** (same seed 42, same generated dataset; only the
documented policy/config lever differs), with a **pre-registered** hypothesis → mechanism →
expected signature → verdict criteria block committed in each scenario config's `description`
BEFORE any run (quoted verbatim at the top of each scenario's comparison file — verdicts are
judged against what was predicted, not fit to the numbers afterward). Base world:
`configs/realism-medium-retention.json` (10k users × 100k reels, event mode, preference evolution
+ retention on, 9 simulated days) + per-day ecosystem instrumentation
(`evaluation.ecosystem_metrics`). All four V2 §6 metric groups plus the long-term group are
reported per scenario in `./<scenario>/<scenario>-comparison.md`; **no aggregate score is ever
computed** (D22). Verdicts below are honest per the pre-registered criteria — including partial
and not-observed outcomes; a "failed" pathology is a finding about the simulator's mechanics, not
a reporting failure.

Method notes that apply across scenarios:
- The 9-day horizon emits a near-empty day-9 boundary row; per-day readings quote day 8 (last
  full day) or whole-run aggregates, and analyses filter day rows by the `impressions` column.
- `mean_preference_entropy` rises in EVERY arm at this operating point (Phase-20
  saturation/aversion push users off exhausted channels faster than any policy narrows them), so
  entropy is not a usable bubble signal here; exposure-based readings (niche/tail/HHI shares)
  carry the concentration verdicts.
- Two effects are statistical-test-enforced in-tree (Tier 4 acceptance 2 and 3):
  `RagebaitAmplificationStatisticalTest` and `ExplorationRecovery` (2 cases), both live and green.

---

## 1. Filter-bubble formation — VERDICT: PARTIAL

Exploit-only serving (ε=0, similarity-only ranking) vs shipped defaults
(`hnsw_ranker_diversity`, ε=0.05, diversity on), same world.
**Confirmed:** niche-content starvation onset — the bubble arm serves the `niche_treasure`
archetype at 0.065 whole-run share vs control's 0.120 (−46%; day-8: 0.078 vs 0.119), and
distortion runs slightly higher (0.0504 vs 0.0489 shift-from-initial).
**Not observed:** interest-diversity collapse — preference entropy RISES in both arms
(2.836→2.852); Phase-20 saturation/aversion structurally counteract narrowing at η=0.02.
**Inverted:** creator-tail starvation — the pure-similarity bubble is LESS creator-concentrated
than the (popularity-chasing) default control (tail share 0.278 vs 0.104; HHI 0.0044 vs 0.0114):
concentration is a popularity-feedback effect, not a similarity-bubble effect in this simulator.
Details: ./filter_bubble/filter_bubble-comparison.md

## 2. Ragebait amplification — VERDICT: SUPPORTED (test-enforced, Tier 4 acceptance 2)

Engagement-preset ranking vs satisfaction-proxy control on a susceptible-heavy world (ragebait
archetype mixture 2×, same catalog both arms; `controversyTolerance` is not cohort-overridable —
the content mixture is the documented susceptibility lever).
Engagement serves **69.0% ragebait** (sustained ≈0.69 every day) vs proxy's **0.0%**; mean hidden
satisfaction −0.559 vs −0.156; U_s −4.32 vs −1.74; regret-classified exits 0.59% vs 0.00%;
engagement's daily impressions decay to ~1/5 of proxy's by late run (the ecosystem starves
itself). Trust separates only weakly (0.0221 vs 0.0247) — both arms sit at the Phase-20
trust-floor operating point, so welfare/regret/exits carry the harm signal.
Enforced by `RagebaitAmplificationStatisticalTest.EngagementOverservesRagebaitAndDegradesWelfare`
(margins at the demonstrated half-gap: share > 0.30, satisfaction gap > 0.25 OR trust gap > 0.05).
Details: ./ragebait_amplification/ragebait_amplification-comparison.md

## 3. Popularity feedback loop — VERDICT: PARTIAL

Popularity-heavy ranking (popularity 0.07→0.5, similarity 0.50→0.3) vs base defaults.
**Confirmed:** tail-creator starvation — tail (outside top decile by cumulative impressions)
share 0.053 vs control 0.143 (~2.7× lower, consistent every single day): the "new/small creator
exposure decay" signature.
**Not observed:** rising creator HHI — whole-run HHI 0.0113 vs control 0.0127 (wrong direction on
8/9 days): the loop concentrates exposure into a broad popular head rather than a few
mega-creators at this catalog size.
Details: ./popularity_feedback/popularity_feedback-comparison.md

## 4. Niche-content starvation — VERDICT: PARTIAL

Mainstream-optimizing ranking (popularity 0.4, similarity 0.7, modality-match weights zeroed) vs
base. **Confirmed whole-run:** `niche_treasure` exposure share 0.082 vs 0.110 and in-cohort match
rate 0.268 vs 0.283 both decline. **Honesty caveats:** the effect is front-loaded and REVERSES by
day 8 (mainstream 0.162 vs control 0.125), and the niche-treasure cohort's immediate satisfaction
is *less* negative under the mainstream policy (−0.025 vs −0.122) — the starvation that occurs is
transient and does not produce the hypothesized welfare harm at this horizon.
Details: ./niche_starvation/niche_starvation-comparison.md

## 5. Creator overconcentration — VERDICT: CONFIRMED

Creator-affinity-heavy ranking (creator_affinity 0.07→0.4) uncapped (`hnsw_ranker`) vs the same
weights under diversity hard caps (`hnsw_ranker_diversity`, ≤2/creator).
Uncapped concentrates more on every one of the 9 days (whole-run HHI 0.0205 vs 0.0180) and is
worse for sessions (U_s −2.72 vs −2.64) — the caps buy diversity AND welfare simultaneously. The
fatigue-exit sub-metric moved slightly the wrong way (0.407 vs 0.424) and is reported as such.
Details: ./creator_overconcentration/creator_overconcentration-comparison.md

## 6. Exploration recovery — VERDICT: CONDITIONAL (validity confirmed; test-enforced at the concentrated scale, Tier 4 acceptance 3)

Exploit-heavy world with ε switched on at day 4 (`exploration.enable_at_day`) vs ε=0 control.
**Validity: PASS at full scale** — days 0–3 are byte-identical across all arms (the time gate is
provably stream-safe). **Recovery at medium scale: not observed** — post-gate niche/tail/HHI
trajectories coincide (ε 0.05/0.10/0.50 all ≈ control): the medium dataset's exploit bubble is
already diffuse across 5,000 creators (tail 0.43, HHI 0.0015), leaving no concentration headroom
to recover from; the ε=0.5 probe rules out under-exploration. **Recovery in the concentrated
regime: demonstrated and test-enforced** — at 600u/3k reels the exploit bubble concentrates
(tail 0.7525) and ε recovers neglected exposure (0.7792 at ε=0.30, 0.7822 at ε=0.60; HHI falls
0.0209→0.0193), enforced by `ExplorationRecovery.{PreGateDaysMatchControl,
PostGateTailExposureRecoversAboveControl}`.
Details: ./exploration_recovery/exploration_recovery-comparison.md

## 7. Satisfaction-vs-retention conflict — VERDICT: NO TRADE-OFF (honest non-confirmation)

Five-point linear weight sweep engagement→proxy (λ ∈ {0, .25, .5, .75, 1}), same world.
Retention and welfare **co-move** (Kendall τ = +0.80 across λ; the pre-registered criterion
required opposition): pure engagement (λ=0) is *dominated* on every axis (retention_7d 0.969 vs
0.995–0.996; sessions/user/day 0.86 vs 1.4–1.7; satisfaction −0.48 vs −0.24…−0.12; U_s −3.94 vs
−2.36…−1.54), with ~66% of both axes' gains arriving in the first quarter-step (knee λ≈0.25–0.5).
Comfort-content dynamics did surface (arch_comfort share ×15 across the sweep;
satisfaction-weighted retention peaks at λ=0.75, 0.9976, above pure proxy). **Mechanism finding:**
Phase-20 retention is satisfaction/trust-driven by construction, so a genuine
engagement-retention-vs-welfare conflict would require a satisfaction-decoupled return mechanism
(e.g. compulsive habit) the simulator deliberately does not model — recorded as designed future
work. V2 §10 item 7 (policy changes long-term satisfaction AND retention) is demonstrated
regardless: the policy lever moves retention_7d by 2.7 pp, session volume 2×, and satisfaction by
0.36.
Details: ./satisfaction_vs_retention/satisfaction_vs_retention-comparison.md

---

Reproduction: every arm is deterministic (D8/D9); scenario configs with their pre-registration
blocks under `configs/scenarios/`; per-arm resolved configs in each `./<scenario>/<arm>/config.json`;
runner scripts `scripts/run_phase21_{a,b,c}.sh`; renderer `scripts/phase21_scenarios.py`; package
run notes under `docs/scenarios/`.
