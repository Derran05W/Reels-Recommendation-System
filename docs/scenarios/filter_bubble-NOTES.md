# Phase 21 — filter_bubble scenario NOTES (package A)

Config: `configs/scenarios/filter_bubble.json` (this file IS the control arm + carries the
pre-registered `description`). Runner: `scripts/run_phase21_a.sh`. Results root (gitignored):
`results/phase21/filter_bubble/`.

## Pre-registered block (verbatim from the config `description`, written BEFORE any run)

> HYPOTHESIS: an exploit-only recommender (no exploration, similarity-dominant ranking)
> progressively narrows users' hidden interest distributions over the 9-day run relative to a
> recommender running shipped exploration+diversity defaults on the same world — the classic
> filter-bubble pathology. MECHANISM: with exploration.epsilon=0 the ExplorationCandidateSource
> fires no out-of-preference slots, and a similarity-heavy ranking (similarity_weight 0.95 with
> every novelty/diversity/popularity/creator/exploration ranking signal and the repetition penalty
> zeroed) serves each user content maximally aligned to their CURRENT hidden preference;
> satisfaction-driven PreferenceEvolution::applyImpression then reinforces that narrow region every
> impression, compounding daily. The niche_treasure archetype and small/tail creators — which reach
> users mainly via exploration/diversity injection — lose exposure. EXPECTED SIGNATURE vs control:
> (1) per-day mean_preference_entropy declines faster and to a lower day-8 value; (2) higher
> mean_preference_shift_from_initial (more distortion); (3) lower whole-run arch_niche_treasure
> exposure share; (4) lower whole-run tail_creator_share. VERDICT CRITERIA: effect CONFIRMED if
> day-8 mean_preference_entropy(bubble) < control AND mean_preference_shift_from_initial(bubble) >
> control AND whole-run arch_niche_treasure(bubble) < control AND whole-run
> tail_creator_share(bubble) <= control. Report "effect not observed"/"partial" honestly if
> entropy/niche/tail directions fail (e.g. the shipped diversity reranker, still on in both arms,
> compensates).

## Arms (seed 42, identical generated world; only the listed knobs differ)

| arm | algorithm | exploration.epsilon | ranking | dir |
|-----|-----------|--------------------|---------|-----|
| control | hnsw_ranker_diversity (COMPLETE) | 0.05 | shipped: sim 0.50 / qual 0.10 / fresh 0.08 / pop 0.07 / trend 0.08 / creator 0.07 / expl 0.05 / rep_pen 0.15 | `results/phase21/filter_bubble/control/` |
| bubble | hnsw_ranker_diversity (COMPLETE) | 0.00 | sim 0.95 / qual 0 / fresh 0 / pop 0 / trend 0 / creator 0 / expl 0 / rep_pen 0 | `results/phase21/filter_bubble/bubble/` |

`exploration.enabled=true` in BOTH arms (source registered, per-slot bernoulli draws stream-aligned;
epsilon=0 fires no slots). diversity reranker on in both (mmr_lambda 0.75). Generated per-arm
configs: `results/phase21/filter_bubble/generated-configs/`.

## Headline numbers (seed 42; day 8 = last full simulated day; whole-run from the `ecosystem` summary block)

| metric | control | bubble | direction vs pre-registration |
|--------|---------|--------|-------------------------------|
| mean_preference_entropy, day 0 | 2.83609 | 2.83514 | — (start ~equal) |
| mean_preference_entropy, day 8 | 2.85210 | 2.85203 | entropy ROSE in both; **no collapse** |
| mean_pref_shift_from_initial, day 8 | 0.04893 | 0.05040 | bubble higher (predicted) but tiny |
| arch_niche_treasure, whole-run | 0.11947 | 0.06448 | **bubble −46% (starvation CONFIRMED)** |
| arch_niche_treasure, day 8 | 0.11908 | 0.07774 | **bubble −35% (CONFIRMED)** |
| tail_creator_share, whole-run | 0.10358 | 0.27782 | **INVERTED** (bubble higher) |
| tail_creator_share, day 8 | 0.17317 | 0.42524 | **INVERTED** |
| creator_hhi, whole-run | 0.01141 | 0.00436 | bubble LESS concentrated (inverted) |
| niche_in_cohort_match_rate, whole-run | 0.28003 | 0.28431 | ~equal |

(Whole-run tail/hhi are dominated by the ~147k-impression cold-start day 0; the day-8 steady-state
row tells the same story. Both arms share seed 42 / identical world; the bubble arm is byte-identical
to `exploration_recovery/control` — same exploit policy — a determinism cross-check.)

## Verdict recommendation: **PARTIAL — one signature confirmed, two not (report honestly)**

- **CONFIRMED — niche-content starvation.** The exploit bubble serves 35–46% less `niche_treasure`
  content than the shipped control at every steady-state day. This is the clean, on-theme
  filter-bubble harm: without exploration/diversity injection, the niche channel withers.
- **NOT observed — interest-diversity collapse.** `mean_preference_entropy` does not fall; it drifts
  slightly UP in BOTH arms (2.835 → 2.852). The Phase-20 saturation/aversion dynamics actively
  counteract preference narrowing, and at the shipped `eta_evo=0.02` over 9 days the total
  preference shift is tiny (~0.05) in both arms, so the hidden-preference distortion signal barely
  separates (bubble 0.0504 vs control 0.0489). Honest: at shipped adaptation strength this simulator
  does not reproduce a hidden-interest collapse under exploit-only serving.
- **INVERTED — creator concentration / tail exposure.** The pure-similarity bubble is LESS
  creator-concentrated than the shipped control (tail 0.278 vs 0.104; HHI 0.0044 vs 0.0114). Creator
  concentration here is driven by the control's `popularity_weight`/`trending_weight` (a
  popularity-feedback pathology), NOT by similarity exploitation — so this metric belongs to the
  popularity-feedback / creator-overconcentration scenarios (package C), not to the filter bubble.

Integrator takeaway for ECOSYSTEM.md: filter-bubble = **niche-content starvation demonstrated**;
interest-entropy collapse and creator concentration are explicitly NOT this scenario's effects in
this model (documented, not hidden).


## Method notes

- Numbers are python-extracted from each arm's `summary.json` (`ecosystem`, `long_term` blocks),
  `longterm_metrics.csv` (trailing `mean_preference_entropy` column), and `ecosystem_metrics.csv`.
- Day rows are filtered by `impressions > 0`; the 9-day horizon (777600 s = 9×86400) emits a
  near-empty day-9 boundary row that is skipped — "day 8" is the last full simulated day.
- Both arms share seed 42 and the identical generated world (dataset streams are independent of the
  ranking/exploration policy), so all cross-arm differences are the policy effect.
