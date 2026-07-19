#!/usr/bin/env python3
"""phase17_comparison.py — Phase 17 Tier-2 core experiment comparison (V2 TDD §4.10 core
experiment, §6): FIXED vs PERSONALIZED diversity across FOUR trait cohorts (focused /
novelty_seeker / creator_loyal / easily_fatigued), as produced by
scripts/run_phase17_experiment.sh's 8-arm matrix.

Reuses `phase16_comparison.py` (imported directly, which itself imports `phase15_comparison.py`
-- design decision D15: this script contains no simulation logic, only reads result files and
renders tables) for its `Phase16Arm` base class (summary/config/archetype-exposure loading,
`distinguishing_weights`, `satisfaction_per_minute`, the `_na()`/`NA` pending-integration
convention) and its small table-rendering helper. This worktree (Phase 17 package C) is AHEAD of
where phase16_comparison.py was written: `session_health` is a REAL, populated summary.json block
here (Phase 16 landed, see include/rr/evaluation/session_health_metrics.hpp), not a
pending-integration stub -- so this script reads its fields by their VERIFIED exact paths (checked
directly against that header and a real published `results/published/phase16/*/summary.json`)
rather than phase16_comparison.py's pre-integration guessed candidate-key lists, most notably for
the exit-type shares, which are nested under `session_health.exit_type_shares.<type>` (NOT the
flat `<type>_share` keys phase16_comparison.py guessed before package B landed).

Given the eight Phase 17 arm result directories (one per {focused, novelty_seeker, creator_loyal,
easily_fatigued} x {fixed, personalized}), this reads each arm's summary.json (+ config.json for
the arm-identity section, + welfare_archetype_metrics.csv for per-archetype exposure) and writes:

  comparison.csv   one row per arm (8 rows), one column per reported metric across all four V2 §6
                   metric groups plus DELIVERED FEED LENGTH (see below).
  comparison.md    headline (personalized-vs-fixed U_s delta per cohort), an integration-status
                   banner, an arm-identity section, a per-cohort fixed-vs-personalized panel, a
                   per-cohort delta table, four-group panels (V2 §6: engagement / hidden welfare /
                   session health / recommendation quality) for the PERSONALIZED arms, per-archetype
                   exposure for the personalized arms, and notes.

Arm order is always cohort order (focused, novelty_seeker, creator_loyal, easily_fatigued) x
(fixed, personalized) -- a missing/unreadable arm still gets a row/column, with every cell reported
as unavailable rather than the row being silently dropped, so the output shape never changes across
invocations (same contract as phase15/16_comparison.py).

DELIVERED FEED LENGTH (the Phase 9 short-feed hazard, V2 TDD §4.10 "report feed-length effects
explicitly"): the shipped summary.json schema has no dedicated "feed length as constructed"
figure (FeedDiversity.feedSize is computed per-feed but never aggregated to a mean and exposed in
summary.json's `diversity` block -- verified by reading include/rr/evaluation/diversity_metrics.hpp
and a real summary.json). The best DERIVABLE proxy, always available, is
`counts.impressions / counts.requests` -- mean impressions actually delivered per request. Reported
HONESTLY: under `realism.session_dynamics` (on for every arm here), this conflates the Phase 9
hard-cap short-feed hazard with session-exit truncation (a user who exits mid-feed never receives
the feed's remaining items) -- both are real "how much did the user actually get served" effects,
but they are not the same mechanism. A handful of candidate keys for a future dedicated
"served-length" figure are checked first (never populated today; kept for forward-compatibility,
matching the defensive-candidate idiom phase15/16_comparison.py already use elsewhere).

Package A/B pending-integration state (read defensively, and DETECTED at runtime rather than
assumed -- mirrors tests/property/personalized_vs_fixed_statistical_test.cpp's runtime detector):
  - Package A (trait-cohort overrides): TraitCohortSpec is a name+weight-only stub in this tree
    (include/rr/infrastructure/cohort_config.hpp) -- every cohort_mix entry selects a LABEL only,
    with no effect on trait sampling, so the four cohort configs are expected to produce
    BIT-IDENTICAL fixed-arm output until package A lands. This script checks it directly (comparing
    the four fixed arms' `session_health.mean_session_utility`) rather than assuming it.
  - Package B (ToleranceEstimator + PersonalizedDiversityReranker): the stub
    PersonalizedDiversityReranker delegates to the fixed DiversityReranker
    (include/rr/recommendation/personalized_diversity_reranker.hpp), so each cohort's personalized
    arm is expected to be BIT-IDENTICAL to its fixed counterpart until package B lands. Also
    detected at runtime, per cohort, rather than assumed.
  Both checks render as an explicit "PENDING INTEGRATION" note rather than a misleading "+0.0%"
  delta wherever the underlying values are bit-identical -- see `PENDING` below.

Usage
-----
    python3 scripts/phase17_comparison.py \\
        --focused-fixed <dir> --focused-personalized <dir> \\
        --noveltyseeker-fixed <dir> --noveltyseeker-personalized <dir> \\
        --creatorloyal-fixed <dir> --creatorloyal-personalized <dir> \\
        --easilyfatigued-fixed <dir> --easilyfatigued-personalized <dir> \\
        [--out results/published/phase17] [--precision 6]

    (No third-party dependencies -- plain python3 is enough.)

Exit status: 0 if at least one of the eight arms had a readable summary.json, 1 otherwise.
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
import phase16_comparison as p16  # noqa: E402

COHORT_ORDER = ["focused", "noveltyseeker", "creatorloyal", "easilyfatigued"]
COHORT_DISPLAY = {
    "focused": "focused",
    "noveltyseeker": "novelty_seeker",
    "creatorloyal": "creator_loyal",
    "easilyfatigued": "easily_fatigued",
}
VARIANTS = ["fixed", "personalized"]

NA = p16.NA  # "n/a (pre-integration)" -- reused for genuinely-missing package-A/B2-era fields, if any
PENDING = "n/a (PENDING A/B INTEGRATION)"  # this script's own marker: both values ARE present and
# numeric but bit-identical, because the personalization/cohort mechanism they'd need to differ
# through has not landed in this tree yet (see module docstring).

# Suggested per-cohort trait-override JSON blocks (plan Phase 17 brief; sync with package A at
# integration -- NOT valid input to today's TraitCohortSpec::from_json stub, which throws on any
# key besides name/weight). Documented here (not inside the shipped configs, which must actually
# parse today) so this script's Notes section and run_phase17_experiment.sh's header agree verbatim.
SUGGESTED_TRAIT_OVERRIDES = {
    "focused": '{"repetition_tolerance": [0.8, 1.0], "novelty_seeking": [0.0, 0.3]}',
    "noveltyseeker": '{"novelty_seeking": [0.7, 1.0], "repetition_tolerance": [0.0, 0.3], '
                     '"novelty_tolerance": [0.7, 1.0]}',
    "creatorloyal": '{"creator_loyalty": [0.8, 1.0]}',
    "easilyfatigued": '{"repetition_tolerance": [0.0, 0.2], "novelty_tolerance": [0.0, 0.3]}',
}


class Phase17Arm(p16.Phase16Arm):
    """p16.Phase16Arm (summary/config/archetype-exposure loading, distinguishing_weights,
    satisfaction_per_minute) tagged with its cohort/variant identity, plus the DELIVERED FEED
    LENGTH derived metric (see module docstring)."""

    def __init__(self, arm_id: str, directory: Optional[Path], cohort: str, variant: str):
        super().__init__(arm_id, directory)
        self.cohort = cohort
        self.variant = variant

    def _na(self) -> str:
        return "n/a (arm not provided)"

    def delivered_feed_length(self):
        if not self.available:
            return self._na()
        impressions = p15._get(self.summary, "counts", "impressions")
        requests = p15._get(self.summary, "counts", "requests")
        if p15._is_num(impressions) and p15._is_num(requests) and requests > 0:
            return impressions / requests
        # Forward-compatible fallback: not populated by any shipped schema today (see docstring).
        for key in ("mean_feed_size", "mean_delivered_feed_size", "mean_served_feed_length"):
            value = p15._get(self.summary, "diversity", key)
            if p15._is_num(value):
                return value
        return self._na()

    def value(self, path: Optional[tuple]):
        return self.delivered_feed_length() if path is None else self.metric(path)


# --- Metric groups (V2 TDD §6, D22: reported as SEPARATE blocks, no aggregate score) -------------
# Each entry: (column key, header, summary.json path tuple; None => delivered_feed_length()).
# Paths verified directly against include/rr/evaluation/{welfare_metrics,session_health_metrics}.hpp
# and a real results/published/phase16/*/summary.json -- not guessed candidates.

ENGAGEMENT_COLUMNS: list[tuple[str, str, Optional[tuple]]] = [
    ("mean_watch_seconds", "watch time (mean watchSeconds/impression)", ("metrics", "mean_watch_seconds")),
    ("completion_rate", "completion rate", ("metrics", "completion_rate")),
    ("like_rate", "like rate", ("metrics", "like_rate")),
    ("share_rate", "share rate", ("metrics", "share_rate")),
    ("follow_rate", "follow rate", ("metrics", "follow_rate")),
    ("comment_rate", "comment rate", ("metric_groups", "engagement", "comment_rate")),
    ("save_rate", "save rate", ("metric_groups", "engagement", "save_rate")),
    ("profile_visit_rate", "profile-visit rate", ("metric_groups", "engagement", "profile_visit_rate")),
    ("reward_per_impression", "reward/impression (engagement proxy)", ("metrics", "reward_per_impression")),
]

WELFARE_COLUMNS: list[tuple[str, str, Optional[tuple]]] = [
    ("mean_immediate_satisfaction", "MEAN HIDDEN SATISFACTION (whole-run)",
     ("welfare", "mean_immediate_satisfaction")),
    ("mean_hidden_regret", "hidden regret (LatentReaction.regret)", ("welfare", "mean_regret")),
    ("satisfaction_per_minute_watch", "satisfaction per watch-minute (whole-run)",
     ("welfare", "satisfaction_per_minute")),
    ("harmful_fatigue", "harmful fatigue (mean, end-of-session; REAL under session_dynamics)",
     ("welfare", "harmful_fatigue")),
    ("platform_trust", "platform trust (NOT-YET-MODELED placeholder, P20)", ("welfare", "platform_trust")),
]

SESSION_HEALTH_COLUMNS: list[tuple[str, str, Optional[tuple]]] = [
    ("mean_duration_seconds", "mean session duration (time-before-exit, s)",
     ("session_health", "mean_duration_seconds")),
    ("median_duration_seconds", "median session duration (s)", ("session_health", "median_duration_seconds")),
    ("mean_impressions_per_session", "mean impressions/session",
     ("session_health", "mean_impressions_per_session")),
    ("early_failure_exit_rate", "early-failure-exit rate", ("session_health", "early_failure_exit_rate")),
    ("natural_completion_rate", "natural-completion (satisfied-exit) rate",
     ("session_health", "natural_completion_rate")),
    ("mean_session_utility", "U_s MEAN (session utility)", ("session_health", "mean_session_utility")),
    ("regret_per_minute", "regret per minute (session-scoped)", ("session_health", "regret_per_minute")),
    ("satisfaction_per_minute_session", "satisfaction per minute (session-scoped)",
     ("session_health", "satisfaction_per_minute")),
    ("next_session_starting_satisfaction", "next-session starting satisfaction",
     ("session_health", "next_session_starting_satisfaction")),
    ("sessions_closed", "closed sessions (count)", ("session_health", "sessions")),
    ("open_sessions", "open sessions (still-open at run end)", ("session_health", "open_sessions")),
]

EXIT_SHARE_COLUMNS: list[tuple[str, str, Optional[tuple]]] = [
    ("failure_share", "failure", ("session_health", "exit_type_shares", "failure")),
    ("satisfied_share", "satisfied", ("session_health", "exit_type_shares", "satisfied")),
    ("fatigue_share", "fatigue", ("session_health", "exit_type_shares", "fatigue")),
    ("external_share", "external", ("session_health", "exit_type_shares", "external")),
    ("regret_share", "regret", ("session_health", "exit_type_shares", "regret")),
    ("open_share", "open (still-open / RunEnded)", ("session_health", "exit_type_shares", "open")),
]

RECOMMENDATION_QUALITY_COLUMNS: list[tuple[str, str, Optional[tuple]]] = [
    ("mean_true_affinity", "mean true affinity", ("metrics", "mean_true_affinity")),
    ("est_hidden_cosine", "estimated<->hidden cosine", ("learning", "final_estimated_hidden_cosine")),
    ("mean_unique_topics", "mean unique topics/feed", ("diversity", "mean_unique_topics")),
    ("mean_unique_creators", "mean unique creators/feed", ("diversity", "mean_unique_creators")),
    ("mean_intra_list_similarity", "mean intra-list similarity", ("diversity", "mean_intra_list_similarity")),
    ("mean_topic_hhi", "mean topic concentration (HHI)", ("diversity", "mean_topic_hhi")),
    ("mean_creator_hhi", "mean creator concentration (HHI)", ("diversity", "mean_creator_hhi")),
    ("delivered_feed_length", "DELIVERED FEED LENGTH (mean impressions/request)", None),
]

ALL_GROUPS = [
    ("Engagement", ENGAGEMENT_COLUMNS),
    ("Hidden user welfare", WELFARE_COLUMNS),
    ("Session health", SESSION_HEALTH_COLUMNS),
    ("Session health -- exit-type shares", EXIT_SHARE_COLUMNS),
    ("Recommendation quality", RECOMMENDATION_QUALITY_COLUMNS),
]

# The brief's explicit per-cohort panel metric list (V2 TDD §4.10: "report feed-length effects
# explicitly"): U_s, early-failure/regret/satisfied exit shares, session duration/impressions,
# satisfaction+regret per minute, welfare satisfaction, delivered feed length.
HEADLINE_SUBSET: list[tuple[str, str, Optional[tuple]]] = [
    ("mean_session_utility", "U_s mean", ("session_health", "mean_session_utility")),
    ("early_failure_exit_rate", "early-failure-exit rate", ("session_health", "early_failure_exit_rate")),
    ("regret_share", "regret-exit share", ("session_health", "exit_type_shares", "regret")),
    ("satisfied_share", "satisfied-exit share", ("session_health", "exit_type_shares", "satisfied")),
    ("mean_duration_seconds", "mean session duration (s)", ("session_health", "mean_duration_seconds")),
    ("mean_impressions_per_session", "mean impressions/session",
     ("session_health", "mean_impressions_per_session")),
    ("satisfaction_per_minute_session", "satisfaction/min (session-scoped)",
     ("session_health", "satisfaction_per_minute")),
    ("regret_per_minute", "regret/min (session-scoped)", ("session_health", "regret_per_minute")),
    ("mean_immediate_satisfaction", "welfare satisfaction (mean, whole-run)",
     ("welfare", "mean_immediate_satisfaction")),
    ("delivered_feed_length", "DELIVERED FEED LENGTH (mean impressions/request)", None),
]


def load_arms(args: argparse.Namespace) -> dict[tuple[str, str], Phase17Arm]:
    arms: dict[tuple[str, str], Phase17Arm] = {}
    for cohort in COHORT_ORDER:
        for variant in VARIANTS:
            directory = getattr(args, f"{cohort}_{variant}")
            arms[(cohort, variant)] = Phase17Arm(f"{cohort}-{variant}", directory, cohort, variant)
    return arms


def format_delta(fixed_arm: Phase17Arm, personalized_arm: Phase17Arm, path: Optional[tuple],
                  precision: int) -> str:
    """personalized-minus-fixed delta, with the PENDING marker (see module docstring) when both
    values are present but bit-identical -- the expected pre-integration state."""
    fv = fixed_arm.value(path)
    pv = personalized_arm.value(path)
    if not (p15._is_num(fv) and p15._is_num(pv)):
        return "n/a"
    if fv == pv:
        return PENDING
    delta = pv - fv
    pct = f"{(delta / abs(fv) * 100.0):+.1f}%" if fv != 0 else "n/a (baseline 0)"
    return f"{delta:+.{precision}g} ({pct})"


def cohort_differentiation_status(arms: dict[tuple[str, str], Phase17Arm]) -> str:
    """Package-A detector (mirrors the C++ statistical test's runtime detector): are the four
    cohorts' FIXED arms distinguishable at all? Checked on session_health.mean_session_utility."""
    path = ("session_health", "mean_session_utility")
    values = []
    for cohort in COHORT_ORDER:
        arm = arms[(cohort, "fixed")]
        if not arm.available:
            return ("UNKNOWN -- not every fixed arm was provided; pass all four --*-fixed "
                    "directories to check this.")
        v = arm.value(path)
        if not p15._is_num(v):
            return "UNKNOWN -- session_health.mean_session_utility missing from a fixed arm's summary.json."
        values.append(v)
    if len(set(values)) == 1:
        return (f"NOT YET ACTIVE -- all four fixed-arm cohorts are BIT-IDENTICAL on U_s "
                f"({p15.fmt(values[0], 6)}). Package A's per-trait cohort overrides are not merged in "
                f"this tree (TraitCohortSpec is name+weight-only, include/rr/infrastructure/"
                f"cohort_config.hpp) -- cohort_mix currently only labels a cohort without changing any "
                f"user's trait sampling, so \"focused\" / \"novelty_seeker\" / \"creator_loyal\" / "
                f"\"easily_fatigued\" populations are behaviourally identical pre-integration.")
    return (f"ACTIVE -- fixed-arm cohorts differ on U_s ({', '.join(p15.fmt(v, 6) for v in values)}); "
            f"Package A's per-trait overrides appear to be live.")


def personalization_status(arms: dict[tuple[str, str], Phase17Arm]) -> str:
    """Package-B detector: for each cohort, is personalized bit-identical to fixed?"""
    path = ("session_health", "mean_session_utility")
    identical, different, unavailable = [], [], []
    for cohort in COHORT_ORDER:
        fixed, personalized = arms[(cohort, "fixed")], arms[(cohort, "personalized")]
        if not (fixed.available and personalized.available):
            unavailable.append(cohort)
            continue
        fv, pv = fixed.value(path), personalized.value(path)
        if not (p15._is_num(fv) and p15._is_num(pv)):
            unavailable.append(cohort)
        elif fv == pv:
            identical.append(cohort)
        else:
            different.append(cohort)
    parts = []
    if identical:
        parts.append("NOT YET ACTIVE for " + ", ".join(COHORT_DISPLAY[c] for c in identical) +
                     " (personalized arm is BIT-IDENTICAL to fixed -- "
                     "PersonalizedDiversityReranker's stub delegates to the fixed DiversityReranker, "
                     "include/rr/recommendation/personalized_diversity_reranker.hpp)")
    if different:
        parts.append("ACTIVE for " + ", ".join(COHORT_DISPLAY[c] for c in different) +
                     " (personalized differs from fixed on U_s)")
    if unavailable:
        parts.append("UNKNOWN for " + ", ".join(COHORT_DISPLAY[c] for c in unavailable) +
                     " (missing arm directory or summary.json)")
    return "; ".join(parts) if parts else "UNKNOWN -- no arms provided."


def group_rows(arm_list: list[tuple[str, Phase17Arm]], columns: list[tuple[str, str, Optional[tuple]]],
              precision: int) -> list[list[str]]:
    rows = []
    for label, arm in arm_list:
        row = [label]
        for _key, _header, path in columns:
            row.append(p15.fmt(arm.value(path), precision))
        rows.append(row)
    return rows


def render_table(header: list[str], rows: list[list[str]]) -> str:
    return p16._render_table(header, rows)


def build_headline(arms: dict[tuple[str, str], Phase17Arm], precision: int) -> list[str]:
    lines = []
    path = ("session_health", "mean_session_utility")
    for cohort in COHORT_ORDER:
        fixed, personalized = arms[(cohort, "fixed")], arms[(cohort, "personalized")]
        display = COHORT_DISPLAY[cohort]
        if not (fixed.available and personalized.available):
            lines.append(f"- **{display}**: not available (need both --{cohort}-fixed and "
                         f"--{cohort}-personalized directories).")
            continue
        fv, pv = fixed.value(path), personalized.value(path)
        delta_str = format_delta(fixed, personalized, path, precision)
        lines.append(f"- **{display}**: U_s personalized {p15.fmt(pv, precision)} vs fixed "
                     f"{p15.fmt(fv, precision)} -> {delta_str}")
    return lines


def render_arms_section(arms: dict[tuple[str, str], Phase17Arm]) -> str:
    lines = []
    for cohort in COHORT_ORDER:
        for variant in VARIANTS:
            arm = arms[(cohort, variant)]
            lines.append(f"- **{arm.name}** -- cohort={COHORT_DISPLAY[cohort]}, variant={variant}")
            if not arm.available:
                lines.append(f"  - status: NOT AVAILABLE ({arm._na()})")
                continue
            lines.append(f"  - directory: `{arm.directory}`")
            resolved_personalized = p15._get(arm.config, "realism", "personalized_diversity")
            resolved_cohort_mix = p15._get(arm.config, "realism", "cohort_mix")
            lines.append(f"  - resolved realism.personalized_diversity: {resolved_personalized}")
            lines.append(f"  - resolved realism.cohort_mix: {resolved_cohort_mix}")
    return "\n".join(lines)


def render_personalized_archetype_section(arms: dict[tuple[str, str], Phase17Arm], precision: int) -> str:
    exposure_by_cohort = {c: arms[(c, "personalized")].archetype_exposure for c in COHORT_ORDER}
    if not any(exposure_by_cohort.values()):
        return ("n/a -- no personalized arm has a readable `welfare_archetype_metrics.csv` with a "
                "recognizable archetype/share column pair.")
    archetypes: list[str] = []
    seen = set()
    for cohort in COHORT_ORDER:
        for name in (exposure_by_cohort[cohort] or {}):
            if name not in seen:
                seen.add(name)
                archetypes.append(name)
    header = ["archetype"] + [COHORT_DISPLAY[c] for c in COHORT_ORDER]
    rows = []
    for name in archetypes:
        row = [name]
        for cohort in COHORT_ORDER:
            value = (exposure_by_cohort[cohort] or {}).get(name)
            row.append(p15.fmt(value, precision) if value is not None else "n/a")
        rows.append(row)
    return render_table(header, rows)


def write_csv(path: Path, arms: dict[tuple[str, str], Phase17Arm], precision: int) -> None:
    all_columns = ENGAGEMENT_COLUMNS + WELFARE_COLUMNS + SESSION_HEALTH_COLUMNS + \
        EXIT_SHARE_COLUMNS + RECOMMENDATION_QUALITY_COLUMNS
    header = ["arm", "cohort", "variant"] + [h for _k, h, _p in all_columns]
    with path.open("w", newline="") as fh:
        writer = csv.writer(fh)
        writer.writerow(header)
        for cohort in COHORT_ORDER:
            for variant in VARIANTS:
                arm = arms[(cohort, variant)]
                row = [arm.name, COHORT_DISPLAY[cohort], variant]
                for _key, _header, path_tuple in all_columns:
                    row.append(p15.fmt(arm.value(path_tuple), precision))
                writer.writerow(row)


def render_markdown(arms: dict[tuple[str, str], Phase17Arm], precision: int) -> str:
    parts = []
    parts.append("# Phase 17 -- Personalized vs Fixed Diversity Across Four Trait Cohorts\n")
    parts.append(
        "V2 TDD §4.10 Tier-2 core experiment: FIXED vs PERSONALIZED diversity re-ranking crossed "
        "with four named trait cohorts (focused, novelty_seeker, creator_loyal, easily_fatigued), "
        "algorithm `hnsw_ranker_diversity` on `configs/realism-medium-cohort-*.json` "
        "(gates `content_v2`/`latent_reactions`/`session_dynamics` on, single-cohort `cohort_mix` "
        "per config), seed 42. Generated by `scripts/phase17_comparison.py`.\n"
    )

    parts.append("## Headline -- personalized-vs-fixed U_s delta per cohort\n")
    parts.extend(build_headline(arms, precision))
    parts.append(
        f"\n`{PENDING}` means both arms' values ARE present and numeric but bit-identical -- the "
        "expected state until package A (cohort trait overrides) and/or package B (real "
        "personalized reranking) land; see Integration status below.\n"
    )

    parts.append("## Integration status\n")
    parts.append(f"- **Cohort differentiation (package A)**: {cohort_differentiation_status(arms)}")
    parts.append(f"- **Personalization (package B)**: {personalization_status(arms)}")
    parts.append("")

    parts.append("## Arms\n")
    parts.append(render_arms_section(arms))
    parts.append("")

    parts.append("## Per-cohort panel: fixed vs personalized\n")
    parts.append(
        "One row per arm (8 rows), grouped by cohort (fixed then personalized). Columns are the "
        "brief's mandated per-cohort metric list: U_s, early-failure/regret/satisfied exit shares, "
        "session duration/impressions, satisfaction+regret per minute (session-scoped), welfare "
        "satisfaction (whole-run), and DELIVERED FEED LENGTH (the Phase 9 short-feed hazard -- see "
        "Notes for its exact definition and confound under session_dynamics).\n"
    )
    combined_rows = []
    for cohort in COHORT_ORDER:
        for variant in VARIANTS:
            combined_rows.append((f"{COHORT_DISPLAY[cohort]} ({variant})", arms[(cohort, variant)]))
    parts.append(render_table(["arm"] + [h for _k, h, _p in HEADLINE_SUBSET],
                              group_rows(combined_rows, HEADLINE_SUBSET, precision)))
    parts.append("")

    parts.append("## Per-cohort deltas (personalized minus fixed)\n")
    parts.append(
        f"`{PENDING}` cells are bit-identical fixed/personalized pairs (see Integration status). "
        "A real delta (once package B lands) will show a signed number and a percentage.\n"
    )
    delta_header = ["cohort"] + [h for _k, h, _p in HEADLINE_SUBSET]
    delta_rows = []
    for cohort in COHORT_ORDER:
        fixed, personalized = arms[(cohort, "fixed")], arms[(cohort, "personalized")]
        row = [COHORT_DISPLAY[cohort]]
        for _key, _header, path in HEADLINE_SUBSET:
            row.append(format_delta(fixed, personalized, path, precision))
        delta_rows.append(row)
    parts.append(render_table(delta_header, delta_rows))
    parts.append("")

    parts.append("## Four-group panels -- PERSONALIZED arms only (V2 TDD §6, D22)\n")
    parts.append(
        "The fixed arms are already covered row-by-row in the per-cohort panel above; this section "
        "applies the mandated four-SEPARATE-groups reporting discipline (no aggregate score, D22) "
        "to the phase's headline condition -- the four cohorts under PERSONALIZED diversity.\n"
    )
    personalized_rows = [(COHORT_DISPLAY[c], arms[(c, "personalized")]) for c in COHORT_ORDER]
    for group_name, columns in ALL_GROUPS:
        parts.append(f"### {group_name}\n")
        parts.append(render_table(["cohort"] + [h for _k, h, _p in columns],
                                  group_rows(personalized_rows, columns, precision)))
        parts.append("")

    parts.append("## Per-archetype exposure -- PERSONALIZED arms\n")
    parts.append(
        "Does personalized diversity shift exposure across the hidden archetype catalog relative to "
        "what the per-cohort panel's fixed rows would show? (V2 TDD §4.4 catalog: "
        "genuinely-satisfying, useful, ragebait, clickbait, comfort, polished-irrelevant, "
        "niche-treasure, background-music.)\n"
    )
    parts.append(render_personalized_archetype_section(arms, precision))
    parts.append("")

    parts.append("## Notes\n")
    parts.append(
        "- **No aggregate score**: per D22/V2 TDD §6, the four metric groups above are never "
        "combined into one number. U_s itself is not a new aggregate introduced by this script -- "
        "it is the V2 TDD §4.9 session-utility DEFINITION (Σsatisfaction − λ1·Σregret − "
        "λ2·harmfulFatigue − λ3·[failure exit]), a single session-health group metric, reported "
        "here as the headline because it is exactly what Tier-2 acceptance is stated in terms of "
        "(\"personalized ≥ fixed on U_s for focused and easily_fatigued\").\n"
        "- **DELIVERED FEED LENGTH** = `counts.impressions / counts.requests` (always derivable). "
        "This is the best available proxy for the Phase 9 short-feed hazard (feeds shrinking under "
        "hard diversity caps, `results/published/phase9/comparison.md`) but is HONESTLY CONFOUNDED "
        "here with session-exit truncation: under `realism.session_dynamics` (on for every arm in "
        "this report), a user whose session exits mid-feed never receives the feed's remaining "
        "items, which lowers this ratio for reasons unrelated to the diversity reranker's cap "
        "behaviour. No dedicated construction-time-only feed-length figure exists in the shipped "
        "summary.json schema as of this package (verified against "
        "include/rr/evaluation/diversity_metrics.hpp); a small set of forward-compatible candidate "
        "keys is checked first in case a future package adds one, but none are populated today.\n"
        "- **Two different \"satisfaction per minute\" and \"regret\" denominators appear side by "
        "side in this report** (same distinction phase16_comparison.py documents): the hidden-"
        "welfare group's `satisfaction per watch-minute (whole-run)` divides by WATCH-minutes over "
        "the whole run, while the session-health group's `satisfaction/regret per minute "
        "(session-scoped)` divides by SESSION-DURATION minutes (time-before-exit) per closed "
        "session. They are expected to diverge, especially for cohorts/arms with many short, "
        "regret-truncated sessions.\n"
        "- **Pending-integration detection is COMPUTED, not assumed** (`cohort_differentiation_"
        "status` / `personalization_status` above): this script compares real numbers across arms "
        "at read time rather than hard-coding \"expect no effect\". If package A or B has landed by "
        "the time this is run, the Integration status section and the per-cohort delta table will "
        "show real, non-`" + PENDING + "` deltas automatically, with no script changes needed.\n"
        "- **Suggested per-cohort trait-override JSON blocks** (for the integrator to merge into "
        "each cohort config's single `cohort_mix` entry once package A's `TraitCohortSpec` grows "
        "override fields -- NOT valid input to today's stub, which rejects any key beyond "
        "name/weight):\n"
        + "\n".join(f"  - {COHORT_DISPLAY[c]}: `{SUGGESTED_TRAIT_OVERRIDES[c]}`" for c in COHORT_ORDER)
        + "\n"
        "- **Harmful fatigue is REAL here, not a placeholder**: under `realism.session_dynamics` "
        "(on for every arm in this report), `welfare.harmful_fatigue` realizes the previously-"
        "placeholder Phase 15 column from the session-health group's `harmful_fatigue_mean` (see "
        "each arm's summary.json `welfare.harmful_fatigue_source` note). `platform_trust` remains a "
        "P20 not-yet-modeled placeholder (constant 0) everywhere.\n"
        "- **Concurrency-contention caveat**: if the arms were run CONCURRENTLY "
        "(`scripts/run_phase17_experiment.sh`'s default 4-concurrent x 2-wave mode), wall-clock/"
        "timing numbers carry cache and memory-bandwidth contention (same caveat as Phase 15/16). "
        "Every other number in this report is deterministic (rng/clock-free, D8/D9) and unaffected "
        "by run mode.\n"
    )
    return "\n".join(parts) + "\n"


def parse_args(argv=None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Compare the Phase 17 fixed-vs-personalized x four-cohort experiment "
                    "(V2 TDD §4.10) as comparison.csv + comparison.md.",
    )
    for cohort in COHORT_ORDER:
        for variant in VARIANTS:
            parser.add_argument(f"--{cohort}-{variant}", type=Path, default=None,
                                help=f"{COHORT_DISPLAY[cohort]} {variant} arm result directory")
    parser.add_argument("--out", type=Path, default=Path("results/published/phase17"),
                        help="output directory for comparison.csv/comparison.md "
                             "(default: results/published/phase17)")
    parser.add_argument("--precision", type=int, default=6,
                        help="significant figures for floats (default: 6)")
    return parser.parse_args(argv)


def main(argv=None) -> int:
    args = parse_args(argv)
    arms = load_arms(args)

    if not any(arm.available for arm in arms.values()):
        p15.warn("no readable summary.json in any of the eight arms")
        return 1

    args.out.mkdir(parents=True, exist_ok=True)

    csv_path = args.out / "comparison.csv"
    write_csv(csv_path, arms, args.precision)

    md_path = args.out / "comparison.md"
    md_path.write_text(render_markdown(arms, args.precision))

    print(f"phase17_comparison: wrote {csv_path}")
    print(f"phase17_comparison: wrote {md_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
