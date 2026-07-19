#!/usr/bin/env python3
"""phase19_comparison.py — Phase 19 Tier-3 core experiment comparison (V2 TDD §4.13 core experiment,
§6): the BATCH-DEPTH (feed prefetch) freshness-versus-cost frontier under abrupt preference drift,
as produced by scripts/run_phase19_experiment.sh's four-arm matrix (serving.prefetch_depth in
{1, 3, 10, 20}, serving.refill_threshold=0 fixed, everything else identical --
configs/realism-medium-events-drift.json, algorithm hnsw_ranker, seed 42).

Reuses `phase15_comparison.py` (imported directly -- design decision D15: this script contains no
simulation logic, only reads result files and renders tables) for `_get`/`_is_num`/`_read_json`/
`warn`/`fmt`, and `phase17_comparison.py` (imported directly) for its `PENDING` marker convention and
`_render_table` passthrough -- this worktree (Phase 19 package C) follows the exact same
pending-integration idiom P16/P17's comparison scripts established.

Given the four Phase 19 arm result directories (one per batch depth), this reads each arm's
summary.json (+ config.json for the arm-identity section) and writes:

  comparison.csv   one row per arm (4 rows: batch1/batch3/batch10/batch20), one column per reported
                   metric -- the freshness-versus-cost frontier columns plus the two already-real
                   headline metrics (mean hidden satisfaction, session utility U_s).
  comparison.md    headline (batch1-vs-batch20 deltas on the frontier metrics), an integration-status
                   banner, an arm-identity section, the frontier table (ordered by ascending batch
                   depth: 1, 3, 10, 20 -- the natural frontier sweep), and notes.

Arm order is always ascending batch depth (1, 3, 10, 20) -- a missing/unreadable arm still gets a
row, with every cell reported as unavailable rather than the row being silently dropped, so the
output shape never changes across invocations (same contract as phase15/16/17_comparison.py).

WHAT'S REAL TODAY VS WHAT'S PENDING PACKAGE A (read defensively, and DETECTED AT RUNTIME rather than
assumed -- mirrors tests/property/batch_adaptation_statistical_test.cpp's runtime detector):

  - REAL TODAY (no guessing needed, exact paths verified against src/evaluation/results_writer.cpp /
    src/evaluation/event_driven_runner.cpp / include/rr/evaluation/*.hpp):
      * `counts.requests`      -- one RequestFeed event per request (event_driven_runner.cpp
                                  increments this once per RequestFeed handler invocation); this IS
                                  a genuine "feed request count" already, it just does not yet VARY
                                  with prefetch depth because the runner ignores serving.* pre-
                                  integration (depth-1 refill regardless of config).
      * `welfare.mean_immediate_satisfaction`, `session_health.mean_session_utility` -- the existing
        V2 §6 hidden-welfare / session-health headline numbers (Phase 14-16), already populated
        under this experiment's full gate stack.
      * `event_mode.event_log_digest` -- the D20 deterministic event-log digest (Phase 18); used
        here only as the mechanical "are these two arms' event sequences identical" detector, not as
        a reported metric.
  - PENDING PACKAGE A (this phase's own new instrumentation, V2 TDD §4.13/plan Phase 19 task 2: "cost
    + staleness instrumentation"): adaptation delay after drift, satisfaction lost before refresh,
    ranking-computation count (candidates scored), stale-impression rate, mean staleness. None of
    these exist anywhere in this worktree's summary.json schema yet (verified: grepped
    include/rr/evaluation and src/evaluation for "ranking_computation"/"stale"/"adaptation_delay" --
    no hits). Package A/B's EXACT key names are unknown pre-merge, so each is read by a short list of
    plausible CANDIDATE (block, key) paths -- most likely `event_mode.*` (Phase 18 already appends
    event-mode-only fields there, purely additively, "appended after every existing block") or a
    sibling `serving.*` block (mirroring the config block's own name). The first present numeric
    candidate wins; none present renders `n/a (pending package A)`. This is the SAME defensive
    multi-candidate idiom phase16_comparison.py's SESSION_HEALTH_METRICS table established for
    Phase 16's then-pending session_health block -- if package A lands a key not in a candidate list
    below, add it (most-specific first) rather than rewriting the read path.
  - Note: `AdaptationReport` (`summary.json`'s existing top-level `adaptation` block, Phase 10) is
    NOT a candidate source for adaptation delay here -- Phase 18's commit.md explicitly records that
    "coldStart/adaptation report blocks stay unconfigured in event mode ... P19's drift experiment
    defines its own adaptation measures", confirmed by inspecting
    src/evaluation/event_driven_runner.cpp (it never populates `result.adaptation`). Event mode needs
    its OWN drift-adaptation measure (this phase's job), not a repurposing of the round-robin one.

Usage
-----
    python3 scripts/phase19_comparison.py \\
        --batch1 <dir> --batch3 <dir> --batch10 <dir> --batch20 <dir> \\
        [--out results/published/phase19] [--precision 6]

    (No third-party dependencies -- plain python3 is enough.)

Exit status: 0 if at least one of the four arms had a readable summary.json, 1 otherwise.
"""
from __future__ import annotations

import argparse
import csv
import sys
from pathlib import Path
from typing import Optional

# Make sibling-module import work regardless of CWD (scripts/ is not a package -- these scripts are
# plain, independently-runnable files per D15/TDD §22, so this is a straight file-relative import).
sys.path.insert(0, str(Path(__file__).resolve().parent))
import phase15_comparison as p15  # noqa: E402  (see sys.path.insert above)
import phase17_comparison as p17  # noqa: E402  (PENDING marker + _render_table passthrough)

BATCH_DEPTHS = [1, 3, 10, 20]

NA_ARM = "n/a (arm not provided)"
NA_PENDING_A = "n/a (pending package A: field not yet in summary.json)"
PENDING = p17.PENDING  # "n/a (PENDING A/B INTEGRATION)" -- reused for the bit-identical-arms case

# --- Candidate key paths (variable depth) for Phase 19's instrumentation. The LANDED package-A
# schema nests everything under `event_mode.serving.*` (src/evaluation/results_writer.cpp) -- those
# paths lead each list; the pre-merge guesses remain as fallbacks. --------------------------------
CANDIDATES: dict[str, list[tuple]] = {
    "adaptation_delay": [
        ("event_mode", "serving", "adaptation_delay", "mean_interactions"),
        ("event_mode", "adaptation_delay_interactions"),
        ("event_mode", "adaptation_delay_seconds"),
        ("serving", "adaptation_delay_interactions"),
        ("serving", "adaptation_delay_seconds"),
    ],
    "satisfaction_lost_before_refresh": [
        ("event_mode", "serving", "satisfaction_lost_before_refresh"),
        ("serving", "satisfaction_lost_before_refresh"),
    ],
    "ranking_computations": [
        ("event_mode", "ranking_computation_count"),
        ("event_mode", "serving", "ranking_computations"),
        ("serving", "ranking_computation_count"),
        ("serving", "ranking_computations"),
    ],
    "stale_impression_rate": [
        ("event_mode", "serving", "stale_impression_rate"),
        ("serving", "stale_impression_rate"),
    ],
    "mean_staleness": [
        ("event_mode", "mean_staleness_seconds"),
        ("event_mode", "serving", "mean_staleness"),
        ("serving", "mean_staleness_seconds"),
        ("serving", "mean_staleness"),
    ],
}


class Phase19Arm:
    """One batch-depth arm's loaded data (summary.json + config.json)."""

    def __init__(self, batch_depth: int, directory: Optional[Path]):
        self.batch_depth = batch_depth
        self.name = f"batch{batch_depth}"
        self.directory = directory
        self.summary = p15._read_json(directory / "summary.json") if directory is not None else None
        self.config = p15._read_json(directory / "config.json") if directory is not None else None
        if directory is not None and self.summary is None:
            p15.warn(f"{self.name}: no readable summary.json under {directory}")

    @property
    def available(self) -> bool:
        return self.summary is not None

    def metric(self, path: tuple):
        """Exact-path lookup for an ALREADY-REAL field (see module docstring)."""
        if not self.available:
            return NA_ARM
        value = p15._get(self.summary, *path)
        return NA_ARM if value is None else value

    def candidate_metric(self, name: str):
        """Best-effort candidate-path lookup for a PENDING package-A field (see CANDIDATES)."""
        if not self.available:
            return NA_ARM
        for path in CANDIDATES[name]:
            value = p15._get(self.summary, *path)
            if p15._is_num(value):
                return value
        return NA_PENDING_A

    def resolved_prefetch_depth(self):
        return p15._get(self.config, "serving", "prefetch_depth") if self.config else None

    def resolved_refill_threshold(self):
        return p15._get(self.config, "serving", "refill_threshold") if self.config else None


# --- Report columns (V2 TDD §4.13 core experiment report list): -----------------------------------
# (column key, header, kind) where kind is "real" (exact-path metric() lookup) or "candidate"
# (candidate_metric() lookup, name matches a CANDIDATES key). `path` is the exact-path tuple for
# "real" columns, ignored for "candidate" columns.
COLUMNS: list[tuple[str, str, str, Optional[tuple]]] = [
    ("feed_request_count", "feed-request count", "real", ("counts", "requests")),
    ("ranking_computations", "ranking computations (candidates scored)", "candidate", None),
    ("adaptation_delay", "adaptation delay after drift", "candidate", None),
    ("satisfaction_lost_before_refresh", "satisfaction lost before refresh", "candidate", None),
    ("stale_impression_rate", "stale-impression rate", "candidate", None),
    ("mean_staleness", "mean staleness", "candidate", None),
    ("mean_immediate_satisfaction", "mean hidden satisfaction (whole-run)", "real",
     ("welfare", "mean_immediate_satisfaction")),
    ("mean_session_utility", "U_s mean (session utility)", "real",
     ("session_health", "mean_session_utility")),
    ("impressions", "impressions (context)", "real", ("counts", "impressions")),
]

# The frontier's two headline axes (V2 TDD §4.13: "freshness-versus-cost"). Cost prefers the new
# ranking-computation count; freshness prefers adaptation delay. Both fall back to an
# already-real/adjacent proxy when the primary is still pending (noted explicitly in the rendered
# section, never silently substituted).
COST_PRIMARY = "ranking_computations"
COST_FALLBACK = "feed_request_count"
FRESHNESS_PRIMARY = "adaptation_delay"
FRESHNESS_FALLBACK = "satisfaction_lost_before_refresh"


def load_arms(args: argparse.Namespace) -> dict[int, Phase19Arm]:
    return {depth: Phase19Arm(depth, getattr(args, f"batch{depth}")) for depth in BATCH_DEPTHS}


def frontier_value(arm: Phase19Arm, primary: str, fallback: str) -> tuple[object, bool]:
    """(value, used_fallback). `primary`/`fallback` are column keys from COLUMNS."""
    value = column_value(arm, primary)
    if p15._is_num(value):
        return value, False
    return column_value(arm, fallback), True


def column_value(arm: Phase19Arm, key: str):
    for col_key, _header, kind, path in COLUMNS:
        if col_key == key:
            return arm.metric(path) if kind == "real" else arm.candidate_metric(col_key)
    raise KeyError(key)  # pragma: no cover -- programmer error if this fires


def serving_status(arms: dict[int, Phase19Arm]) -> str:
    """Package-A runtime detector (mirrors the C++ statistical test's runtime detector): is
    prefetch depth having ANY measurable effect yet? Checked on event_mode.event_log_digest and the
    already-real counts.requests -- both are expected BIT-IDENTICAL across all four arms until
    package A wires serving.* into the event runner's refill logic."""
    digest_path = ("event_mode", "event_log_digest")
    digests, requests = [], []
    for depth in BATCH_DEPTHS:
        arm = arms[depth]
        if not arm.available:
            return ("UNKNOWN -- not every batch-depth arm was provided; pass all four --batchN "
                    "directories to check this.")
        digest = p15._get(arm.summary, *digest_path)
        req = p15._get(arm.summary, "counts", "requests")
        if digest is None or not p15._is_num(req):
            return ("UNKNOWN -- event_mode.event_log_digest or counts.requests missing from an "
                    "arm's summary.json (was this run under simulation.scheduler='event_queue'?).")
        digests.append(digest)
        requests.append(req)
    if len(set(digests)) == 1 and len(set(requests)) == 1:
        return (f"NOT YET ACTIVE -- all four batch-depth arms are BIT-IDENTICAL "
                f"(event_log_digest={digests[0]!r}, requests={requests[0]:g}). Package A's serving "
                f"strategies are not merged in this tree -- the event runner ignores serving.* and "
                f"always performs depth-1 refill regardless of the configured prefetch_depth.")
    return (f"ACTIVE -- batch-depth arms differ (event_log_digest values: "
            f"{', '.join(str(d) for d in digests)}; requests: {requests}); package A's serving "
            f"strategies appear to be live.")


def render_table(header: list[str], rows: list[list[str]]) -> str:
    return p17.render_table(header, rows)


def build_headline(arms: dict[int, Phase19Arm], precision: int) -> list[str]:
    lines = []
    lo, hi = arms[1], arms[20]
    if not (lo.available and hi.available):
        lines.append("- **batch1 vs batch20**: not available (need both --batch1 and --batch20 "
                     "directories).")
        return lines
    for key, header, _kind, _path in COLUMNS:
        if key == "impressions":
            continue  # context column only, not a headline comparison
        va, vb = column_value(lo, key), column_value(hi, key)
        if not (p15._is_num(va) and p15._is_num(vb)):
            lines.append(f"- {header}: not available yet ({va if not p15._is_num(va) else vb}).")
            continue
        if va == vb:
            lines.append(f"- {header}: **{p15.fmt(va, precision)}** for both batch1 and batch20 "
                         f"-> {PENDING}.")
            continue
        delta = vb - va
        pct = f"{(delta / abs(va) * 100.0):+.1f}%" if va != 0 else "n/a (baseline 0)"
        lines.append(f"- {header}: batch1 **{p15.fmt(va, precision)}** vs batch20 "
                     f"**{p15.fmt(vb, precision)}** ({pct}).")
    return lines


def render_arms_section(arms: dict[int, Phase19Arm]) -> str:
    lines = []
    for depth in BATCH_DEPTHS:
        arm = arms[depth]
        lines.append(f"- **{arm.name}** -- serving.prefetch_depth={depth}")
        if not arm.available:
            lines.append(f"  - status: NOT AVAILABLE ({NA_ARM})")
            continue
        lines.append(f"  - directory: `{arm.directory}`")
        lines.append(f"  - resolved serving.prefetch_depth: {arm.resolved_prefetch_depth()}")
        lines.append(f"  - resolved serving.refill_threshold: {arm.resolved_refill_threshold()}")
    return "\n".join(lines)


def render_frontier_section(arms: dict[int, Phase19Arm], precision: int) -> str:
    header = ["batch depth", "cost (see note)", "freshness (see note)"]
    rows = []
    cost_fallback_used = freshness_fallback_used = False
    for depth in BATCH_DEPTHS:
        arm = arms[depth]
        if not arm.available:
            rows.append([str(depth), NA_ARM, NA_ARM])
            continue
        cost, cost_fb = frontier_value(arm, COST_PRIMARY, COST_FALLBACK)
        fresh, fresh_fb = frontier_value(arm, FRESHNESS_PRIMARY, FRESHNESS_FALLBACK)
        cost_fallback_used = cost_fallback_used or cost_fb
        freshness_fallback_used = freshness_fallback_used or fresh_fb
        rows.append([str(depth), p15.fmt(cost, precision), p15.fmt(fresh, precision)])
    note = []
    note.append(f"cost = `{COST_PRIMARY}`" + (f" (fell back to `{COST_FALLBACK}` where pending)"
                                               if cost_fallback_used else ""))
    note.append(f"freshness = `{FRESHNESS_PRIMARY}`" +
                (f" (fell back to `{FRESHNESS_FALLBACK}` where pending)"
                 if freshness_fallback_used else ""))
    return render_table(header, rows) + "\n\n" + "; ".join(note) + ".\n"


def write_csv(path: Path, arms: dict[int, Phase19Arm], precision: int) -> None:
    header = ["arm", "batch_depth"] + [h for _k, h, _kind, _p in COLUMNS]
    with path.open("w", newline="") as fh:
        writer = csv.writer(fh)
        writer.writerow(header)
        for depth in BATCH_DEPTHS:
            arm = arms[depth]
            row = [arm.name, str(depth)]
            for key, _header, _kind, _path in COLUMNS:
                row.append(p15.fmt(column_value(arm, key), precision))
            writer.writerow(row)


def render_markdown(arms: dict[int, Phase19Arm], precision: int) -> str:
    parts = []
    parts.append("# Phase 19 -- Batch-Depth Freshness-vs-Cost Frontier Under Abrupt Drift\n")
    parts.append(
        "V2 TDD §4.13 Tier-3 core experiment: serving.prefetch_depth in {1, 3, 10, 20} "
        "(serving.refill_threshold=0 fixed), algorithm `hnsw_ranker` on "
        "`configs/realism-medium-events-drift.json` (event-mode, full "
        "content_v2/latent_reactions/session_dynamics gate stack, the P10 abrupt-drift schedule "
        "retimed for event mode -- see that config / `scripts/run_phase19_experiment.sh`'s header "
        "for the horizon/drift design notes), seed 42. Generated by "
        "`scripts/phase19_comparison.py`.\n"
    )

    parts.append("## Headline -- batch1 vs batch20\n")
    parts.extend(build_headline(arms, precision))
    parts.append(
        f"\n`{PENDING}` means both arms' values ARE present and numeric but bit-identical -- the "
        "expected state until package A (real serving strategies) lands; see Integration status "
        "below. A cell reading \"not available yet\" means the underlying field does not exist in "
        "this arm's summary.json at all (also pending package A).\n"
    )

    parts.append("## Integration status\n")
    parts.append(f"- **Serving strategies (package A)**: {serving_status(arms)}")
    parts.append("")

    parts.append("## Arms\n")
    parts.append(render_arms_section(arms))
    parts.append("")

    parts.append("## Freshness-versus-cost frontier (V2 TDD §4.13)\n")
    parts.append(
        "One point per batch depth, ordered by ascending depth (the natural frontier sweep: larger "
        "prefetch batches amortize ranking cost over more served impressions per request, at the "
        "price of staler content when preferences move).\n"
    )
    parts.append(render_frontier_section(arms, precision))

    parts.append("## Full per-arm table\n")
    parts.append("See `comparison.csv` for the same table in machine-readable form.\n")
    full_header = ["arm"] + [h for _k, h, _kind, _p in COLUMNS]
    full_rows = [[arms[d].name] + [p15.fmt(column_value(arms[d], k), precision)
                                    for k, _h, _kind, _p in COLUMNS]
                 for d in BATCH_DEPTHS]
    parts.append(render_table(full_header, full_rows))
    parts.append("")

    parts.append("## Notes\n")
    parts.append(
        "- **No aggregate score** (D22/V2 TDD §6): the frontier above is reported as two SEPARATE "
        "axes (cost, freshness), never combined into one number. `mean_immediate_satisfaction` and "
        "`mean_session_utility` (U_s) are reported in the full table for context but are not "
        "themselves the frontier's axes -- see V2 TDD §4.13's own report list (adaptation delay, "
        "satisfaction lost before refresh, feed-request count, ranking-computation count, "
        "stale-impression rate).\n"
        "- **What's real today vs. pending package A** -- see this script's module docstring for "
        "the full contract. In short: `feed_request_count`, `mean_immediate_satisfaction`, and "
        "`mean_session_utility` are already-real, populated fields; `ranking_computations`, "
        "`adaptation_delay`, `satisfaction_lost_before_refresh`, `stale_impression_rate`, and "
        "`mean_staleness` are Phase 19's own new instrumentation (plan Phase 19 task 2), read "
        "defensively by CANDIDATE summary.json key path because package A/B's exact key names are "
        "not fixed from this worktree. If package A lands a key not in this script's `CANDIDATES` "
        "table, add it (most-specific first) -- no other part of this script needs to change.\n"
        "- **Pending-integration detection is COMPUTED, not assumed** (`serving_status` above): "
        "this script compares real numbers (event-log digest, request counts) across arms at read "
        "time rather than hard-coding \"expect no effect\". If package A has landed by the time "
        "this is run, the Integration status section and the headline deltas will show real, "
        "non-`" + PENDING + "` numbers automatically, with no script changes needed.\n"
        "- **Concurrency-contention caveat**: if the arms were run CONCURRENTLY "
        "(`scripts/run_phase19_experiment.sh`'s default 4-concurrent mode), wall-clock/timing "
        "numbers carry cache and memory-bandwidth contention (same caveat as every prior phase's "
        "concurrent runs). Every other number in this report is deterministic (rng/clock-free, "
        "D8/D9) and unaffected by run mode.\n"
        "- **Drift design**: the base config's `drift.events` block is copied VERBATIM from "
        "`configs/phase10-drift.json` (four disjoint per-user quartile cohorts, each retargeted to "
        "a 3-topic mix at per-user interaction 100) -- see "
        "`configs/realism-medium-events-drift.json` / `scripts/run_phase19_experiment.sh`'s header "
        "for the full horizon-sizing and retiming reasoning (in short: `at_interaction` keys off "
        "each user's own completed-interaction count, identically under both runners, so no "
        "retiming arithmetic was needed -- only the run's `horizon_seconds` needed sizing so users "
        "have a chance to reach and recover from that per-user interaction mark).\n"
    )
    return "\n".join(parts) + "\n"


def parse_args(argv=None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Compare the Phase 19 batch-depth freshness-vs-cost frontier experiment "
                    "(V2 TDD §4.13) as comparison.csv + comparison.md.",
    )
    for depth in BATCH_DEPTHS:
        parser.add_argument(f"--batch{depth}", type=Path, default=None,
                            help=f"batch-depth-{depth} arm result directory "
                                 f"(serving.prefetch_depth={depth})")
    parser.add_argument("--out", type=Path, default=Path("results/published/phase19"),
                        help="output directory for comparison.csv/comparison.md "
                             "(default: results/published/phase19)")
    parser.add_argument("--precision", type=int, default=6,
                        help="significant figures for floats (default: 6)")
    return parser.parse_args(argv)


def main(argv=None) -> int:
    args = parse_args(argv)
    arms = load_arms(args)

    if not any(arm.available for arm in arms.values()):
        p15.warn("no readable summary.json in any of the four batch-depth arms")
        return 1

    args.out.mkdir(parents=True, exist_ok=True)

    csv_path = args.out / "comparison.csv"
    write_csv(csv_path, arms, args.precision)

    md_path = args.out / "comparison.md"
    md_path.write_text(render_markdown(arms, args.precision))

    print(f"phase19_comparison: wrote {csv_path}")
    print(f"phase19_comparison: wrote {md_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
