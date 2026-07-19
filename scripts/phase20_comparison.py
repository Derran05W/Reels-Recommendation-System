#!/usr/bin/env python3
"""phase20_comparison.py — Phase 20 Tier-4 acceptance experiment 1 comparison (V2 TDD §4.15-4.17,
§6, plan Phase 20 task 5, contracts §6): the POLICY-INFLUENCE experiment, as produced by
scripts/run_phase20_experiment.sh's four-arm matrix (engagement-on / engagement-off / proxy-on /
proxy-off, all on `configs/realism-medium-retention.json`, algorithm hnsw_ranker, seed 42).

Reuses `phase15_comparison.py` (imported directly -- design decision D15: this script contains no
simulation logic, only reads result files and renders tables) for `_get`/`_is_num`/`_read_json`/
`warn`/`fmt`, and `phase17_comparison.py` (imported directly) for its `render_table` passthrough --
the same sys.path-relative import pattern scripts/phase19_comparison.py established.

Every summary.json/CSV key this script reads is quoted VERBATIM from `docs/design/P20-CONTRACTS.md`
§5 (the frozen long-term schema Package B implements and this package consumes) -- NO candidate-path
guessing this phase (contracts §6 is explicit about this, unlike the pre-integration CANDIDATES
dance phase16/17/19_comparison.py had to do for schemas that were not yet frozen at the time those
scripts were written). Package B's ACTUAL behaviour is not available in this worktree (packages A/B
are no-op stubs here -- PreferenceEvolution/RetentionModel never mutate hidden state, so a real run
cannot be produced from this tree; this script is tooling built against the frozen schema for the
integrator to run post-merge), so every read is defensive on PRESENCE, never on a guessed rename:
  - `hidden_preference_final.csv` (contracts §5, "gate-on only, evaluation carve-out"): read by
    trying to open it at `<arm-dir>/hidden_preference_final.csv` and checking the frozen fixed
    columns are present; ABSENT (file missing, or missing a required column) is treated as "this
    arm's per-user export is not available", never as an error. This is normal and EXPECTED for
    every `-off` arm this script's own run_phase20_experiment.sh produces (they set BOTH
    `realism.preference_evolution=false` AND `retention.enabled=false`, so
    `long_term.configured` -- the same gate that controls this export -- is false).
  - `summary.json`'s `long_term` block: read the same way -- BLOCK ABSENCE (not a per-key guess) is
    what distinguishes a gate-off arm; a present block's individual keys are read by their exact §5
    names and a key genuinely missing from an otherwise-present block is reported distinctly.

ONE SCAFFOLD-VS-CONTRACT DISCREPANCY FLAGGED (not adapted around silently, per this package's
instructions): contracts §5 lists the `long_term` summary.json block as mirroring the
`LongTermReport` struct with EXACTLY these snake_case keys: `retention_1d`, `retention_7d`,
`sessions_per_user_per_day`, `satisfaction_weighted_retention`, `churn_rate`,
`mean_churn_probability`, `mean_final_trust`, `mean_final_habit`,
`mean_preference_shift_from_initial`, `mean_final_preference_entropy`, plus `note`. The scaffold's
ACTUAL `src/evaluation/results_writer.cpp` (this worktree, function `writeSummaryJson`, the
`if (result.longTerm.configured)` block) additionally emits `retention_configured` (mirroring
`LongTermReport::retentionConfigured`) as an 11th key not in that literal list. This is read here
defensively as a BONUS/optional field (never required, never assumed present) and reported in the
"Arms and data availability" section when present -- see `Arm.retention_configured_hint()`. It does
not conflict with anything (purely additive, D22), but the integrator should be aware the emitted
block has one more key than contracts §5's literal enumeration names.

This reads each arm's summary.json / config.json / hidden_preference_final.csv and writes:

  comparison.csv   one row per arm (4 rows), one column per §5/welfare/session_health/engagement
                   metric this script reports (the "retention/churn/trust/engagement across all
                   four arms" table -- item 3 of this package's brief).
  comparison.md    two headlines (policy divergence -- exit criterion 1; per-policy distortion vs.
                   the gate-off counterfactual), an arms/data-availability section, the four-group
                   metric table (long-term / hidden welfare / session health / engagement), and
                   notes (D22 no-aggregate-score, concurrency caveat, drift-removed design note,
                   off-twin-absence-by-design explanation).

Arm order is always engagement-on, engagement-off, proxy-on, proxy-off (deterministic, regardless
of CLI argument order or which arms are available) -- a missing/unreadable arm still gets a row, with
every cell reported as unavailable rather than the row being silently dropped (same contract as
phase15/16/17/19_comparison.py).

Usage
-----
    python3 scripts/phase20_comparison.py \\
        --engagement-on <dir> --engagement-off <dir> --proxy-on <dir> --proxy-off <dir> \\
        [--out results/published/phase20] [--precision 6]

    python3 scripts/phase20_comparison.py --self-test
        Runs an in-script synthetic-fixture smoke test of the matching/cosine math and the
        renderers (tempdir fixtures, nothing committed) and exits; ignores every other argument.

    (No third-party dependencies -- plain python3 is enough.)

Exit status: 0 if at least one of the four arms had a readable summary.json (or --self-test passed),
1 otherwise.
"""
from __future__ import annotations

import argparse
import csv
import json
import math
import sys
import tempfile
from pathlib import Path
from typing import Optional

# Make sibling-module import work regardless of CWD (scripts/ is not a package -- these scripts are
# plain, independently-runnable files per D15/TDD §22, so this is a straight file-relative import).
sys.path.insert(0, str(Path(__file__).resolve().parent))
import phase15_comparison as p15  # noqa: E402  (see sys.path.insert above)
import phase17_comparison as p17  # noqa: E402  (render_table passthrough only)

ARMS = ["engagement-on", "engagement-off", "proxy-on", "proxy-off"]
POLICIES = ["engagement", "proxy"]

NA_ARM = "n/a (arm not provided)"
NA_GATE_OFF = "n/a (gate off)"

# Frozen hidden_preference_final.csv fixed columns (contracts §5); sem_v0..sem_v{D-1} follow and D
# is read from the header, never assumed.
HIDDEN_PREF_REQUIRED_COLUMNS = [
    "user_id", "plasticity", "churned", "sem_shift", "visual_shift", "music_shift", "emotional_shift",
]

# config.hpp RankingConfig defaults (verified directly against include/rr/infrastructure/config.hpp
# in this worktree) -- used only for the "non-default ranking weights" provenance display in the
# Arms section, an independent cross-check (vs. hardcoding what run_phase20_experiment.sh's patch
# dicts ought to produce) that a policy patch actually landed in an arm's resolved config.json.
DEFAULT_RANKING: dict = {
    "similarity_weight": 0.50,
    "quality_weight": 0.10,
    "freshness_weight": 0.08,
    "popularity_weight": 0.07,
    "trending_weight": 0.08,
    "creator_affinity_weight": 0.07,
    "exploration_weight": 0.05,
    "repetition_penalty": 0.15,
    "duration_match_weight": 0.05,
    "impression_penalty_weight": 0.05,
    "session_topic_weight": 0.05,
    "freshness_half_life_seconds": 604800.0,
    "trending_half_life_seconds": 21600.0,
    "visual_match_weight": 0.0,
    "music_match_weight": 0.0,
    "emotional_match_weight": 0.0,
    "clickbait_weight": 0.0,
    "emotional_intensity_weight": 0.0,
    "usefulness_weight": 0.0,
    "production_quality_weight": 0.0,
    "information_density_weight": 0.0,
    "language_match_weight": 0.0,
    "save_popularity_weight": 0.0,
}

# --- Report column groups (contracts §5 for long_term; verified-real §6 keys elsewhere in this
# worktree's src/evaluation/results_writer.cpp for welfare/session_health/engagement -- see this
# file's module docstring). (column key, header, exact summary.json path). ------------------------
LONG_TERM_COLUMNS: list[tuple[str, str, tuple]] = [
    ("retention_1d", "retention (1d)", ("long_term", "retention_1d")),
    ("retention_7d", "retention (7d)", ("long_term", "retention_7d")),
    ("sessions_per_user_per_day", "sessions/user/day", ("long_term", "sessions_per_user_per_day")),
    ("satisfaction_weighted_retention", "satisfaction-weighted retention",
     ("long_term", "satisfaction_weighted_retention")),
    ("churn_rate", "churn rate", ("long_term", "churn_rate")),
    ("mean_churn_probability", "mean churn probability", ("long_term", "mean_churn_probability")),
    ("mean_final_trust", "mean final trust", ("long_term", "mean_final_trust")),
    ("mean_final_habit", "mean final habit", ("long_term", "mean_final_habit")),
    ("mean_preference_shift_from_initial", "mean pref shift from initial (within-world)",
     ("long_term", "mean_preference_shift_from_initial")),
    ("mean_final_preference_entropy", "mean final preference entropy",
     ("long_term", "mean_final_preference_entropy")),
]

WELFARE_COLUMNS: list[tuple[str, str, tuple]] = [
    ("mean_immediate_satisfaction", "mean hidden satisfaction",
     ("welfare", "mean_immediate_satisfaction")),
    ("mean_regret", "mean hidden regret", ("welfare", "mean_regret")),
    ("satisfaction_per_minute", "satisfaction per minute", ("welfare", "satisfaction_per_minute")),
    ("platform_trust", "welfare trust (platform_trust)", ("welfare", "platform_trust")),
]

SESSION_HEALTH_COLUMNS: list[tuple[str, str, tuple]] = [
    ("sh_sessions", "sessions (closed)", ("session_health", "sessions")),
    ("mean_session_utility", "mean session utility U_s", ("session_health", "mean_session_utility")),
    ("early_failure_exit_rate", "early-failure exit rate",
     ("session_health", "early_failure_exit_rate")),
    ("natural_completion_rate", "natural completion rate",
     ("session_health", "natural_completion_rate")),
    ("harmful_fatigue_mean", "harmful fatigue (mean)", ("session_health", "harmful_fatigue_mean")),
    ("exit_share_satisfied", "exit share: satisfied",
     ("session_health", "exit_type_shares", "satisfied")),
    ("exit_share_fatigue", "exit share: fatigue", ("session_health", "exit_type_shares", "fatigue")),
    ("exit_share_regret", "exit share: regret", ("session_health", "exit_type_shares", "regret")),
]

ENGAGEMENT_COLUMNS: list[tuple[str, str, tuple]] = [
    ("reward_per_impression", "reward/impression", ("metrics", "reward_per_impression")),
    ("mean_watch_seconds", "mean watch seconds", ("metrics", "mean_watch_seconds")),
    ("completion_rate", "completion rate", ("metrics", "completion_rate")),
    ("like_rate", "like rate", ("metrics", "like_rate")),
    ("share_rate", "share rate", ("metrics", "share_rate")),
    ("follow_rate", "follow rate", ("metrics", "follow_rate")),
    ("comment_rate", "comment rate", ("metric_groups", "engagement", "comment_rate")),
    ("save_rate", "save rate", ("metric_groups", "engagement", "save_rate")),
    ("profile_visit_rate", "profile-visit rate", ("metric_groups", "engagement", "profile_visit_rate")),
]

GROUPS: list[tuple[str, list]] = [
    ("Long-term (Phase 20)", LONG_TERM_COLUMNS),
    ("Hidden welfare", WELFARE_COLUMNS),
    ("Session health", SESSION_HEALTH_COLUMNS),
    ("Engagement", ENGAGEMENT_COLUMNS),
]

ALL_COLUMNS: list[tuple[str, str, tuple]] = (
    LONG_TERM_COLUMNS + WELFARE_COLUMNS + SESSION_HEALTH_COLUMNS + ENGAGEMENT_COLUMNS
)


# --- Matching / cosine math --------------------------------------------------------------------


def cosine(a: list, b: list) -> Optional[float]:
    """1-normalized dot product. None (undefined) for mismatched lengths, empty vectors, or either
    vector being zero-norm -- callers treat None as "skip this pair", never a crash."""
    if not a or len(a) != len(b):
        return None
    dot = sum(x * y for x, y in zip(a, b))
    norm_a = math.sqrt(sum(x * x for x in a))
    norm_b = math.sqrt(sum(x * x for x in b))
    if norm_a == 0.0 or norm_b == 0.0:
        return None
    return dot / (norm_a * norm_b)


def _mean(xs: list) -> float:
    return sum(xs) / len(xs) if xs else float("nan")


def _percentile(xs: list, p: float) -> float:
    """Linear-interpolation percentile (numpy's default method); p in [0, 100]."""
    if not xs:
        return float("nan")
    s = sorted(xs)
    n = len(s)
    if n == 1:
        return s[0]
    k = (n - 1) * (p / 100.0)
    f, c = math.floor(k), math.ceil(k)
    if f == c:
        return s[int(k)]
    return s[f] + (s[c] - s[f]) * (k - f)


def _median(xs: list) -> float:
    return _percentile(xs, 50.0)


def summarize(xs: list) -> dict:
    return {"mean": _mean(xs), "median": _median(xs), "p90": _percentile(xs, 90.0), "n": len(xs)}


def matched_distortions(rows_a: dict, rows_b: dict) -> tuple:
    """Per-user 1-cos(sem_v_a, sem_v_b) over user_id rows present in BOTH dicts (contracts §5
    method). Returns (distortions, matched_count, union_count, skipped_zero_norm_count)."""
    common = set(rows_a) & set(rows_b)
    union = set(rows_a) | set(rows_b)
    distortions = []
    skipped = 0
    for uid in common:
        c = cosine(rows_a[uid]["sem_v"], rows_b[uid]["sem_v"])
        if c is None:
            skipped += 1
            continue
        distortions.append(1.0 - c)
    return distortions, len(common), len(union), skipped


def read_hidden_preference_final(path: Path) -> Optional[dict]:
    """Frozen schema (contracts §5): user_id,plasticity,churned,sem_shift,visual_shift,music_shift,
    emotional_shift,sem_v0..sem_v{D-1}. Returns {user_id: {...,"sem_v": [float,...]}}, or None if the
    file is absent, unreadable, or missing a required column / any sem_v<i> column -- the caller
    treats None as "this arm's per-user export is not available" (the expected state for a gate-off
    arm), never raises. D is derived from however many sem_v<i> columns the header actually has.
    """
    if not path.exists():
        return None
    try:
        with path.open(newline="") as fh:
            reader = csv.DictReader(fh)
            fieldnames = reader.fieldnames or []
            missing = [c for c in HIDDEN_PREF_REQUIRED_COLUMNS if c not in fieldnames]
            if missing:
                p15.warn(f"{path}: missing required frozen column(s) {missing}; treating the "
                         f"per-user export as absent")
                return None
            sem_cols = sorted(
                (f for f in fieldnames if f.startswith("sem_v") and f[len("sem_v"):].isdigit()),
                key=lambda f: int(f[len("sem_v"):]),
            )
            if not sem_cols:
                p15.warn(f"{path}: no sem_v<i> columns found; treating the per-user export as absent")
                return None

            rows: dict = {}
            for line_no, row in enumerate(reader, start=2):
                uid = row.get("user_id")
                if uid is None:
                    continue
                try:
                    rows[uid] = {
                        "plasticity": float(row["plasticity"]),
                        "churned": row["churned"],
                        "sem_shift": float(row["sem_shift"]),
                        "visual_shift": float(row["visual_shift"]),
                        "music_shift": float(row["music_shift"]),
                        "emotional_shift": float(row["emotional_shift"]),
                        "sem_v": [float(row[c]) for c in sem_cols],
                    }
                except (KeyError, ValueError) as exc:
                    p15.warn(f"{path}: skipping unparseable row at line {line_no} "
                             f"(user_id={uid!r}): {exc}")
            return rows if rows else None
    except Exception as exc:  # malformed CSV / IO error
        p15.warn(f"failed to read {path}: {exc}")
        return None


# --- Arm loading --------------------------------------------------------------------------------


class Arm:
    """One arm's loaded data: summary.json + config.json + a best-effort hidden_preference_final.csv
    (frozen name, contracts §5; absent is normal for a gate-off arm)."""

    def __init__(self, name: str, directory: Optional[Path]):
        self.name = name
        self.directory = directory
        self.summary = p15._read_json(directory / "summary.json") if directory is not None else None
        self.config = p15._read_json(directory / "config.json") if directory is not None else None
        self.hidden_pref = (
            read_hidden_preference_final(directory / "hidden_preference_final.csv")
            if directory is not None else None
        )
        if directory is not None and self.summary is None:
            p15.warn(f"{name}: no readable summary.json under {directory}")

    @property
    def available(self) -> bool:
        return self.summary is not None

    def block_present(self, block: str) -> bool:
        return isinstance(self.summary, dict) and isinstance(self.summary.get(block), dict)

    def metric(self, path: tuple):
        """Frozen-name-exact lookup (no candidate guessing, contracts §6). Distinguishes: the whole
        arm never loaded (NA_ARM); the CONTAINING BLOCK is absent -- e.g. `long_term` on a gate-off
        arm, the expected/documented state (NA_GATE_OFF for `long_term`, a generic block-absent
        message otherwise); or just this one key is missing from an otherwise-present block."""
        if not self.available:
            return NA_ARM
        value = p15._get(self.summary, *path)
        if value is not None:
            return value
        if not self.block_present(path[0]):
            if path[0] == "long_term":
                return NA_GATE_OFF
            return f"n/a (block '{path[0]}' absent)"
        return f"n/a (key '{'.'.join(path)}' absent)"

    def gate_state(self) -> tuple:
        """(preference_evolution, retention.enabled) as resolved in this arm's OWN config.json --
        always written (D12), unlike summary.json's gated long_term block."""
        pe = p15._get(self.config, "realism", "preference_evolution")
        ret = p15._get(self.config, "retention", "enabled")
        return pe, ret

    def retention_configured_hint(self):
        """Best-effort read of the scaffold's extra `long_term.retention_configured` key -- NOT in
        contracts §5's literal list (see module docstring's flagged discrepancy); read defensively
        as a bonus field, never required."""
        return p15._get(self.summary, "long_term", "retention_configured") if self.available else None

    def distinguishing_ranking_weights(self) -> dict:
        """Ranking-block keys whose resolved value differs from the plain config.hpp default
        (DEFAULT_RANKING) -- an independent cross-check that a policy patch landed, without needing
        the phase15 arm directories on disk."""
        ranking = p15._get(self.config, "ranking") or {}
        return {k: v for k, v in ranking.items()
                if isinstance(v, (int, float)) and not isinstance(v, bool)
                and v != DEFAULT_RANKING.get(k, 0.0)}


def load_arms(args: argparse.Namespace) -> dict:
    dirs = {
        "engagement-on": args.engagement_on,
        "engagement-off": args.engagement_off,
        "proxy-on": args.proxy_on,
        "proxy-off": args.proxy_off,
    }
    return {name: Arm(name, d) for name, d in dirs.items()}


# --- Headline computations ----------------------------------------------------------------------


def policy_divergence(engagement_on: Arm, proxy_on: Arm) -> dict:
    """Exit criterion 1 (contracts §6 / plan Phase 20 task 5): per-user 1-cos between the SAME
    world's engagement-on and proxy-on final semantic preferences."""
    if not (engagement_on.hidden_pref and proxy_on.hidden_pref):
        return {"available": False}
    distortions, matched, union, skipped = matched_distortions(
        engagement_on.hidden_pref, proxy_on.hidden_pref)
    if skipped:
        p15.warn(f"policy divergence: skipped {skipped} matched user(s) with a zero-norm semantic "
                 f"preference vector (cosine undefined)")
    if matched == 0:
        p15.warn("policy divergence: zero matched user_id rows between engagement-on and proxy-on "
                 "-- check the two arms really are the same world/seed")
    return {"available": True, "matched_users": matched, "union_users": union,
            "stats": summarize(distortions)}


def policy_distortion(on_arm: Arm, off_arm: Arm, policy: str) -> dict:
    """Per-policy distortion vs. the gate-off counterfactual (contracts §5 method): matched 1-cos
    between the SAME policy's -on and -off `hidden_preference_final.csv`. Falls back to the -on
    arm's own within-world `long_term.mean_preference_shift_from_initial`, labeled explicitly, when
    the -off twin's export is absent -- the expected state for this package's own 4-arm matrix
    (run_phase20_experiment.sh's `-off` arms turn off BOTH P20 gates)."""
    if on_arm.hidden_pref and off_arm.hidden_pref:
        distortions, matched, union, skipped = matched_distortions(on_arm.hidden_pref,
                                                                    off_arm.hidden_pref)
        if skipped:
            p15.warn(f"{policy} distortion: skipped {skipped} matched user(s) with a zero-norm "
                     f"semantic preference vector (cosine undefined)")
        return {"policy": policy, "method": "cross_run", "matched_users": matched,
                "union_users": union, "stats": summarize(distortions)}
    shift = on_arm.metric(("long_term", "mean_preference_shift_from_initial"))
    p15.warn(f"{policy}: off-twin hidden_preference_final.csv absent -- falling back to "
             f"{policy}-on's own long_term.mean_preference_shift_from_initial (within-world shift); "
             f"the cross-run number requires a counterfactual export package B has not shipped in "
             f"this tree (see comparison.md's Headline 2 notes)")
    return {"policy": policy, "method": "fallback_shift_from_initial", "shift_from_initial": shift}


# --- Rendering -----------------------------------------------------------------------------------


def render_table(header: list, rows: list) -> str:
    return p17.render_table(header, rows)


def render_divergence_section(div: dict, precision: int) -> list:
    lines = ["## Headline 1 -- Policy divergence: engagement-on vs. proxy-on (exit criterion 1)\n"]
    if not div["available"]:
        lines.append(
            "Not available -- need both `--engagement-on` and `--proxy-on` directories with a "
            "readable `hidden_preference_final.csv` (frozen header, contracts §5).\n"
        )
        return lines
    s = div["stats"]
    lines.append(
        f"Per-user `1 - cos(sem_v_engagement, sem_v_proxy)` over **{div['matched_users']}** matched "
        f"`user_id` rows (of {div['union_users']} total distinct users across both arms):\n"
    )
    lines.append(f"- mean: **{p15.fmt(s['mean'], precision)}**")
    lines.append(f"- median: **{p15.fmt(s['median'], precision)}**")
    lines.append(f"- p90: **{p15.fmt(s['p90'], precision)}**")
    lines.append(
        "\nA non-zero divergence here is this experiment's headline finding (Tier-4 acceptance item "
        "1, V2 TDD §4.15-4.17): the SAME users, same world, same seed, end up with measurably "
        "different hidden semantic preferences purely as a function of which ranking policy served "
        "them.\n"
    )
    return lines


def render_distortion_section(results: dict, precision: int) -> list:
    lines = ["## Headline 2 -- Distortion vs. gate-off counterfactual, per policy\n"]
    lines.append(
        "Contracts §5 method: per-user `1 - cos(sem_final_on, sem_final_off)` between matched "
        "`hidden_preference_final.csv` rows of the SAME policy's -on and -off arms. The `-off` "
        "twins (run_phase20_experiment.sh, integration-corrected design) are evolution-OFF / "
        "retention-ON worlds: `long_term.configured` stays true, so the export IS written and the "
        "cross-run number below is computed directly. In these twins nothing moves hidden "
        "preferences (evolution off, no drift), so each policy's cross-run distortion should equal "
        "its -on arm's own `mean_preference_shift_from_initial` -- that identity holding in the "
        "tables below is a built-in verification of the matched-user pipeline, and the interesting "
        "comparison is BETWEEN the two policies' distortion magnitudes. (The file-absence fallback "
        "path below is retained defensively for arms produced by older configs.)\n"
    )
    for policy in POLICIES:
        r = results[policy]
        lines.append(f"### {policy}\n")
        if r["method"] == "cross_run":
            s = r["stats"]
            lines.append(
                f"Cross-run distortion over **{r['matched_users']}** matched users (of "
                f"{r['union_users']} total): mean **{p15.fmt(s['mean'], precision)}**, median "
                f"**{p15.fmt(s['median'], precision)}**, p90 **{p15.fmt(s['p90'], precision)}**.\n"
            )
        else:
            lines.append(
                f"off-twin export absent; distortion computed as shift-from-initial + policy "
                f"divergence: `{policy}-on`'s own within-world "
                f"`long_term.mean_preference_shift_from_initial` = "
                f"**{p15.fmt(r['shift_from_initial'], precision)}** (see Headline 1 above for this "
                f"policy's divergence from its counterpart policy -- together these are the best "
                f"available proxy for cross-run distortion until an off-twin export exists).\n"
            )
    return lines


def render_availability_section(arms: dict) -> list:
    lines = ["## Arms and data availability\n"]
    for name in ARMS:
        arm = arms[name]
        lines.append(f"- **{name}**")
        if not arm.available:
            lines.append(f"  - status: NOT AVAILABLE ({NA_ARM})")
            continue
        pe, ret = arm.gate_state()
        weights = arm.distinguishing_ranking_weights()
        weights_str = (", ".join(f"{k}={v:g}" for k, v in sorted(weights.items()))
                      or "(none -- V1 parity)")
        lines.append(f"  - directory: `{arm.directory}`")
        lines.append(f"  - resolved realism.preference_evolution={pe}, retention.enabled={ret}")
        lines.append(f"  - non-default ranking weights: {weights_str}")
        lines.append(f"  - `long_term` block present: {arm.block_present('long_term')}")
        hint = arm.retention_configured_hint()
        if hint is not None:
            lines.append(f"  - `long_term.retention_configured` (scaffold addition, not in "
                         f"contracts §5's literal list -- see module docstring): {hint}")
        rows = len(arm.hidden_pref) if arm.hidden_pref else 0
        lines.append(f"  - `hidden_preference_final.csv`: {'present, ' + str(rows) + ' row(s)' if arm.hidden_pref else 'ABSENT'}")
    return lines


def render_group_table(arms: dict, columns: list, precision: int) -> str:
    header = ["arm"] + [h for _k, h, _p in columns]
    rows = []
    for name in ARMS:
        arm = arms[name]
        rows.append([name] + [p15.fmt(arm.metric(p), precision) for _k, _h, p in columns])
    return render_table(header, rows)


def render_notes() -> list:
    return [
        "## Notes\n",
        "- **No aggregate score** (D22 / V2 TDD §6): every group above (long-term, hidden welfare, "
        "session health, engagement) is reported SEPARATELY; this script never combines them into "
        "one number, and neither do plot_results.py's Phase 20 additions.\n",
        "- **Concurrency-contention caveat**: if the four arms were run CONCURRENTLY "
        "(`scripts/run_phase20_experiment.sh`'s default 4-concurrent mode), wall-clock/timing "
        "numbers carry cache and memory-bandwidth contention (same caveat as every prior phase's "
        "concurrent runs). Every other number in this report is deterministic (rng/clock-free, "
        "D8/D9) and unaffected by run mode.\n",
        "- **Drift-removed design**: `configs/realism-medium-retention.json` has NO `drift` block "
        "at all (the key is entirely absent, not merely an empty events list -- both resolve to "
        "the same disabled state under D17's convention). This isolates ENDOGENOUS preference "
        "evolution (this phase) from Phase 10's EXOGENOUS scheduled drift; the two mechanisms' "
        "interaction is documented (contracts §3: drift retargets preferences exogenously at its "
        "interaction mark, evolution keeps operating on the retargeted vector afterward) but "
        "deliberately not swept in this experiment.\n",
        "- **Off-twins are evolution-OFF / retention-ON counterfactuals** (integration-corrected "
        "design, see `run_phase20_experiment.sh`'s header): retention keeps `long_term.configured` "
        "true, so `-off` arms carry the full `long_term` block and `hidden_preference_final.csv` "
        "(static preferences -- the matched-user baseline for Headline 2). Their retention runs on "
        "STATIC trust (= the platformTrust trait; package A only writes trust under the evolution "
        "gate), so the trust columns get an evolution-isolated reference. Only "
        "`preference_evolution` is flipped between twins; every other gate and weight is "
        "identical.\n",
        "- **Frozen-schema reads only** (contracts §6): every key this script reads is quoted "
        "verbatim from contracts §5, or, for the engagement group, already-landed Phase 15/16 keys "
        "verified directly against `src/evaluation/results_writer.cpp` in this worktree -- no "
        "candidate-path guessing. A key this script expects but package B renames at integration "
        "shows as a clearly labeled n/a rather than a wrong number; see this script's module "
        "docstring for the one scaffold-vs-contract discrepancy this package flagged "
        "(`long_term.retention_configured`, an extra additive key beyond contracts §5's literal "
        "10-key list).\n",
    ]


def render_markdown(arms: dict, precision: int) -> str:
    parts = ["# Phase 20 -- Policy-Influence Experiment: Engagement vs. Satisfaction-Proxy\n"]
    parts.append(
        "V2 TDD §4.15-4.17, Tier-4 acceptance item 1 (plan Phase 20 task 5, contracts §6): "
        "`engagement-on` / `engagement-off` / `proxy-on` / `proxy-off` on "
        "`configs/realism-medium-retention.json` (event mode, full "
        "content_v2/latent_reactions/session_dynamics gate stack, `drift` block REMOVED entirely, "
        "9 simulated days), algorithm `hnsw_ranker`, seed 42. Generated by "
        "`scripts/phase20_comparison.py`.\n"
    )

    div = policy_divergence(arms["engagement-on"], arms["proxy-on"])
    parts.extend(render_divergence_section(div, precision))

    distortion_results = {
        policy: policy_distortion(arms[f"{policy}-on"], arms[f"{policy}-off"], policy)
        for policy in POLICIES
    }
    parts.extend(render_distortion_section(distortion_results, precision))

    parts.extend(render_availability_section(arms))
    parts.append("")

    parts.append("## Retention / churn / trust / engagement across all four arms\n")
    for label, columns in GROUPS:
        parts.append(f"### {label}\n")
        parts.append(render_group_table(arms, columns, precision))
        parts.append("")

    parts.extend(render_notes())
    return "\n".join(parts) + "\n"


def write_csv(path: Path, arms: dict, precision: int) -> None:
    header = ["arm", "policy", "gate"] + [h for _k, h, _p in ALL_COLUMNS]
    with path.open("w", newline="") as fh:
        writer = csv.writer(fh)
        writer.writerow(header)
        for name in ARMS:
            arm = arms[name]
            policy, gate = name.rsplit("-", 1)
            row = [name, policy, gate]
            for _k, _h, col_path in ALL_COLUMNS:
                row.append(p15.fmt(arm.metric(col_path), precision))
            writer.writerow(row)


# --- CLI -------------------------------------------------------------------------------------


def parse_args(argv=None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Compare the Phase 20 policy-influence experiment (V2 TDD §4.15-4.17, Tier-4 "
                    "acceptance item 1) as comparison.csv + comparison.md.",
    )
    parser.add_argument("--engagement-on", type=Path, default=None, dest="engagement_on",
                        help="engagement-on arm result directory")
    parser.add_argument("--engagement-off", type=Path, default=None, dest="engagement_off",
                        help="engagement-off (gate-off counterfactual) arm result directory")
    parser.add_argument("--proxy-on", type=Path, default=None, dest="proxy_on",
                        help="proxy-on arm result directory")
    parser.add_argument("--proxy-off", type=Path, default=None, dest="proxy_off",
                        help="proxy-off (gate-off counterfactual) arm result directory")
    parser.add_argument("--out", type=Path, default=Path("results/published/phase20"),
                        help="output directory for comparison.csv/comparison.md "
                             "(default: results/published/phase20)")
    parser.add_argument("--precision", type=int, default=6,
                        help="significant figures for floats (default: 6)")
    parser.add_argument("--self-test", action="store_true", dest="self_test",
                        help="run an in-script synthetic-fixture smoke test of the matching/cosine "
                             "math and the renderers, print PASS/FAIL, and exit (ignores every "
                             "other argument; no committed fixture files)")
    return parser.parse_args(argv)


def _check(cond: bool, message: str) -> bool:
    print(f"  [{'PASS' if cond else 'FAIL'}] {message}")
    return cond


def run_self_test() -> int:
    """Synthetic-fixture smoke test (no committed fixture files): builds tiny
    hidden_preference_final.csv + summary.json + config.json fixtures for all four arms under a
    tempdir, hand-verifies the matching/cosine math, the defensive NA logic, and runs the
    markdown/CSV renderers end-to-end. Prints one PASS/FAIL line per check."""
    print("phase20_comparison --self-test")
    ok = True

    ok &= _check(abs(cosine([1.0, 0.0], [1.0, 0.0]) - 1.0) < 1e-12,
                "cosine: identical vectors -> 1")
    ok &= _check(abs(cosine([1.0, 0.0], [0.0, 1.0]) - 0.0) < 1e-12,
                "cosine: orthogonal vectors -> 0")
    ok &= _check(abs(cosine([1.0, 0.0], [-1.0, 0.0]) - (-1.0)) < 1e-12,
                "cosine: opposite vectors -> -1")
    ok &= _check(cosine([0.0, 0.0], [1.0, 0.0]) is None,
                "cosine: zero-norm vector -> None (undefined)")

    with tempfile.TemporaryDirectory(prefix="phase20-selftest-") as tmp:
        root = Path(tmp)

        def write_hidden_pref(path: Path, rows: list) -> None:
            path.parent.mkdir(parents=True, exist_ok=True)
            with path.open("w", newline="") as fh:
                w = csv.writer(fh)
                w.writerow(["user_id", "plasticity", "churned", "sem_shift", "visual_shift",
                           "music_shift", "emotional_shift", "sem_v0", "sem_v1", "sem_v2"])
                w.writerows(rows)

        def write_json(path: Path, obj: dict) -> None:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(json.dumps(obj))

        # user 1/2/3 present in both engagement-on and proxy-on; user 4 only in proxy-on (must be
        # excluded from matching). Hand-computable cosines: user1/user2 rotate 90 degrees (cos=0,
        # distortion=1); user3 is IDENTICAL in both (cos=1, distortion=0).
        write_hidden_pref(root / "engagement-on" / "hidden_preference_final.csv", [
            ["1", "0.5", "false", "0.1", "0.05", "0.02", "0.03", "1.0", "0.0", "0.0"],
            ["2", "0.5", "false", "0.1", "0.05", "0.02", "0.03", "0.0", "1.0", "0.0"],
            ["3", "0.5", "true", "0.1", "0.05", "0.02", "0.03",
             "0.7071067811865476", "0.7071067811865476", "0.0"],
        ])
        write_hidden_pref(root / "proxy-on" / "hidden_preference_final.csv", [
            ["1", "0.5", "false", "0.1", "0.05", "0.02", "0.03", "0.0", "1.0", "0.0"],
            ["2", "0.5", "false", "0.1", "0.05", "0.02", "0.03", "1.0", "0.0", "0.0"],
            ["3", "0.5", "true", "0.1", "0.05", "0.02", "0.03",
             "0.7071067811865476", "0.7071067811865476", "0.0"],
            ["4", "0.5", "false", "0.1", "0.05", "0.02", "0.03", "1.0", "0.0", "0.0"],
        ])
        # engagement-off / proxy-off deliberately get NO hidden_preference_final.csv (the expected
        # gate-off state -- see module docstring).

        def base_summary(long_term: bool) -> dict:
            s = {
                "welfare": {"mean_immediate_satisfaction": 0.42, "mean_regret": 0.1,
                           "satisfaction_per_minute": 0.05, "platform_trust": 0.6},
                "session_health": {"sessions": 100, "mean_session_utility": 0.3,
                                  "early_failure_exit_rate": 0.1, "natural_completion_rate": 0.5,
                                  "harmful_fatigue_mean": 0.05,
                                  "exit_type_shares": {"satisfied": 0.5, "fatigue": 0.2,
                                                       "regret": 0.05}},
                "metrics": {"reward_per_impression": 0.2, "mean_watch_seconds": 12.0,
                           "completion_rate": 0.4, "like_rate": 0.1, "share_rate": 0.02,
                           "follow_rate": 0.01},
                "metric_groups": {"engagement": {"comment_rate": 0.01, "save_rate": 0.02,
                                                 "profile_visit_rate": 0.03}},
            }
            if long_term:
                s["long_term"] = {
                    "retention_configured": True,
                    "retention_1d": 0.8, "retention_7d": 0.5, "sessions_per_user_per_day": 1.2,
                    "satisfaction_weighted_retention": 0.55, "churn_rate": 0.1,
                    "mean_churn_probability": 0.12, "mean_final_trust": 0.58,
                    "mean_final_habit": 0.4, "mean_preference_shift_from_initial": 0.1234,
                    "mean_final_preference_entropy": 1.5,
                }
            return s

        def base_config(pe: bool, ret: bool, weights: dict) -> dict:
            ranking = dict(DEFAULT_RANKING)
            ranking.update(weights)
            return {"realism": {"preference_evolution": pe, "session_dynamics": True},
                   "retention": {"enabled": ret}, "ranking": ranking}

        write_json(root / "engagement-on" / "summary.json", base_summary(True))
        write_json(root / "engagement-on" / "config.json",
                  base_config(True, True, {"similarity_weight": 0.6}))
        write_json(root / "engagement-off" / "summary.json", base_summary(False))
        write_json(root / "engagement-off" / "config.json",
                  base_config(False, False, {"similarity_weight": 0.6}))
        write_json(root / "proxy-on" / "summary.json", base_summary(True))
        write_json(root / "proxy-on" / "config.json",
                  base_config(True, True, {"clickbait_weight": -0.15}))
        write_json(root / "proxy-off" / "summary.json", base_summary(False))
        write_json(root / "proxy-off" / "config.json",
                  base_config(False, False, {"clickbait_weight": -0.15}))

        arms = {name: Arm(name, root / name) for name in ARMS}

        distortions, matched, union, skipped = matched_distortions(
            arms["engagement-on"].hidden_pref, arms["proxy-on"].hidden_pref)
        ok &= _check(matched == 3, f"matched_distortions: matched_users == 3 (got {matched})")
        ok &= _check(union == 4, f"matched_distortions: union_users == 4 (got {union})")
        ok &= _check(skipped == 0, f"matched_distortions: skipped == 0 (got {skipped})")
        ok &= _check(sorted(round(d, 6) for d in distortions) == [0.0, 1.0, 1.0],
                    f"matched_distortions: per-user distortions == [0, 1, 1] "
                    f"(got {sorted(distortions)})")
        stats = summarize(distortions)
        ok &= _check(abs(stats["mean"] - (2.0 / 3.0)) < 1e-9,
                    f"summarize: mean == 2/3 (got {stats['mean']})")
        ok &= _check(abs(stats["median"] - 1.0) < 1e-9,
                    f"summarize: median == 1.0 (got {stats['median']})")
        ok &= _check(abs(stats["p90"] - 1.0) < 1e-9,
                    f"summarize: p90 == 1.0 (got {stats['p90']})")

        div = policy_divergence(arms["engagement-on"], arms["proxy-on"])
        ok &= _check(div["available"] and div["matched_users"] == 3,
                    "policy_divergence: available, matched_users == 3")

        dist_eng = policy_distortion(arms["engagement-on"], arms["engagement-off"], "engagement")
        ok &= _check(dist_eng["method"] == "fallback_shift_from_initial",
                    "policy_distortion: falls back to shift-from-initial when the off-twin CSV is "
                    "absent")
        ok &= _check(abs(dist_eng["shift_from_initial"] - 0.1234) < 1e-9,
                    "policy_distortion: fallback value equals the on-arm's own "
                    "mean_preference_shift_from_initial")

        ok &= _check(arms["engagement-off"].metric(("long_term", "retention_1d")) == NA_GATE_OFF,
                    "Arm.metric: a gate-off arm's long_term.* reads as NA_GATE_OFF")
        satisfaction = arms["engagement-off"].metric(("welfare", "mean_immediate_satisfaction"))
        ok &= _check(isinstance(satisfaction, float),
                    "Arm.metric: a gate-off arm's welfare.* is still a real number "
                    "(latent_reactions/session_dynamics stay on)")
        missing_arm = Arm("engagement-on", None)
        ok &= _check(missing_arm.metric(("welfare", "mean_immediate_satisfaction")) == NA_ARM,
                    "Arm.metric: an arm with no directory reads as NA_ARM")

        pe, ret = arms["engagement-off"].gate_state()
        ok &= _check(pe is False and ret is False,
                    "gate_state: engagement-off resolves to (False, False)")
        weights = arms["engagement-on"].distinguishing_ranking_weights()
        ok &= _check(weights.get("similarity_weight") == 0.6,
                    "distinguishing_ranking_weights: detects the patched similarity_weight")

        md = render_markdown(arms, precision=6)
        ok &= _check("Policy divergence" in md and "gate off" in md.lower(),
                    "render_markdown: produced text mentions divergence and the gate-off state")
        out_csv = root / "out" / "comparison.csv"
        out_csv.parent.mkdir(parents=True, exist_ok=True)  # main() does this before write_csv too
        write_csv(out_csv, arms, precision=6)
        with out_csv.open() as fh:
            rows = list(csv.reader(fh))
        ok &= _check(len(rows) == 1 + len(ARMS),
                    f"write_csv: header + {len(ARMS)} arm rows (got {len(rows)} total rows)")

    print(f"phase20_comparison --self-test: {'ALL PASS' if ok else 'SOME FAILED'}")
    return 0 if ok else 1


def main(argv=None) -> int:
    args = parse_args(argv)
    if args.self_test:
        return run_self_test()

    arms = load_arms(args)
    if not any(arm.available for arm in arms.values()):
        p15.warn("no readable summary.json in any of the four arms")
        return 1

    args.out.mkdir(parents=True, exist_ok=True)

    csv_path = args.out / "comparison.csv"
    write_csv(csv_path, arms, args.precision)

    md_path = args.out / "comparison.md"
    md_path.write_text(render_markdown(arms, args.precision))

    print(f"phase20_comparison: wrote {csv_path}")
    print(f"phase20_comparison: wrote {md_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
