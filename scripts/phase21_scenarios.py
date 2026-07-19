#!/usr/bin/env python3
"""phase21_scenarios.py — Phase 21 per-scenario arm comparison (V2 TDD §4.18, Tier 4 acceptance
2-3, §6; docs/design/P21-CONTRACTS.md §6, package D).

Renders ONE scenario's arm-vs-arm comparison (a control plus 1-4 treatment arms on an identical
world, contracts §4) as `<out>/<scenario>-comparison.md` + `.csv`. This script contains no
simulation logic (D15): it only reads each arm's `summary.json` / `config.json` /
`ecosystem_metrics.csv` / `longterm_metrics.csv` and renders tables — the same contract as
phase15/17/20_comparison.py, whose `_get`/`_read_json`/`_is_num`/`warn`/`fmt` helpers this script
imports directly (sys.path-relative import, the pattern phase19/20_comparison.py established).

Reads, by FROZEN name only (contracts §6: "absent block -> labeled n/a cell, never a crash or a
guessed key" -- no candidate-path guessing anywhere in this file):

  - The four V2 §6 metric groups, verified directly against this checkout's
    `src/evaluation/results_writer.cpp` (the same verification approach phase15/17/20_comparison.py
    use), matching that file's own `metric_groups` index (engagement / hidden_user_welfare /
    session_health / recommendation_quality):
      * Engagement       -- `counts`/`metrics`/`metric_groups.engagement` (impressions, requests,
                             watch/completion/like/share/follow, comment/save/profile-visit,
                             reward/impression). Unconditional (V1 baseline).
      * Hidden welfare    -- `welfare` block incl. `platform_trust` (P20 gate). Present whenever
                             `realism.latent_reactions` is on.
      * Session health    -- `session_health` block (P16 gate: `realism.session_dynamics`).
      * Recommendation quality -- `diversity` block (unconditional) + `metrics.mean_true_affinity`
                             + `learning.final_estimated_hidden_cosine`.
  - `long_term` (P20 gate: `realism.preference_evolution || retention.enabled`) -- the frozen
    contracts §5 snake_case keys: retention_1d, retention_7d, sessions_per_user_per_day,
    satisfaction_weighted_retention, churn_rate, mean_churn_probability, mean_final_trust,
    mean_final_habit, mean_preference_shift_from_initial, mean_final_preference_entropy. (The
    scaffold's real writer also emits an 11th bonus key, `retention_configured` -- see
    phase20_comparison.py's module docstring; this script does not read it, since this package's
    brief lists exactly the ten frozen keys above and reading extra un-briefed keys would be its
    own kind of guessing.)
  - `ecosystem` (P21 gate: `evaluation.ecosystem_metrics`, contracts §2) -- creator_hhi_final_day,
    creator_hhi_whole_run, tail_creator_share_whole_run, arch_share_whole_run (object keyed by the
    eight catalog archetype names in index order: genuinely_satisfying, useful, ragebait,
    clickbait, comfort, polished_irrelevant, niche_treasure, background_music -- verified against
    `kEcosystemArchNames` in results_writer.cpp), niche_in_cohort_match_rate_whole_run.
  - `ecosystem_metrics.csv` + `longterm_metrics.csv` (contracts §2/§3, frozen headers) for the
    per-day mini-table: creator_hhi / arch_ragebait / arch_niche_treasure / tail_creator_share (from
    the former) and the TRAILING `mean_preference_entropy` column (from the latter), at the first /
    middle / final SIMULATED DAY present in either file (a defensive union -- the two files are
    gated independently, so a run with one but not the other still gets a partial row instead of
    being dropped).
  - `config.json`'s top-level `description` string (contracts §4: the pre-registered "HYPOTHESIS: …
    MECHANISM: … EXPECTED SIGNATURE: … VERDICT CRITERIA: …" block, identical across a scenario's
    arms) -- read from the first arm (in the order given on the command line) whose config.json has
    a non-empty `description`, and quoted verbatim at the top of the rendered report.

NO SCHEMA DISCREPANCY FOUND: every key this script reads was checked directly against this
checkout's `src/evaluation/results_writer.cpp` (HEAD d769edb, the Phase 21 scaffolding commit) and
matches docs/design/P21-CONTRACTS.md §2/§3 exactly (unlike phase20_comparison.py's one flagged
`retention_configured` discrepancy, there is nothing to flag here).

Design choice (not a contract ambiguity -- this script's own output shape): `<scenario>-comparison
.csv` carries ONLY the flat whole-run arms-x-groups table (one row per arm, matching every prior
phaseN_comparison.py's write_csv shape, plus a leading `scenario` column so per-scenario CSVs
concatenate cleanly). The per-day mini-table, the verbatim pre-registration quote, and the VERDICT
section are prose/structurally-different content that belongs in the `.md` only -- mirroring how
phase20_comparison.py's headline/availability sections are `.md`-only while its `comparison.csv`
stays a flat metric table.

Usage
-----
    python3 scripts/phase21_scenarios.py \\
        --scenario filter_bubble \\
        --arm control=results/phase21/filter_bubble/control/<experiment-id> \\
        --arm treatment=results/phase21/filter_bubble/treatment/<experiment-id> \\
        --out results/phase21/filter_bubble [--verdict "TEXT"] [--precision 6]

    Writes `results/phase21/filter_bubble/filter_bubble-comparison.md` + `.csv`. Any number of
    `--arm LABEL=DIR` may be repeated (control + 1-4 treatments, contracts §4); arms render in the
    order given on the command line. A missing/unreadable arm still gets a row/column, with every
    cell reported as unavailable rather than the row being silently dropped (same contract as every
    prior phaseN_comparison.py).

    python3 scripts/phase21_scenarios.py --self-test
        Builds synthetic fixture dirs (tiny summary.json + config.json + ecosystem_metrics.csv +
        longterm_metrics.csv) under a tempdir, runs the loaders/renderers end-to-end (including a
        full in-process `main()` call), asserts the outputs contain expected cells, prints PASS/FAIL
        lines, cleans up, and exits. Ignores every other argument.

    (No third-party dependencies -- plain python3 is enough, same as phase15/17/20_comparison.py;
    only scripts/plot_results.py's additions need the uv/3.12 environment.)

Exit status: 0 if at least one arm had a readable summary.json (or --self-test passed), 1 if none
did, 2 on a CLI usage error (e.g. missing --scenario/--arm/--out without --self-test).
"""
from __future__ import annotations

import argparse
import csv
import json
import sys
import tempfile
from pathlib import Path
from typing import Optional

# Make sibling-module import work regardless of CWD (scripts/ is not a package -- these scripts are
# plain, independently-runnable files per D15/TDD §22, the sys.path-relative import pattern
# scripts/phase19_comparison.py established and phase20_comparison.py reuses).
sys.path.insert(0, str(Path(__file__).resolve().parent))
import phase15_comparison as p15  # noqa: E402  (see sys.path.insert above)

NA_ARM = "n/a (no readable summary.json)"
DEFAULT_VERDICT = "VERDICT: integrator judgement pending"

# Frozen archetype catalog names, catalog index order (contracts §2, verified against
# `kEcosystemArchNames` in src/evaluation/results_writer.cpp). Shared by the ecosystem_metrics.csv
# `arch_*` columns and the `ecosystem.arch_share_whole_run` summary object.
ARCH_NAMES: list = [
    "genuinely_satisfying", "useful", "ragebait", "clickbait", "comfort",
    "polished_irrelevant", "niche_treasure", "background_music",
]

# --- Report column groups: (column key, header, exact summary.json path). Every path verified
# directly against src/evaluation/results_writer.cpp in this checkout (D769edb) -- no candidate-path
# guessing (contracts §6). -------------------------------------------------------------------------

ENGAGEMENT_COLUMNS: list = [
    ("impressions", "impressions", ("counts", "impressions")),
    ("requests", "requests", ("counts", "requests")),
    ("mean_watch_seconds", "mean watch seconds", ("metrics", "mean_watch_seconds")),
    ("completion_rate", "completion rate", ("metrics", "completion_rate")),
    ("like_rate", "like rate", ("metrics", "like_rate")),
    ("share_rate", "share rate", ("metrics", "share_rate")),
    ("follow_rate", "follow rate", ("metrics", "follow_rate")),
    ("comment_rate", "comment rate", ("metric_groups", "engagement", "comment_rate")),
    ("save_rate", "save rate", ("metric_groups", "engagement", "save_rate")),
    ("profile_visit_rate", "profile-visit rate", ("metric_groups", "engagement", "profile_visit_rate")),
    ("reward_per_impression", "reward/impression", ("metrics", "reward_per_impression")),
]

WELFARE_COLUMNS: list = [
    ("mean_immediate_satisfaction", "mean hidden satisfaction",
     ("welfare", "mean_immediate_satisfaction")),
    ("mean_regret", "mean hidden regret", ("welfare", "mean_regret")),
    ("satisfaction_per_minute", "satisfaction per minute", ("welfare", "satisfaction_per_minute")),
    ("harmful_fatigue", "harmful fatigue", ("welfare", "harmful_fatigue")),
    ("platform_trust", "platform trust", ("welfare", "platform_trust")),
]

SESSION_HEALTH_COLUMNS: list = [
    ("sessions", "sessions (closed)", ("session_health", "sessions")),
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

RECOMMENDATION_QUALITY_COLUMNS: list = [
    ("mean_true_affinity", "mean true affinity", ("metrics", "mean_true_affinity")),
    ("final_estimated_hidden_cosine", "estimated<->hidden cosine",
     ("learning", "final_estimated_hidden_cosine")),
    ("mean_unique_creators", "mean unique creators/feed", ("diversity", "mean_unique_creators")),
    ("mean_creator_hhi_per_feed", "mean creator HHI (per feed)", ("diversity", "mean_creator_hhi")),
    ("mean_intra_list_similarity", "mean intra-list similarity",
     ("diversity", "mean_intra_list_similarity")),
]

# Frozen contracts §5 long_term keys (exactly the ten this package's brief lists -- deliberately NOT
# including the scaffold's bonus `retention_configured` key, see module docstring).
LONG_TERM_COLUMNS: list = [
    ("retention_1d", "retention (1d)", ("long_term", "retention_1d")),
    ("retention_7d", "retention (7d)", ("long_term", "retention_7d")),
    ("sessions_per_user_per_day", "sessions/user/day", ("long_term", "sessions_per_user_per_day")),
    ("satisfaction_weighted_retention", "satisfaction-weighted retention",
     ("long_term", "satisfaction_weighted_retention")),
    ("churn_rate", "churn rate", ("long_term", "churn_rate")),
    ("mean_churn_probability", "mean churn probability", ("long_term", "mean_churn_probability")),
    ("mean_final_trust", "mean final trust", ("long_term", "mean_final_trust")),
    ("mean_final_habit", "mean final habit", ("long_term", "mean_final_habit")),
    ("mean_preference_shift_from_initial", "mean pref shift from initial",
     ("long_term", "mean_preference_shift_from_initial")),
    ("mean_final_preference_entropy", "mean final preference entropy",
     ("long_term", "mean_final_preference_entropy")),
]

# Frozen contracts §2 ecosystem keys (creator_hhi_final_day / creator_hhi_whole_run /
# tail_creator_share_whole_run / arch_share_whole_run{8 names} / niche_in_cohort_match_rate_whole_run).
ECOSYSTEM_COLUMNS: list = (
    [
        ("creator_hhi_final_day", "creator HHI (final day)", ("ecosystem", "creator_hhi_final_day")),
        ("creator_hhi_whole_run", "creator HHI (whole run)", ("ecosystem", "creator_hhi_whole_run")),
        ("tail_creator_share_whole_run", "tail-creator share (whole run)",
         ("ecosystem", "tail_creator_share_whole_run")),
    ]
    + [
        (f"arch_share_whole_run__{name}", f"arch share whole-run: {name}",
         ("ecosystem", "arch_share_whole_run", name))
        for name in ARCH_NAMES
    ]
    + [
        ("niche_in_cohort_match_rate_whole_run", "niche in-cohort match rate (whole run)",
         ("ecosystem", "niche_in_cohort_match_rate_whole_run")),
    ]
)

GROUPS: list = [
    ("Engagement", ENGAGEMENT_COLUMNS),
    ("Hidden welfare", WELFARE_COLUMNS),
    ("Session health", SESSION_HEALTH_COLUMNS),
    ("Recommendation quality", RECOMMENDATION_QUALITY_COLUMNS),
    ("Long-term (Phase 20)", LONG_TERM_COLUMNS),
    ("Ecosystem (Phase 21)", ECOSYSTEM_COLUMNS),
]

ALL_COLUMNS: list = (
    ENGAGEMENT_COLUMNS + WELFARE_COLUMNS + SESSION_HEALTH_COLUMNS + RECOMMENDATION_QUALITY_COLUMNS
    + LONG_TERM_COLUMNS + ECOSYSTEM_COLUMNS
)

# The five per-day metrics pulled from the two frozen CSVs (contracts §2/§3) for the mini-table:
# (column key, header, source) where source in {"ecosystem", "longterm"} selects which CSV-derived
# per-day dict to read.
PER_DAY_METRICS: list = [
    ("creator_hhi", "creator_hhi", "ecosystem"),
    ("arch_ragebait", "arch_ragebait", "ecosystem"),
    ("arch_niche_treasure", "arch_niche_treasure", "ecosystem"),
    ("tail_creator_share", "tail_creator_share", "ecosystem"),
    ("mean_preference_entropy", "mean_preference_entropy", "longterm"),
]


# --- Arm loading --------------------------------------------------------------------------------


class Arm:
    """One arm's loaded data: summary.json + config.json (frozen-key reads only, contracts §6)."""

    def __init__(self, label: str, directory: Optional[Path]):
        self.label = label
        self.directory = directory
        self.summary = p15._read_json(directory / "summary.json") if directory is not None else None
        self.config = p15._read_json(directory / "config.json") if directory is not None else None
        if directory is not None and self.summary is None:
            p15.warn(f"{label}: no readable summary.json under {directory}")

    @property
    def available(self) -> bool:
        return self.summary is not None

    def block_present(self, block: str) -> bool:
        return isinstance(self.summary, dict) and isinstance(self.summary.get(block), dict)

    def metric(self, path: tuple):
        """Frozen-name-exact lookup (no candidate guessing, contracts §6). Distinguishes: the whole
        arm never loaded (NA_ARM); the CONTAINING TOP-LEVEL BLOCK is absent -- e.g. `ecosystem` on a
        gate-off arm, or `long_term` pre-Phase-20 (a clearly labeled, distinct message); or just this
        one key is missing from an otherwise-present block. Works for arbitrarily deep paths (e.g.
        the 3-deep `("ecosystem", "arch_share_whole_run", "ragebait")`) since block-presence is
        always checked against path[0], the top-level gated block.
        """
        if not self.available:
            return NA_ARM
        value = p15._get(self.summary, *path)
        if value is not None:
            return value
        if not self.block_present(path[0]):
            return f"n/a (block '{path[0]}' absent)"
        return f"n/a (key '{'.'.join(path)}' absent)"

    def description(self) -> Optional[str]:
        """This arm's config.json top-level `description` string (contracts §4 pre-registration
        block), or None if config.json is unavailable or the field is absent/empty."""
        if not isinstance(self.config, dict):
            return None
        desc = self.config.get("description")
        return desc if isinstance(desc, str) and desc.strip() else None


def _parse_arm_arg(s: str) -> tuple:
    """Parses one `--arm LABEL=DIR` value. Raises ValueError (caught by argparse's `type=`
    machinery, which reports it as a normal usage error) when there is no '=' or either side is
    empty after stripping."""
    if "=" not in s:
        raise ValueError(f"--arm value {s!r} must be of the form LABEL=DIR")
    label, _, directory = s.partition("=")
    label, directory = label.strip(), directory.strip()
    if not label or not directory:
        raise ValueError(f"--arm value {s!r} must be of the form LABEL=DIR (both non-empty)")
    return label, directory


def load_arms(arm_pairs: list) -> dict:
    """Builds the label -> Arm map in COMMAND-LINE ORDER (unlike phase15/17/20_comparison.py's
    fixed ARM_ORDER: this tool's arm set/count/names vary per scenario, contracts §4, so there is no
    fixed canonical order -- CLI order is the only meaningful one). A repeated label overwrites the
    earlier one (warned)."""
    arms: dict = {}
    for label, directory in arm_pairs:
        if label in arms:
            p15.warn(f"--arm {label}=... given more than once; using the last occurrence")
        arms[label] = Arm(label, Path(directory))
    return arms


# --- Per-day CSV reads (contracts §2/§3 frozen headers) -----------------------------------------


def read_ecosystem_csv(path: Path) -> dict:
    """day(int) -> {creator_hhi, tail_creator_share, arch_ragebait, arch_niche_treasure} from
    ecosystem_metrics.csv (contracts §2 frozen header). Returns {} if the file is absent,
    unreadable, or missing any of the required columns -- never raises; the caller treats {} as "no
    per-day ecosystem data for this arm" (expected for a gate-off arm, contracts §1)."""
    if not path.exists():
        return {}
    required = ["day", "creator_hhi", "tail_creator_share", "arch_ragebait", "arch_niche_treasure"]
    try:
        with path.open(newline="") as fh:
            reader = csv.DictReader(fh)
            fieldnames = reader.fieldnames or []
            missing = [c for c in required if c not in fieldnames]
            if missing:
                p15.warn(f"{path}: missing required column(s) {missing}; treating per-day "
                         f"ecosystem data as unavailable")
                return {}
            out: dict = {}
            for row in reader:
                try:
                    out[int(row["day"])] = {
                        "creator_hhi": float(row["creator_hhi"]),
                        "tail_creator_share": float(row["tail_creator_share"]),
                        "arch_ragebait": float(row["arch_ragebait"]),
                        "arch_niche_treasure": float(row["arch_niche_treasure"]),
                    }
                except (KeyError, ValueError) as exc:
                    p15.warn(f"{path}: skipping unparseable row (day={row.get('day')!r}): {exc}")
            return out
    except Exception as exc:  # malformed CSV / IO error
        p15.warn(f"failed to read {path}: {exc}")
        return {}


def read_longterm_entropy_csv(path: Path) -> dict:
    """day(int) -> mean_preference_entropy(float), the TRAILING column longterm_metrics.csv gains
    under contracts §3. Returns {} if the file is absent, unreadable, or missing `day` /
    `mean_preference_entropy` -- never raises; {} is the expected state for a pre-Phase-21 run whose
    longterm_metrics.csv predates the trailing column, or any run with long_term not configured."""
    if not path.exists():
        return {}
    try:
        with path.open(newline="") as fh:
            reader = csv.DictReader(fh)
            fieldnames = reader.fieldnames or []
            if "day" not in fieldnames or "mean_preference_entropy" not in fieldnames:
                p15.warn(f"{path}: missing day/mean_preference_entropy column(s); treating "
                         f"per-day entropy as unavailable")
                return {}
            out: dict = {}
            for row in reader:
                try:
                    out[int(row["day"])] = float(row["mean_preference_entropy"])
                except (KeyError, ValueError) as exc:
                    p15.warn(f"{path}: skipping unparseable row (day={row.get('day')!r}): {exc}")
            return out
    except Exception as exc:
        p15.warn(f"failed to read {path}: {exc}")
        return {}


def per_day_snapshot(arm: Arm) -> list:
    """First / middle / final SIMULATED DAY rows (contracts §6 "per-day mini-table") for this arm,
    pulled from ecosystem_metrics.csv + longterm_metrics.csv (a defensive UNION of whichever days
    either file has -- the two files are gated independently, contracts §1/§3, so an arm with only
    one of them still gets a partial row rather than being dropped). Returns a list of dicts with
    keys slice/day/creator_hhi/arch_ragebait/arch_niche_treasure/tail_creator_share/
    mean_preference_entropy (each value a float or a distinct "n/a (...)" string). A single
    slice="n/a" fallback row is returned when NEITHER file has any day at all for this arm.
    """
    eco = read_ecosystem_csv(arm.directory / "ecosystem_metrics.csv") if arm.directory else {}
    lt = read_longterm_entropy_csv(arm.directory / "longterm_metrics.csv") if arm.directory else {}
    days = sorted(set(eco) | set(lt))
    if not days:
        na = "n/a (no ecosystem_metrics.csv or longterm_metrics.csv day data for this arm)"
        return [{"slice": "n/a", "day": "n/a", "creator_hhi": na, "arch_ragebait": na,
                 "arch_niche_treasure": na, "tail_creator_share": na, "mean_preference_entropy": na}]

    picks = [("first", days[0]), ("mid", days[len(days) // 2]), ("final", days[-1])]
    rows = []
    for slice_label, day in picks:
        e = eco.get(day)
        lt_val = lt.get(day)
        e_na = f"n/a (no ecosystem_metrics.csv row for day {day})"
        lt_na = f"n/a (no longterm_metrics.csv row for day {day})"
        rows.append({
            "slice": slice_label,
            "day": day,
            "creator_hhi": e["creator_hhi"] if e else e_na,
            "arch_ragebait": e["arch_ragebait"] if e else e_na,
            "arch_niche_treasure": e["arch_niche_treasure"] if e else e_na,
            "tail_creator_share": e["tail_creator_share"] if e else e_na,
            "mean_preference_entropy": lt_val if lt_val is not None else lt_na,
        })
    return rows


def find_description(arms: dict) -> Optional[str]:
    """The scenario's pre-registered `description` (contracts §4), read from the FIRST arm (in the
    order given on the command line) whose config.json carries a non-empty one. All of a scenario's
    arms share the same description (same scenario config, different arm overrides), so any arm
    having it is sufficient; None if no arm's config.json has it (config.json unreadable for every
    arm, or every arm predates the field)."""
    for arm in arms.values():
        desc = arm.description()
        if desc is not None:
            return desc
    return None


# --- Rendering -----------------------------------------------------------------------------------


def render_table(header: list, rows: list) -> str:
    """Left-justified, padded, pipe-delimited markdown table (same visual style as
    phase15_comparison.py's render_markdown_table, generalized to an explicit header/rows pair)."""
    widths = [len(h) for h in header]
    for row in rows:
        for i, cell in enumerate(row):
            widths[i] = max(widths[i], len(str(cell)))

    def line(cells: list) -> str:
        return "| " + " | ".join(str(c).ljust(widths[i]) for i, c in enumerate(cells)) + " |"

    out = [line(header), "| " + " | ".join("-" * w for w in widths) + " |"]
    out.extend(line(row) for row in rows)
    return "\n".join(out)


def render_group_table(arms: dict, columns: list, precision: int) -> str:
    header = ["arm"] + [h for _k, h, _p in columns]
    rows = []
    for arm in arms.values():
        rows.append([arm.label] + [p15.fmt(arm.metric(p), precision) for _k, _h, p in columns])
    return render_table(header, rows)


def render_per_day_table(arms: dict, precision: int) -> str:
    header = ["arm", "slice", "day"] + [h for _k, h, _s in PER_DAY_METRICS]
    rows = []
    for arm in arms.values():
        for snap in per_day_snapshot(arm):
            row = [arm.label, snap["slice"], p15.fmt(snap["day"], precision)]
            for key, _h, _s in PER_DAY_METRICS:
                row.append(p15.fmt(snap[key], precision))
            rows.append(row)
    return render_table(header, rows)


def render_arms_section(arms: dict) -> list:
    lines = ["## Arms and data availability\n"]
    block_names = ["welfare", "session_health", "long_term", "ecosystem"]
    for arm in arms.values():
        lines.append(f"- **{arm.label}**")
        lines.append(f"  - directory: `{arm.directory}`")
        if not arm.available:
            lines.append(f"  - status: NOT AVAILABLE ({NA_ARM})")
            continue
        presence = ", ".join(f"{b}={arm.block_present(b)}" for b in block_names)
        lines.append(f"  - gated-block presence: {presence}")
        lines.append(f"  - config.json `description` present: {arm.description() is not None}")
    return lines


def render_notes() -> list:
    return [
        "## Notes\n",
        "- **No aggregate score** (D22 / V2 TDD §6): every group above (engagement, hidden "
        "welfare, session health, recommendation quality, long-term, ecosystem) is reported "
        "SEPARATELY; this script never combines them into one number, and neither do "
        "plot_results.py's Phase 21 additions.\n",
        "- **Frozen-schema reads only** (contracts §6): every key this script reads is quoted "
        "verbatim from contracts §2 (ecosystem) / §3 (longterm_metrics.csv's trailing column) / "
        "§5 (long_term, inherited from Phase 20) / the results_writer.cpp-verified V2 §6 group "
        "keys -- no candidate-path guessing. A block genuinely absent for an arm (e.g. a control "
        "arm run with a different gate set) renders as a clearly labeled n/a cell, never a crash "
        "or a silently-guessed alternate key.\n",
        "- **Per-day mini-table method**: first/middle/final SIMULATED DAY over the UNION of days "
        "present in `ecosystem_metrics.csv` and `longterm_metrics.csv` (the two files are gated "
        "independently -- contracts §1 requires the event scheduler for the former, §5's P20 gates "
        "for the latter -- so an arm with only one of them still gets a partial row).\n",
        "- **CSV/MD content split**: `<scenario>-comparison.csv` carries only the flat whole-run "
        "arms-x-groups table (one row per arm); the per-day mini-table, the verbatim "
        "pre-registration quote, and the verdict are `.md`-only (see this script's module "
        "docstring).\n",
        "- **Concurrency-contention caveat**: if arms were run CONCURRENTLY (contracts §4 caps "
        "this at 2 simulate processes per package), wall-clock/timing numbers carry cache and "
        "memory-bandwidth contention; every other number in this report is deterministic "
        "(rng/clock-free, D8/D9) and unaffected by run mode.\n",
    ]


def render_description_section(arms: dict) -> list:
    lines = ["## Pre-registration (verbatim, from an arm's `config.json` `description`)\n"]
    desc = find_description(arms)
    if desc is None:
        lines.append(
            "*n/a -- no arm's `config.json` has a non-empty top-level `description` field "
            "(contracts §4 requires every scenario config to carry the pre-registered "
            "HYPOTHESIS/MECHANISM/EXPECTED SIGNATURE/VERDICT CRITERIA block BEFORE any run; its "
            "absence here means either config.json is unreadable for every arm, or every arm "
            "predates the field).*\n"
        )
        return lines
    lines.extend(f"> {line}" if line else ">" for line in desc.splitlines())
    lines.append("")
    return lines


def render_markdown(scenario: str, arms: dict, verdict_text: str, precision: int) -> str:
    parts = [f"# Phase 21 -- {scenario}: Arm Comparison\n"]
    parts.append(
        f"V2 TDD §4.18, Tier 4 acceptance 2-3, §6 (docs/design/P21-CONTRACTS.md §4/§6): scenario "
        f"`{scenario}`, a control plus 1-4 treatment arms on an IDENTICAL world (same seed), only "
        f"ranking weights / exploration / cohort_mix / archetype mixture differing between arms. "
        f"Generated by `scripts/phase21_scenarios.py`.\n"
    )

    parts.extend(render_description_section(arms))

    parts.extend(render_arms_section(arms))
    parts.append("")

    parts.append("## Metric groups (four V2 §6 groups + long-term + ecosystem)\n")
    for label, columns in GROUPS:
        parts.append(f"### {label}\n")
        parts.append(render_group_table(arms, columns, precision))
        parts.append("")

    parts.append("## Per-day snapshot (first / mid / final simulated day)\n")
    parts.append(
        "creator_hhi / arch_ragebait / arch_niche_treasure / tail_creator_share from "
        "`ecosystem_metrics.csv`; mean_preference_entropy from `longterm_metrics.csv`'s trailing "
        "column (contracts §2/§3).\n"
    )
    parts.append(render_per_day_table(arms, precision))
    parts.append("")

    parts.append("## Verdict\n")
    parts.append(f"{verdict_text}\n")

    parts.extend(render_notes())
    return "\n".join(parts) + "\n"


def write_csv(path: Path, scenario: str, arms: dict, precision: int) -> None:
    header = ["scenario", "arm"] + [h for _k, h, _p in ALL_COLUMNS]
    with path.open("w", newline="") as fh:
        writer = csv.writer(fh)
        writer.writerow(header)
        for arm in arms.values():
            row = [scenario, arm.label]
            for _k, _h, col_path in ALL_COLUMNS:
                row.append(p15.fmt(arm.metric(col_path), precision))
            writer.writerow(row)


# --- CLI -------------------------------------------------------------------------------------


def parse_args(argv=None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Render one Phase 21 scenario's arm-vs-arm comparison "
                    "(docs/design/P21-CONTRACTS.md §6) as <scenario>-comparison.{md,csv}.",
    )
    parser.add_argument("--scenario", default=None,
                        help="scenario name, e.g. filter_bubble (required unless --self-test)")
    parser.add_argument("--arm", action="append", dest="arms", default=None, type=_parse_arm_arg,
                        metavar="LABEL=DIR",
                        help="one arm as LABEL=DIR; repeat for each arm (control + 1-4 treatments, "
                             "contracts §4). Required at least once, unless --self-test.")
    parser.add_argument("--out", type=Path, default=None,
                        help="output directory for <scenario>-comparison.{md,csv} "
                             "(required unless --self-test)")
    parser.add_argument("--verdict", default=None,
                        help="verdict text (default: 'VERDICT: integrator judgement pending')")
    parser.add_argument("--precision", type=int, default=6,
                        help="significant figures for floats (default: 6)")
    parser.add_argument("--self-test", action="store_true", dest="self_test",
                        help="run an in-script synthetic-fixture smoke test end-to-end, print "
                             "PASS/FAIL lines, and exit (ignores every other argument; no "
                             "committed fixture files)")
    args = parser.parse_args(argv)
    if not args.self_test:
        if not args.scenario:
            parser.error("--scenario is required (unless --self-test)")
        if not args.arms:
            parser.error("at least one --arm LABEL=DIR is required (unless --self-test)")
        if not args.out:
            parser.error("--out is required (unless --self-test)")
    return args


def _check(cond: bool, message: str) -> bool:
    print(f"  [{'PASS' if cond else 'FAIL'}] {message}")
    return cond


def run_self_test() -> int:
    """Synthetic-fixture smoke test (no committed fixture files): three arms under a tempdir --
    `control` (every block present, full 5-day ecosystem/longterm CSVs), `treatment` (summary.json
    deliberately MISSING the `ecosystem` block, and NO ecosystem_metrics.csv at all, to exercise the
    n/a-labeling and per-day union paths; longterm_metrics.csv present with a different day range),
    and `missing` (points at a directory with no files at all, to exercise NA_ARM). Exercises the
    loaders/renderers directly AND a full in-process `main()` call. Prints one PASS/FAIL line per
    check."""
    print("phase21_scenarios --self-test")
    ok = True

    with tempfile.TemporaryDirectory(prefix="phase21-scenarios-selftest-") as tmp:
        root = Path(tmp)

        def write_json(path: Path, obj: dict) -> None:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(json.dumps(obj))

        def write_ecosystem_csv(path: Path, days: list) -> None:
            path.parent.mkdir(parents=True, exist_ok=True)
            with path.open("w", newline="") as fh:
                w = csv.writer(fh)
                w.writerow(["day", "impressions", "creator_hhi", "tail_creator_share",
                           "arch_genuinely_satisfying", "arch_useful", "arch_ragebait",
                           "arch_clickbait", "arch_comfort", "arch_polished_irrelevant",
                           "arch_niche_treasure", "arch_background_music",
                           "niche_in_cohort_match_rate"])
                for day, hhi, tail, rage, niche in days:
                    w.writerow([day, 1000, hhi, tail, 0.1, 0.1, rage, 0.1, 0.1, 0.1, niche, 0.1,
                               0.5])

        def write_longterm_csv(path: Path, days: list) -> None:
            path.parent.mkdir(parents=True, exist_ok=True)
            with path.open("w", newline="") as fh:
                w = csv.writer(fh)
                w.writerow(["day", "sessions", "active_users", "sessions_per_active_user",
                           "mean_session_satisfaction", "mean_trust", "cumulative_churned",
                           "mean_pref_shift_from_initial", "mean_preference_entropy"])
                for day, entropy in days:
                    w.writerow([day, 50, 40, 1.25, 0.4, 0.6, 2, 0.05, entropy])

        pre_reg = ("HYPOTHESIS: exploit-only ranking collapses interest diversity. MECHANISM: "
                   "epsilon=0 with high personalization weight. EXPECTED SIGNATURE: falling "
                   "mean_final_preference_entropy. VERDICT CRITERIA: entropy drop > 15% vs "
                   "control.")

        def base_summary(with_ecosystem: bool) -> dict:
            s = {
                "counts": {"impressions": 12000, "requests": 3000},
                "metrics": {"mean_watch_seconds": 12.0, "completion_rate": 0.4, "like_rate": 0.1,
                           "share_rate": 0.02, "follow_rate": 0.01, "reward_per_impression": 0.2,
                           "mean_true_affinity": 0.55},
                "metric_groups": {"engagement": {"comment_rate": 0.01, "save_rate": 0.02,
                                                 "profile_visit_rate": 0.03}},
                "learning": {"final_estimated_hidden_cosine": 0.8},
                "diversity": {"mean_unique_creators": 6.0, "mean_creator_hhi": 0.3,
                             "mean_intra_list_similarity": 0.2},
                "welfare": {"mean_immediate_satisfaction": 0.42, "mean_regret": 0.1,
                           "satisfaction_per_minute": 0.05, "harmful_fatigue": 0.03,
                           "platform_trust": 0.6},
                "session_health": {"sessions": 100, "mean_session_utility": 0.3,
                                  "early_failure_exit_rate": 0.1, "natural_completion_rate": 0.5,
                                  "harmful_fatigue_mean": 0.05,
                                  "exit_type_shares": {"satisfied": 0.5, "fatigue": 0.2,
                                                       "regret": 0.05}},
                "long_term": {
                    "retention_1d": 0.8, "retention_7d": 0.5, "sessions_per_user_per_day": 1.2,
                    "satisfaction_weighted_retention": 0.55, "churn_rate": 0.1,
                    "mean_churn_probability": 0.12, "mean_final_trust": 0.58,
                    "mean_final_habit": 0.4, "mean_preference_shift_from_initial": 0.1234,
                    "mean_final_preference_entropy": 1.5,
                },
            }
            if with_ecosystem:
                s["ecosystem"] = {
                    "creator_hhi_final_day": 0.22,
                    "creator_hhi_whole_run": 0.18,
                    "tail_creator_share_whole_run": 0.35,
                    "arch_share_whole_run": {name: round(1.0 / 8, 6) for name in ARCH_NAMES},
                    "niche_in_cohort_match_rate_whole_run": 0.6,
                }
            return s

        write_json(root / "control" / "summary.json", base_summary(True))
        write_json(root / "control" / "config.json", {"description": pre_reg})
        write_ecosystem_csv(root / "control" / "ecosystem_metrics.csv",
                            [(0, 0.30, 0.40, 0.05, 0.10), (1, 0.28, 0.42, 0.06, 0.11),
                             (2, 0.25, 0.45, 0.07, 0.12), (3, 0.20, 0.48, 0.08, 0.14),
                             (4, 0.18, 0.50, 0.09, 0.16)])
        write_longterm_csv(root / "control" / "longterm_metrics.csv",
                           [(0, 1.9), (1, 1.8), (2, 1.7), (3, 1.6), (4, 1.5)])

        # treatment: no `ecosystem` block, no ecosystem_metrics.csv at all; longterm_metrics.csv IS
        # present (a different day range: 10..12) -- exercises the union-of-days per-day path and
        # the block-absent n/a labeling.
        write_json(root / "treatment" / "summary.json", base_summary(False))
        write_json(root / "treatment" / "config.json", {"description": ""})  # empty -> not usable
        write_longterm_csv(root / "treatment" / "longterm_metrics.csv",
                           [(10, 2.1), (11, 2.0), (12, 1.95)])

        # missing: directory exists but is empty (no summary.json/config.json at all).
        (root / "missing").mkdir(parents=True, exist_ok=True)

        arms = load_arms([("control", str(root / "control")), ("treatment", str(root / "treatment")),
                          ("missing", str(root / "missing"))])

        ok &= _check(list(arms.keys()) == ["control", "treatment", "missing"],
                    "load_arms: preserves command-line order")
        ok &= _check(arms["control"].available and arms["treatment"].available,
                    "Arm.available: control and treatment both load")
        ok &= _check(not arms["missing"].available, "Arm.available: missing arm is unavailable")

        ok &= _check(arms["missing"].metric(("welfare", "mean_immediate_satisfaction")) == NA_ARM,
                    "Arm.metric: an unavailable arm reads as NA_ARM")
        ok &= _check(
            abs(arms["control"].metric(("welfare", "platform_trust")) - 0.6) < 1e-12,
            "Arm.metric: control's welfare.platform_trust reads correctly")
        eco_absent = arms["treatment"].metric(("ecosystem", "creator_hhi_final_day"))
        ok &= _check(eco_absent == "n/a (block 'ecosystem' absent)",
                    f"Arm.metric: treatment's absent ecosystem block reads as a labeled n/a "
                    f"(got {eco_absent!r})")
        rage_share = arms["control"].metric(("ecosystem", "arch_share_whole_run", "ragebait"))
        ok &= _check(abs(rage_share - 1.0 / 8) < 1e-9,
                    "Arm.metric: 3-deep arch_share_whole_run.ragebait path resolves correctly")
        missing_key = arms["control"].metric(("ecosystem", "arch_share_whole_run", "nonexistent"))
        ok &= _check(
            missing_key == "n/a (key 'ecosystem.arch_share_whole_run.nonexistent' absent)",
            f"Arm.metric: a genuinely-missing key (present block) is labeled distinctly "
            f"(got {missing_key!r})")

        ok &= _check(arms["control"].description() == pre_reg,
                    "Arm.description: control's non-empty description reads verbatim")
        ok &= _check(arms["treatment"].description() is None,
                    "Arm.description: an empty description string reads as None")
        ok &= _check(find_description(arms) == pre_reg,
                    "find_description: finds control's description across the whole arm set")

        eco_days = read_ecosystem_csv(root / "control" / "ecosystem_metrics.csv")
        ok &= _check(len(eco_days) == 5 and abs(eco_days[2]["creator_hhi"] - 0.25) < 1e-12,
                    "read_ecosystem_csv: parses all rows, day 2 creator_hhi correct")
        lt_days = read_longterm_entropy_csv(root / "control" / "longterm_metrics.csv")
        ok &= _check(len(lt_days) == 5 and abs(lt_days[4] - 1.5) < 1e-12,
                    "read_longterm_entropy_csv: parses all rows, day 4 entropy correct")
        ok &= _check(read_ecosystem_csv(root / "treatment" / "ecosystem_metrics.csv") == {},
                    "read_ecosystem_csv: absent file returns {} (never raises)")

        snap_control = per_day_snapshot(arms["control"])
        ok &= _check([r["slice"] for r in snap_control] == ["first", "mid", "final"],
                    "per_day_snapshot: control has first/mid/final slices")
        ok &= _check([r["day"] for r in snap_control] == [0, 2, 4],
                    f"per_day_snapshot: control picks days [0, 2, 4] (got "
                    f"{[r['day'] for r in snap_control]})")
        ok &= _check(abs(snap_control[-1]["creator_hhi"] - 0.18) < 1e-12,
                    "per_day_snapshot: control's final-day creator_hhi matches the fixture")

        snap_treatment = per_day_snapshot(arms["treatment"])
        ok &= _check([r["day"] for r in snap_treatment] == [10, 11, 12],
                    f"per_day_snapshot: treatment (ecosystem CSV absent) still gets days from "
                    f"longterm_metrics.csv alone (got {[r['day'] for r in snap_treatment]})")
        ok &= _check(
            isinstance(snap_treatment[0]["creator_hhi"], str)
            and snap_treatment[0]["creator_hhi"].startswith("n/a"),
            "per_day_snapshot: treatment's creator_hhi (no ecosystem CSV) is a labeled n/a")
        ok &= _check(abs(snap_treatment[-1]["mean_preference_entropy"] - 1.95) < 1e-12,
                    "per_day_snapshot: treatment's final-day entropy matches its own fixture")

        snap_missing = per_day_snapshot(arms["missing"])
        ok &= _check(len(snap_missing) == 1 and snap_missing[0]["slice"] == "n/a",
                    "per_day_snapshot: an arm with neither CSV gets one n/a fallback row")

        md = render_markdown("selftest_scenario", arms, DEFAULT_VERDICT, precision=6)
        ok &= _check(pre_reg in md, "render_markdown: quotes the pre-registration text verbatim")
        ok &= _check(DEFAULT_VERDICT in md, "render_markdown: default verdict text present")
        ok &= _check("n/a (block 'ecosystem' absent)" in md,
                    "render_markdown: the treatment arm's absent-ecosystem n/a cell is present")
        ok &= _check(all(f"### {label}" in md for label, _cols in GROUPS),
                    "render_markdown: all six group section headers present")
        ok &= _check("## Per-day snapshot" in md, "render_markdown: per-day section present")

        out_dir = root / "out"
        out_dir.mkdir(parents=True, exist_ok=True)
        csv_path = out_dir / "selftest_scenario-comparison.csv"
        write_csv(csv_path, "selftest_scenario", arms, precision=6)
        with csv_path.open() as fh:
            csv_rows = list(csv.reader(fh))
        ok &= _check(len(csv_rows) == 1 + len(arms),
                    f"write_csv: header + {len(arms)} arm rows (got {len(csv_rows)} total rows)")
        ok &= _check(csv_rows[0][:2] == ["scenario", "arm"],
                    "write_csv: leading scenario/arm columns")

        ok &= _check(_parse_arm_arg("control=/tmp/x") == ("control", "/tmp/x"),
                    "_parse_arm_arg: parses LABEL=DIR")
        try:
            _parse_arm_arg("no-equals-sign")
            ok &= _check(False, "_parse_arm_arg: raises ValueError without '='")
        except ValueError:
            ok &= _check(True, "_parse_arm_arg: raises ValueError without '='")

        # Full in-process CLI/main() round trip, custom --verdict text.
        main_out = root / "main-out"
        rc = main([
            "--scenario", "cli_test", "--arm", f"control={root / 'control'}",
            "--arm", f"treatment={root / 'treatment'}", "--out", str(main_out),
            "--verdict", "custom verdict text", "--precision", "4",
        ])
        ok &= _check(rc == 0, "main(): exits 0 for a scenario with at least one available arm")
        md_path = main_out / "cli_test-comparison.md"
        csv_path2 = main_out / "cli_test-comparison.csv"
        ok &= _check(md_path.exists() and csv_path2.exists(),
                    "main(): writes cli_test-comparison.{md,csv}")
        if md_path.exists():
            md_text = md_path.read_text()
            ok &= _check("custom verdict text" in md_text,
                        "main(): custom --verdict text lands in the rendered report")

        rc_missing_args = main(["--scenario", "x"])
        ok &= _check(rc_missing_args == 2,
                    f"main(): missing --arm/--out (no --self-test) exits 2 "
                    f"(got {rc_missing_args})")

    print(f"phase21_scenarios --self-test: {'ALL PASS' if ok else 'SOME FAILED'}")
    return 0 if ok else 1


def main(argv=None) -> int:
    try:
        args = parse_args(argv)
    except SystemExit as exc:
        return exc.code if isinstance(exc.code, int) else 2
    if args.self_test:
        return run_self_test()

    arms = load_arms(args.arms)
    if not any(arm.available for arm in arms.values()):
        p15.warn("no readable summary.json in any given arm")
        return 1

    args.out.mkdir(parents=True, exist_ok=True)
    verdict_text = args.verdict.strip() if args.verdict else DEFAULT_VERDICT

    csv_path = args.out / f"{args.scenario}-comparison.csv"
    write_csv(csv_path, args.scenario, arms, args.precision)

    md_path = args.out / f"{args.scenario}-comparison.md"
    md_path.write_text(render_markdown(args.scenario, arms, verdict_text, args.precision))

    print(f"phase21_scenarios: wrote {csv_path}")
    print(f"phase21_scenarios: wrote {md_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
