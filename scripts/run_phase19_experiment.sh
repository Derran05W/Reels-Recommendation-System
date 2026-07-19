#!/usr/bin/env bash
#
# run_phase19_experiment.sh — Phase 19 Tier-3 core experiment: BATCH-DEPTH (feed prefetch) trade-off
# under abrupt preference drift (V2 TDD §4.13 + Tier-3 core experiment, plan Phase 19 task 3).
#
# FOUR arms = serving.prefetch_depth in {1, 3, 10, 20}, everything else held fixed:
#
#   batch1    hnsw_ranker  configs/realism-medium-events-drift.json + serving.prefetch_depth=1
#   batch3    hnsw_ranker  configs/realism-medium-events-drift.json + serving.prefetch_depth=3
#   batch10   hnsw_ranker  configs/realism-medium-events-drift.json + serving.prefetch_depth=10
#   batch20   hnsw_ranker  configs/realism-medium-events-drift.json + serving.prefetch_depth=20
#
# The base config is event-mode `configs/realism-medium-events-drift.json` (this package's own new
# config: realism-medium-sessions.json's 10k-user/100k-reel/32-topic/64-dim dataset plus
# `simulation.scheduler="event_queue"`, `horizon_seconds=172800` [2 simulated days], and the P10
# abrupt-drift `drift.events` block copied VERBATIM from configs/phase10-drift.json — see that
# config's own header / this script's "DESIGN NOTES" section below for the full reasoning). Every
# arm sets `serving.refill_threshold=0` explicitly (refill fires only when the prefetched deque is
# empty — the single free variable across arms is prefetch depth, keeping this a clean one-variable
# sweep) via the SAME tiny stdlib-only JSON-patch pattern package C of Phase 17 established
# (scripts/run_phase17_experiment.sh's per-arm python3 heredoc) — never hand-duplicates the base
# config, so every arm is guaranteed identical to the others in every field except
# `serving.prefetch_depth`. Generated configs are left on disk next to the results (not a throwaway
# mktemp) so `cat` on them is a trivial way to confirm exactly what ran.
#
# DESIGN NOTES (horizon + drift retiming — this package's brief asked for horizon_seconds sized so
# users average ~=200 interactions, estimated from "P18's tiny-run rates: ~62 impressions/user in 6h
# at defaults", suggesting 86400*2=172800s as a starting candidate to document and let the
# integrator retune. VERIFIED AND CORRECTED below with direct measurements against this tree,
# because the stated basis turned out not to reproduce):
#   - THE STATED BASIS DOES NOT REPRODUCE. Re-running the COMMITTED
#     tests/golden/event-digest/config.json (200 users/2000 reels/dim 32, full gate stack,
#     horizon_seconds=21600 [6h]) today yields only ~1,567 impressions / 200 users (~7.8/user), not
#     the ~62/user commit.md's Phase 18 prose describes (12,380 impressions/200 users) — most likely
#     that prose came from an uncommitted intermediate run predating the pinned digest fixture's
#     final parameters, not the config as committed. Rather than propagate a non-reproducing number,
#     this config's horizon is sized from DIRECT MEASUREMENTS (see
#     tests/property/batch_adaptation_statistical_test.cpp's header for the exact same correction at
#     small scale, and this package's final report for the raw numbers): at this config's shape (32
#     topics, dim 64, feed_size 10, vector_candidates 500 — population/reel-corpus size scaled down
#     to 300 users / 3,000 reels purely for fast measurement, since per-user session/return dynamics
#     are independent of population size), horizon_seconds=86400 (1 day) measured ~48 impressions/
#     user, and horizon_seconds=172800 (2 days, the brief's own candidate) measured ~106/user —
#     roughly HALF the ~200/user target, confirming the brief's suggested 2-day value would have
#     undershot substantially. A linear fit through those two measured points
#     (slope=(106-48)/86400=0.00067/s, intercept=48-0.00067*86400=-9.5) projects
#     impressions/user = -9.5 + 0.00067*horizon_seconds, i.e. ~192/user at horizon_seconds=302400
#     (86400*3.5 = 3.5 simulated days) — the value this config uses. This is REGARDLESS an
#     EXTRAPOLATION (measured at 300 users, applied to this config's 10,000): per-user session/
#     return timelines are independent of OTHER users in this model (only shared global popularity/
#     trending accumulators are population-size-sensitive, and those do not gate an individual
#     user's own progression), so population-invariance of the per-user rate is a reasonable
#     modelling assumption, not a measurement at the full 10k scale. Retune `horizon_seconds` here
#     once a real medium-scale event-mode run exists (the integrator, per the brief) — this
#     experiment's drift fires at PER-USER interaction 100 (`at_interaction`, copied verbatim from
#     configs/phase10-drift.json — see below) and needs headroom on both sides of that mark (a
#     stable pre-drift baseline plus enough post-drift interactions to observe recovery), so erring
#     toward a slightly-generous horizon over the exact 200/user figure is the safer direction if the
#     real number disagrees with this projection.
#   - DRIFT: `drift.events` below is configs/phase10-drift.json's block COPIED VERBATIM (four
#     disjoint quartile cohorts by `hash01(userId)`, each retargeted to a 3-topic mix, all at
#     `at_interaction=100`). No retiming arithmetic was needed: `DriftEvent.atInteraction` keys off
#     each user's own COMPLETED-INTERACTION COUNT (a per-user counter incremented identically by
#     both runners — see include/rr/infrastructure/config.hpp's DriftEvent doc comment and
#     commit.md's Phase 18 entry: "drift keeps the verbatim interaction-count keying"), not wall-
#     clock/simulated-clock time, so the SAME `at_interaction=100` value fires at the same point in
#     each user's own consumption timeline under event mode as it does under round-robin — no unit
#     conversion is meaningful or necessary. The only real design question is horizon sizing (above):
#     the horizon must be generous enough that most users actually REACH interaction 100 (and enough
#     beyond it to show recovery), which is exactly what the 2-day choice targets.
#
# PACKAGE STATUS AT THIS SCRIPT'S SCAFFOLD (Phase 19 package C, worktree p19-package-c): package A
# (serving strategies + invalidation semantics) is a PARALLEL, invisible worktree. In THIS tree,
# `serving.*` config EXISTS and PARSES (validated event-mode-only at load, see
# src/infrastructure/config.cpp), but the event runner IGNORES every serving.* value and always
# behaves as Phase 18 shipped it (depth-1 refill regardless of `prefetch_depth`/`refill_threshold`).
# Every arm below is therefore EXPECTED to produce BIT-IDENTICAL output (same event-log digest, same
# impression/request counts, same welfare/session-health numbers) until package A lands — this is
# the same pending-integration state scripts/phase19_comparison.py and
# tests/property/batch_adaptation_statistical_test.cpp are written to DETECT and report honestly (a
# computed check, not an assumoption), not a bug in this script. `counts.requests` in each arm's
# summary.json (an EXISTING, already-real field — see src/evaluation/event_driven_runner.cpp) is a
# genuine "feed request count" today (one RequestFeed event per request); it is expected to start
# DIFFERING across arms (fewer, larger requests at higher prefetch depth) the moment package A wires
# prefetch depth into the runner's refill logic.
#
# Bash on purpose, NOT zsh (see scripts/run_phase11_load.sh / run_phase15_experiment.sh /
# run_phase16_experiment.sh / run_phase17_experiment.sh precedent): the word-splitting this script
# relies on is POSIX sh behaviour zsh does not do by default. For a long batch, run it detached:
#   nohup bash scripts/run_phase19_experiment.sh > results/phase19/nohup.log 2>&1 &
#
# CONCURRENCY: 4-concurrent (one wave; bounds contention to at most 4 simulate processes at once, per
# this package's brief: "concurrent with per-arm --out"). --sequential runs all four arms one at a
# time instead. Every number in this run except wall-clock/timing is deterministic (rng/clock-free,
# D8/D9) and UNAFFECTED by which mode you choose (same caveat as every prior phase's concurrent
# scripts).
#
# SCALE AND EXPECTED WALL TIME: each arm is the full 10k-user x 100k-reel x 32-topic x 64-dim medium
# dataset (configs/realism-medium-events-drift.json), event-mode, horizon 302400s (3.5 simulated
# days — see DESIGN NOTES above for why), full content_v2/latent_reactions/session_dynamics gate
# stack, hnsw_ranker. Extrapolated from this package's own 300-user Debug-build diagnostics (medium
# config's exact shape: 32 topics, dim 64, feed_size 10, vector_candidates 500; NOT a medium-scale
# reference run, which does not exist in this worktree): wall_seconds measured ~14.7s @ horizon
# 86400 and ~31.4s @ horizon 172800 (300 users, Debug), a roughly linear fit projects ~56s @ horizon
# 302400 for 300 users; scaling by population (10,000/300 ~= 33.3x) gives ~1,880s (~31 min) per arm
# in DEBUG. Release is typically markedly faster for this kind of numeric-heavy simulation code, so
# treat "roughly 10-25 minutes per arm in Release" as the planning estimate — a wide, honestly-
# extrapolated range, not a measured number. Event mode is separately expected to run FASTER than an
# equivalent round-robin arm at the same interaction budget (V2 TDD §4.8-driven probabilistic exits
# truncate a user's remaining consumption once fired — see scripts/run_phase16_experiment.sh's
# header for the identical reasoning), and the published round-robin "-sessions" arms at this exact
# 10k/100k/200-interactions/user dataset (results/published/phase15/*, phase16 re-run) observed
# hnsw_ranker wall times of roughly 2.5-9.5 minutes per arm (Release, concurrent) — a sanity floor
# this experiment's estimate above is consistent with, given it targets a similar total interaction
# volume. Smoke-test at tiny scale first (see below), and have the integrator record the real
# medium-scale number the first time this script is run to completion.
#
# Overridable via env or flags (a flag always wins over its env var): SIMULATE_BIN, CONFIGS_DIR,
# RESULTS_ROOT, SEED, ALGORITHM. Build the Release binary first, e.g. from the reel-rank repo root:
#   cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release -DREELRANK_VDB_DIR=/path/to/vector-db
#   cmake --build build-release --target simulate -j
#   SIMULATE_BIN=./build-release/apps/simulate scripts/run_phase19_experiment.sh
#
# SMOKE-SCALE RUNS: there is no simulation-shrinking --smoke flag on this script (the simulate
# binary's own --smoke flag is a fixed 50-user/500-reel ROUND-ROBIN dataset that does not set
# scheduler=event_queue, so it cannot exercise this experiment's serving/drift/event-mode config at
# all). To smoke-test THIS SCRIPT's plumbing (config generation + 4-arm batching), point
# --configs-dir at a directory holding your own tiny `realism-medium-events-drift.json` (same
# basename, smaller simulation.users/reels/dimensions, a short horizon_seconds, the full gate stack
# on, scheduler=event_queue) — everything else about this script is unchanged. A convenient starting
# point is tests/golden/event-digest/config.json (200 users/2000 reels/dim 32/6h horizon, full gate
# stack already on) plus a `drift` block copied from this config.
#
# Compare / plot / test afterward:
#   python3 scripts/phase19_comparison.py --batch1 <dir> --batch3 <dir> --batch10 <dir> \
#       --batch20 <dir> --out results/published/phase19
#   uv run --project scripts scripts/plot_results.py --canonical  # or plot_freshness_cost_frontier
#       directly, see plot_results.py's module docstring
#   ctest --test-dir build --output-on-failure -R BatchAdaptationStatisticalTest

set -euo pipefail

SIMULATE_BIN="${SIMULATE_BIN:-build-release/apps/simulate}"
CONFIGS_DIR="${CONFIGS_DIR:-configs}"
RESULTS_ROOT="${RESULTS_ROOT:-results/phase19}"
SEED="${SEED:-42}"
ALGORITHM="${ALGORITHM:-hnsw_ranker}"
SEQUENTIAL=0

usage() {
    cat <<'EOF'
usage: run_phase19_experiment.sh [--bin PATH] [--configs-dir DIR] [--out DIR] [--seed N]
                                  [--algorithm NAME] [--sequential]

  --bin PATH        simulate binary (default: build-release/apps/simulate; env SIMULATE_BIN)
  --configs-dir DIR directory holding realism-medium-events-drift.json (default: configs;
                     env CONFIGS_DIR)
  --out DIR         results root; each arm writes under DIR/<arm-name>/, generated per-arm configs
                     under DIR/generated-configs/ (default: results/phase19; env RESULTS_ROOT)
  --seed N          master seed, applied to every arm (default: 42; env SEED)
  --algorithm NAME  recommendation algorithm, applied to every arm (default: hnsw_ranker;
                     env ALGORITHM)
  --sequential      run the four arms one at a time instead of 4-concurrent (see the concurrency
                     note in this file's header)
  -h, --help        this message
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --bin) SIMULATE_BIN="$2"; shift 2 ;;
        --configs-dir) CONFIGS_DIR="$2"; shift 2 ;;
        --out) RESULTS_ROOT="$2"; shift 2 ;;
        --seed) SEED="$2"; shift 2 ;;
        --algorithm) ALGORITHM="$2"; shift 2 ;;
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
    echo "ERROR: python3 not found on PATH -- required for the per-arm JSON patch step." >&2
    exit 1
fi

BASE_CFG="$CONFIGS_DIR/realism-medium-events-drift.json"
if [[ ! -f "$BASE_CFG" ]]; then
    echo "ERROR: config not found: $BASE_CFG" >&2
    exit 1
fi

mkdir -p "$RESULTS_ROOT/logs" "$RESULTS_ROOT/generated-configs"

BATCH_DEPTHS=(1 3 10 20)

# Generate the four prefetch-depth variants: base config + "serving": {"prefetch_depth": N,
# "refill_threshold": 0}. A tiny stdlib-only JSON patch (D15: no simulation logic in Python, this is
# plumbing -- the same pattern scripts/run_phase17_experiment.sh established) -- never hand-
# duplicates the base config's content, so every arm is guaranteed identical to the others in every
# field except prefetch_depth.
declare -A ARM_CFG
for depth in "${BATCH_DEPTHS[@]}"; do
    name="batch${depth}"
    ARM_CFG[$name]="$RESULTS_ROOT/generated-configs/realism-medium-events-drift-$name.json"
    python3 - "$BASE_CFG" "${ARM_CFG[$name]}" "$depth" <<'PYEOF'
import json
import sys

with open(sys.argv[1]) as f:
    cfg = json.load(f)
depth = int(sys.argv[3])
cfg["serving"] = {"prefetch_depth": depth, "refill_threshold": 0}
with open(sys.argv[2], "w") as f:
    json.dump(cfg, f, indent=2)
    f.write("\n")
PYEOF
done

echo "=== Phase 19 experiment: batch-depth (feed prefetch) trade-off under abrupt drift ==="
echo "  binary      : $SIMULATE_BIN"
echo "  configs-dir : $CONFIGS_DIR"
echo "  base config : $BASE_CFG"
echo "  algorithm   : $ALGORITHM"
echo "  out         : $RESULTS_ROOT"
echo "  seed        : $SEED"
if [[ $SEQUENTIAL -eq 1 ]]; then
    echo "  mode        : sequential"
else
    echo "  mode        : 4-concurrent (one wave)"
fi
echo "  start       : $(date)"
echo

# run_arm <name> <config-file>
#
# Runs one arm to completion, logging to $RESULTS_ROOT/logs/<name>.log, and returns simulate's exit
# code. Never uses `set -e`-fatal constructs internally so a failing arm never takes the rest of the
# script down with it -- callers guard every invocation with `|| rc=$?`.
run_arm() {
    local name="$1" config="$2"
    local out_dir="$RESULTS_ROOT/$name"
    local log="$RESULTS_ROOT/logs/$name.log"
    mkdir -p "$out_dir"
    : > "$log"
    echo ">>> [$name] algorithm=$ALGORITHM config=$config seed=$SEED -> $out_dir"
    local rc=0
    "$SIMULATE_BIN" --config "$config" --algorithm "$ALGORITHM" --seed "$SEED" --out "$out_dir" \
        >> "$log" 2>&1 || rc=$?
    if [[ $rc -eq 0 ]]; then
        echo "<<< [$name] done"
    else
        echo "<<< [$name] FAILED (exit $rc) -- see $log"
    fi
    return $rc
}

ARM_NAMES=()
ARM_CONFIGS=()
for depth in "${BATCH_DEPTHS[@]}"; do
    name="batch${depth}"
    ARM_NAMES+=("$name")
    ARM_CONFIGS+=("${ARM_CFG[$name]}")
done

ARM_RC=()

if [[ $SEQUENTIAL -eq 1 ]]; then
    for i in "${!ARM_NAMES[@]}"; do
        rc=0
        run_arm "${ARM_NAMES[$i]}" "${ARM_CONFIGS[$i]}" || rc=$?
        ARM_RC+=("$rc")
    done
else
    ARM_PIDS=()
    for i in "${!ARM_NAMES[@]}"; do
        run_arm "${ARM_NAMES[$i]}" "${ARM_CONFIGS[$i]}" &
        ARM_PIDS+=($!)
    done
    for i in "${!ARM_NAMES[@]}"; do
        rc=0
        wait "${ARM_PIDS[$i]}" || rc=$?
        ARM_RC+=("$rc")
    done
fi

echo
echo "=== Phase 19 experiment: ALL ARMS DONE ==="
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
    printf '  %-10s %-60s %s\n' "$name" "${resolved_dir:-$log}" "$status"
done
echo
echo "Compare the completed arms, e.g.:"
echo "  python3 scripts/phase19_comparison.py \\"
echo "      --batch1 <dir> --batch3 <dir> --batch10 <dir> --batch20 <dir> \\"
echo "      --out results/published/phase19"
echo
echo "Plot the freshness-versus-cost frontier, e.g.:"
echo "  uv run --project scripts scripts/plot_results.py --canonical"
echo "  (or call plot_freshness_cost_frontier(...) directly -- see plot_results.py's docstring)"
echo
echo "Run the §7 batch-size-one statistical test (in-process reduced-scale, expect a SKIP until"
echo "package A lands the real serving strategies):"
echo "  ctest --test-dir build --output-on-failure -R BatchAdaptationStatisticalTest"

exit $overall_rc
