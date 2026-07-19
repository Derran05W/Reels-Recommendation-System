# Phase 21 — exploration_recovery scenario NOTES (package A)

Config: `configs/scenarios/exploration_recovery.json` (this file IS the control arm + carries the
pre-registered `description`). Runner: `scripts/run_phase21_a.sh`. Results root (gitignored):
`results/phase21/exploration_recovery/`. Test-enforced by
`tests/property/exploration_recovery_statistical_test.cpp` (Tier-4 acceptance 3).

## Pre-registered block (verbatim from the config `description`, written BEFORE any run)

> HYPOTHESIS: in a neglected-interest world produced by an exploit-heavy policy, switching
> exploration on partway through the run (exploration.enable_at_day=4) recovers neglected-CHANNEL
> exposure (tail/small creators and off-preference content) relative to a never-exploring control on
> the same world, and MORE exploration recovers more (dose-response epsilon 0.10 >= 0.05).
> MECHANISM: all three arms use a similarity-dominant exploit ranking (similarity_weight 0.95, other
> signals zeroed) PLUS exploration_weight=1.0 in the shared base — exploration_weight makes
> exploration-SOURCED candidates visible in the ranked feed WHEN they exist, instead of being buried
> by the similarity term; it is INERT in the epsilon=0 control and in every arm's pre-gate window
> (no exploration candidates exist there), so the control stays a pure exploit bubble and days 0-3
> are byte-identical across all arms. A reduced-scale pilot (the committed statistical test)
> established that with exploration_weight=0 the fired exploration content never reaches impressions,
> and that mean_preference_entropy does NOT fall under the exploit bubble here (Phase-20
> saturation/aversion make hidden preferences WANDER, so entropy can drift UP in every arm and is
> reported but is NOT the primary recovery signal). At enable_at_day=4 the recover arms fire
> exploration (underexposed / random-fresh / uncertain-topic modes) so neglected tail creators +
> off-preference content reach users. EXPECTED SIGNATURE: (validity) each recover arm's day 0-3 rows
> EQUAL control's exactly; (recovery) after day 4, tail_creator_share rises ABOVE control and
> creator_hhi falls BELOW control, the served archetype mix broadens, and recover10 >= recover05.
> VERDICT CRITERIA: recovery CONFIRMED if the days 0-3 validity match holds AND post-gate
> tail_creator_share(recover05) exceeds control by a clear margin AND creator_hhi(recover05) < control
> AND recover10 at least as strong. Report "effect not observed" if tail/hhi do not move.

## Arms (seed 42, identical generated world; only exploration.epsilon + enable_at_day differ)

| arm | exploration.epsilon | enable_at_day | dir |
|-----|--------------------|---------------|-----|
| control | 0.00 | -1 (never) | `results/phase21/exploration_recovery/control/` |
| recover05 | 0.05 | 4 | `results/phase21/exploration_recovery/recover05/` |
| recover10 | 0.10 | 4 | `results/phase21/exploration_recovery/recover10/` |

All three: algorithm hnsw_ranker_diversity (COMPLETE), `exploration.enabled=true`,
`exploration.guaranteed_slots=6`, shared exploit ranking (sim 0.95 / exploration_weight 1.0 / all
other listed weights 0). Generated per-arm configs:
`results/phase21/exploration_recovery/generated-configs/`.

## Results (seed 42; enable_at_day=4 ⇒ pre-gate days 0–3, post-gate days 4–8; day 8 = last full day)

### Validity (stream-alignment) — **PASS**

Days 0–3 are **byte-identical** across control, recover05, recover10 (and the recover50 probe below)
on every per-day field checked (impressions, entropy, tail_creator_share, arch_niche_treasure,
creator_hhi, mean_pref_shift). The `exploration.enable_at_day` gate holds effective ε=0 pre-gate and
consumes the same rng draws, so the world is identical until day 4 — the strong stream-safety
guarantee, confirmed at full scale.

### Recovery — post-gate (days 4–8) impression-weighted pooled

| metric | control | recover05 | recover10 | recover50 (probe) |
|--------|---------|-----------|-----------|-------------------|
| tail_creator_share | 0.43010 | 0.42771 | 0.43196 | 0.42717 |
| arch_niche_treasure | 0.07585 | 0.07569 | 0.07485 | 0.07528 |
| creator_hhi | 0.00149 | 0.00149 | 0.00146 | 0.00146 |

Day-8: entropy 2.85204 / 2.85204 / 2.85133 / 2.85223; shift 0.05040 / 0.05059 / 0.05019 / 0.05058.
All arms coincide to noise — **no divergence after the gate at any tested ε (0.05, 0.10, even 0.50)**.

### Verdict recommendation: **VALIDITY confirmed; RECOVERY not observed at this scale (honest); mechanism test-enforced**

- **Root cause (measured, not assumed).** The full-scale exploit bubble already serves *very
  diffusely*: with 5000 creators / 100k reels, pure-similarity ranking spreads impressions across a
  huge creator pool (tail ≈ 0.43, creator_hhi ≈ 0.0015). There is essentially **no neglected-creator
  concentration to relieve**, so exploration has nothing to recover — I ran an extra ε=0.50 probe
  arm (`results/phase21/exploration_recovery/recover50_probe/`) specifically to rule out "ε too
  small": it too shows no lift, so the limiter is concentration headroom, not ε.
- **The mechanism IS real and IS enforced.** `tests/property/exploration_recovery_statistical_test.cpp`
  demonstrates recovery at reduced scale where the exploit bubble DOES concentrate (100 creators →
  bubble tail 0.75 / HHI 0.021): turning exploration on at the gate lifts post-gate tail_creator_share
  to 0.78 and drops creator_hhi to ~0.019 (dose-responsive, days-0–2 validity-matched). That test
  test-enforces Tier-4 acceptance 3.
- **Honest headline for ECOSYSTEM.md.** Exploration recovery is **conditional on the bubble actually
  concentrating a channel.** At the medium-dataset scale a similarity-only policy does not concentrate
  (it diffuses across the large creator pool), so no recovery is possible or needed; the recovery
  effect is demonstrated (and test-guarded) in the concentrated regime. The stream-safe
  `enable_at_day` gate is validated at full scale (days 0–3 identical) either way.

(Note: mean_preference_entropy is not a recovery signal here — it drifts slightly UP in every arm as
in filter_bubble; the Phase-20 saturation/aversion dynamics dominate over ε-driven broadening.)


## Method notes

- Numbers python-extracted from each arm's `summary.json` (`ecosystem`, `long_term`),
  `ecosystem_metrics.csv` (per-day `tail_creator_share`, `creator_hhi`, `arch_*`), and
  `longterm_metrics.csv` (`mean_preference_entropy`, `mean_pref_shift_from_initial`).
- enable_at_day=4 ⇒ effective epsilon 0 on simulated days 0–3; the gate goes live on day 4. Day
  rows filtered by `impressions > 0`; the near-empty day-9 boundary row (777600 s = 9×86400) is
  skipped; "day 8" is the last full simulated day. Post-gate window = days 4–8.
- Validity check: recover-arm days 0–3 rows must equal control's (the stream-alignment guarantee the
  statistical test enforces in-process at reduced scale).
