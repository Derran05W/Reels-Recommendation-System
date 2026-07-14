#!/usr/bin/env bash
#
# run_phase11_load.sh — Phase 11 load-driver cell matrix (plan task 2/3, deliverable).
#
# Runs the FULL published matrix SERIALLY, one process per (corpus, dim) cell-group, on an IDLE
# machine. Serial + one-process-per-cell is REQUIRED for trustworthy numbers: parallel runs would
# contaminate latency, and peak RSS is a process-lifetime value so it is only meaningful per process.
# This is the script the orchestrator runs ON MAIN at integration to produce the published figures;
# do NOT run it inside a worktree alongside other package builds.
#
# Bash on purpose (NOT zsh): the unquoted "--threads $THREADS" style and word-splitting below assume
# POSIX sh word-splitting, which zsh does not do by default.
#
# Matrix (each cell-group swept over threads 1,2,4,8,10, one process each):
#   (10k reels, 1k users, 64d)      candidateLimit auto -> 200 (TDD 27 small)
#   (100k reels, 10k users, 64d)    candidateLimit auto -> 500 (TDD 27 medium)
#   (100k reels, 10k users, 128d)   dimension sweep
#   (1M reels, 100k users, 64d)     large stretch (TDD 17.3 1M row)
#
# >=2000 timed requests/thread everywhere. The 1M cell is bounded: an M5 estimate puts it around
# ~10-12 min at 2000 req/thread (index build ~1.5 min + ~8 min sweep), comfortably under the ~45 min
# budget, so it keeps 2000. If a slower machine or a larger dim pushes the 1M cell past ~45 min,
# lower REQS_1M (e.g. to 1000) — that is the only cell the budget note permits scaling down, and any
# such change must be recorded next to the published numbers.
#
# Overridable via env: BENCH_BIN, OUT_DIR, SEED, REQS, WARMUP, THREADS, REQS_1M.
# Build the binary first, e.g. (from the reel-rank repo root):
#   cmake -S . -B build-rel -DCMAKE_BUILD_TYPE=Release -DREELRANK_VDB_DIR=/path/to/vector-db
#   cmake --build build-rel --target benchmark_recommender -j
#   BENCH_BIN=./build-rel/apps/benchmark_recommender scripts/run_phase11_load.sh

set -euo pipefail

BENCH_BIN="${BENCH_BIN:-./build-rel/apps/benchmark_recommender}"
OUT_DIR="${OUT_DIR:-results/phase11/load}"
SEED="${SEED:-42}"
REQS="${REQS:-2000}"        # timed requests per thread (all cells except the 1M override)
REQS_1M="${REQS_1M:-2000}"  # timed requests per thread for the 1M stretch cell
WARMUP="${WARMUP:-200}"
THREADS="${THREADS:-1,2,4,8,10}"

if [[ ! -x "$BENCH_BIN" ]]; then
    echo "ERROR: benchmark_recommender not found or not executable at: $BENCH_BIN" >&2
    echo "       Build it first (see the header of this script) or set BENCH_BIN." >&2
    exit 1
fi

mkdir -p "$OUT_DIR"

echo "=== Phase 11 load matrix ==="
echo "  binary : $BENCH_BIN"
echo "  out    : $OUT_DIR"
echo "  seed   : $SEED   threads: $THREADS   warmup: $WARMUP   req/thread: $REQS (1M: $REQS_1M)"
echo "  start  : $(date)"
echo

# run_cell <reels> <users> <dim> <req-per-thread> <label>
run_cell() {
    local reels="$1" users="$2" dim="$3" reqs="$4" label="$5"
    echo ">>> CELL $label: reels=$reels users=$users dim=$dim threads=$THREADS req/thread=$reqs"
    local t0 t1
    t0=$(date +%s)
    "$BENCH_BIN" \
        --reels "$reels" \
        --users "$users" \
        --dim "$dim" \
        --threads "$THREADS" \
        --requests-per-thread "$reqs" \
        --warmup "$WARMUP" \
        --seed "$SEED" \
        --out "$OUT_DIR"
    t1=$(date +%s)
    echo "<<< CELL $label done in $((t1 - t0))s"
    echo
}

run_cell 10000    1000   64  "$REQS"    "small-10k-64d"
run_cell 100000   10000  64  "$REQS"    "medium-100k-64d"
run_cell 100000   10000  128 "$REQS"    "medium-100k-128d"
run_cell 1000000  100000 64  "$REQS_1M" "large-1M-64d"

echo "=== Phase 11 load matrix COMPLETE ==="
echo "  finished: $(date)"
echo "  results under: $OUT_DIR/ (one load-* directory per cell-group, each with load_metrics.csv +"
echo "                 config.json + summary.json + metadata.json)"
