# Phase 21 Package C — Scenario: `popularity_feedback`

Contracts: `docs/design/P21-CONTRACTS.md` §2/§4/§7. Plan: `plan/05-PHASES-EVENTS-LONGTERM.md`
Phase 21 task 2 ("Popularity feedback loop"). V2 TDD §4.18.

## Pre-registration (written before any run; verbatim from the config's `description` field)

```
HYPOTHESIS: Raising the ranking policy's weight on raw popularity (and correspondingly lowering
personalized-similarity weight) lets already-popular creators/reels accumulate a compounding
share of impressions across simulated days, at the expense of newer/smaller creators, relative to
the base-weight control on the identical world.
MECHANISM: Every feed-serving ranking pass scores candidates with ranking.popularity_weight on
the reel's (accumulating) popularity signal. Under a popularity-heavy weight (0.5, up from 0.07),
reels/creators that already have more impressions rank higher on EVERY subsequent request,
mechanically directing more of each day's impressions to the already-popular set (a rich-get-
richer feedback loop), while similarity_weight (down from 0.50 to 0.30) proportionally weakens
the personalized/relevance counterweight that would otherwise let long-tail content surface via
good topic/user match.
EXPECTED SIGNATURE: Day-over-day rising creator_hhi in the popularity arm vs a flatter/lower
trajectory in control; falling tail_creator_share (impression share of creators outside the
cumulative top decile, contracts §2) in the popularity arm relative to control, read as a
new/small-creator exposure-decay proxy.
VERDICT CRITERIA: CONFIRMED if by the final simulated day the popularity arm's creator_hhi
exceeds control's AND tail_creator_share is lower than control's (both directions). PARTIAL if
only one of the two metrics moves in the hypothesized direction. NOT OBSERVED if neither metric
diverges from control beyond a small/noise-level margin (same seed/world, so any divergence is
attributable to the weight change, but a negligible-magnitude move is reported honestly as
not-observed/inconclusive rather than confirmed).
```

## Design

Both arms derived from `configs/realism-medium-retention.json` (10k users/100k reels, event mode,
evolution+retention on, 9 simulated days, serving depth 10), algorithm `hnsw_ranker`, seed 42,
`evaluation.ecosystem_metrics=true`. Identical world; only the `ranking` block differs.

| Arm | Config | Ranking patch | Output dir |
|---|---|---|---|
| `control` | `configs/scenarios/popularity_feedback.json` | none (base weights) | `results/phase21/popularity_feedback/control/hnsw_ranker-seed42-20260719T085051/` |
| `popularity` | `results/phase21/popularity_feedback/generated-configs/popularity.json` | `popularity_weight: 0.07→0.5`, `similarity_weight: 0.50→0.3` | `results/phase21/popularity_feedback/popularity/hnsw_ranker-seed42-20260719T085015/` |

Both runs exited 0; both output directories confirmed to contain `summary.json` +
`ecosystem_metrics.csv` (+ `longterm_metrics.csv`). Wall time: control 522.9s, popularity 486.5s.

## Headline numbers (python-extracted from `summary.json` / `ecosystem_metrics.csv`)

**Whole-run (`ecosystem` block):**

| Metric | control | popularity | Direction vs hypothesis |
|---|---|---|---|
| `creator_hhi_whole_run` | 0.012739 | 0.011261 | **wrong way** (popularity slightly *lower*) |
| `tail_creator_share_whole_run` | 0.142775 | 0.053414 | **confirmed** (popularity ~2.7x lower) |
| `niche_in_cohort_match_rate_whole_run` | 0.283190 | 0.273186 | (not part of this hypothesis; nearly flat) |

**Per-day `creator_hhi`** (day 0–8; day 9 excluded, see caveat below):

- control: 0.0416, 0.0169, 0.0118, 0.0101, 0.0095, 0.0088, 0.0091, 0.0090, 0.0087
- popularity: 0.0390, 0.0177, 0.0106, 0.0092, 0.0084, 0.0079, 0.0085, 0.0089, 0.0079

Control's daily HHI is *higher* than popularity's on 8 of 9 real days (all except day 1, where
popularity is marginally higher: 0.01766 vs 0.01686).

**Per-day `tail_creator_share`** (day 0–8):

- control: 0.0647, 0.2279, 0.2817, 0.3062, 0.3075, 0.2967, 0.2834, 0.2831, 0.2627
- popularity: 0.0403, 0.0956, 0.1034, 0.0912, 0.0784, 0.0782, 0.0689, 0.0710, 0.0768

Popularity's tail share is lower than control's on **every single day** (often by 2–4x, e.g. day 4:
0.307 vs 0.078) — a robust, consistent effect.

**`long_term.retention_7d`**: control 0.9944, popularity 0.9924 — both near-ceiling; retention is
saturated/uninformative at this operating point (return-delay mean 6h makes near-universal 7-day
return regardless of arm), not a useful discriminator here.

## Data-quality caveat: day-9 boundary row

`horizon_seconds=777600` is *exactly* `9 × 86400`, so `floor(t/86400)` produces a valid day-index-9
row capturing whatever lands exactly at the horizon boundary: **1 impression** for control, **0**
for popularity. The scaffold's summary field `ecosystem.creator_hhi_final_day` is computed from
this literal last row, so it reads 1.0 for control (a single impression is trivially 100% HHI) and
0.0 for popularity — a tie-breaking artifact, not a real signal. Day 8 (the last full day) is the
meaningful "final day" comparison point; the day-by-day series above uses days 0–8 only. This
affects every Phase 21 scenario built on this base config/horizon and is worth flagging to the
integrator for `ECOSYSTEM.md` (not a bug in the HHI formula itself — just which day is "final" at
this exact day-granularity/horizon combination).

## Verdict: **PARTIAL** (per pre-registered criteria)

`tail_creator_share` confirms the hypothesis strongly and consistently (every day, whole-run 2.7x
lower under the popularity-heavy preset). `creator_hhi` does **not** confirm — it is flat-to-slightly
*lower* under the popularity preset on nearly every real day and in the whole-run aggregate. Per the
pre-registered criteria ("PARTIAL if only one of the two metrics moves in the hypothesized
direction"), this scenario is a genuine PARTIAL, not a clean confirm — reported honestly rather than
rounding up.

Plausible mechanistic read (not verified further, offered as interpretation): popularity-weighted
ranking robustly shifts impression share from the long tail to the top-decile group *as a whole*
(mechanically, since "popularity" itself is a monotone function of accumulated impressions,
already-popular reels compound), which is exactly what `tail_creator_share` measures. But it does
not necessarily concentrate *further within* that top-decile group onto one or two dominant
creators — `creator_hhi` (sum of squared shares across *all* creators) can stay flat or even drop if
the extra top-decile impressions get spread across many already-popular creators rather than
funneled to a single mega-creator. This reads as a "big-publishers-vs-small-creators" story rather
than a "one influencer takes over" story — worth surfacing to the integrator as a nuance for
`ECOSYSTEM.md` (the two concentration proxies in the frozen schema can legitimately diverge).

## Files

- `configs/scenarios/popularity_feedback.json` (committed; control arm's exact config)
- `results/phase21/popularity_feedback/generated-configs/popularity.json` (generated treatment config)
- `results/phase21/popularity_feedback/{control,popularity}/` (run outputs, gitignored)
- `results/phase21/popularity_feedback/logs/{control,popularity}.log` (simulate stdout/stderr)
