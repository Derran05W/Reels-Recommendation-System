# Phase 8 — Exploration and cold start: ε sweep + injection experiments

**Setup.** `configs/medium.json` derivative (committed as `configs/phase8-coldstart.json`), seed 42:
10,000 users × 100,000 reels × 200 interactions/user, plus **mid-simulation injection at round 10
of 20**: 5,000 new reels (4.76% of the final catalog) and 500 new users (injected reels first,
then users; injected users receive requests from round 10 on, giving them exactly the 100-impression
TDD 18.5 cold-start window). Algorithm `hnsw_ranker_exploration` (TDD 16.7) at
ε ∈ {0, 0.02, 0.05, 0.10}, plus an `hnsw_ranker` arm on the identical config as the no-op
cross-check. All arms: online learning enabled, guaranteed_slots=2, fresh_window_seconds=259200.
2,050,000 impressions per arm. Full per-arm output in `eps000/ … eps010/`, `ranker-noop-xcheck/`;
machine-readable table in `comparison.csv`.

## ε = 0 is a verified no-op at full scale (exit criterion 1)

`hnsw_ranker_exploration` at ε=0 and plain `hnsw_ranker` on the same config/seed produced
**byte-identical** `recommendation_metrics.csv`, `learning_curve.csv`, `regret_curve.csv`,
`retrieval_metrics.csv`, `new_user_curve.csv`, and `new_reel_exposure.csv` (verified with `cmp`;
only wall-clock timing files differ). The exploration recommender consumes per-request gate draws
that `hnsw_ranker` does not, but at ε=0 no gate ever fires and no draw touches the feed — the
property holds at 2.05M-impression scale, not just in the unit/property suites.

## ε sweep — whole population (2.05M impressions/arm)

| ε | reward/impression | mean true affinity | est↔hidden cosine (final) | cumulative regret | mean regret |
|------|--------|--------|--------|---------|--------|
| 0.00 | 0.21277 | 0.24040 | 0.41785 | 2186.56 | 0.54056 |
| 0.02 | 0.21285 | 0.24114 | 0.42017 | **2159.40** | **0.53384** |
| 0.05 | **0.21527** | **0.24442** | **0.42453** | 2168.45 | 0.53608 |
| 0.10 | 0.21398 | 0.24273 | 0.42152 | 2175.30 | 0.53778 |

Moderate exploration is **free or better** for the whole population: every ε > 0 arm beats ε=0 on
reward, affinity, alignment, and regret simultaneously. The best single operating point is
ε=0.05 on reward/affinity/alignment (+1.2% reward vs ε=0) and ε=0.02 on cumulative regret
(−1.2%); ε=0.10 starts giving back reward relative to 0.05 while still beating ε=0. The
alignment gain (est↔hidden cosine 0.4179 → 0.4245) shows the mechanism: exploration feeds the
online learner off-policy evidence it cannot get from a pure exploit loop.

## New-reel exposure (exit criterion 2b)

| ε | distinct injected reels exposed (of 5,000) | injected-reel impressions | share of all impressions |
|------|-------|--------|--------|
| 0.00 | 1,300 (26.0%) | 97,537 | 4.76% |
| 0.02 | 1,367 (27.3%) | 92,564 | 4.52% |
| 0.05 | 1,408 (28.2%) | 95,320 | 4.65% |
| 0.10 | **1,453 (29.1%)** | 95,901 | 4.68% |

Distinct-reel **coverage rises monotonically with ε** (+11.8% at ε=0.10 vs ε=0) while total
injected-impression volume stays ≈ the catalog share (4.76%) in every arm: exploration does not
inflate new-content volume, it **spreads it across more reels** — exactly the underexposed-content
behaviour TDD 12.7 asks for. (ε=0 reaches 26% coverage on its own because injected reels are
immediately HNSW-indexed via `onReelsAppended` and compete normally on trending/popularity;
`new_reel_exposure.csv` per arm gives the round-by-round ramp from zero at injection.)

## New-user cold start (exit criteria 2a, 3)

Pooled mean oracle regret over the 500 injected users' first N impressions, plus their pooled
reward curve endpoint (`new_user_curve.csv`):

| ε | regret first 10 | first 25 | first 50 | first 100 | reward @ impression 100 | interactions to pre-injection target reward |
|------|--------|--------|--------|--------|--------|-----|
| 0.00 | **0.67974** | 0.65475 | 0.62764 | 0.59512 | 0.17556 | not reached |
| 0.02 | 0.68012 | 0.64502 | 0.62219 | 0.58957 | **0.22383** | not reached |
| 0.05 | 0.68062 | **0.64425** | **0.61348** | **0.58133** | 0.22201 | **98** |
| 0.10 | 0.68188 | 0.65148 | 0.62703 | 0.59776 | 0.18098 | not reached |

**With vs without exploration (ε=0.05 vs ε=0):** new-user regret is lower at every window from 25
impressions on (−1.6% at 25, −2.3% at 50, −2.3% at 100), while the first-10 window is flat
(+0.1%, the noise-level immediate cost of the explore slots). By impression 100 the ε=0.05 new
users earn **+26% reward per impression** over the ε=0 new users (0.2220 vs 0.1756), and ε=0.05
is the only arm whose injected users reach the pre-injection population mean reward inside their
100-impression window (at 98). ε=0.10 over-explores: its new users do worse than ε=0.02/0.05 on
every window. New users demonstrably receive mixed content — their outcomes move with ε, and the
exploration-labeled slots are protected by the guaranteed-slots rule (min(fired gates,
guaranteed_slots=2, available) per feed, documented in `Orchestrator`).

## Conclusion

ε=0.05 — the TDD 12.7 suggested default — is confirmed as the shipped default: best
whole-population reward/affinity/alignment, best new-user regret at every window ≥ 25, the only
arm to reach target reward in-window, near-best coverage. The ε=0.02 arm shows the cumulative-
regret optimum is slightly below 0.05 for the *existing* population; the cold-start benefit at
0.05 dominates for the phase's purpose.

## Notes and caveats

- The five arms ran **concurrently** on one machine (wall 710–1106 s): absolute latency numbers
  carry cache/memory contention (retrieval p50 2.35–4.05 ms across arms vs ~1.9 ms solo in
  Phase 7); cross-arm quality comparisons are like-for-like (deterministic feeds, identical
  sampling streams). Full-pipeline p95 stayed under the §27 10 ms target in every arm (max
  8.79 ms retrieval p95 under 5-way contention).
- Regret is in true-affinity units (Phase 4 deviation, `oracle.regret_units_note` in every
  summary.json).
- `interactions_to_target_reward` uses a deliberately hard target — the pre-injection population
  mean (a population with 100 impressions of learning behind it) — so "not reached" within 100
  impressions is expected for most arms; the per-window regret rows are the primary criterion.
- `metadata.json` records `reel_rank_dirty: true` (runs predate the final phase commit — same
  provenance situation as Phases 1/4/5/6/7).
