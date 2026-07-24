# Published figures (V2) — the ReelRank realism-upgrade canonical set

Fifteen entries (sixteen PNGs — one entry pairs two images) spanning Phases 15–23 (the V2 realism
upgrade: hidden satisfaction, session dynamics, personalized diversity, event-mode serving, policy
influence on preferences/retention, ecosystem failure modes, offline evaluation, learned
multi-objective ranking). This is the V2 companion to `results/published/figures/` (the V1 §26 set,
phases 1–11) — **it does not replace that set**; both are committed side by side. Filenames are
prefixed `pNN_` by the phase whose data they come from, because several phases reuse the same
`scripts/plot_results.py` plotting function (e.g. `engagement_vs_welfare.png`,
`retention_welfare_frontier.png` are each produced multiple times across phases) and would
otherwise collide in one flat directory.

**Regenerated vs. copied.** 14 of 15 entries below are regenerated from committed
`results/published/phaseNN/**` CSVs/JSON — 12 via `scripts/plot_results.py` (run under the pinned
environment, `cd scripts && UV_PYTHON=3.12 python3 -m uv run plot_results.py ...`) and 2 via a small
bespoke script (Appendix, below) for the two figures that have no equivalent `plot_results.py`
function. 1 entry (`p20_preference_divergence_hist.png`) is **copied** from
`results/published/phase20/figures/` because its input (`hidden_preference_final.csv`, 10,000 raw
per-user hidden-preference-vector rows per arm) was never committed to `results/published` (too
large/raw for a published CSV; only the derived percentiles in `phase20/comparison.md` were
published) — there is nothing to regenerate it from in this tree. Every regenerated figure below
was produced **twice**, into separate directories, and byte-diffed (`cmp`) before being kept; all
12 `plot_results.py` regenerations and both bespoke-script figures were byte-identical across both
runs (see each entry's Regenerate line — this is the exact command run twice).

No hardware/latency caveat applies to this set (unlike V1's figures 1–3/6/10/11): every quantity
plotted here is a deterministic simulation output (rng/clock-free, D8/D9), not a wall-clock
latency/throughput measurement, so run-to-run machine contention is irrelevant to every number
shown.

**Companion deliverable**: `scripts/demo_v2.sh` — the two-worlds demo (same users/content, two
ranking objectives) referenced in the Phase 24 brief; see that script's own header.

**Reading the Regenerate lines below**: `--out <tmp>` means an arbitrary scratch output directory —
`plot_results.py`'s non-`--canonical` mode always writes *every* applicable plot for the given input
directories (not just the one this entry names), so each command below produces several PNGs and
only the one named in that entry is kept, then copied/renamed into
`results/published/figures-v2/` under its final `pNN_...` name.

---

## The figures

### 1. `p15_engagement_vs_welfare.png`
- **Shows** Scatter of engagement (reward/impression) vs. mean hidden satisfaction, one point per
  Phase 15 arm (semantic, engagement, proxy, oracle).
- **Takeaway** Engagement and hidden welfare are genuinely distinct, sometimes-opposed axes (V2 TDD
  §3.2's thesis): the engagement-optimized arm leads on reward/impression (**0.542** vs semantic's
  **0.504**, +7.5%) but posts the **lowest** hidden satisfaction of all four arms (**0.328**, even
  below semantic's 0.355) — satisfaction-proxy nearly doubles it (**0.653**, +98.9% vs engagement)
  and the oracle upper-bounds at **0.806**.
- **Source** `results/published/phase15/{semantic,engagement,proxy,oracle}/summary.json`
  (`metrics.reward_per_impression`, `welfare.mean_immediate_satisfaction`).
- **Regenerate** `cd scripts && UV_PYTHON=3.12 python3 -m uv run plot_results.py ../results/published/phase15/{semantic,engagement,proxy,oracle} --out <tmp>` (also writes reward_curve.png/alignment_curve.png/multiobjective_frontier.png as unavoidable side effects of this tool's always-write-every-applicable-plot design; keep only `engagement_vs_welfare.png`, rename with the `p15_` prefix). Byte-identical across two runs.

### 2. `p16_session_health_panel.png`
- **Shows** Two-panel figure: (left) U_s — mean session utility — per simulated round, one line per
  Phase 16 arm; (right) whole-run session-exit taxonomy (satisfied/fatigue/failure/regret/external/
  open), one 100%-stacked bar per arm.
- **Takeaway** U_s cleanly separates the engagement arm from the rest even though session dynamics
  are shared: **-1.638** (engagement) vs **1.575** (proxy), **-0.919** (semantic), **2.931**
  (oracle) — and the exit taxonomy shows why: engagement's early-failure-exit share is **32.7%**
  (vs proxy's **2.6%**, ~12.8×), while proxy's satisfied-exit share (**9.9%**) is the highest of the
  four. Raw session duration is deliberately not plotted (V2 TDD §4.9: a long ragebait-padded
  session isn't "better" than a short satisfying one) — U_s and the exit mix are the honest
  measures.
- **Source** `results/published/phase16/{semantic,engagement,proxy,oracle}/session_health.csv`
  (round, mean_session_utility) and `.../summary.json` (`session_health.exit_type_shares`).
- **Regenerate** bespoke script (Appendix) — `plot_results.py` has no session-health plotting
  function; see Appendix for why and the full script. Byte-identical across two runs.

### 3. `p17_cohort_panel.png`
- **Shows** Grouped bar chart: U_s (mean session utility), fixed vs. personalized diversity, one
  pair of bars per Phase 17 trait cohort (focused, novelty_seeker, creator_loyal, easily_fatigued),
  annotated with the personalized-minus-fixed delta.
- **Takeaway** Personalized diversity beats fixed on U_s for **all four** cohorts — **focused**
  +0.068 (+15.4%), **novelty_seeker** +0.094 (+14.6%), **easily_fatigued** +0.085 (+15.8%),
  **creator_loyal** +0.005 (+1.9%, smallest gain) — matching the Tier-2 acceptance criterion
  (personalized ≥ fixed on U_s for focused and easily_fatigued) and extending it to all four.
- **Source** `results/published/phase17/{focused,noveltyseeker,creatorloyal,easilyfatigued}-{fixed,personalized}/summary.json` (`session_health.mean_session_utility`).
- **Regenerate** bespoke script (Appendix). Byte-identical across two runs.

### 4. `p19_freshness_cost_frontier.png`
- **Shows** Scatter of ranking cost (candidates scored, whole run) vs. freshness (mean adaptation
  delay after drift, interactions), one point per Phase 19 batch-depth arm (1/3/10/20).
- **Takeaway** The frontier is **not monotonic**: batch20 (cheapest, 207.9M ranking computations —
  79.4% less than batch1's 1011.6M) reaches almost the same freshness (**4.082**) as batch1
  (**4.062**), while the intermediate depths are paradoxically *worse* on freshness (batch3
  **4.227**, batch10 **4.249**) — deeper prefetch batching is not simply a freshness-for-cost
  trade at this drift magnitude; batch20 is close to a free lunch relative to batch3/10.
- **Source** `results/published/phase19/{batch1,batch3,batch10,batch20}/summary.json`
  (`event_mode.serving.ranking_computations`, `event_mode.serving.adaptation_delay.mean_interactions`).
- **Regenerate** `cd scripts && UV_PYTHON=3.12 python3 -m uv run plot_results.py ../results/published/phase19/{batch1,batch3,batch10,batch20} --out <tmp>` (keep `freshness_cost_frontier.png`). Byte-identical across two runs.

### 5. `p20_preference_divergence_hist.png`  — COPIED (not regenerated)
- **Shows** Histogram of per-user `1 - cos(sem_v_engagement, sem_v_proxy)` over 10,000 matched users
  (same world/seed, engagement-on vs. proxy-on).
- **Takeaway** Mean divergence **0.0388** (median 0.0177, p90 0.106) over all 10,000 matched users —
  the SAME users, same world, same seed end up with measurably different hidden semantic
  preferences purely as a function of which ranking policy served them (Phase 20's Tier-4
  acceptance-1 headline finding).
- **Source/provenance** `results/published/phase20/figures/preference_divergence_hist.png`, copied
  verbatim (byte-for-byte) from the Phase 20 publication. **Not regenerable in this tree**: its
  input, `hidden_preference_final.csv` (10,000 raw per-user vector rows per arm), was intentionally
  not committed to `results/published` (only the derived percentiles in
  `results/published/phase20/comparison.md` were published) — see that file's Headline 1.
- **Copy command** `cp results/published/phase20/figures/preference_divergence_hist.png results/published/figures-v2/p20_preference_divergence_hist.png`

### 6. `p20_trust_trajectory.png`
- **Shows** Mean platform trust per simulated day, one line per Phase 20 arm (engagement-on,
  engagement-off, proxy-on, proxy-off).
- **Takeaway** Both "-on" (evolution-enabled) arms collapse toward a shared near-zero trust floor
  by day 9 (engagement-on **0.018**, proxy-on **0.024**) while both "-off" (evolution-disabled,
  static-trust) counterfactuals hold flat at **0.704** throughout — the trust collapse is an
  ENDOGENOUS-preference-evolution effect, not a retention-mechanics effect (the two "-off" lines
  are visually indistinguishable, confirming they share the same static-trust reference by
  construction).
- **Source** `results/published/phase20/{engagement-on,engagement-off,proxy-on,proxy-off}/longterm_metrics.csv` (`day`, `mean_trust`).
- **Regenerate** `cd scripts && UV_PYTHON=3.12 python3 -m uv run plot_results.py ../results/published/phase20/{engagement-on,engagement-off,proxy-on,proxy-off} --phase20 ../results/published/phase20/engagement-on ../results/published/phase20/proxy-on --out <tmp>` (keep `trust_trajectory.png`). Byte-identical across two runs.

### 7. `p20_retention_by_day.png`
- **Shows** Active users and session counts per simulated day, one pair of lines per Phase 20 arm.
- **Takeaway** engagement-on has both the lowest active-user count and the lowest session count of
  the four arms from day 1 onward; proxy-off (the evolution-off, high-static-trust counterfactual)
  sustains far more sessions/day throughout (~59k–64k vs. engagement-on's ~6k–10k) — matching the
  retention_7d point estimates (engagement-on **0.9692** vs. proxy-off **0.9998**). Every arm's
  curves converge toward the horizon boundary at day 9 (a near-empty final-day artifact of the
  777,600s = 9.0-day horizon, the same boundary-row effect documented in
  `results/published/phase21/ECOSYSTEM.md`'s method notes) — read the day 0–8 trend, not the day-9
  endpoint.
- **Source** `results/published/phase20/{engagement-on,engagement-off,proxy-on,proxy-off}/longterm_metrics.csv` (`day`, `active_users`, `sessions`).
- **Regenerate** same command as figure 6 (keep `retention_by_day.png` instead). Byte-identical across two runs.

### 8. `p21_creator_hhi_by_day_popularity_feedback.png`
- **Shows** Creator-concentration HHI per simulated day, control vs. popularity-heavy ranking
  (Phase 21 `popularity_feedback` scenario).
- **Takeaway** Honest non-confirmation, exactly per `ECOSYSTEM.md`'s pre-registered verdict
  (PARTIAL): whole-run HHI is actually *lower* under the popularity-heavy arm (**0.0113**) than
  control (**0.0127**) — wrong direction on 8 of 9 days. **Read days 0–8 only**: day 9 is a
  near-empty boundary row (1 impression for control, 0 for popularity — confirmed directly from
  `ecosystem_metrics.csv`), which trivially produces a spurious HHI=1.0 single-creator spike for
  control that this auto-generated plot does not filter out (the published `comparison.md`
  analysis filters day rows by the `impressions` column before reporting; this raw regeneration
  does not). The popularity loop's real, confirmed signature is tail-creator starvation (tail share
  **0.053** vs. control's **0.143**), not rising HHI — visible in `ecosystem_metrics.csv`'s
  `tail_creator_share` column, not in this figure.
- **Source** `results/published/phase21/popularity_feedback/{control,popularity}/ecosystem_metrics.csv` (`day`, `creator_hhi`).
- **Regenerate** `cd scripts && UV_PYTHON=3.12 python3 -m uv run plot_results.py ../results/published/phase21/popularity_feedback/{control,popularity} --out <tmp>` (keep `creator_hhi_by_day.png`). Byte-identical across two runs.

### 9. `p21_archetype_share_by_day_ragebait_engagement.png` + `p21_archetype_share_by_day_ragebait_proxy.png`
- **Shows** Stacked archetype-mix-by-day (8 hidden archetypes), one PNG per arm (engagement vs.
  proxy), Phase 21 `ragebait_amplification` scenario — plotted as a pair since the contrast IS the
  finding.
- **Takeaway** The suite's strongest, **test-enforced** verdict (SUPPORTED,
  `RagebaitAmplificationStatisticalTest`): the engagement arm's ragebait share dominates the mix
  every day (whole-run mean **69.0%**), while proxy's is **0.0%** every day. Mean hidden
  satisfaction: **-0.559** (engagement) vs. **-0.156** (proxy); U_s: **-4.32** vs. **-1.74**;
  regret-classified exits **0.59%** vs. **0.00%** — the harm shows up in welfare/regret, not just
  the archetype mix itself.
- **Source** `results/published/phase21/ragebait_amplification/{engagement,proxy}/ecosystem_metrics.csv` (`day`, `arch_*` columns).
- **Regenerate** `cd scripts && UV_PYTHON=3.12 python3 -m uv run plot_results.py ../results/published/phase21/ragebait_amplification/{engagement,proxy} --out <tmp>` (keep both `archetype_share_by_day_engagement.png` and `archetype_share_by_day_proxy.png`). Byte-identical across two runs (both files).

### 10. `p21_entropy_by_day_filter_bubble.png`
- **Shows** Mean preference entropy per simulated day, control vs. exploit-only ("bubble") ranking
  (Phase 21 `filter_bubble` scenario).
- **Takeaway** Entropy **rises** in BOTH arms across the run (**2.836 → 2.852**, bubble tracking
  fractionally below control throughout) — confirming `ECOSYSTEM.md`'s explicit "not observed" /
  method-note finding: at this operating point, Phase-20 saturation/aversion pushes users off
  exhausted channels regardless of policy, so entropy is **not a usable bubble-detection signal**
  here. The bubble scenario's real, confirmed signature is niche-content-exposure starvation
  (**0.065** vs. control's **0.120** share) — visible only in exposure metrics, never in this
  entropy figure. Included deliberately as the suite's honesty callout, not a positive result.
- **Source** `results/published/phase21/filter_bubble/{control,bubble}/longterm_metrics.csv` (`day`, `mean_preference_entropy`).
- **Regenerate** `cd scripts && UV_PYTHON=3.12 python3 -m uv run plot_results.py ../results/published/phase21/filter_bubble/{control,bubble} --out <tmp>` (keep `entropy_by_day.png`). Byte-identical across two runs.

### 11. `p21_retention_welfare_frontier_creator_overconcentration.png`
- **Shows** Scatter of retention_7d vs. mean hidden satisfaction, uncapped creator-affinity-heavy
  ranking vs. diversity-capped (≤2/creator), Phase 21 `creator_overconcentration` scenario.
- **Takeaway** VERDICT: CONFIRMED — a rare win-win. retention_7d is essentially tied (**0.99420**
  uncapped vs. **0.99390** capped, a 0.03-point difference) while mean hidden satisfaction differs
  sharply in the capped arm's favor (**-0.259** vs. **-0.280**); capped is also less concentrated
  (whole-run HHI **0.0180** vs. **0.0205**) and slightly better on session utility (U_s **-2.64**
  vs. **-2.72**). Diversity caps buy lower concentration AND better welfare simultaneously, at
  essentially no retention cost.
- **Source** `results/published/phase21/creator_overconcentration/{affinity,capped}/summary.json` (`long_term.retention_7d`, `welfare.mean_immediate_satisfaction`).
- **Regenerate** `cd scripts && UV_PYTHON=3.12 python3 -m uv run plot_results.py ../results/published/phase21/creator_overconcentration/{affinity,capped} --out <tmp>` (keep `retention_welfare_frontier.png`). Byte-identical across two runs.

### 12. `p22_offline_auc.png`
- **Shows** Grouped bar chart of held-out AUC, learned model vs. 3 baselines (global frequency,
  per-source frequency, served-score), one group per binary target (temporal split).
- **Takeaway** The learned model beats all 3 baselines on 5 of 6 binary targets: `completed`
  **0.714** (best baseline 0.628), `not_interested` **0.714** (0.651), `session_exit` **0.664**
  (0.630), `liked` **0.604** (0.588), `shared` **0.563** (0.556). `followed` is a near coin-flip
  (**0.501**) — a low-base-rate (1.5%), high-variance target, read as directional only.
- **Source** `results/published/phase22/models-temporal/training_eval.csv`.
- **Regenerate** `cd scripts && UV_PYTHON=3.12 python3 -m uv run plot_results.py ../results/published/phase22/logworld --phase22 ../results/published/phase22/models-temporal/training_eval.csv --out <tmp>` (the positional `logworld` dir is required by `plot_results.py`'s `main()` before it reaches the `--phase22` branch, and also emits that single run's own reward_curve.png/etc. as a side effect — discard those, keep `offline_auc.png`). Byte-identical across two runs.

### 13. `p22_calibration_session_exit.png`
- **Shows** Reliability diagram (predicted vs. actual, 10 equal-count bins, marker area ∝ bin
  count) for the learned `session_exit` model, temporal split, n=20,887 held-out rows.
- **Takeaway** Reasonably well-calibrated, not perfectly: the curve sits slightly above the y=x
  reference through the low-to-mid bins (e.g. predicted 0.15 → actual 0.18; predicted 0.29 → actual
  0.32 — mild under-confidence, the model under-states exit risk in that range) before crossing
  back below the line at the top bin (predicted 0.50 → actual 0.455). Chosen as "the" calibration
  figure (of 8 available targets) for its narrative continuity with the P16/P17 session-health
  story and its strong paired AUC advantage (figure 12).
- **Source** `results/published/phase22/calibration/models-temporal/calibration-session_exit.csv`.
- **Regenerate** `cd scripts && UV_PYTHON=3.12 python3 -m uv run plot_results.py ../results/published/phase22/logworld --phase22 ../results/published/phase22/models-temporal/training_eval.csv --phase22-calibration ../results/published/phase22/calibration/models-temporal --out <tmp>` (writes all 8 `calibration_<target>.png`; keep only `calibration_session_exit.png`). Byte-identical across two runs.

### 14. `p23_multiobjective_frontier.png`
- **Shows** Scatter of engagement (reward/impression) vs. mean hidden satisfaction, marker area ∝
  retention_7d, one point per Phase 23 arm (hand_tuned, semantic, learned, learned_survey_off,
  w_sat_100, w_sat_70, w_watch_70, w_watch_100, w_watch_100_noexit).
- **Takeaway** The learned multi-objective ranker dominates hand-tuned even in its balanced
  configuration (reward/impression **0.404** vs. **0.399**, mean hidden satisfaction **-0.202** vs.
  **-0.298**) — and `w_sat_100` (weight swept toward satisfaction) is best on **both** plotted axes
  simultaneously (reward **0.421**, satisfaction **-0.149**), the monotone frontier the phase's
  weight sweep was designed to expose. The pure-semantic control is dominated on both axes
  (**0.249** / **-0.399**).
- **Source** `results/published/phase23/{hand_tuned,semantic,learned,learned_survey_off,w_sat_100,w_sat_70,w_watch_70,w_watch_100,w_watch_100_noexit}/summary.json` (`metrics.reward_per_impression`, `welfare.mean_immediate_satisfaction`, `long_term.retention_7d`).
- **Regenerate** `cd scripts && UV_PYTHON=3.12 python3 -m uv run plot_results.py ../results/published/phase23/{hand_tuned,semantic,learned,learned_survey_off,w_sat_100,w_sat_70,w_watch_70,w_watch_100,w_watch_100_noexit} --out <tmp>` (keep `multiobjective_frontier.png`). Byte-identical across two runs.

### 15. `p23_offline_closedloop_gap.png` — bonus (beyond the Phase 24 brief's explicit P23 figure list)
- **Shows** Grouped bar chart, one group per (learned arm × offline target): offline held-out
  AUC/RMSE point estimate vs. the aligned closed-loop delta (vs. hand_tuned baseline) on the most
  related summary.json metric.
- **Takeaway** Offline held-out gains largely translate into real closed-loop gains for the SAME
  arm: e.g. the `learned` arm's `completed`-AUC edge (0.715 vs. best-baseline 0.665) co-occurs with
  a positive closed-loop reward/impression delta (+0.0055 vs. hand_tuned); across the 9-arm matrix,
  arms with more/larger "beats=all" offline targets tend to show larger positive closed-loop
  deltas on the aligned metric — the closest thing in this repo to an offline-training-validity
  check. Added beyond the brief's literal ask (which named only `multiobjective_frontier`) because
  it is a zero-marginal-cost addition to the same `plot_results.py` invocation and directly extends
  the "final phase" offline-vs-closed-loop narrative; flagged here explicitly as an addition, not
  hidden.
- **Source** `results/published/phase23/gap_analysis.csv`.
- **Regenerate** same command as figure 14, plus `--phase23 ../results/published/phase23/gap_analysis.csv` (keep `offline_closedloop_gap.png`). Byte-identical across two runs.

---

## Appendix: bespoke regeneration script (figures 2 and 3)

`scripts/plot_results.py` has no plotting function for session-health exit taxonomy/U_s (grepped:
no `session_health`/`exit_reason`/`U_s`-panel function exists there — confirmed by searching every
PNG ever produced anywhere in this repo, none matches) or for a fixed-vs-personalized cohort panel.
Phase 24 Package B's touch-list is scoped to `results/published/figures-v2/` (PNGs + this README)
and `scripts/demo_v2.sh` — it does not include `scripts/plot_results.py` — so these two figures are
produced by the standalone script below instead of a new committed function. **This script is
intentionally not committed anywhere in the repo**; it reads only already-committed
`results/published/phase16|17/**/{session_health.csv,summary.json}` (no gitignored/raw data), so
both figures are genuine regenerations, not copies. To reproduce: save the block below as
`bespoke_figures.py` and run (from `scripts/`, to reuse the pinned uv/Python-3.12/matplotlib
environment):

```sh
cd scripts && UV_PYTHON=3.12 python3 -m uv run /path/to/bespoke_figures.py /path/to/reel-rank <outdir>
```

This single invocation writes both `p16_session_health_panel.png` and `p17_cohort_panel.png` to
`<outdir>` (confirmed byte-identical across two separate runs).

```python
#!/usr/bin/env python3
"""Bespoke figures-v2 generator for the two V2 canonical figures that have NO equivalent function
in scripts/plot_results.py (P16 session-health exit taxonomy + U_s panel; P17 fixed-vs-personalized
cohort panel). Package B, Phase 24: plot_results.py is out of this package's touch-list (figures +
demo only), so these two figures are produced by this small standalone script instead of adding new
functions there. Reads ONLY already-committed results/published/phaseNN/**/{session_health.csv,
summary.json} -- no gitignored/raw data -- so both figures are genuinely REGENERATED, not copied.
Styled to match scripts/plot_results.py's conventions (FIGSIZE, DPI, GRID_ALPHA, tab10 COLOR_CYCLE,
grid+legend+tight_layout savefig pattern) for visual consistency with the rest of the canonical set.

Usage: python3 bespoke_figures.py <repo_root> <outdir>
Writes: <outdir>/p16_session_health_panel.png, <outdir>/p17_cohort_panel.png
Deterministic: pure function of committed CSV/JSON content, no rng, no wall-clock-derived layout.
"""
import json
import sys
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import pandas as pd

FIGSIZE = (8, 5)
DPI = 150
GRID_ALPHA = 0.3
COLOR_CYCLE = plt.get_cmap("tab10").colors


def finish_axes(ax, xlabel, ylabel, title):
    ax.set_xlabel(xlabel)
    ax.set_ylabel(ylabel)
    ax.set_title(title)
    ax.grid(alpha=GRID_ALPHA)
    handles, labels = ax.get_legend_handles_labels()
    if len(handles) > 1:
        ax.legend(handles, labels, fontsize=8)


# --- P16: session-health exit taxonomy + U_s panel ----------------------------------------------
# V2 TDD §4.9/§6 (session utility U_s, exit taxonomy); source: results/published/phase16/<arm>/
# {session_health.csv, summary.json}, arms = semantic/engagement/proxy/oracle (same 4 arms/order as
# scripts/plot_results.py's own Phase 15/16 calls).
_P16_ARMS = ["semantic", "engagement", "proxy", "oracle"]
_EXIT_CATEGORIES = ["satisfied", "fatigue", "failure", "regret", "external", "open"]


def make_p16(repo_root: Path, outdir: Path) -> None:
    ph16 = repo_root / "results" / "published" / "phase16"
    fig, (ax_left, ax_right) = plt.subplots(1, 2, figsize=(15, 5.2))

    # Left: U_s (mean_session_utility) per simulated round, one line per arm -- mirrors
    # plot_results.py's plot_creator_hhi_by_day (line-per-arm-from-a-per-round-CSV) convention.
    for i, arm in enumerate(_P16_ARMS):
        csv_path = ph16 / arm / "session_health.csv"
        df = pd.read_csv(csv_path)
        ax_left.plot(df["round"], df["mean_session_utility"], color=COLOR_CYCLE[i], label=arm)
    ax_left.axhline(0, color="0.6", linestyle=":", linewidth=1.0, zorder=1)
    finish_axes(ax_left, "round", "U_s (mean session utility)",
                "Session Utility (U_s) by Round")

    # Right: whole-run exit-type-share 100%-stacked bar, one bar per arm, segments = the V2 §4.8
    # exit taxonomy -- mirrors plot_results.py's plot_archetype_share_by_day (stacked composition)
    # convention, read from each arm's summary.json session_health.exit_type_shares (whole-run
    # aggregate; "open" = still-open-at-run-end sessions, RunEnded, excluded from the other
    # exit-rate denominators per SessionExitType::RunEnded's documented convention).
    bottoms = [0.0] * len(_P16_ARMS)
    xs = range(len(_P16_ARMS))
    for ci, cat in enumerate(_EXIT_CATEGORIES):
        heights = []
        for arm in _P16_ARMS:
            summary = json.load(open(ph16 / arm / "summary.json"))
            shares = summary["session_health"]["exit_type_shares"]
            heights.append(shares.get(cat, 0.0))
        ax_right.bar(xs, heights, bottom=bottoms, color=COLOR_CYCLE[ci % len(COLOR_CYCLE)],
                     label=cat, width=0.6, zorder=3)
        bottoms = [b + h for b, h in zip(bottoms, heights)]
    ax_right.set_xticks(list(xs))
    ax_right.set_xticklabels(_P16_ARMS)
    ax_right.set_ylim(0.0, 1.02)
    finish_axes(ax_right, "arm", "share of closed sessions (+ open)",
                "Session-Exit Taxonomy (whole-run)")

    fig.suptitle("Phase 16 -- Session Health: U_s Trajectory + Exit Taxonomy", fontsize=12)
    fig.tight_layout(rect=(0, 0, 1, 0.96))
    fig.savefig(outdir / "p16_session_health_panel.png", dpi=DPI)
    plt.close(fig)


# --- P17: fixed-vs-personalized cohort panel ------------------------------------------------------
# V2 TDD §4.10/§6 Tier-2 headline ("personalized-vs-fixed U_s delta per cohort"); source:
# results/published/phase17/<cohort-slug>-{fixed,personalized}/summary.json session_health.
# mean_session_utility. Directory slugs drop underscores (creatorloyal, easilyfatigued,
# noveltyseeker); display names restore them to match phase17/comparison.md's own cohort names.
_P17_COHORTS = [
    ("focused", "focused"),
    ("noveltyseeker", "novelty_seeker"),
    ("creatorloyal", "creator_loyal"),
    ("easilyfatigued", "easily_fatigued"),
]


def make_p17(repo_root: Path, outdir: Path) -> None:
    ph17 = repo_root / "results" / "published" / "phase17"
    fixed_vals, personalized_vals, labels = [], [], []
    for slug, display in _P17_COHORTS:
        fixed = json.load(open(ph17 / f"{slug}-fixed" / "summary.json"))
        personalized = json.load(open(ph17 / f"{slug}-personalized" / "summary.json"))
        fixed_vals.append(fixed["session_health"]["mean_session_utility"])
        personalized_vals.append(personalized["session_health"]["mean_session_utility"])
        labels.append(display)

    fig, ax = plt.subplots(figsize=FIGSIZE)
    x = list(range(len(labels)))
    width = 0.35
    ax.bar([xi - width / 2 for xi in x], fixed_vals, width=width, color=COLOR_CYCLE[0],
           label="fixed", zorder=3)
    ax.bar([xi + width / 2 for xi in x], personalized_vals, width=width, color=COLOR_CYCLE[1],
           label="personalized", zorder=3)
    for xi, f, p in zip(x, fixed_vals, personalized_vals):
        delta = p - f
        pct = (delta / abs(f) * 100.0) if f != 0 else float("nan")
        ax.annotate(f"{delta:+.3f}\n({pct:+.1f}%)", (xi, max(f, p)),
                    textcoords="offset points", xytext=(0, 6), ha="center", fontsize=7.5)
    ax.axhline(0, color="0.6", linestyle=":", linewidth=1.0, zorder=1)
    ax.set_xticks(x)
    ax.set_xticklabels(labels)
    ax.margins(y=0.18)
    finish_axes(ax, "trait cohort", "U_s (mean session utility)",
                "Personalized vs. Fixed Diversity: U_s per Cohort")
    fig.tight_layout()
    fig.savefig(outdir / "p17_cohort_panel.png", dpi=DPI)
    plt.close(fig)


def main() -> int:
    repo_root = Path(sys.argv[1])
    outdir = Path(sys.argv[2])
    outdir.mkdir(parents=True, exist_ok=True)
    make_p16(repo_root, outdir)
    make_p17(repo_root, outdir)
    print(f"bespoke_figures: wrote p16_session_health_panel.png, p17_cohort_panel.png to {outdir}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
```
