# Phase 21 Package C — Scenario: `niche_starvation`

Contracts: `docs/design/P21-CONTRACTS.md` §2/§4/§7. Plan: `plan/05-PHASES-EVENTS-LONGTERM.md`
Phase 21 task 2 ("Niche-content starvation"). V2 TDD §4.18.

## Pre-registration (written before any run; verbatim from the config's `description` field)

```
HYPOTHESIS: A mainstream-optimizing ranking policy (popularity- and generic-similarity-heavy,
zero weight on every modality-match feature) starves the niche_treasure archetype of exposure and
degrades its in-cohort targeting relative to the base-weight control on the identical world.
MECHANISM: Raising popularity_weight to 0.4 and similarity_weight to 0.7 while zeroing
visual_match_weight/music_match_weight/emotional_match_weight removes any ranking credit for the
fine-grained modality affinities that would otherwise let a niche_treasure reel (archetype
catalog: highly satisfying to a narrow cohort band, niche_cohort_width=0.15) surface to the
specific narrow cohort it is built for, while amplifying generic popularity/similarity signals
that favor broad-appeal content instead.
EXPECTED SIGNATURE: Lower arch_niche_treasure whole-run exposure share and lower
niche_in_cohort_match_rate (share of niche_treasure impressions actually served within the reel's
hidden niche cohort band) in `mainstream` vs `control`; corresponding decline in the
niche_treasure row of welfare_archetype_metrics.csv (lower exposure_share and/or lower
mean_immediate_satisfaction, since off-cohort niche impressions are a worse match).
VERDICT CRITERIA: CONFIRMED if BOTH arch_niche_treasure share AND niche_in_cohort_match_rate are
lower in `mainstream` vs `control`. PARTIAL if only one moves in the hypothesized direction. NOT
OBSERVED if neither declines beyond a small/noise-level margin.
```

## Design

Both arms derived from `configs/realism-medium-retention.json` (10k users/100k reels, event mode,
evolution+retention on, 9 simulated days, serving depth 10), algorithm `hnsw_ranker`, seed 42,
`evaluation.ecosystem_metrics=true`. Identical world; only the `ranking` block differs.

| Arm | Config | Ranking patch | Output dir |
|---|---|---|---|
| `control` | `configs/scenarios/niche_starvation.json` | none (base weights) | `results/phase21/niche_starvation/control/hnsw_ranker-seed42-20260719T090016/` |
| `mainstream` | `results/phase21/niche_starvation/generated-configs/mainstream.json` | `popularity_weight: 0.07→0.4`, `similarity_weight: 0.50→0.7`, `visual_match_weight/music_match_weight/emotional_match_weight: →0.0` (already 0.0 by default; set explicitly for a self-documenting patch) | `results/phase21/niche_starvation/mainstream/hnsw_ranker-seed42-20260719T090002/` |

Both runs exited 0; both output directories confirmed to contain `summary.json` +
`ecosystem_metrics.csv` + `welfare_archetype_metrics.csv`. Wall time: control 564.3s, mainstream
549.8s.

## Headline numbers (python-extracted from `summary.json` / `ecosystem_metrics.csv` /
`welfare_archetype_metrics.csv`)

**Whole-run (`ecosystem` block):**

| Metric | control | mainstream | Direction vs hypothesis |
|---|---|---|---|
| `arch_share_whole_run.niche_treasure` | 0.110348 | 0.081561 | confirmed at whole-run level (~26% relatively lower) — **but see reversal below** |
| `niche_in_cohort_match_rate_whole_run` | 0.283190 | 0.267530 | confirmed (~5.5% relatively lower) |

**Per-day `arch_niche_treasure` exposure share** (day 0–8; day 9 excluded, boundary artifact —
see caveat):

- control: 0.1150, 0.1209, 0.0920, 0.0995, 0.0915, 0.1020, 0.1084, 0.1199, **0.1249**
- mainstream: **0.0298**, 0.0693, 0.0823, 0.0959, 0.1006, 0.1287, 0.1442, 0.1576, **0.1616**

This is the most important finding in this scenario: the whole-run "starvation" is driven almost
entirely by heavy suppression on day 0 (mainstream 0.030 vs control 0.115 — less than a third), but
the gap **closes and then fully reverses** over the 9-day run. By day 8 (the last full day),
mainstream's niche_treasure share (0.162) is *higher* than control's (0.125). The scenario is not a
persistent steady-state starvation; it's a transient early-run effect that erodes and inverts.

**Per-day `niche_in_cohort_match_rate`** (day 0–8): control ranges 0.255–0.300 (no clear trend,
ends ~0.283); mainstream ranges 0.256–0.279 (starts and ends close to control, mostly slightly
lower — day 6 is the one day mainstream is higher: 0.2699 vs 0.2584). Much smaller, noisier gap
than the exposure-share metric, but the whole-run direction (mainstream lower) holds.

**`welfare_archetype_metrics.csv`, `niche_treasure` row:**

| Metric | control | mainstream | Direction vs hypothesis |
|---|---|---|---|
| `impressions` | 48,621 | 34,370 | fewer in mainstream (consistent with lower exposure share) |
| `exposure_share` | 0.110348 | 0.081561 | confirmed direction |
| `mean_immediate_satisfaction` | −0.121667 | **−0.025498** | **wrong way** — mainstream's niche impressions are *less* dissatisfying, not more |
| `mean_regret` | 0.044536 | 0.047153 | mainstream slightly higher (consistent direction, small magnitude) |

The satisfaction reading directly contradicts the mechanism's "off-cohort niche impressions are a
worse match" reasoning: the (fewer) niche_treasure impressions mainstream *does* serve are
associated with **better**, not worse, immediate satisfaction than control's.

**`long_term.retention_7d`**: control 0.9944, mainstream 0.9946 — both near-ceiling, saturated,
uninformative here (as in the other two scenarios in this trio).

## Data-quality caveat: day-9 boundary row

Same artifact as the other two scenarios (`horizon_seconds=777600` = exactly `9 × 86400`): day 9
carries 0 impressions in both arms here, so `arch_niche_treasure`/`niche_in_cohort_match_rate` both
correctly read 0 that day per the documented "zero-impression days emit zeros" convention (this
field's zero-day handling is fine; it's the day-8-vs-day-9 "which day is final" question that
matters for the daily-trend read above, not a defect in the per-day formula).

## Verdict: **PARTIAL** (downgraded from a literal pre-registered "CONFIRMED")

By the letter of the pre-registered whole-run criteria ("CONFIRMED if BOTH arch_niche_treasure
share AND niche_in_cohort_match_rate are lower in mainstream vs control"), this scenario technically
passes — both whole-run numbers are lower under `mainstream`. But reporting a clean CONFIRMED would
hide two things the fuller data show plainly:

1. The exposure-share effect is **not a steady-state starvation** — it is concentrated in the first
   few days and **fully reverses by the last full day** (mainstream ends *higher* than control).
   Plausible mechanism (offered as interpretation, not independently verified further): the
   mainstream preset's heavy popularity/similarity weighting concentrates early impressions onto a
   small set of broadly-appealing reels; as those reels accumulate repetition/impression-penalty
   fatigue at the population level, the ranker is pushed to reach further into the catalog by
   mid-to-late run, pulling in more niche_treasure content than the more evenly-spread control
   policy needed to from day 1. This is the same "popularity compounds, then saturates" dynamic
   visible in the `popularity_feedback` scenario's per-day series.
2. The welfare/satisfaction reading for niche_treasure impressions **improves**, not worsens, under
   `mainstream` — directly contradicting the mechanism's "off-cohort match ⇒ worse experience"
   reasoning.

Given the reversal and the contradicted welfare direction, the honest overall call is **PARTIAL**:
real, measurable, whole-run-level starvation signature exists and is worth reporting, but it is a
transient early-run effect rather than a persistent one, and the welfare channel does not corroborate
the "worse experience" half of the story. `niche_in_cohort_match_rate` is the more consistent (if
smaller-magnitude) of the two exposure-side signals across the run.

## Files

- `configs/scenarios/niche_starvation.json` (committed; control arm's exact config)
- `results/phase21/niche_starvation/generated-configs/mainstream.json` (generated treatment config)
- `results/phase21/niche_starvation/{control,mainstream}/` (run outputs, gitignored)
- `results/phase21/niche_starvation/logs/{control,mainstream}.log` (simulate stdout/stderr)
