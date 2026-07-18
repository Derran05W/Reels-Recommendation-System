#!/usr/bin/env bash
#
# run_phase15_experiment.sh — Phase 15 four-arm engagement-vs-satisfaction experiment
# (V2 TDD §4.4 core experiment / plan Phase 15 task 3).
#
# Runs, by default CONCURRENTLY, each arm writing under its OWN --out root (REQUIRED: concurrent
# same-second same-algorithm runs collide on experiment-id — "engagement" and "proxy" are both
# hnsw_ranker, so sharing an --out root could clash their <algorithm>-seed<seed>-<timestamp>
# directory names; separate roots make that impossible regardless of timing):
#
#   semantic     hnsw                 configs/realism-medium.json            (D23 semantic baseline)
#   engagement   hnsw_ranker          configs/realism-medium-engagement.json (watch-correlated preset)
#   proxy        hnsw_ranker          configs/realism-medium-proxy.json      (satisfaction-proxy preset)
#   oracle       oracle_satisfaction  configs/realism-medium.json            (EVALUATION-ONLY upper
#                                                                              bound; PENDING PACKAGE
#                                                                              B2 — throws on its
#                                                                              first request until
#                                                                              OracleSatisfactionRecommender
#                                                                              is implemented; wired
#                                                                              here so integration is
#                                                                              a config/flag flip, not
#                                                                              a script rewrite)
#
# Bash on purpose, NOT zsh (see scripts/run_phase11_load.sh precedent): the word-splitting this
# script relies on is POSIX sh behaviour zsh does not do by default. For a long batch, run it
# detached:
#   nohup bash scripts/run_phase15_experiment.sh > results/phase15/nohup.log 2>&1 &
#
# CONCURRENCY / LATENCY CAVEAT (document in any comparison written from this run's output): the
# default concurrent mode runs up to four simulate processes on one machine at once, so this run's
# `timing`/wall-clock numbers carry cache and memory-bandwidth contention (same caveat as Phase 10's
# seven concurrent arms). Every OTHER reported number is unaffected: simulation randomness is
# rng/clock-free (D8/D9), so reward, hidden satisfaction/regret, affinity, and every deterministic
# CSV are byte-identical to a solo run at the same seed. Pass --sequential for contention-free timing
# at the cost of roughly 4x wall time.
#
# SCALE: each medium arm is 10k users x 100k reels x 200 interactions/user (~2M impressions) and
# takes roughly 7-12 minutes in Release. The oracle arm currently fails on its FIRST request (the
# package-B2 stub throws std::logic_error before any dataset-scale work is wasted), so it exits in
# seconds — this is EXPECTED pre-integration, not a script bug; the script's own exit code reflects
# only non-oracle failures (see the summary loop below).
#
# Overridable via env or flags (a flag always wins over its env var): SIMULATE_BIN, CONFIGS_DIR,
# RESULTS_ROOT, SEED. Build the Release binary first, e.g. from the reel-rank repo root:
#   cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release -DREELRANK_VDB_DIR=/path/to/vector-db
#   cmake --build build-release --target simulate -j
#   SIMULATE_BIN=./build-release/apps/simulate scripts/run_phase15_experiment.sh
#
# SMOKE-SCALE RUNS: there is no simulation-shrinking --smoke flag on this script (the simulate
# binary's own --smoke flag is a fixed 50-user/500-reel dataset, too small to be useful for a named
# multi-arm comparison, and it does not touch the realism/ranking blocks this experiment varies). To
# smoke-test the plumbing instead, point --configs-dir at a directory holding your own tiny
# realism-medium[.json|-engagement.json|-proxy.json] triple (same basenames, smaller
# simulation.users/reels/interactions_per_user) — everything else about this script is unchanged.

set -euo pipefail

SIMULATE_BIN="${SIMULATE_BIN:-build-release/apps/simulate}"
CONFIGS_DIR="${CONFIGS_DIR:-configs}"
RESULTS_ROOT="${RESULTS_ROOT:-results/phase15}"
SEED="${SEED:-42}"
SEQUENTIAL=0
SKIP_ORACLE=0

usage() {
    cat <<'EOF'
usage: run_phase15_experiment.sh [--bin PATH] [--configs-dir DIR] [--out DIR] [--seed N]
                                  [--sequential] [--skip-oracle]

  --bin PATH        simulate binary (default: build-release/apps/simulate; env SIMULATE_BIN)
  --configs-dir DIR directory holding realism-medium.json / realism-medium-engagement.json /
                     realism-medium-proxy.json (default: configs; env CONFIGS_DIR)
  --out DIR         results root; each arm writes under DIR/<arm-name>/ (default: results/phase15;
                     env RESULTS_ROOT)
  --seed N          master seed, applied to every arm (default: 42; env SEED)
  --sequential      run the four arms one at a time instead of concurrently (default: concurrent;
                     see the concurrency/latency caveat in this file's header)
  --skip-oracle     do not attempt the oracle_satisfaction arm (still pending package B2)
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
        --skip-oracle) SKIP_ORACLE=1; shift ;;
        -h|--help) usage; exit 0 ;;
        *) echo "error: unknown argument: $1" >&2; usage >&2; exit 2 ;;
    esac
done

if [[ ! -x "$SIMULATE_BIN" ]]; then
    echo "ERROR: simulate binary not found or not executable at: $SIMULATE_BIN" >&2
    echo "       Build it first (see the header of this script) or pass --bin / set SIMULATE_BIN." >&2
    exit 1
fi

SEMANTIC_CFG="$CONFIGS_DIR/realism-medium.json"
ENGAGEMENT_CFG="$CONFIGS_DIR/realism-medium-engagement.json"
PROXY_CFG="$CONFIGS_DIR/realism-medium-proxy.json"
ORACLE_CFG="$CONFIGS_DIR/realism-medium.json" # oracle ignores ranking weights; same base dataset

for f in "$SEMANTIC_CFG" "$ENGAGEMENT_CFG" "$PROXY_CFG"; do
    if [[ ! -f "$f" ]]; then
        echo "ERROR: config not found: $f" >&2
        exit 1
    fi
done

mkdir -p "$RESULTS_ROOT/logs"

echo "=== Phase 15 experiment: semantic vs engagement-optimized vs satisfaction-proxy vs oracle ==="
echo "  binary      : $SIMULATE_BIN"
echo "  configs-dir : $CONFIGS_DIR"
echo "  out         : $RESULTS_ROOT"
echo "  seed        : $SEED"
if [[ $SEQUENTIAL -eq 1 ]]; then
    echo "  mode        : sequential"
else
    echo "  mode        : concurrent (see latency-contention caveat in this script's header)"
fi
echo "  oracle      : $([[ $SKIP_ORACLE -eq 1 ]] && echo "skipped (--skip-oracle)" || echo "attempted (pending package B2 — expected to fail)")"
echo "  start       : $(date)"
echo

# run_arm <name> <algorithm> <config-file>
#
# Runs one arm to completion, logging to $RESULTS_ROOT/logs/<name>.log, and returns simulate's exit
# code. Never uses `set -e`-fatal constructs internally so a failing arm (the oracle, pre-B2) never
# takes the rest of the script down with it — callers guard every invocation with `|| rc=$?`.
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

ARM_NAMES=(semantic engagement proxy)
ARM_ALGOS=(hnsw hnsw_ranker hnsw_ranker)
ARM_CONFIGS=("$SEMANTIC_CFG" "$ENGAGEMENT_CFG" "$PROXY_CFG")
if [[ $SKIP_ORACLE -eq 0 ]]; then
    ARM_NAMES+=(oracle)
    ARM_ALGOS+=(oracle_satisfaction)
    ARM_CONFIGS+=("$ORACLE_CFG")
fi

ARM_RC=()

if [[ $SEQUENTIAL -eq 1 ]]; then
    for i in "${!ARM_NAMES[@]}"; do
        rc=0
        run_arm "${ARM_NAMES[$i]}" "${ARM_ALGOS[$i]}" "${ARM_CONFIGS[$i]}" || rc=$?
        ARM_RC+=("$rc")
    done
else
    ARM_PIDS=()
    for i in "${!ARM_NAMES[@]}"; do
        run_arm "${ARM_NAMES[$i]}" "${ARM_ALGOS[$i]}" "${ARM_CONFIGS[$i]}" &
        ARM_PIDS+=($!)
    done
    for i in "${!ARM_NAMES[@]}"; do
        rc=0
        wait "${ARM_PIDS[$i]}" || rc=$?
        ARM_RC+=("$rc")
    done
fi

echo
echo "=== Phase 15 experiment: ALL ARMS DONE ==="
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
    elif [[ "$name" == "oracle" ]]; then
        status="FAILED (expected pre-integration: oracle_satisfaction throws until package B2 lands)"
    else
        status="FAILED (exit $rc)"
        overall_rc=1
    fi
    printf '  %-11s %-70s %s\n' "$name" "${resolved_dir:-$log}" "$status"
done
echo
echo "Compare the completed arms, e.g.:"
echo "  python3 scripts/phase15_comparison.py \\"
echo "      --semantic <semantic dir> --engagement <engagement dir> --proxy <proxy dir> \\"
echo "      [--oracle <oracle dir>] --out results/published/phase15"
echo
echo "Plot the headline figure (engagement_vs_welfare.png, among others), e.g.:"
echo "  cd scripts && UV_PYTHON=3.12 python3 -m uv run plot_results.py \\"
echo "      ../<semantic dir> ../<engagement dir> ../<proxy dir> ../<oracle dir> \\"
echo "      --labels semantic,engagement,proxy,oracle --out ../results/published/phase15/figures"

exit $overall_rc
