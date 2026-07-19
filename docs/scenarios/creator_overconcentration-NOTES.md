# Phase 21 Package C — Scenario: `creator_overconcentration`

Contracts: `docs/design/P21-CONTRACTS.md` §2/§4/§7. Plan: `plan/05-PHASES-EVENTS-LONGTERM.md`
Phase 21 task 2 ("Creator overconcentration"). V2 TDD §4.18.

## Pre-registration (written before any run; verbatim from the config's `description` field)

```
HYPOTHESIS: A creator-affinity-heavy ranking policy with no diversity constraint concentrates a
day's impressions on far fewer creators than the SAME affinity-heavy policy served through the
diversity-reranking path that enforces a hard <=2-per-creator-per-feed cap; the uncapped arm
should also show measurably more fatigue-driven session exits and lower session utility (U_s) as
users saturate on a small creator set.
MECHANISM: ranking.creator_affinity_weight raised to 0.4 (from 0.07) on BOTH arms makes creator
affinity the dominant non-similarity score term, pushing a user's historically-liked creators to
the top of every candidate pool. The `affinity` arm serves via hnsw_ranker (ranking only, no
re-ranking pass). The `capped` arm serves the IDENTICAL config via hnsw_ranker_diversity
(FullRecommender: exploration + the DiversityReranker's hard max_per_creator=2 constraint + MMR),
which structurally prevents any one creator from filling more than 2 of the 10 feed slots per
request regardless of how highly that creator scores.
EXPECTED SIGNATURE: Materially higher creator_hhi (per-day and whole-run) in `affinity` vs
`capped`; higher session_health.exit_type_shares.fatigue and lower
session_health.mean_session_utility (U_s) in `affinity` vs `capped`.
VERDICT CRITERIA: CONFIRMED if creator_hhi is higher in `affinity` AND at least one of {fatigue
exit share higher, mean_session_utility lower} holds in `affinity` vs `capped`. PARTIAL if only
the HHI gap appears without a session-health signature (the cap controls concentration but the
welfare channel doesn't register a difference at this scale/horizon). NOT OBSERVED if creator_hhi
is not meaningfully higher in the uncapped arm.
```

## Design

Both arms load the **same** committed config `configs/scenarios/creator_overconcentration.json`
(derived from `configs/realism-medium-retention.json`, `ranking.creator_affinity_weight` patched
0.07→0.4, `evaluation.ecosystem_metrics=true`, seed 42) and differ **only** by the `--algorithm`
CLI flag (simulate.cpp applies `--algorithm` to `config.algorithm` before running, so each run's own
written `config.json` correctly reflects the algorithm it actually ran under). No generated
config-file variant was needed for this scenario.

| Arm | Algorithm | Output dir |
|---|---|---|
| `affinity` | `hnsw_ranker` (no re-ranking; uncapped) | `results/phase21/creator_overconcentration/affinity/hnsw_ranker-seed42-20260719T090950/` |
| `capped` | `hnsw_ranker_diversity` (FullRecommender: exploration + diversity re-rank, `max_per_creator=2` hard cap) | `results/phase21/creator_overconcentration/capped/hnsw_ranker_diversity-seed42-20260719T091741/` |

Both runs exited 0; both output directories confirmed to contain `summary.json` +
`ecosystem_metrics.csv` + `session_health.csv`. Wall time: affinity 573.4s, capped 1044.0s (capped
costs more, expected — it runs the extra diversity re-ranking pass).

## Headline numbers (python-extracted from `summary.json` / `ecosystem_metrics.csv`)

**Creator concentration:**

| Metric | affinity | capped | Direction vs hypothesis |
|---|---|---|---|
| `creator_hhi_whole_run` | 0.020544 | 0.017950 | **confirmed** (affinity ~14.5% relatively higher) |
| `creator_hhi` day 8 (last full day) | 0.013406 | 0.012207 | **confirmed** (~9.8% relatively higher) |
| `creator_hhi_final_day` (frozen field) | 1.0 | 1.0 | **uninformative** — see caveat below |

Per-day `creator_hhi`, days 0–8 (affinity vs capped): affinity is higher than capped on **every
single day**: 0.0511/0.0399, 0.0211/0.0179, 0.0157/0.0144, 0.0144/0.0144 (~tied), 0.0136/0.0133,
0.0135/0.0133, 0.0138/0.0125, 0.0134/0.0130, 0.0134/0.0122. A consistent, robust 9/9-day signal.

**Session health / welfare:**

| Metric | affinity | capped | Direction vs hypothesis |
|---|---|---|---|
| `session_health.mean_session_utility` (U_s) | −2.7201 | −2.6399 | **confirmed** (affinity lower/worse) |
| `session_health.exit_type_shares.fatigue` | 0.4070 | 0.4240 | **wrong way** (capped slightly higher) |
| `session_health.early_failure_exit_rate` | 0.5270 | 0.5144 | affinity slightly higher |
| `session_health.harmful_fatigue_mean` | 0.3162 | 0.3148 | ~flat |
| `sessions` (closed) | 114,446 | 116,243 | ~flat |
| `long_term.retention_7d` | 0.9942 | 0.9939 | ~flat, saturated |
| `long_term.mean_final_trust` | 0.01441 | 0.01521 | ~flat |

Both arms show harsh overall session dynamics at `creator_affinity_weight=0.4` (early-failure exit
rate ~51–53%, fatigue exit share ~41–42%, U_s deeply negative ≈ −2.6 to −2.7) — this looks like a
property of the extreme affinity weight itself (present in both arms identically), not something
the cap fixes; the cap's effect is the smaller, but consistent, *relative* gap between the two arms.

## Data-quality caveat: frozen `creator_hhi_final_day` is uninformative here

Same day-9 boundary-row artifact as the other two scenarios in this trio (`horizon_seconds=777600`
is exactly `9 × 86400`): both arms' day-9 row carries exactly **1** impression, so both trivially
compute HHI = 1.0 — an exact tie that carries zero discriminating information. The whole-run
aggregate and the day-8 (last full day) / full daily-series comparisons above are what actually
carry the signal; worth flagging to the integrator for `ECOSYSTEM.md` so the frozen `_final_day`
field isn't quoted alone as "no difference."

## Verdict: **CONFIRMED** (per pre-registered criteria)

`creator_hhi` is higher in `affinity` than `capped` — robustly, on every one of the 9 real days and
in the whole-run aggregate (not just the (uninformative) frozen final-day field). `U_s`
(`mean_session_utility`) is also lower/worse in `affinity`, satisfying the pre-registered "at least
one of {fatigue share higher, U_s lower}" clause. Per the pre-registered VERDICT CRITERIA, this is a
clean CONFIRMED.

Honest caveat: the *specific* fatigue-exit-share sub-metric moved in the opposite direction from the
mechanism's prediction (capped 42.4% vs affinity 40.7%, i.e. slightly *more* fatigue exits under the
capped/diverse arm, not fewer) — the welfare signal is carried by overall session utility (U_s),
not by the raw fatigue-exit-share channel specifically. Report this nuance rather than implying both
welfare sub-signals cooperated.

## Files

- `configs/scenarios/creator_overconcentration.json` (committed; shared config for both arms)
- `results/phase21/creator_overconcentration/{affinity,capped}/` (run outputs, gitignored)
- `results/phase21/creator_overconcentration/logs/{affinity,capped}.log` (simulate stdout/stderr)
