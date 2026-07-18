#!/usr/bin/env python3
"""phase15_comparison.py — Phase 15 four-arm comparison (V2 TDD §4.4 core experiment).

Given the four Phase 15 arm result directories (semantic / engagement-optimized /
satisfaction-proxy / oracle_satisfaction, as produced by scripts/run_phase15_experiment.sh), reads
each arm's summary.json (+ config.json for the arm-identity section, + a best-effort read of
welfare_metrics.csv for per-archetype exposure) and writes:

  comparison.csv   one row per arm, one column per §4.4 report-list metric.
  comparison.md    headline deltas, an arm-identity section (algorithm + distinguishing V2 ranking
                   weights), the per-arm table, a per-archetype exposure table, and notes — the
                   same shape as results/published/phase10/comparison.md.

Arm order is always semantic, engagement, proxy, oracle (deterministic ordering, regardless of CLI
argument order or which arms are available) — a missing/unreadable arm still gets a row, with every
cell reported as unavailable rather than the row being silently dropped, so the output shape never
changes across invocations.

This script reads summary.json/config.json/welfare_metrics.csv and prints/writes tables; it
contains no simulation logic (design decision D15), matching compare_results.py.

Package-A/B2 dependency (read defensively, see notes below):
  - comment_rate / save_rate / profile_visit_rate (metrics block) and per-archetype exposure
    (welfare_metrics.csv) are package A additions (plan Phase 15 tasks 1/3) not present in this
    worktree; missing keys render as "n/a (pre-integration)" rather than raising.
  - satisfaction_per_minute is computed from mean_immediate_satisfaction / (mean_watch_seconds/60)
    unless a package-A-provided figure is already present under a handful of candidate key names
    (checked first).
  - The oracle arm is implemented by package B2 (plan Phase 15 task 4); until it lands its first
    request throws (OracleSatisfactionRecommender stub), so no oracle summary.json exists and the
    oracle column reports "n/a (oracle pending package B2)".

Usage
-----
    python3 scripts/phase15_comparison.py \\
        --semantic <dir> --engagement <dir> --proxy <dir> [--oracle <dir>] \\
        [--out results/published/phase15] [--precision 6]

    (No third-party dependencies -- plain python3 is enough; `uv run --project scripts` also works
    since scripts/ is a valid uv project, but is not required here.)

Exit status: 0 if at least one of the three required arms (semantic/engagement/proxy) had a
readable summary.json, 1 otherwise.
"""
from __future__ import annotations

import argparse
import csv
import json
import sys
from pathlib import Path
from typing import Optional

ARM_ORDER = ["semantic", "engagement", "proxy", "oracle"]

ARM_DESCRIPTION = {
    "semantic": "semantic-similarity ranker (algorithm: hnsw, configs/realism-medium.json)",
    "engagement": "engagement-optimized preset (algorithm: hnsw_ranker, "
                  "configs/realism-medium-engagement.json)",
    "proxy": "satisfaction-proxy preset (algorithm: hnsw_ranker, configs/realism-medium-proxy.json)",
    "oracle": "oracle_satisfaction, EVALUATION-ONLY upper bound (configs/realism-medium.json)",
}

NA_PRE_INTEGRATION = "n/a (pre-integration)"
NA_PENDING_B2 = "n/a (oracle pending package B2)"

# The ten Phase-15 V2 ranking-feature weights (config.hpp RankingConfig); all default 0.0, so
# printing only the non-zero ones in the arm-identity section shows exactly what each preset varies.
V2_WEIGHT_KEYS = [
    "visual_match_weight", "music_match_weight", "emotional_match_weight", "clickbait_weight",
    "emotional_intensity_weight", "usefulness_weight", "production_quality_weight",
    "information_density_weight", "language_match_weight", "save_popularity_weight",
]

# (column key, header, (section, key) path into summary.json) for every plain summary.json lookup.
# satisfaction_per_minute and archetype exposure are handled separately (derived / different shape).
METRIC_COLUMNS: list[tuple[str, str, tuple[str, ...]]] = [
    ("algorithm", "algorithm", ("algorithm",)),
    ("impressions", "impressions", ("counts", "impressions")),
    ("mean_watch_seconds", "watch time (mean watchSeconds/impression)", ("metrics", "mean_watch_seconds")),
    ("completion_rate", "completion rate", ("metrics", "completion_rate")),
    ("like_rate", "like rate", ("metrics", "like_rate")),
    ("share_rate", "share rate", ("metrics", "share_rate")),
    ("follow_rate", "follow rate", ("metrics", "follow_rate")),
    ("comment_rate", "comment rate", ("metric_groups", "engagement", "comment_rate")),
    ("save_rate", "save rate", ("metric_groups", "engagement", "save_rate")),
    ("profile_visit_rate", "profile-visit rate", ("metric_groups", "engagement", "profile_visit_rate")),
    ("reward_per_impression", "reward/impression (engagement proxy)", ("metrics", "reward_per_impression")),
    ("mean_hidden_satisfaction", "MEAN HIDDEN SATISFACTION", ("welfare", "mean_immediate_satisfaction")),
    ("mean_hidden_regret", "hidden regret (LatentReaction.regret)", ("welfare", "mean_regret")),
    ("satisfaction_per_minute", "satisfaction per minute", None),  # derived, see satisfaction_per_minute()
    ("mean_true_affinity", "mean true affinity", ("metrics", "mean_true_affinity")),
    ("est_hidden_cosine", "estimated<->hidden cosine", ("learning", "final_estimated_hidden_cosine")),
]

# Headline comparisons surfaced as deltas (V2 TDD §4.4 Tier-1 acceptance dimensions): (label, path,
# arm-a, arm-b) -- reported as "a vs b" with a% delta relative to b. Purely mechanical: the sign
# is whatever the data says, no claim of statistical significance is made (see comparison.md notes).
HEADLINE_COMPARISONS = [
    ("watch time/impression: engagement vs semantic", ("metrics", "mean_watch_seconds"), "engagement", "semantic"),
    ("reward/impression (engagement proxy): engagement vs semantic", ("metrics", "reward_per_impression"),
     "engagement", "semantic"),
    ("mean hidden satisfaction: proxy vs engagement", ("welfare", "mean_immediate_satisfaction"),
     "proxy", "engagement"),
    ("mean hidden satisfaction: proxy vs semantic", ("welfare", "mean_immediate_satisfaction"),
     "proxy", "semantic"),
    ("hidden regret: engagement vs proxy", ("welfare", "mean_regret"), "engagement", "proxy"),
    ("mean hidden satisfaction: oracle vs proxy (oracle upper-bounds satisfaction)",
     ("welfare", "mean_immediate_satisfaction"), "oracle", "proxy"),
]


def warn(message: str) -> None:
    print(f"phase15_comparison: {message}", file=sys.stderr)


def _read_json(path: Path) -> Optional[dict]:
    if not path.exists():
        return None
    try:
        with path.open() as fh:
            return json.load(fh)
    except Exception as exc:  # malformed JSON
        warn(f"failed to read {path}: {exc}")
        return None


def _get(d: Optional[dict], *keys):
    """Nested dict.get, short-circuiting to None on a missing key or non-dict at any level."""
    cur = d
    for k in keys:
        if not isinstance(cur, dict) or k not in cur:
            return None
        cur = cur[k]
    return cur


def _is_num(v) -> bool:
    return isinstance(v, (int, float)) and not isinstance(v, bool)


def _archetype_exposure(directory: Path) -> Optional[dict[str, float]]:
    """Best-effort read of a per-archetype exposure-share breakdown.

    Package A owns `welfare_metrics.csv` (plan Phase 15 task 1: "welfare metrics framework incl.
    welfare_metrics.csv + per-archetype exposure"); it does not exist in this worktree, so this
    always returns None here (-> reported "n/a (pre-integration)"). Schema-tolerant so it survives
    whatever column names land at integration without needing a rewrite: looks for an "archetype"
    column plus either a share/exposure/fraction/pct column (used directly) or an
    impressions/count column (normalized into shares). Returns None if the file is absent, empty,
    or not recognizable in either shape -- never raises.
    """
    path = directory / "welfare_archetype_metrics.csv"
    if not path.exists():
        return None
    try:
        with path.open(newline="") as fh:
            rows = list(csv.DictReader(fh))
    except Exception as exc:
        warn(f"failed to read {path}: {exc}")
        return None
    if not rows:
        return None
    fields = list(rows[0].keys())
    # Prefer the human-readable name column (package A ships archetype_index AND archetype_name).
    archetype_col = next((f for f in fields if "archetype" in f.lower() and "name" in f.lower()),
                         None) or next((f for f in fields if "archetype" in f.lower()), None)
    if archetype_col is None:
        return None
    share_col = next(
        (f for f in fields
         if any(tok in f.lower() for tok in ("share", "exposure", "fraction", "pct", "percent"))),
        None,
    )
    try:
        if share_col is not None:
            return {row[archetype_col]: float(row[share_col]) for row in rows}
        count_col = next(
            (f for f in fields if any(tok in f.lower() for tok in ("impression", "count", "n"))), None
        )
        if count_col is None:
            return None
        counts = {row[archetype_col]: float(row[count_col]) for row in rows}
    except (KeyError, ValueError):
        return None
    total = sum(counts.values())
    if total <= 0:
        return None
    return {name: count / total for name, count in counts.items()}


class Arm:
    """One arm's loaded data (summary.json + config.json + best-effort archetype exposure)."""

    def __init__(self, name: str, directory: Optional[Path]):
        self.name = name
        self.directory = directory
        self.summary = _read_json(directory / "summary.json") if directory is not None else None
        self.config = _read_json(directory / "config.json") if directory is not None else None
        self.archetype_exposure = _archetype_exposure(directory) if directory is not None else None
        if directory is not None and self.summary is None:
            warn(f"{name}: no readable summary.json under {directory}")

    @property
    def available(self) -> bool:
        return self.summary is not None

    def _na(self) -> str:
        return NA_PENDING_B2 if self.name == "oracle" else NA_PRE_INTEGRATION

    def metric(self, path: Optional[tuple[str, ...]]):
        if not self.available or path is None:
            return self._na()
        value = _get(self.summary, *path)
        return self._na() if value is None else value

    def satisfaction_per_minute(self):
        if not self.available:
            return self._na()
        # Prefer a package-A-provided figure if/when one lands under any of these candidate keys.
        for key in ("satisfaction_per_minute", "mean_satisfaction_per_minute", "satisfaction_per_min"):
            value = _get(self.summary, "welfare", key)
            if value is not None:
                return value
        satisfaction = _get(self.summary, "welfare", "mean_immediate_satisfaction")
        watch_seconds = _get(self.summary, "metrics", "mean_watch_seconds")
        if not _is_num(satisfaction) or not _is_num(watch_seconds) or watch_seconds <= 0:
            return NA_PRE_INTEGRATION
        return satisfaction / (watch_seconds / 60.0)

    def distinguishing_weights(self) -> dict[str, float]:
        """Non-zero V2 ranking weights from this arm's resolved config.json (empty if unavailable)."""
        ranking = _get(self.config, "ranking") or {}
        return {k: ranking[k] for k in V2_WEIGHT_KEYS if isinstance(ranking.get(k), (int, float)) and ranking[k] != 0}


def load_arms(args: argparse.Namespace) -> dict[str, Arm]:
    dirs = {
        "semantic": args.semantic,
        "engagement": args.engagement,
        "proxy": args.proxy,
        "oracle": args.oracle,
    }
    return {name: Arm(name, d) for name, d in dirs.items()}


def fmt(value, precision: int) -> str:
    if isinstance(value, bool):
        return "true" if value else "false"
    if isinstance(value, float):
        return f"{value:.{precision}g}"
    return str(value)


def build_rows(arms: dict[str, Arm], precision: int) -> list[list[str]]:
    rows = []
    for name in ARM_ORDER:
        arm = arms[name]
        row = [name]
        for _key, _header, path in METRIC_COLUMNS:
            value = arm.satisfaction_per_minute() if path is None else arm.metric(path)
            row.append(fmt(value, precision))
        rows.append(row)
    return rows


def write_csv(path: Path, rows: list[list[str]]) -> None:
    header = ["arm"] + [header for _key, header, _path in METRIC_COLUMNS]
    with path.open("w", newline="") as fh:
        writer = csv.writer(fh)
        writer.writerow(header)
        writer.writerows(rows)


def render_markdown_table(rows: list[list[str]]) -> str:
    header = ["arm"] + [header for _key, header, _path in METRIC_COLUMNS]
    widths = [len(h) for h in header]
    for row in rows:
        for i, cell in enumerate(row):
            widths[i] = max(widths[i], len(cell))

    def line(cells: list[str]) -> str:
        return "| " + " | ".join(c.ljust(widths[i]) for i, c in enumerate(cells)) + " |"

    out = [line(header), "| " + " | ".join("-" * w for w in widths) + " |"]
    out.extend(line(row) for row in rows)
    return "\n".join(out)


def build_headline(arms: dict[str, Arm], precision: int) -> list[str]:
    lines = []
    for label, path, name_a, name_b in HEADLINE_COMPARISONS:
        a, b = arms[name_a], arms[name_b]
        va = _get(a.summary, *path) if a.available else None
        vb = _get(b.summary, *path) if b.available else None
        if not (_is_num(va) and _is_num(vb)):
            lines.append(f"- {label}: not available (needs a readable summary.json for both arms).")
            continue
        delta = ((va - vb) / abs(vb) * 100.0) if vb != 0 else None
        delta_str = f"{delta:+.1f}%" if delta is not None else "n/a (baseline is 0)"
        lines.append(f"- {label}: **{fmt(va, precision)}** vs **{fmt(vb, precision)}** ({delta_str}).")
    return lines


def render_archetype_section(arms: dict[str, Arm], precision: int) -> str:
    have_any = any(arms[name].archetype_exposure for name in ARM_ORDER)
    if not have_any:
        return (
            "n/a (pre-integration) -- no arm directory has a `welfare_metrics.csv` with a "
            "recognizable archetype/share (or archetype/impressions) column pair yet; package A "
            "lands per-archetype exposure at integration (plan Phase 15 task 1)."
        )
    archetypes: list[str] = []
    seen = set()
    for name in ARM_ORDER:
        exposure = arms[name].archetype_exposure or {}
        for archetype in exposure:
            if archetype not in seen:
                seen.add(archetype)
                archetypes.append(archetype)
    header = ["archetype"] + ARM_ORDER
    rows = []
    for archetype in archetypes:
        row = [archetype]
        for name in ARM_ORDER:
            exposure = arms[name].archetype_exposure or {}
            value = exposure.get(archetype)
            row.append(fmt(value, precision) if value is not None else "n/a")
        rows.append(row)
    widths = [len(h) for h in header]
    for row in rows:
        for i, cell in enumerate(row):
            widths[i] = max(widths[i], len(cell))

    def line(cells: list[str]) -> str:
        return "| " + " | ".join(c.ljust(widths[i]) for i, c in enumerate(cells)) + " |"

    lines = [line(header), "| " + " | ".join("-" * w for w in widths) + " |"]
    lines.extend(line(row) for row in rows)
    return "\n".join(lines)


def render_arms_section(arms: dict[str, Arm]) -> str:
    lines = []
    for name in ARM_ORDER:
        arm = arms[name]
        lines.append(f"- **{name}** -- {ARM_DESCRIPTION[name]}")
        if not arm.available:
            reason = "pending package B2 (stub throws on first request)" if name == "oracle" \
                else "no readable summary.json"
            lines.append(f"  - status: NOT AVAILABLE ({reason})")
            continue
        weights = arm.distinguishing_weights()
        weights_str = ", ".join(f"{k}={v:g}" for k, v in weights.items()) if weights else "(all zero -- V1 parity)"
        lines.append(f"  - directory: `{arm.directory}`")
        lines.append(f"  - non-zero V2 ranking weights: {weights_str}")
    return "\n".join(lines)


def render_markdown(arms: dict[str, Arm], rows: list[list[str]], precision: int) -> str:
    parts = []
    parts.append("# Phase 15 -- Engagement vs. Hidden Satisfaction\n")
    parts.append(
        "V2 TDD §4.4 core experiment: semantic-similarity, engagement-optimized, "
        "satisfaction-proxy, and (evaluation-only) oracle_satisfaction arms on "
        "`configs/realism-medium.json` (gates `content_v2`/`latent_reactions` on) and its two "
        "weight-preset variants, seed 42. Generated by `scripts/phase15_comparison.py`.\n"
    )

    parts.append("## Headline\n")
    parts.extend(build_headline(arms, precision))
    parts.append(
        "\nDeltas above are computed directly from each arm's summary.json; they make no claim of "
        "statistical significance by themselves (see Notes) -- read them alongside the full "
        "per-arm table below.\n"
    )

    parts.append("## Arms\n")
    parts.append(render_arms_section(arms))
    parts.append("")

    parts.append("## Per-arm results\n")
    parts.append(
        "See `comparison.csv` for the same table in machine-readable form. Columns follow the V2 "
        "TDD §4.4 report list: engagement (watch time, completion/like/share/follow, "
        "comment/save/profile-visit), hidden welfare (mean immediate satisfaction, regret, "
        "satisfaction/minute), and recommendation-quality basics (mean true affinity, "
        "estimated<->hidden cosine).\n"
    )
    parts.append(render_markdown_table(rows))
    parts.append("")

    parts.append("## Per-archetype exposure\n")
    parts.append(
        "Does the engagement arm over-serve ragebait/clickbait relative to the semantic baseline "
        "and the satisfaction-proxy arm? (V2 TDD §4.4 archetype catalog: genuinely-satisfying, "
        "useful, ragebait, clickbait, comfort, polished-irrelevant, niche-treasure, "
        "background-music.) Shares should sum to ~1.0 per arm column.\n"
    )
    parts.append(render_archetype_section(arms, precision))
    parts.append("")

    parts.append("## Notes\n")
    parts.append(
        "- **Session health is limited pre-Phase-16**: session exits, return rate, sessions/day, "
        "and retention/churn (V2 TDD §6 \"session health\" group) are not reported here -- "
        "`realism.session_dynamics` lands in Phase 16 and this run predates it. Reported here: "
        "the engagement group, the hidden-welfare slice available since Phase 14 (immediate "
        "satisfaction + regret; harmful fatigue and platform trust are documented P16/P20 "
        "placeholders, not yet modeled), and recommendation-quality basics. No single aggregate "
        "score is computed anywhere in this report (D22/V2 TDD §6 rule).\n"
        "- **Concurrency-contention caveat**: if the arms were run CONCURRENTLY "
        "(`scripts/run_phase15_experiment.sh`'s default mode), this run's wall-clock/timing "
        "numbers carry cache and memory-bandwidth contention (same caveat as Phase 10's seven "
        "concurrent arms). Every other number in this report is deterministic (rng/clock-free, "
        "D8/D9) and unaffected by contention -- cross-arm comparisons on the table above are "
        "like-for-like regardless of run mode.\n"
        "- **\"Regret\" here is HIDDEN welfare regret**, not recommendation regret: the table's "
        "`hidden regret` column is `welfare.mean_regret` (LatentReaction.regret, \"wishes they had "
        "skipped\", V2 TDD §4.3, D18 evaluation carve-out) -- a different quantity from each arm's "
        "own `oracle.mean_regret` (a true-affinity gap vs. the oracle's exhaustive top-k, the V1 "
        "Phase-4 recommendation-quality regret), which is not reproduced in this table.\n"
        "- **`satisfaction_per_minute`** uses a package-A-provided summary.json figure when present "
        "(checked candidate keys: `welfare.satisfaction_per_minute`, "
        "`welfare.mean_satisfaction_per_minute`, `welfare.satisfaction_per_min`); otherwise it is "
        "derived here as `mean_immediate_satisfaction / (mean_watch_seconds / 60)`.\n"
        "- **Pre-integration `n/a` cells**: `comment_rate`/`save_rate`/`profile_visit_rate` "
        "(engagement group extension) and per-archetype exposure (`welfare_metrics.csv`) are "
        "package A additions (plan Phase 15 tasks 1 and 3) not yet present in this worktree; they "
        "are read defensively and render as `n/a (pre-integration)` rather than raising.\n"
        "- **Oracle arm**: `oracle_satisfaction` is implemented by package B2 (plan Phase 15 task "
        "4); until it lands, `OracleSatisfactionRecommender` throws on its first request (no "
        "dataset-scale time wasted), so no oracle summary.json exists pre-integration and its "
        "column renders `n/a (oracle pending package B2)`. Once it lands it upper-bounds hidden "
        "satisfaction among the four arms by construction (it scores directly from the hidden "
        "LatentReaction expectation) -- the headline's oracle-vs-proxy comparison is the check for "
        "that.\n"
    )
    return "\n".join(parts) + "\n"


def parse_args(argv=None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Compare the Phase 15 four-arm engagement-vs-satisfaction experiment "
                    "(V2 TDD §4.4) as comparison.csv + comparison.md.",
    )
    parser.add_argument("--semantic", required=True, type=Path,
                        help="semantic (hnsw) arm result directory")
    parser.add_argument("--engagement", required=True, type=Path,
                        help="engagement-optimized (hnsw_ranker + engagement preset) arm result directory")
    parser.add_argument("--proxy", required=True, type=Path,
                        help="satisfaction-proxy (hnsw_ranker + proxy preset) arm result directory")
    parser.add_argument("--oracle", type=Path, default=None,
                        help="oracle_satisfaction arm result directory (optional; omit while "
                             "package B2 is pending)")
    parser.add_argument("--out", type=Path, default=Path("results/published/phase15"),
                        help="output directory for comparison.csv/comparison.md "
                             "(default: results/published/phase15)")
    parser.add_argument("--precision", type=int, default=6,
                        help="significant figures for floats (default: 6)")
    return parser.parse_args(argv)


def main(argv=None) -> int:
    args = parse_args(argv)
    arms = load_arms(args)

    required_available = [arms[n].available for n in ("semantic", "engagement", "proxy")]
    if not any(required_available):
        warn("no readable summary.json in any of the required arms (semantic/engagement/proxy)")
        return 1

    args.out.mkdir(parents=True, exist_ok=True)
    rows = build_rows(arms, args.precision)

    csv_path = args.out / "comparison.csv"
    write_csv(csv_path, rows)

    md_path = args.out / "comparison.md"
    md_path.write_text(render_markdown(arms, rows, args.precision))

    print(f"phase15_comparison: wrote {csv_path}")
    print(f"phase15_comparison: wrote {md_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
