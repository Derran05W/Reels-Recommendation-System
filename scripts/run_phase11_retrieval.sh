#!/usr/bin/env bash
#
# run_phase11_retrieval.sh — compose the full Phase 11 TDD 17.3 retrieval sweep.
#
# Run BY THE ORCHESTRATOR at integration, SERIALLY, on an otherwise-idle machine (published
# latencies must not be contaminated by concurrent builds/runs — do NOT run this inside a worktree
# alongside sibling packages). Each pass is one benchmark_retrieval invocation writing its own
# results/<experiment-id>/ subtree under $OUT.
#
# ---------------------------------------------------------------------------------------------
# ESTIMATED SERIAL WALL TIME (Apple M5, 10 cores, 24 GB; from Phase 11 package B smoke/calibration
# runs — a single 100k dim64 efC200 all-M build+sweep measured 85 s, with per-build times
# M8=16 s / M16=22 s / M32=37 s):
#
#   (a) 100k full grid (dims 64,128 x M x efC{50,100,200,400})   ~14  min   (index builds dominate)
#   (b) 10k dims-completeness (dims 32,64,128,256, efC 200)      ~1   min
#   (c) 1M stretch (dim 64 M{16,32}; + optional dim 128 M16)     ~12  min   (+ ~9 min if dim128 on)
#   (d) clustered 100k operating point (dims 64,128, M16)        ~2   min
#   (e) distance-computation pass (100k x 64, counting on)       ~2   min
#   ------------------------------------------------------------------------
#   TOTAL  ~31 min (RUN_1M_DIM128=0)  /  ~40 min (RUN_1M_DIM128=1, the default)
#
# These are estimates; index-build time is the long pole and can run 1.5-2x on a slower/busier
# host, so budget up to ~70 min. This is comfortably under the ~3 h ceiling, so NOTHING is trimmed
# by default. If a slower integration host pushes the estimate past ~3 h, trim in this priority
# order (keep the scientifically load-bearing passes): keep (c), (d), and (a) restricted to
# efC {100,200} — drop (b) and (e) first, then efC {50,400} from (a).
#
# Peak RAM: the 1M dim-64 M32 index peaks near ~1.5-2 GB RSS (100k M32 measured 193 MB); dim-128
# M16 at 1M adds a ~512 MB index plus the exact ground-truth copy. All well within 24 GB.
# ---------------------------------------------------------------------------------------------
#
# Env overrides:
#   BIN             path to the Release benchmark_retrieval binary
#                   (default: <repo>/build-rel/apps/benchmark_retrieval)
#   OUT             results root (default: <repo>/results/phase11/retrieval)
#   SEED            master seed (default: 42)
#   RUN_1M_DIM128   1 => also run the optional 1M dim-128 M16 cell (default: 1)
#
set -euo pipefail

# Resolve repo root from this script's location (scripts/ is a direct child of the repo root).
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

BIN="${BIN:-${REPO_ROOT}/build-rel/apps/benchmark_retrieval}"
OUT="${OUT:-${REPO_ROOT}/results/phase11/retrieval}"
SEED="${SEED:-42}"
RUN_1M_DIM128="${RUN_1M_DIM128:-1}"

if [[ ! -x "${BIN}" ]]; then
    echo "error: benchmark_retrieval binary not found or not executable: ${BIN}" >&2
    echo "       build it first: cmake --build build-rel --target benchmark_retrieval" >&2
    exit 1
fi

mkdir -p "${OUT}"
echo "=== Phase 11 retrieval sweep ==="
echo "  bin : ${BIN}"
echo "  out : ${OUT}"
echo "  seed: ${SEED}   RUN_1M_DIM128=${RUN_1M_DIM128}"
echo

SWEEP_START=$(date +%s)

# run <label> <subdir> <args...> — invoke benchmark_retrieval, echoing progress and the RESULT_DIR.
run() {
    local label="$1" subdir="$2"
    shift 2
    local dest="${OUT}/${subdir}"
    mkdir -p "${dest}"
    echo ">>> [${label}] $(date '+%H:%M:%S')  benchmark_retrieval $*"
    local t0 t1
    t0=$(date +%s)
    # benchmark_retrieval prints RESULT_DIR=... on stdout and progress on stderr.
    "${BIN}" --seed "${SEED}" --out "${dest}" "$@"
    t1=$(date +%s)
    echo "<<< [${label}] done in $(( t1 - t0 )) s"
    echo
}

# (a) 100k full grid: dims {64,128} x M {8,16,32} x efC {50,100,200,400} x ef x k, >=1000 queries.
run "a/100k-full" "100k-full" \
    --dims 64,128 --efcs 50,100,200,400 --vector-counts 100000 --queries 1000

# (b) 10k dimension-completeness: dims {32,64,128,256} x M {8,16,32} x efC 200, full ef x k.
run "b/10k-dims" "10k-dims" \
    --dims 32,64,128,256 --efcs 200 --vector-counts 10000 --queries 1000

# (c) 1M stretch: dim 64, M {16,32}, efC 200, full ef x k, >=300 queries.
run "c/1M-d64" "1M-d64" \
    --dims 64 --ms 16,32 --efcs 200 --vector-counts 1000000 --queries 300

# (c, optional) 1M dim 128, M 16 — smoke-derived estimate says it fits in ~9 extra minutes / RAM.
if [[ "${RUN_1M_DIM128}" == "1" ]]; then
    run "c/1M-d128-m16" "1M-d128-m16" \
        --dims 128 --ms 16 --efcs 200 --vector-counts 1000000 --queries 300
fi

# (d) clustered operating point: 100k clustered, dims {64,128}, M 16, efC 200 — the TDD 27
#     "Recall@10 > 90%" verdict data. ef {64,128,256} is required; we sweep all five ef (the two
#     extra cells are ~free — build cost is per (dim,M,efC), independent of ef) and all k.
run "d/clustered-100k" "clustered-100k" \
    --clustered --clustered-query-source reels \
    --dims 64,128 --ms 16 --efcs 200 --vector-counts 100000 --queries 1000

# (e) distance-computation pass: 100k x 64, M {8,16,32}, efC 200, counting ON. Rows carry
#     distance_comps_per_query; their latency is flagged non-clean (distance_comps_per_query >= 0)
#     so it is NOT published as clean latency — clean latency comes from passes (a)-(d).
run "e/dist-100k-d64" "dist-100k-d64" \
    --count-distances --dims 64 --ms 8,16,32 --efcs 200 --vector-counts 100000 --queries 1000

SWEEP_END=$(date +%s)
echo "=== Phase 11 retrieval sweep complete in $(( SWEEP_END - SWEEP_START )) s ==="
echo "Results under: ${OUT}"
