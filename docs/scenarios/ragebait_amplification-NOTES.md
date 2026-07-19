# Phase 21 — RAGEBAIT AMPLIFICATION — Package B NOTES

Working results live under `results/phase21/ragebait_amplification/` (gitignored — `results/*` is
ignored except `results/published/`), so this canonical NOTES file is committed here under
`docs/scenarios/`; an identical copy is dropped at `results/phase21/ragebait_amplification/NOTES.md`
for the integrator's tooling. Numbers below are extracted programmatically (throwaway session script) from each arm's
frozen `summary.json` (§2/§3/§6 keys) — not hand-typed; re-derivable with `scripts/phase21_scenarios.py` (package D). Tier 4 acceptance 2 is additionally
TEST-ENFORCED by `tests/property/ragebait_amplification_statistical_test.cpp`.

## Pre-registration (formatted from the `description` field of `configs/scenarios/ragebait_amplification.json`; that field holds the full pre-registered block written before any run)

- **HYPOTHESIS:** In a ragebait-susceptible content world, an ENGAGEMENT-optimizing ranking policy
  over-serves ragebait relative to a SATISFACTION-PROXY policy, and that over-exposure produces
  measurable NEGATIVE long-term outcomes: lower hidden welfare (mean satisfaction), lower platform
  trust, and more regret-classified session exits.
- **MECHANISM / LEVER (documented honestly):** the hidden ragebait-susceptibility trait
  (`controversyTolerance`) is NOT overridable via `realism.cohort_mix`, so the susceptible-heavy
  WORLD is created via the CONTENT MIXTURE — `realism.archetypes` raises the ragebait archetype
  mixture weight from its default 0.10 to **0.20 (2x)**, renormalizing the other seven by 0.8/0.9 so
  the catalog still sums to 1.0 (all other archetype parameters copied verbatim). The SAME catalog +
  seed 42 feed BOTH arms, so the world (reels + hidden state) is identical; only ranking weights
  differ. Engagement preset ranks ragebait's high emotional-intensity/controversy/clickbait content
  UP (`emotional_intensity_weight=+0.08`, no clickbait penalty, `similarity_weight=0.6`); proxy
  control ranks it DOWN (`clickbait_weight=-0.15`, `emotional_intensity_weight=-0.05`,
  usefulness/production/info-density positive). Ragebait carries `satisfaction_bias=-0.35` and
  `regret_bias=+0.35`.
- **EXPECTED SIGNATURE:** `arch_ragebait` whole-run share HIGHER under engagement (elevated/rising
  across days); `welfare.mean_immediate_satisfaction` LOWER; platform trust
  (`welfare.platform_trust` / `long_term.mean_final_trust`) LOWER; `session_health`
  `exit_type_shares.regret` ELEVATED under engagement.
- **VERDICT CRITERIA:** SUPPORTED iff engagement `arch_ragebait` share > proxy AND at least one of
  {mean satisfaction, platform_trust, mean_final_trust} is lower under engagement AND regret-exit
  share is not lower under engagement. Otherwise "effect not observed".

## Lever used

Ragebait ARCHETYPE mixture weight raised 0.10 → 0.20 (2x), other seven renormalized ×(0.8/0.9),
identical catalog in both arms (world identity). This is the honest lever because
`controversyTolerance` is not exposed through `realism.cohort_mix` (P21 scaffold intel).

## Arms (base `configs/scenarios/ragebait_amplification.json`, seed 42, hnsw_ranker, event mode,
`evaluation.ecosystem_metrics=true`; only the ranking block differs — `scripts/run_phase21_b.sh`
does `cfg["ranking"].update(patch)`)

| arm | ranking preset | resolved dir |
|-----|----------------|--------------|
| engagement | P20 engagement patch (12 keys) | `results/phase21/ragebait_amplification/engagement/hnsw_ranker-seed42-*` |
| proxy | P20 satisfaction-proxy patch (6 keys) | `results/phase21/ragebait_amplification/proxy/hnsw_ranker-seed42-*` |

## Headline numbers (full scale: 10k users / 100k reels / 32 topics / 64 dims / 9 days)

| metric (frozen key) | engagement | proxy | reading |
|---------------------|-----------:|------:|---------|
| `ecosystem.arch_share_whole_run.ragebait` | **0.6899** | **0.0000** | engagement serves 69% ragebait; proxy suppresses it entirely |
| `welfare.mean_immediate_satisfaction`     | **−0.5587** | **−0.1556** | hidden welfare far worse under engagement |
| `welfare.platform_trust`                  | 0.0221 | 0.0247 | trust lower under engagement (both arms erode trust in this susceptible world) |
| `long_term.mean_final_trust`              | 0.0221 | 0.0247 | same (welfare/long_term agree by construction) |
| `session_health.exit_type_shares.regret` | **0.0059** | **0.0000** | regret-classified exits exist ONLY under engagement |
| `session_health.mean_session_utility` (U_s) | −4.321 | −1.737 | context: session utility 2.5x worse under engagement |
| `welfare.mean_regret`                     | 0.3662 | 0.0194 | context: 19x the per-impression regret |

Per-day `arch_ragebait` (engagement, `ecosystem_metrics.csv`, days 0–8; day 9 has exactly 1
impression and is a tail artifact = 1.000): 0.693, 0.720, 0.684, 0.692, 0.672, 0.665, 0.682, 0.678,
0.690 — **sharply elevated from day 0 and sustained ≈0.69** (engagement floods ragebait immediately
rather than ramping; the signature is "higher", not "rising", which the pre-registration allowed as
"higher and/or rising"). Proxy per-day `arch_ragebait` is 0.000 on every day.

Secondary disengagement signal: engagement's daily impressions decay 62,707 → 11,486 (days 0–8)
while proxy's decay 185,138 → 54,448 — the proxy arm sustains ~5x the daily activity by the end.
(Wall: engagement 366s, proxy 750s, 2-concurrent Release — the healthier arm simulates more
sessions and costs more wall time.)

## Verdict

**SUPPORTED** — all three pre-registered criteria hold:
1. `arch_ragebait` share: engagement 0.6899 > proxy 0.0000 (over-serving, sustained per-day).
2. Welfare lower under engagement: mean satisfaction −0.5587 < −0.1556 (decisive), and both trust
   measures 0.0221 < 0.0247 (direction agrees; small magnitude — in this ragebait-heavy world even
   the proxy arm ends with low trust, so satisfaction/regret are the sharper welfare axes here).
3. Regret exits not lower under engagement: 0.0059 > 0.0000.

Honest caveats: the trust gap is small (0.0026); the regret-exit share is small in absolute terms
(0.59% of closed sessions) though strictly positive vs zero. The dominant harm signature is the
satisfaction/regret/U_s collapse plus the activity decay. Tier 4 acceptance 2 ("engagement
optimization can create measurable negative long-term outcomes") is demonstrated at full scale and
TEST-ENFORCED at reduced scale by `RagebaitAmplificationStatisticalTest` (margins: ragebait-share
gap > 0.30, satisfaction gap > 0.25 OR trust gap > 0.05 — all ~half the demonstrated gaps).
