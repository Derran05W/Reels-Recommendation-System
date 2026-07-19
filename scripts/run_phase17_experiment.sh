#!/usr/bin/env bash
#
# run_phase17_experiment.sh — Phase 17 Tier-2 core experiment: FIXED vs PERSONALIZED diversity
# across FOUR trait cohorts (V2 TDD §4.10 core experiment / plan Phase 17 task 4).
#
# 8 arms = 4 cohorts x {fixed, personalized}:
#
#   focused-fixed              hnsw_ranker_diversity  configs/realism-medium-cohort-focused.json
#   focused-personalized       hnsw_ranker_diversity  <generated: + realism.personalized_diversity=true>
#   noveltyseeker-fixed        hnsw_ranker_diversity  configs/realism-medium-cohort-noveltyseeker.json
#   noveltyseeker-personalized hnsw_ranker_diversity  <generated: + realism.personalized_diversity=true>
#   creatorloyal-fixed         hnsw_ranker_diversity  configs/realism-medium-cohort-creatorloyal.json
#   creatorloyal-personalized  hnsw_ranker_diversity  <generated: + realism.personalized_diversity=true>
#   easilyfatigued-fixed       hnsw_ranker_diversity  configs/realism-medium-cohort-easilyfatigued.json
#   easilyfatigued-personalized hnsw_ranker_diversity <generated: + realism.personalized_diversity=true>
#
# Each base cohort config is realism-medium-sessions.json (content_v2 + latent_reactions +
# session_dynamics all on, same 10k-user/100k-reel/200-interaction medium dataset as Phase 15/16)
# plus a single-entry `realism.cohort_mix` pinning the WHOLE population to that one named cohort
# (focused / novelty_seeker / creator_loyal / easily_fatigued). The FIXED arm runs that config as-is
# (realism.personalized_diversity defaults false -> DiversityReranker, the untouched P9 machinery).
# The PERSONALIZED arm needs the identical dataset/ranking/cohort config with exactly one extra key
# flipped on (realism.personalized_diversity = true -> FullRecommender selects
# PersonalizedDiversityReranker instead, see src/recommendation/full_recommender.cpp) — rather than
# shipping four near-duplicate "-personalized.json" files (fewer files preferred, per this
# package's brief), this script generates them into `$RESULTS_ROOT/generated-configs/` with a tiny
# python3 JSON-patch step (stdlib only, no simulation logic per D15 — this is bash-level plumbing,
# not a Python port of any model) before launching any arm. The generated files are left on disk
# next to the results (not a throwaway mktemp) so `cat` on them is a trivial way to confirm exactly
# what ran.
#
# PACKAGE STATUS AT THIS SCRIPT'S SCAFFOLD (Phase 17 package C, worktree p17-package-c): package A
# (trait-cohort overrides on TraitCohortSpec) and package B (real ToleranceEstimator +
# PersonalizedDiversityReranker) are PARALLEL, invisible worktrees. In THIS tree:
#   - TraitCohortSpec is a name+weight-only stub (include/rr/infrastructure/cohort_config.hpp) — a
#     cohort_mix entry selects a LABEL but every user still samples the Phase 13 tolerance traits
#     from the full default [0,1] range regardless of the label, so the four cohort configs above
#     are behaviourally IDENTICAL pre-integration (same population, different cohort_mix name only).
#   - PersonalizedDiversityReranker is a stub that delegates to the fixed DiversityReranker
#     (include/rr/recommendation/personalized_diversity_reranker.hpp), so personalized-arm output is
#     expected to match its fixed-arm counterpart bit-for-bit at this scaffold.
# Both are EXPECTED to produce numerically indistinguishable fixed-vs-personalized and
# cohort-vs-cohort results until A/B land — this is the same pending-integration state
# scripts/phase17_comparison.py and tests/property/personalized_vs_fixed_statistical_test.cpp are
# written to detect and report honestly (not a bug in this script).
#
# Suggested per-cohort trait-override JSON blocks (to sync with package A at integration — these
# are NOT valid input to today's from_json stub, which throws on any key besides name/weight; the
# integrator merges the override fields into each cohort_mix entry above once
# src/infrastructure/cohort_config.cpp lands them):
#   focused         {"repetition_tolerance": [0.8, 1.0], "novelty_seeking": [0.0, 0.3]}
#   novelty_seeker  {"novelty_seeking": [0.7, 1.0], "repetition_tolerance": [0.0, 0.3],
#                    "novelty_tolerance": [0.7, 1.0]}
#   creator_loyal   {"creator_loyalty": [0.8, 1.0]}
#   easily_fatigued {"repetition_tolerance": [0.0, 0.2], "novelty_tolerance": [0.0, 0.3]}
#
# Bash on purpose, NOT zsh (see scripts/run_phase11_load.sh / run_phase15_experiment.sh /
# run_phase16_experiment.sh precedent): the word-splitting this script relies on is POSIX sh
# behaviour zsh does not do by default. For a long batch, run it detached:
#   nohup bash scripts/run_phase17_experiment.sh > results/phase17/nohup.log 2>&1 &
#
# CONCURRENCY: 4 concurrent x 2 waves (bounds contention to at most 4 simulate processes at once,
# per this package's brief) — wave 1 runs the four FIXED arms concurrently, wave 2 runs the four
# PERSONALIZED arms concurrently. --sequential runs all eight one at a time instead. Every number
# in this run except wall-clock/timing is deterministic (rng/clock-free, D8/D9) and UNAFFECTED by
# which mode you choose or how the arms interleave (same caveat as Phase 15/16's concurrent runs).
#
# SCALE AND EXPECTED WALL TIME: each arm is the same 10k-user x 100k-reel x 200-interactions/user
# medium dataset as Phase 15/16 (~2M impressions before session-dynamics truncation), with all of
# content_v2/latent_reactions/session_dynamics on plus hnsw_ranker_diversity. Phase 16's ranked
# "-sessions" arms on this exact dataset/gate combination were OBSERVED at ~12.9 min each
# (Release, concurrent) — use that as this script's per-arm estimate too (diversity re-ranking adds
# negligible per-request cost next to retrieval+ranking, and session-dynamics-driven early exits
# shrink total impressions the same way for every arm here). Two waves of four concurrent arms
# therefore land around ~26 min total wall time for all eight arms (Release build; see the header
# note on Debug vs Release below). --sequential instead costs roughly 8x one arm's solo time.
#
# Overridable via env or flags (a flag always wins over its env var): SIMULATE_BIN, CONFIGS_DIR,
# RESULTS_ROOT, SEED. Build the Release binary first, e.g. from the reel-rank repo root:
#   cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release -DREELRANK_VDB_DIR=/path/to/vector-db
#   cmake --build build-release --target simulate -j
#   SIMULATE_BIN=./build-release/apps/simulate scripts/run_phase17_experiment.sh
#
# SMOKE-SCALE RUNS: there is no simulation-shrinking --smoke flag on this script (the simulate
# binary's own --smoke flag is a fixed 50-user/500-reel dataset that still fully parses whatever
# config it is given — including the realism/cohort_mix block — before shrinking the dataset, which
# is exactly what makes `simulate --config <cohort config> --algorithm hnsw_ranker_diversity --smoke`
# a valid parse-validation smoke test for these configs on its own). To smoke-test THIS SCRIPT's
# plumbing (the personalized-config generation + 8-arm batching), point --configs-dir at a directory
# holding your own tiny realism-medium-cohort-{focused,noveltyseeker,creatorloyal,easilyfatigued}.json
# quadruplet (same basenames, smaller simulation.users/reels/interactions_per_user, all three realism
# gates on, one cohort_mix entry each) — everything else about this script is unchanged.

set -euo pipefail

SIMULATE_BIN="${SIMULATE_BIN:-build-release/apps/simulate}"
CONFIGS_DIR="${CONFIGS_DIR:-configs}"
RESULTS_ROOT="${RESULTS_ROOT:-results/phase17}"
SEED="${SEED:-42}"
SEQUENTIAL=0
ALGORITHM="hnsw_ranker_diversity"

usage() {
    cat <<'EOF'
usage: run_phase17_experiment.sh [--bin PATH] [--configs-dir DIR] [--out DIR] [--seed N]
                                  [--sequential]

  --bin PATH        simulate binary (default: build-release/apps/simulate; env SIMULATE_BIN)
  --configs-dir DIR directory holding realism-medium-cohort-{focused,noveltyseeker,creatorloyal,
                     easilyfatigued}.json (default: configs; env CONFIGS_DIR)
  --out DIR         results root; each arm writes under DIR/<arm-name>/, generated personalized
                     configs under DIR/generated-configs/ (default: results/phase17; env
                     RESULTS_ROOT)
  --seed N          master seed, applied to every arm (default: 42; env SEED)
  --sequential      run the eight arms one at a time instead of 4-concurrent x 2 waves (see the
                     concurrency note in this file's header)
  -h, --help        this message
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --bin) SIMULATE_BIN="$2"; shift 2 ;;
        --configs-dir) CONFIGS_DIR="$2"; shift 2 ;;
        --out) RESULTS_ROOT="$2"; shift 2 ;;
        --seed) SEED="$2"; shift 2 ;;
        --sequential) SEQUENTIAL=1; shift ;;
        -h|--help) usage; exit 0 ;;
        *) echo "error: unknown argument: $1" >&2; usage >&2; exit 2 ;;
    esac
done

if [[ ! -x "$SIMULATE_BIN" ]]; then
    echo "ERROR: simulate binary not found or not executable at: $SIMULATE_BIN" >&2
    echo "       Build it first (see the header of this script) or pass --bin / set SIMULATE_BIN." >&2
    exit 1
fi

if ! command -v python3 >/dev/null 2>&1; then
    echo "ERROR: python3 not found on PATH -- required for the personalized-config JSON patch step." >&2
    exit 1
fi

COHORTS=(focused noveltyseeker creatorloyal easilyfatigued)

declare -A BASE_CFG
for c in "${COHORTS[@]}"; do
    BASE_CFG[$c]="$CONFIGS_DIR/realism-medium-cohort-$c.json"
    if [[ ! -f "${BASE_CFG[$c]}" ]]; then
        echo "ERROR: config not found: ${BASE_CFG[$c]}" >&2
        exit 1
    fi
done

mkdir -p "$RESULTS_ROOT/logs" "$RESULTS_ROOT/generated-configs"

# Generate the four personalized variants: base config + "realism.personalized_diversity": true.
# A tiny stdlib-only JSON patch (D15: no simulation logic in Python, this is plumbing) -- never
# hand-duplicates the base config's content, so the personalized arm is guaranteed identical to its
# fixed counterpart in every field except this one gate.
declare -A PERSONALIZED_CFG
for c in "${COHORTS[@]}"; do
    PERSONALIZED_CFG[$c]="$RESULTS_ROOT/generated-configs/realism-medium-cohort-$c-personalized.json"
    python3 - "${BASE_CFG[$c]}" "${PERSONALIZED_CFG[$c]}" <<'PYEOF'
import json
import sys

with open(sys.argv[1]) as f:
    cfg = json.load(f)
cfg.setdefault("realism", {})["personalized_diversity"] = True
with open(sys.argv[2], "w") as f:
    json.dump(cfg, f, indent=2)
    f.write("\n")
PYEOF
done

echo "=== Phase 17 experiment: fixed vs personalized diversity x four trait cohorts ==="
echo "  binary      : $SIMULATE_BIN"
echo "  configs-dir : $CONFIGS_DIR"
echo "  out         : $RESULTS_ROOT"
echo "  seed        : $SEED"
if [[ $SEQUENTIAL -eq 1 ]]; then
    echo "  mode        : sequential"
else
    echo "  mode        : 4-concurrent x 2 waves (wave 1 = fixed arms, wave 2 = personalized arms)"
fi
echo "  start       : $(date)"
echo

# run_arm <name> <algorithm> <config-file>
#
# Runs one arm to completion, logging to $RESULTS_ROOT/logs/<name>.log, and returns simulate's exit
# code. Never uses `set -e`-fatal constructs internally so a failing arm never takes the rest of the
# script down with it -- callers guard every invocation with `|| rc=$?`.
run_arm() {
    local name="$1" algorithm="$2" config="$3"
    local out_dir="$RESULTS_ROOT/$name"
    local log="$RESULTS_ROOT/logs/$name.log"
    mkdir -p "$out_dir"
    : > "$log"
    echo ">>> [$name] algorithm=$algorithm config=$config seed=$SEED -> $out_dir"
    local rc=0
    "$SIMULATE_BIN" --config "$config" --algorithm "$algorithm" --seed "$SEED" --out "$out_dir" \
        >> "$log" 2>&1 || rc=$?
    if [[ $rc -eq 0 ]]; then
        echo "<<< [$name] done"
    else
        echo "<<< [$name] FAILED (exit $rc) -- see $log"
    fi
    return $rc
}

ARM_NAMES=()
ARM_ALGOS=()
ARM_CONFIGS=()
for c in "${COHORTS[@]}"; do
    ARM_NAMES+=("$c-fixed")
    ARM_ALGOS+=("$ALGORITHM")
    ARM_CONFIGS+=("${BASE_CFG[$c]}")
done
for c in "${COHORTS[@]}"; do
    ARM_NAMES+=("$c-personalized")
    ARM_ALGOS+=("$ALGORITHM")
    ARM_CONFIGS+=("${PERSONALIZED_CFG[$c]}")
done
# ARM_NAMES is now (indices 0-3 = fixed arms, 4-7 = personalized arms):
#   0 focused-fixed             4 focused-personalized
#   1 noveltyseeker-fixed       5 noveltyseeker-personalized
#   2 creatorloyal-fixed        6 creatorloyal-personalized
#   3 easilyfatigued-fixed      7 easilyfatigued-personalized

ARM_RC=()

if [[ $SEQUENTIAL -eq 1 ]]; then
    for i in "${!ARM_NAMES[@]}"; do
        rc=0
        run_arm "${ARM_NAMES[$i]}" "${ARM_ALGOS[$i]}" "${ARM_CONFIGS[$i]}" || rc=$?
        ARM_RC+=("$rc")
    done
else
    # Wave 1: the four FIXED arms (indices 0-3), 4-concurrent.
    ARM_PIDS=()
    for i in 0 1 2 3; do
        run_arm "${ARM_NAMES[$i]}" "${ARM_ALGOS[$i]}" "${ARM_CONFIGS[$i]}" &
        ARM_PIDS+=($!)
    done
    for pid in "${ARM_PIDS[@]}"; do
        rc=0
        wait "$pid" || rc=$?
        ARM_RC+=("$rc")
    done
    # Wave 2: the four PERSONALIZED arms (indices 4-7), 4-concurrent.
    ARM_PIDS=()
    for i in 4 5 6 7; do
        run_arm "${ARM_NAMES[$i]}" "${ARM_ALGOS[$i]}" "${ARM_CONFIGS[$i]}" &
        ARM_PIDS+=($!)
    done
    for pid in "${ARM_PIDS[@]}"; do
        rc=0
        wait "$pid" || rc=$?
        ARM_RC+=("$rc")
    done
fi

echo
echo "=== Phase 17 experiment: ALL ARMS DONE ==="
echo "  finished: $(date)"
echo
overall_rc=0
for i in "${!ARM_NAMES[@]}"; do
    name="${ARM_NAMES[$i]}"
    rc="${ARM_RC[$i]}"
    log="$RESULTS_ROOT/logs/$name.log"
    resolved_dir="$(awk '/^ *out /{print $2; exit}' "$log" 2>/dev/null || true)"
    if [[ $rc -eq 0 ]]; then
        status="OK"
    else
        status="FAILED (exit $rc)"
        overall_rc=1
    fi
    printf '  %-28s %-60s %s\n' "$name" "${resolved_dir:-$log}" "$status"
done
echo
echo "Compare the completed arms, e.g.:"
echo "  python3 scripts/phase17_comparison.py \\"
echo "      --focused-fixed <dir> --focused-personalized <dir> \\"
echo "      --noveltyseeker-fixed <dir> --noveltyseeker-personalized <dir> \\"
echo "      --creatorloyal-fixed <dir> --creatorloyal-personalized <dir> \\"
echo "      --easilyfatigued-fixed <dir> --easilyfatigued-personalized <dir> \\"
echo "      --out results/published/phase17"
echo
echo "Run the Tier-2 acceptance statistical test (in-process reduced-scale, expect skips until"
echo "package A/B land):"
echo "  ctest --test-dir build --output-on-failure -R PersonalizedVsFixedStatisticalTest"

exit $overall_rc
