# Phase 21 — SATISFACTION-VS-RETENTION FRONTIER — Package B NOTES

Working results under `results/phase21/satisfaction_vs_retention/` (gitignored), so this canonical
NOTES file is committed under `docs/scenarios/`; an identical copy is dropped at
`results/phase21/satisfaction_vs_retention/NOTES.md` for the integrator. Numbers are extracted
programmatically (throwaway session script) from each arm's frozen `summary.json` — not hand-typed;
re-derivable with `scripts/phase21_scenarios.py` (package D).

## Pre-registration (summary; full pre-registered block is the `description` field of
`configs/scenarios/satisfaction_vs_retention.json`)

- **HYPOTHESIS:** a retention/welfare FRONTIER exists — sweeping ranking weight from the engagement
  preset (λ=0) to the satisfaction-proxy preset (λ=1) trades engagement-side outcomes (`retention_7d`,
  `sessions_per_user_per_day`) against welfare-side outcomes (mean hidden satisfaction, session
  utility U_s). Engagement end maximizes retention/time-on-app; proxy end maximizes hidden welfare;
  comfort-content dynamics surface in `arch_comfort`.
- **MECHANISM / BLEND RULE:** ranking weights linearly interpolated per-key over the UNION of the
  engagement and proxy patch keys (17 keys) at λ ∈ {0, 0.25, 0.5, 0.75, 1}. For each union key,
  `value(λ) = (1−λ)·E + λ·P`, where `E` = engagement-patch value if the key is in the engagement
  patch ELSE the BASE config's resolved default, and `P` = proxy-patch value if the key is in the
  proxy patch ELSE the BASE default. So λ=0 reproduces the pure engagement arm and λ=1 the pure proxy
  arm. World is the DEFAULT archetype catalog (same seed 42 across all five arms).
- **EXPECTED SIGNATURE:** `retention_7d` and `sessions_per_user_per_day` highest near λ=0 and
  declining toward λ=1; `welfare.mean_immediate_satisfaction` and `session_health.mean_session_utility`
  (U_s) lowest near λ=0 and rising toward λ=1; `arch_comfort` share shifting across λ. A "knee" where
  welfare gains flatten marks the efficient trade-off point.
- **VERDICT CRITERIA:** FRONTIER DEMONSTRATED iff engagement/retention and welfare move in OPPOSITE
  directions across λ (negative rank correlation between `retention_7d` and mean satisfaction over the
  5 arms); report the knee. Otherwise "no trade-off observed / co-moving".

## Blend rule — the five fully-resolved ranking patches (union of the P20 engagement & proxy keys)

`E` = engagement patch value (else BASE default); `P` = proxy patch value (else BASE default). BASE
resolved defaults (from `configs/realism-medium-retention.json` + config.hpp): `similarity_weight`
0.50, `quality_weight` 0.10, `freshness_weight` 0.08, `popularity_weight` 0.07, `trending_weight`
0.08, `repetition_penalty` 0.15, `duration_match_weight` 0.05, `impression_penalty_weight` 0.05, all
V2 feature weights 0.0. `λ=0` column == the engagement arm; `λ=1` column == the proxy arm.

| ranking key | λ=0.00 | λ=0.25 | λ=0.50 | λ=0.75 | λ=1.00 |
|-------------|-------:|-------:|-------:|-------:|-------:|
| similarity_weight          | 0.6000 | 0.5750 | 0.5500 | 0.5250 | 0.5000 |
| emotional_intensity_weight | 0.0800 | 0.0475 | 0.0150 | −0.0175 | −0.0500 |
| emotional_match_weight     | 0.1000 | 0.0750 | 0.0500 | 0.0250 | 0.0000 |
| music_match_weight         | 0.1200 | 0.0900 | 0.0600 | 0.0300 | 0.0000 |
| visual_match_weight        | 0.0400 | 0.0300 | 0.0200 | 0.0100 | 0.0000 |
| popularity_weight          | 0.0500 | 0.0550 | 0.0600 | 0.0650 | 0.0700 |
| clickbait_weight           | 0.0000 | −0.0375 | −0.0750 | −0.1125 | −0.1500 |
| usefulness_weight          | 0.0000 | 0.0300 | 0.0600 | 0.0900 | 0.1200 |
| production_quality_weight  | 0.0000 | 0.0200 | 0.0400 | 0.0600 | 0.0800 |
| information_density_weight | 0.0000 | 0.0150 | 0.0300 | 0.0450 | 0.0600 |
| language_match_weight      | 0.0000 | 0.0125 | 0.0250 | 0.0375 | 0.0500 |
| quality_weight             | 0.0000 | 0.0250 | 0.0500 | 0.0750 | 0.1000 |
| freshness_weight           | 0.0000 | 0.0200 | 0.0400 | 0.0600 | 0.0800 |
| trending_weight            | 0.0000 | 0.0200 | 0.0400 | 0.0600 | 0.0800 |
| repetition_penalty         | 0.0000 | 0.0375 | 0.0750 | 0.1125 | 0.1500 |
| duration_match_weight      | 0.0000 | 0.0125 | 0.0250 | 0.0375 | 0.0500 |
| impression_penalty_weight  | 0.0000 | 0.0125 | 0.0250 | 0.0375 | 0.0500 |

Keys NOT in the union (`creator_affinity_weight`=0.07, `exploration_weight`=0.05,
`session_topic_weight`=0.05) stay at the base value across all five arms (identical). The runner
regenerates this table deterministically; verify with `diff` on the generated per-arm configs.

## Frontier table (full scale: 10k users / 100k reels / 9 days, DEFAULT catalog, seed 42)

| λ | `retention_7d` | `sessions_per_user_per_day` | `satisfaction_weighted_retention` | mean satisfaction | U_s (`mean_session_utility`) | `arch_comfort` | `arch_ragebait` | `mean_final_trust` |
|---|--------------:|---------------------------:|----------------------------------:|------------------:|----------------------------:|---------------:|----------------:|-------------------:|
| 0.00 (engagement) | 0.9692 | 0.8597 | 0.6616 | −0.4817 | −3.942 | 0.0018 | 0.3939 | 0.0180 |
| 0.25 | 0.9946 | 1.4055 | 0.9506 | −0.2446 | −2.363 | 0.0115 | 0.0255 | 0.0204 |
| 0.50 | 0.9958 | 1.5924 | 0.9823 | −0.1797 | −1.914 | 0.0144 | 0.0003 | 0.0218 |
| 0.75 | 0.9948 | 1.6950 | **0.9976** | −0.1330 | −1.617 | 0.0268 | 0.0000 | 0.0235 |
| 1.00 (proxy) | 0.9960 | 1.7058 | 0.9935 | −0.1224 | −1.541 | 0.0274 | 0.0000 | 0.0238 |

Rank correlation (Kendall tau over the 5 arms) between `retention_7d` and mean satisfaction:
**+0.800** (the pre-registered trade-off criterion required it NEGATIVE). Sessions/user/day and
satisfaction are perfectly concordant (tau = +1.0). Closed sessions per arm: 77,377 → 153,542
(monotone in λ). Per-step deltas (satisfaction / sessions-per-day): λ 0→0.25: +0.237 / +0.546;
0.25→0.50: +0.065 / +0.187; 0.50→0.75: +0.047 / +0.103; 0.75→1.00: +0.011 / +0.011 — ~66% of the
total improvement on both axes lands in the first quarter-step, with gains nearly exhausted after
λ≈0.5. Wall (2-concurrent Release): 438s / 648s / 657s / 684s / 425s.

## Verdict

**NO TRADE-OFF OBSERVED — retention and welfare CO-MOVE (the pre-registered frontier hypothesis is
NOT supported in this world).** Every axis improves together as λ moves from engagement toward the
satisfaction proxy: retention_7d 0.9692 → 0.9960, sessions/user/day 0.8597 → 1.7058, mean hidden
satisfaction −0.4817 → −0.1224, U_s −3.942 → −1.541, trust 0.0180 → 0.0238. The pure engagement
preset (λ=0) is DOMINATED: worse on retention AND welfare simultaneously. Mechanistically this is
coherent, not a measurement artifact: the simulator's retention model (P20) is driven by hidden
satisfaction / regret / trust, and the engagement preset's extra watch-time on
high-emotional-intensity content buys no retention that its satisfaction damage does not take back —
even with the DEFAULT catalog (10% ragebait mixture weight), λ=0 serves 39% ragebait, eroding trust
and suppressing sessions.

What DOES show as pre-registered: (a) a strong "knee" — the marginal improvement collapses after
λ≈0.25–0.5, so most of the welfare recovery is available for a small weight shift away from pure
engagement; (b) the comfort-content dynamic — `arch_comfort` rises 15x (0.0018 → 0.0274) across λ,
and `satisfaction_weighted_retention` peaks at λ=0.75 (0.9976) slightly ABOVE the pure proxy
(0.9935), the one (small) non-monotonicity in the table: the comfort-leaning mid-blend converts
sessions into satisfied retained users marginally better than the pure proxy does. Tiny
`retention_7d` wiggles (0.9958 vs 0.9948, ~10 users of 10k) are below meaningful resolution;
retention_7d is near-saturated for every λ ≥ 0.25 at this horizon.

V2 §10 item 7 ("policy changes long-term satisfaction and retention") IS demonstrated — ranking
policy alone moves retention_7d by 2.7pp, sessions/day by 2x, and mean satisfaction by 0.36 on
identical worlds — but the two move in the SAME direction here. HONEST LIMITATION: a true
satisfaction-vs-retention CONFLICT would need a mechanism this simulator's retention model does not
currently have (e.g. compulsive/habit-driven return decoupled from satisfaction, or
variable-ratio-reinforcement effects); with satisfaction-driven retention, proxy-aligned ranking is
simply better on both axes. That is itself the headline finding of this scenario.
