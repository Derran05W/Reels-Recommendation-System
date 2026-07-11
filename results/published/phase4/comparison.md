# Phase 4 — Baseline comparison (configs/small.json, seed 42)

Three baseline recommenders run end-to-end through the Phase 4 evaluation harness
(`apps/simulate`, Release build): 1000 users x 10000 reels, 50 interactions/user
(5000 requests of feed size 10, 50000 impressions), identical master seed and rng streams.

| metric | random | popularity | exact_vector |
|---|---|---|---|
| mean_true_affinity | 0.0451 | 0.0536 | 0.1058 |
| reward_per_impression | -0.0249 | 0.0153 | 0.0199 |
| reward_per_session | -0.3636 | 0.2249 | 0.2894 |
| mean_watch_ratio | 0.3025 | 0.3569 | 0.3487 |
| mean_watch_seconds | 11.5986 | 8.4888 | 16.6092 |
| completion_rate | 0.2681 | 0.3246 | 0.3100 |
| instant_skip_rate | 0.6082 | 0.5547 | 0.5706 |
| like_rate | 0.0503 | 0.0590 | 0.0600 |
| share_rate | 0.0188 | 0.0218 | 0.0220 |
| follow_rate | 0.0071 | 0.0081 | 0.0084 |
| mean_session_length | 14.5815 | 14.6671 | 14.5560 |
| oracle_mean_regret | 0.7017 | 0.6921 | 0.6453 |
| oracle_cumulative_regret | 883.4361 | 871.3482 | 812.4131 |

Notes:
- Exact personalization beats random on mean true affinity (0.106 vs 0.045, 2.3x) and
  reward per impression (+0.020 vs -0.025) — the Phase 4 exit-criterion comparison.
  Ordering exact > popularity > random holds on affinity, reward, and oracle regret.
- Popularity's higher completion rate (0.325) vs exact (0.310) is expected: it surfaces
  intrinsically high-quality reels that complete well for everyone, while exact_vector
  optimizes affinity, which drives likes/shares/reward rather than raw completion.
- Baselines run with STATIC cold-start estimates (estimatedPreference = global average
  preference, TDD 11.1); no online learning until Phase 7, so learning_curve.csv is flat
  by design and exact_vector is a degraded (unpersonalized-prior) ceiling this phase.
- Oracle regret is measured in TRUE-AFFINITY units on a 25% Bernoulli sample of requests
  (1259/5000 sampled, identical across algorithms because the oracle rng stream is
  independent); see regret_units_note in each summary.json.
- Full per-run output (config.json, summary.json, per-round CSVs, latency, metadata.json
  with reel-rank+vector-db SHAs, build, hardware) in the three run directories alongside
  this file. retrieval_metrics.csv / diversity_metrics.csv are N/A until Phases 5 / 9.
