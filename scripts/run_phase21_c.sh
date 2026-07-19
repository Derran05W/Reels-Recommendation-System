#!/usr/bin/env bash
#
# run_phase21_c.sh — Phase 21 Package C: the three concentration/starvation ecosystem
# failure-mode scenarios (docs/design/P21-CONTRACTS.md, plan/05-PHASES-EVENTS-LONGTERM.md
# Phase 21 task 2, V2 TDD §4.18):
#
#   popularity_feedback         popularity-heavy ranking weights vs base weights -- creator HHI /
#                                tail-creator-share ("new-creator exposure decay" proxy).
#   creator_overconcentration   creator-affinity-heavy weights, hnsw_ranker (no caps) vs the
#                                IDENTICAL weights on hnsw_ranker_diversity (complete mode:
#                                exploration + the diversity reranker's hard <=2/creator cap) --
#                                creator HHI gap + session_health fatigue-exit share + U_s.
#   niche_starvation             mainstream-optimizing (popularity+similarity heavy, zero
#                                modality-match weight) vs base weights -- arch_niche_treasure
#                                exposure share + niche_in_cohort_match_rate +
#                                welfare_archetype_metrics.csv niche_treasure row.
#
# Every arm is derived from configs/realism-medium-retention.json (10k users/100k reels, event
# mode, evolution+retention on, 9 simulated days, serving depth 10, seed 42) via a tiny
# stdlib-only JSON-patch (the pattern established by scripts/run_phase17/19/20_experiment.sh): a
# python3 heredoc reads the base config, applies `dict.update()` on exactly the differing keys
# (never a wholesale block replacement), sets `evaluation.ecosystem_metrics = true` and the
# pre-registered `description` block, and writes the result. Every OTHER field is therefore
# IDENTICAL across every arm below by construction -- same world, same seed, only the documented
# ranking-weight / algorithm difference varies per scenario.
#
# COMMITTED scenario configs (contracts §4/§7 -- "one config per scenario"; this script (re)writes
# these three, they are the CONTROL arm's exact config for popularity_feedback/niche_starvation,
# and the single SHARED config for creator_overconcentration's two arms, which differ only by
# --algorithm):
#   configs/scenarios/popularity_feedback.json        (base ranking weights, unpatched)
#   configs/scenarios/niche_starvation.json           (base ranking weights, unpatched)
#   configs/scenarios/creator_overconcentration.json  (creator_affinity_weight=0.4; used by BOTH
#                                                       its arms, see below)
#
# GENERATED treatment-arm configs (saved next to results, gitignored, contracts §4 "run outputs"):
#   results/phase21/popularity_feedback/generated-configs/popularity.json
#     patch: popularity_weight=0.5, similarity_weight=0.3 (raise popularity, lower personalized
#     similarity -- the documented popularity-heavy preset).
#   results/phase21/niche_starvation/generated-configs/mainstream.json
#     patch: popularity_weight=0.4, similarity_weight=0.7, visual_match_weight=0.0,
#     music_match_weight=0.0, emotional_match_weight=0.0 (mainstream-optimizing: popularity +
#     generic similarity heavy, all modality-match credit zeroed -- these three already default to
#     0.0 in the base config, set explicitly here so the patch is self-documenting).
#
# creator_overconcentration needs NO generated variant: both its arms (`affinity`, `capped`) load
# the SAME committed config (creator_affinity_weight=0.4) and differ ONLY in the --algorithm CLI
# flag passed to `simulate` (hnsw_ranker vs hnsw_ranker_diversity) -- simulate.cpp applies
# --algorithm to config.algorithm BEFORE running, so each run's own written config.json correctly
# reflects the algorithm it actually ran under.
#
# All ranking-weight keys verified against include/rr/infrastructure/config.hpp's RankingConfig /
# src/infrastructure/config.cpp's from_json allowlist (read directly, not guessed) before this
# script was written; --configs-only re-verifies both parse (json.load) and key membership
# programmatically every time this script runs.
#
# Usage:
#   scripts/run_phase21_c.sh [--bin PATH] [--configs-dir DIR] [--out DIR] [--seed N]
#                            [--configs-only]
#
#   --bin PATH         simulate binary (default: build-release/apps/simulate; env SIMULATE_BIN)
#   --configs-dir DIR  directory holding realism-medium-retention.json (default: configs;
#                      env CONFIGS_DIR)
#   --out DIR          results root; each arm writes under DIR/<scenario>/<arm>/, generated
#                      per-arm configs under DIR/<scenario>/generated-configs/, logs under
#                      DIR/<scenario>/logs/ (default: results/phase21; env RESULTS_ROOT)
#   --seed N           master seed, applied to every arm (default: 42; env SEED)
#   --configs-only     generate + validate every config and exit 0 WITHOUT running simulate (the
#                      "generate configs only" pre-flight check contracts asks for)
#
# CONCURRENCY (contracts §4: at most 2 simulate processes per package at any time): six arms run
# in three sequential waves of exactly 2 (paired by scenario) --
#   wave 1: popularity_feedback {control, popularity}
#   wave 2: niche_starvation    {control, mainstream}
#   wave 3: creator_overconcentration {affinity, capped}
#
# For a long batch, run detached and poll the PID yourself (orchestration memory):
#   nohup bash scripts/run_phase21_c.sh > results/phase21/run_phase21_c.log 2>&1 &
#   echo $!    # remember this PID; poll with `kill -0 <pid>` -- do not rely on tool notifications
#
# Each arm's simulate binary is Release, ~10-35 minutes at this config's scale (10k users/100k
# reels/9 simulated days) per this package's own final report (see docs/scenarios/*-NOTES.md for
# the actually-observed wall times).

set -euo pipefail

SIMULATE_BIN="${SIMULATE_BIN:-build-release/apps/simulate}"
CONFIGS_DIR="${CONFIGS_DIR:-configs}"
RESULTS_ROOT="${RESULTS_ROOT:-results/phase21}"
SEED="${SEED:-42}"
CONFIGS_ONLY=0

usage() {
    cat <<'EOF'
usage: run_phase21_c.sh [--bin PATH] [--configs-dir DIR] [--out DIR] [--seed N] [--configs-only]
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --bin) SIMULATE_BIN="$2"; shift 2 ;;
        --configs-dir) CONFIGS_DIR="$2"; shift 2 ;;
        --out) RESULTS_ROOT="$2"; shift 2 ;;
        --seed) SEED="$2"; shift 2 ;;
        --configs-only) CONFIGS_ONLY=1; shift ;;
        -h|--help) usage; exit 0 ;;
        *) echo "error: unknown argument: $1" >&2; usage >&2; exit 2 ;;
    esac
done

if ! command -v python3 >/dev/null 2>&1; then
    echo "ERROR: python3 not found on PATH -- required for config generation/validation." >&2
    exit 1
fi

BASE_CFG="$CONFIGS_DIR/realism-medium-retention.json"
if [[ ! -f "$BASE_CFG" ]]; then
    echo "ERROR: config not found: $BASE_CFG" >&2
    exit 1
fi

SCEN_DIR="$CONFIGS_DIR/scenarios"
mkdir -p "$SCEN_DIR"
for s in popularity_feedback creator_overconcentration niche_starvation; do
    mkdir -p "$RESULTS_ROOT/$s/logs" "$RESULTS_ROOT/$s/generated-configs"
done

echo "=== Phase 21 Package C: config generation ==="

# ---------------------------------------------------------------------------------------------
# Pre-registration text (contracts §4: written BEFORE any run). One file per scenario so the
# python heredocs below never fight bash quoting on multi-line text.
# ---------------------------------------------------------------------------------------------

cat > "$RESULTS_ROOT/popularity_feedback/generated-configs/description.txt" <<'EOF'
HYPOTHESIS: Raising the ranking policy's weight on raw popularity (and correspondingly lowering
personalized-similarity weight) lets already-popular creators/reels accumulate a compounding
share of impressions across simulated days, at the expense of newer/smaller creators, relative to
the base-weight control on the identical world.
MECHANISM: Every feed-serving ranking pass scores candidates with ranking.popularity_weight on
the reel's (accumulating) popularity signal. Under a popularity-heavy weight (0.5, up from 0.07),
reels/creators that already have more impressions rank higher on EVERY subsequent request,
mechanically directing more of each day's impressions to the already-popular set (a rich-get-
richer feedback loop), while similarity_weight (down from 0.50 to 0.30) proportionally weakens
the personalized/relevance counterweight that would otherwise let long-tail content surface via
good topic/user match.
EXPECTED SIGNATURE: Day-over-day rising creator_hhi in the popularity arm vs a flatter/lower
trajectory in control; falling tail_creator_share (impression share of creators outside the
cumulative top decile, contracts §2) in the popularity arm relative to control, read as a
new/small-creator exposure-decay proxy.
VERDICT CRITERIA: CONFIRMED if by the final simulated day the popularity arm's creator_hhi
exceeds control's AND tail_creator_share is lower than control's (both directions). PARTIAL if
only one of the two metrics moves in the hypothesized direction. NOT OBSERVED if neither metric
diverges from control beyond a small/noise-level margin (same seed/world, so any divergence is
attributable to the weight change, but a negligible-magnitude move is reported honestly as
not-observed/inconclusive rather than confirmed).
EOF

cat > "$RESULTS_ROOT/creator_overconcentration/generated-configs/description.txt" <<'EOF'
HYPOTHESIS: A creator-affinity-heavy ranking policy with no diversity constraint concentrates a
day's impressions on far fewer creators than the SAME affinity-heavy policy served through the
diversity-reranking path that enforces a hard <=2-per-creator-per-feed cap; the uncapped arm
should also show measurably more fatigue-driven session exits and lower session utility (U_s) as
users saturate on a small creator set.
MECHANISM: ranking.creator_affinity_weight raised to 0.4 (from 0.07) on BOTH arms makes creator
affinity the dominant non-similarity score term, pushing a user's historically-liked creators to
the top of every candidate pool. The `affinity` arm serves via hnsw_ranker (ranking only, no
re-ranking pass). The `capped` arm serves the IDENTICAL config via hnsw_ranker_diversity
(FullRecommender: exploration + the DiversityReranker's hard max_per_creator=2 constraint + MMR),
which structurally prevents any one creator from filling more than 2 of the 10 feed slots per
request regardless of how highly that creator scores.
EXPECTED SIGNATURE: Materially higher creator_hhi (per-day and whole-run) in `affinity` vs
`capped`; higher session_health.exit_type_shares.fatigue and lower
session_health.mean_session_utility (U_s) in `affinity` vs `capped`.
VERDICT CRITERIA: CONFIRMED if creator_hhi is higher in `affinity` AND at least one of {fatigue
exit share higher, mean_session_utility lower} holds in `affinity` vs `capped`. PARTIAL if only
the HHI gap appears without a session-health signature (the cap controls concentration but the
welfare channel doesn't register a difference at this scale/horizon). NOT OBSERVED if creator_hhi
is not meaningfully higher in the uncapped arm.
EOF

cat > "$RESULTS_ROOT/niche_starvation/generated-configs/description.txt" <<'EOF'
HYPOTHESIS: A mainstream-optimizing ranking policy (popularity- and generic-similarity-heavy,
zero weight on every modality-match feature) starves the niche_treasure archetype of exposure and
degrades its in-cohort targeting relative to the base-weight control on the identical world.
MECHANISM: Raising popularity_weight to 0.4 and similarity_weight to 0.7 while zeroing
visual_match_weight/music_match_weight/emotional_match_weight removes any ranking credit for the
fine-grained modality affinities that would otherwise let a niche_treasure reel (archetype
catalog: highly satisfying to a narrow cohort band, niche_cohort_width=0.15) surface to the
specific narrow cohort it is built for, while amplifying generic popularity/similarity signals
that favor broad-appeal content instead.
EXPECTED SIGNATURE: Lower arch_niche_treasure whole-run exposure share and lower
niche_in_cohort_match_rate (share of niche_treasure impressions actually served within the reel's
hidden niche cohort band) in `mainstream` vs `control`; corresponding decline in the
niche_treasure row of welfare_archetype_metrics.csv (lower exposure_share and/or lower
mean_immediate_satisfaction, since off-cohort niche impressions are a worse match).
VERDICT CRITERIA: CONFIRMED if BOTH arch_niche_treasure share AND niche_in_cohort_match_rate are
lower in `mainstream` vs `control`. PARTIAL if only one moves in the hypothesized direction. NOT
OBSERVED if neither declines beyond a small/noise-level margin.
EOF

# ---------------------------------------------------------------------------------------------
# generate_config <base> <out> <description-file> <ranking-patch-json>
#
# Loads <base>, sets evaluation.ecosystem_metrics=true and description from <description-file>,
# applies dict.update(<ranking-patch-json>) onto the "ranking" sub-object only (never a wholesale
# replacement -- every OTHER key stays whatever <base> resolves to), and writes <out>.
# ---------------------------------------------------------------------------------------------
generate_config() {
    local base="$1" out="$2" desc_file="$3" patch_json="$4"
    python3 - "$base" "$out" "$desc_file" "$patch_json" <<'PYEOF'
import json
import sys

base_path, out_path, desc_path, patch_json = sys.argv[1:5]

with open(base_path) as f:
    cfg = json.load(f)
with open(desc_path) as f:
    description = f.read().strip()

patch = json.loads(patch_json)
cfg["ranking"].update(patch)
cfg["evaluation"]["ecosystem_metrics"] = True
cfg["description"] = description

with open(out_path, "w") as f:
    json.dump(cfg, f, indent=2)
    f.write("\n")
PYEOF
}

# --- Committed scenario configs (contracts: one per scenario under configs/scenarios/) --------

# popularity_feedback / niche_starvation "scenario" files ARE their control arm's exact config
# (no ranking patch beyond the shared evaluation/description additions).
generate_config "$BASE_CFG" "$SCEN_DIR/popularity_feedback.json" \
    "$RESULTS_ROOT/popularity_feedback/generated-configs/description.txt" '{}'

generate_config "$BASE_CFG" "$SCEN_DIR/niche_starvation.json" \
    "$RESULTS_ROOT/niche_starvation/generated-configs/description.txt" '{}'

# creator_overconcentration's single scenario file carries the shared affinity-heavy patch used
# by BOTH its arms (they differ only by --algorithm at run time).
generate_config "$BASE_CFG" "$SCEN_DIR/creator_overconcentration.json" \
    "$RESULTS_ROOT/creator_overconcentration/generated-configs/description.txt" \
    '{"creator_affinity_weight": 0.4}'

# --- Generated treatment-arm configs (next to results, gitignored) -----------------------------

generate_config "$BASE_CFG" "$RESULTS_ROOT/popularity_feedback/generated-configs/popularity.json" \
    "$RESULTS_ROOT/popularity_feedback/generated-configs/description.txt" \
    '{"popularity_weight": 0.5, "similarity_weight": 0.3}'

generate_config "$BASE_CFG" "$RESULTS_ROOT/niche_starvation/generated-configs/mainstream.json" \
    "$RESULTS_ROOT/niche_starvation/generated-configs/description.txt" \
    '{"popularity_weight": 0.4, "similarity_weight": 0.7, "visual_match_weight": 0.0, "music_match_weight": 0.0, "emotional_match_weight": 0.0}'

echo "  wrote $SCEN_DIR/popularity_feedback.json"
echo "  wrote $SCEN_DIR/niche_starvation.json"
echo "  wrote $SCEN_DIR/creator_overconcentration.json"
echo "  wrote $RESULTS_ROOT/popularity_feedback/generated-configs/popularity.json"
echo "  wrote $RESULTS_ROOT/niche_starvation/generated-configs/mainstream.json"

# ---------------------------------------------------------------------------------------------
# Validation: every config (a) parses as JSON, (b) its "ranking" patch keys are a subset of the
# RankingConfig JSON key set (read directly from include/rr/infrastructure/config.hpp /
# src/infrastructure/config.cpp -- unknown keys are config-load errors), (c) top-level keys are a
# subset of ExperimentConfig's allowlist.
# ---------------------------------------------------------------------------------------------
echo
echo "=== Validating generated configs ==="
python3 - "$SCEN_DIR/popularity_feedback.json" "$SCEN_DIR/niche_starvation.json" \
    "$SCEN_DIR/creator_overconcentration.json" \
    "$RESULTS_ROOT/popularity_feedback/generated-configs/popularity.json" \
    "$RESULTS_ROOT/niche_starvation/generated-configs/mainstream.json" <<'PYEOF'
import json
import sys

TOP_LEVEL_KEYS = {
    "simulation", "recommendation", "algorithm", "hnsw", "ranking", "learning", "exploration",
    "diversity", "drift", "behaviour", "behaviour_v2", "session_dynamics", "scheduling",
    "serving", "evolution", "retention", "reward", "evaluation", "realism", "description",
}
RANKING_KEYS = {
    "similarity_weight", "quality_weight", "freshness_weight", "popularity_weight",
    "trending_weight", "creator_affinity_weight", "exploration_weight", "repetition_penalty",
    "duration_match_weight", "impression_penalty_weight", "session_topic_weight",
    "freshness_half_life_seconds", "trending_half_life_seconds", "visual_match_weight",
    "music_match_weight", "emotional_match_weight", "clickbait_weight",
    "emotional_intensity_weight", "usefulness_weight", "production_quality_weight",
    "information_density_weight", "language_match_weight", "save_popularity_weight",
}
EVALUATION_KEYS = {"oracle_sample_rate", "retrieval_sample_rate", "ecosystem_metrics"}

ok = True
for path in sys.argv[1:]:
    try:
        with open(path) as f:
            cfg = json.load(f)
    except Exception as exc:
        print(f"FAIL parse {path}: {exc}")
        ok = False
        continue
    bad_top = set(cfg.keys()) - TOP_LEVEL_KEYS
    bad_ranking = set(cfg.get("ranking", {}).keys()) - RANKING_KEYS
    bad_eval = set(cfg.get("evaluation", {}).keys()) - EVALUATION_KEYS
    if bad_top or bad_ranking or bad_eval:
        print(f"FAIL keys {path}: top={bad_top} ranking={bad_ranking} evaluation={bad_eval}")
        ok = False
        continue
    if cfg.get("simulation", {}).get("scheduler") != "event_queue":
        print(f"FAIL scheduler {path}: ecosystem_metrics requires event_queue")
        ok = False
        continue
    if not cfg.get("evaluation", {}).get("ecosystem_metrics"):
        print(f"FAIL gate {path}: evaluation.ecosystem_metrics is not true")
        ok = False
        continue
    if not cfg.get("description", "").strip():
        print(f"FAIL description {path}: empty pre-registration block")
        ok = False
        continue
    print(f"OK    {path}  (ranking keys patched: sorted below)")

if not ok:
    sys.exit(1)
PYEOF

echo
if [[ $CONFIGS_ONLY -eq 1 ]]; then
    echo "=== --configs-only: skipping simulate execution ==="
    exit 0
fi

if [[ ! -x "$SIMULATE_BIN" ]]; then
    echo "ERROR: simulate binary not found or not executable at: $SIMULATE_BIN" >&2
    exit 1
fi

# run_arm <scenario> <arm> <config> <algorithm>
run_arm() {
    local scenario="$1" arm="$2" config="$3" algorithm="$4"
    local out_dir="$RESULTS_ROOT/$scenario/$arm"
    local log="$RESULTS_ROOT/$scenario/logs/$arm.log"
    mkdir -p "$out_dir"
    : > "$log"
    echo ">>> [$scenario/$arm] algorithm=$algorithm config=$config seed=$SEED -> $out_dir"
    local rc=0
    "$SIMULATE_BIN" --config "$config" --algorithm "$algorithm" --seed "$SEED" --out "$out_dir" \
        >> "$log" 2>&1 || rc=$?
    if [[ $rc -eq 0 ]]; then
        echo "<<< [$scenario/$arm] done"
    else
        echo "<<< [$scenario/$arm] FAILED (exit $rc) -- see $log"
    fi
    return $rc
}

echo "=== Running six arms, three waves of 2 (contracts §4 concurrency cap) ==="
echo "  start: $(date)"

overall_rc=0

run_wave() {
    # args: scenario1 arm1 config1 algo1  scenario2 arm2 config2 algo2
    local pids=()
    run_arm "$1" "$2" "$3" "$4" & pids+=($!)
    run_arm "$5" "$6" "$7" "$8" & pids+=($!)
    local rc=0
    for pid in "${pids[@]}"; do
        wait "$pid" || rc=1
    done
    return $rc
}

run_wave popularity_feedback control "$SCEN_DIR/popularity_feedback.json" hnsw_ranker \
         popularity_feedback popularity "$RESULTS_ROOT/popularity_feedback/generated-configs/popularity.json" hnsw_ranker \
    || overall_rc=1

run_wave niche_starvation control "$SCEN_DIR/niche_starvation.json" hnsw_ranker \
         niche_starvation mainstream "$RESULTS_ROOT/niche_starvation/generated-configs/mainstream.json" hnsw_ranker \
    || overall_rc=1

run_wave creator_overconcentration affinity "$SCEN_DIR/creator_overconcentration.json" hnsw_ranker \
         creator_overconcentration capped "$SCEN_DIR/creator_overconcentration.json" hnsw_ranker_diversity \
    || overall_rc=1

echo
echo "=== Phase 21 Package C: ALL ARMS DONE (finished: $(date)) ==="
exit $overall_rc
