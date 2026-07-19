#!/usr/bin/env bash
#
# run_phase21_a.sh — Phase 21 package A failure-mode scenarios (P21-CONTRACTS §4/§7): filter_bubble
# (2 arms) + exploration_recovery (3 arms), each arm on the IDENTICAL world/seed. Mirrors the
# scripts/run_phase20_experiment.sh JSON-patch pattern: read the committed scenario config (which IS
# the control arm and carries the pre-registered `description` block), apply a tiny stdlib-only
# python nested-merge for each treatment arm, run every arm with its own --out root, never
# hand-duplicate config content. Generated per-arm configs are left on disk under
# <out>/<scenario>/generated-configs/ so `diff` confirms exactly what ran.
#
# SCENARIOS / ARMS (all seed 42, event mode, evaluation.ecosystem_metrics=true, algorithm
# hnsw_ranker_diversity COMPLETE mode; the ONLY per-arm differences are the patches below):
#
#   filter_bubble/control    configs/scenarios/filter_bubble.json  (committed = control, no patch)
#   filter_bubble/bubble     + {"exploration":{"epsilon":0.0},
#                               "ranking":{"similarity_weight":0.95,"quality_weight":0.0,
#                                 "freshness_weight":0.0,"popularity_weight":0.0,
#                                 "trending_weight":0.0,"creator_affinity_weight":0.0,
#                                 "exploration_weight":0.0,"repetition_penalty":0.0}}
#   exploration_recovery/control    configs/scenarios/exploration_recovery.json (committed, no patch)
#   exploration_recovery/recover05  + {"exploration":{"epsilon":0.05,"enable_at_day":4.0}}
#   exploration_recovery/recover10  + {"exploration":{"epsilon":0.10,"enable_at_day":4.0}}
#
# exploration.enabled stays true in EVERY arm so the ExplorationCandidateSource is registered and its
# per-slot bernoulli draws stay stream-aligned (epsilon=0 / gated-effective-0 fire no slots but
# consume the same uniform01 draws — see ExplorationCandidateSource / P21-CONTRACTS §1). The patch
# only ever touches the listed keys via dict.update on the named sub-object, so every OTHER field is
# identical to the committed scenario config by construction.
#
# CONCURRENCY: --max-concurrency N (default 3; sibling packages B/C/D finished, so contention is
# low). Every reported number except wall/latency is deterministic (D8/D9) and unaffected by the
# concurrency choice. Long batch: run detached, e.g.
#   nohup bash scripts/run_phase21_a.sh > results/phase21/nohup.log 2>&1 &
# then poll the out dirs / ps (simulate block-buffers stdout when redirected — gauge progress via
# output files + CPU time, never the logs). Build the Release binary first:
#   cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release -DREELRANK_VDB_DIR=/path/to/vector-db
#   cmake --build build-release --target simulate -j
#
# Overridable via env or flags (flag wins): SIMULATE_BIN, CONFIGS_DIR, RESULTS_ROOT, SEED.

set -euo pipefail

SIMULATE_BIN="${SIMULATE_BIN:-build-release/apps/simulate}"
CONFIGS_DIR="${CONFIGS_DIR:-configs/scenarios}"
RESULTS_ROOT="${RESULTS_ROOT:-results/phase21}"
SEED="${SEED:-42}"
MAX_CONCURRENCY="${MAX_CONCURRENCY:-3}"

usage() {
    cat <<'EOF'
usage: run_phase21_a.sh [--bin PATH] [--configs-dir DIR] [--out DIR] [--seed N]
                        [--max-concurrency N]

  --bin PATH           simulate binary (default: build-release/apps/simulate; env SIMULATE_BIN).
                        Point at /usr/bin/true to smoke-test config generation + plumbing only.
  --configs-dir DIR    dir holding filter_bubble.json / exploration_recovery.json
                        (default: configs/scenarios; env CONFIGS_DIR)
  --out DIR            results root; each arm writes DIR/<scenario>/<arm>/, generated per-arm
                        configs under DIR/<scenario>/generated-configs/ (default: results/phase21)
  --seed N             master seed for every arm (default: 42; env SEED)
  --max-concurrency N  max simultaneous simulate processes (default: 3; env MAX_CONCURRENCY)
  -h, --help           this message
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --bin) SIMULATE_BIN="$2"; shift 2 ;;
        --configs-dir) CONFIGS_DIR="$2"; shift 2 ;;
        --out) RESULTS_ROOT="$2"; shift 2 ;;
        --seed) SEED="$2"; shift 2 ;;
        --max-concurrency) MAX_CONCURRENCY="$2"; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "error: unknown argument: $1" >&2; usage >&2; exit 2 ;;
    esac
done

if [[ ! -x "$SIMULATE_BIN" ]]; then
    echo "ERROR: simulate binary not found/executable at: $SIMULATE_BIN" >&2
    echo "       Build it first (see header) or pass --bin (e.g. --bin /usr/bin/true for plumbing)." >&2
    exit 1
fi
if ! command -v python3 >/dev/null 2>&1; then
    echo "ERROR: python3 not found on PATH -- required for the per-arm JSON patch step." >&2
    exit 1
fi

# Arm table: "scenario|arm|base-config-basename|patch-json". Empty patch => run the committed config.
ARMS=(
    "filter_bubble|control|filter_bubble.json|"
    "filter_bubble|bubble|filter_bubble.json|{\"exploration\":{\"epsilon\":0.0},\"ranking\":{\"similarity_weight\":0.95,\"quality_weight\":0.0,\"freshness_weight\":0.0,\"popularity_weight\":0.0,\"trending_weight\":0.0,\"creator_affinity_weight\":0.0,\"exploration_weight\":0.0,\"repetition_penalty\":0.0}}"
    "exploration_recovery|control|exploration_recovery.json|"
    "exploration_recovery|recover05|exploration_recovery.json|{\"exploration\":{\"epsilon\":0.05,\"enable_at_day\":4.0}}"
    "exploration_recovery|recover10|exploration_recovery.json|{\"exploration\":{\"epsilon\":0.10,\"enable_at_day\":4.0}}"
)

# Generate every arm's config up front (fast; lets `diff` confirm the one-variable design).
declare -a ARM_TAG ARM_CFG ARM_OUT ARM_LOG
for entry in "${ARMS[@]}"; do
    IFS='|' read -r scenario arm base patch <<< "$entry"
    base_cfg="$CONFIGS_DIR/$base"
    if [[ ! -f "$base_cfg" ]]; then
        echo "ERROR: scenario config not found: $base_cfg" >&2
        exit 1
    fi
    gen_dir="$RESULTS_ROOT/$scenario/generated-configs"
    out_dir="$RESULTS_ROOT/$scenario/$arm"
    log_dir="$RESULTS_ROOT/$scenario/logs"
    mkdir -p "$gen_dir" "$out_dir" "$log_dir"
    cfg="$gen_dir/$scenario-$arm.json"
    python3 - "$base_cfg" "$cfg" "$patch" <<'PYEOF'
import json
import sys

with open(sys.argv[1]) as f:
    cfg = json.load(f)
patch = json.loads(sys.argv[3]) if sys.argv[3] else {}

# Nested one-level merge: dict.update on each named sub-object, never a wholesale block replacement,
# so every key NOT in the patch keeps the committed scenario config's value (D6 default semantics).
for section, overrides in patch.items():
    if isinstance(overrides, dict) and isinstance(cfg.get(section), dict):
        cfg[section].update(overrides)
    else:
        cfg[section] = overrides

with open(sys.argv[2], "w") as f:
    json.dump(cfg, f, indent=2)
    f.write("\n")
PYEOF
    ARM_TAG+=("$scenario/$arm")
    ARM_CFG+=("$cfg")
    ARM_OUT+=("$out_dir")
    ARM_LOG+=("$log_dir/$arm.log")
done

echo "=== Phase 21 package A: filter_bubble + exploration_recovery ==="
echo "  binary          : $SIMULATE_BIN"
echo "  configs-dir     : $CONFIGS_DIR"
echo "  out             : $RESULTS_ROOT"
echo "  seed            : $SEED"
echo "  max-concurrency : $MAX_CONCURRENCY"
echo "  arms            : ${#ARM_TAG[@]}"
echo "  start           : $(date)"
echo

# run_arm <index> — one arm to completion; logs to its file; returns simulate's exit code.
run_arm() {
    local i="$1"
    local tag="${ARM_TAG[$i]}" cfg="${ARM_CFG[$i]}" out="${ARM_OUT[$i]}" log="${ARM_LOG[$i]}"
    : > "$log"
    echo ">>> [$tag] config=$cfg seed=$SEED -> $out"
    local rc=0
    "$SIMULATE_BIN" --config "$cfg" --seed "$SEED" --out "$out" >> "$log" 2>&1 || rc=$?
    if [[ $rc -eq 0 ]]; then echo "<<< [$tag] done"; else echo "<<< [$tag] FAILED (exit $rc) -- see $log"; fi
    return $rc
}

# Concurrency-capped dispatch.
declare -a ARM_RC
PIDS=()
PID_IDX=()
reap() {
    local pid="$1"
    local idx="${PID_IDX[$pid]}"
    local rc=0
    wait "$pid" || rc=$?
    ARM_RC[$idx]=$rc
}
for i in "${!ARM_TAG[@]}"; do
    while (( ${#PIDS[@]} >= MAX_CONCURRENCY )); do
        # Poll for a finished arm, then prune finished pids (sleep-poll, not `wait -n`, so this runs
        # on macOS's stock bash 3.2 too).
        sleep 10
        NEXT=()
        for pid in "${PIDS[@]}"; do
            if kill -0 "$pid" 2>/dev/null; then NEXT+=("$pid"); else reap "$pid"; fi
        done
        PIDS=("${NEXT[@]}")
    done
    run_arm "$i" &
    pid=$!
    PIDS+=("$pid")
    PID_IDX[$pid]=$i
done
for pid in "${PIDS[@]}"; do reap "$pid"; done

echo
echo "=== Phase 21 package A: ALL ARMS DONE ($(date)) ==="
overall_rc=0
for i in "${!ARM_TAG[@]}"; do
    rc="${ARM_RC[$i]:-1}"
    if [[ $rc -eq 0 ]]; then status="OK"; else status="FAILED (exit $rc)"; overall_rc=1; fi
    printf '  %-34s %-52s %s\n' "${ARM_TAG[$i]}" "${ARM_OUT[$i]}" "$status"
done
echo
echo "Each arm dir holds ecosystem_metrics.csv + longterm_metrics.csv (trailing mean_preference_entropy)"
echo "+ summary.json (ecosystem/long_term blocks). Pull numbers with python one-liners; see"
echo "docs/scenarios/<scenario>-NOTES.md for the pre-registered blocks + verdict recommendations."
exit $overall_rc
