#!/usr/bin/env bash
#
# run_phase21_b.sh — Phase 21 Package B experiment runner (contracts §4/§5, plan Phase 21 task 2).
#
# Runs the two Package-B failure-mode scenarios, each as multiple arms on ONE identical world
# (same seed, same archetype catalog — only the ranking-weight block differs per arm):
#
#   ragebait_amplification (Tier 4 acceptance 2): a ragebait-SUSCEPTIBLE world (the scenario config
#     configs/scenarios/ragebait_amplification.json raises the ragebait ARCHETYPE mixture weight
#     0.10 -> 0.20, renormalizing the other seven; the controversyTolerance susceptibility trait is
#     NOT overridable via realism.cohort_mix, so the world lever is the content mixture). Two arms:
#       engagement  = base ranking + P20 ENGAGEMENT preset patch
#       proxy       = base ranking + P20 SATISFACTION-PROXY preset patch
#
#   satisfaction_vs_retention (the frontier): DEFAULT archetype catalog. Five arms, ranking weights
#     linearly interpolated per-key over the UNION of the engagement and proxy patch keys at
#     lambda in {0, 0.25, 0.5, 0.75, 1}:
#       lambda00 (=engagement) lambda25 lambda50 lambda75 lambda100 (=proxy)
#     BLEND RULE per union key: value(lambda) = (1-lambda)*E + lambda*P, where E = engagement-patch
#     value if the key is in the engagement patch ELSE the BASE config's resolved ranking default,
#     and P = proxy-patch value if the key is in the proxy patch ELSE the BASE default. So lambda=0
#     reproduces the pure engagement arm and lambda=1 the pure proxy arm.
#
# The two ranking presets are VERBATIM the P20 policy patches (see scripts/run_phase20_experiment.sh
# header for provenance: extracted by diffing results/published/phase15/{engagement,proxy}
# resolved `ranking` blocks). They are dict.update()'d onto the scenario config's `ranking` block,
# never a wholesale replacement, so every key NOT listed stays whatever the scenario config
# resolves to (identical across every arm of a scenario by construction). The BASE ranking defaults
# used to fill interpolation gaps are the resolved values of configs/realism-medium-retention.json
# (which every scenario config inherits): similarity_weight=0.50, quality_weight=0.10,
# freshness_weight=0.08, popularity_weight=0.07, trending_weight=0.08, repetition_penalty=0.15,
# duration_match_weight=0.05, impression_penalty_weight=0.05, and every V2 ranking-feature weight
# 0.0 (config.hpp defaults).
#
# All arms: algorithm hnsw_ranker (ranking weights are inert under any non-weighted algorithm),
# SEED 42 (D8, controlled comparison on IDENTICAL worlds), evaluation.ecosystem_metrics=true (set
# in the scenario configs), event mode. Generated per-arm configs are left on disk under
# $RESULTS_ROOT/<scenario>/generated-configs/ so a plain `diff` re-verifies exactly what ran.
#
# CONCURRENCY: at most MAXPAR (default 2) simulate processes at ANY time, ACROSS both scenarios
# (contracts §4: "at most 2 simulate processes per package"). Arms run in waves of MAXPAR. Bash on
# purpose (not zsh): the word-splitting is POSIX sh behaviour zsh does not do by default. bash 3.2
# safe (no `wait -n`). For a long batch, run detached:
#   nohup bash scripts/run_phase21_b.sh > results/phase21/nohup.log 2>&1 &
#
# Overridable via env or flags (flag wins): SIMULATE_BIN, CONFIGS_DIR, RESULTS_ROOT, SEED,
# ALGORITHM, MAXPAR, SCENARIOS. Build the Release binary first, e.g.:
#   cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release -DREELRANK_VDB_DIR=/path/to/vector-db
#   cmake --build build-release --target simulate -j
#   SIMULATE_BIN=./build-release/apps/simulate bash scripts/run_phase21_b.sh
# Point --bin at /usr/bin/true to smoke-test config generation + the arm matrix with no real sim.

set -euo pipefail

SIMULATE_BIN="${SIMULATE_BIN:-build-release/apps/simulate}"
CONFIGS_DIR="${CONFIGS_DIR:-configs/scenarios}"
RESULTS_ROOT="${RESULTS_ROOT:-results/phase21}"
SEED="${SEED:-42}"
ALGORITHM="${ALGORITHM:-hnsw_ranker}"
MAXPAR="${MAXPAR:-2}"
SCENARIOS="${SCENARIOS:-ragebait_amplification satisfaction_vs_retention}"

usage() {
    cat <<'EOF'
usage: run_phase21_b.sh [--bin PATH] [--configs-dir DIR] [--out DIR] [--seed N]
                        [--algorithm NAME] [--max-par N] [--scenarios "a b"]

  --bin PATH         simulate binary (default: build-release/apps/simulate; env SIMULATE_BIN).
                     Point at /usr/bin/true to smoke-test config generation + the arm matrix.
  --configs-dir DIR  directory holding the scenario configs (default: configs/scenarios).
  --out DIR          results root; each arm writes DIR/<scenario>/<arm>/, generated per-arm configs
                     under DIR/<scenario>/generated-configs/ (default: results/phase21).
  --seed N           master seed, applied to every arm (default: 42).
  --algorithm NAME   recommendation algorithm (default: hnsw_ranker).
  --max-par N        max concurrent simulate processes ACROSS scenarios (default: 2).
  --scenarios "a b"  which scenarios to run (default: both Package-B scenarios).
  -h, --help         this message
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --bin) SIMULATE_BIN="$2"; shift 2 ;;
        --configs-dir) CONFIGS_DIR="$2"; shift 2 ;;
        --out) RESULTS_ROOT="$2"; shift 2 ;;
        --seed) SEED="$2"; shift 2 ;;
        --algorithm) ALGORITHM="$2"; shift 2 ;;
        --max-par) MAXPAR="$2"; shift 2 ;;
        --scenarios) SCENARIOS="$2"; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "error: unknown argument: $1" >&2; usage >&2; exit 2 ;;
    esac
done

if [[ ! -x "$SIMULATE_BIN" ]]; then
    echo "ERROR: simulate binary not found or not executable at: $SIMULATE_BIN" >&2
    echo "       Build it first (see this script's header) or pass --bin / set SIMULATE_BIN." >&2
    exit 1
fi
if ! command -v python3 >/dev/null 2>&1; then
    echo "ERROR: python3 not found on PATH -- required for the per-arm JSON patch step." >&2
    exit 1
fi

# The two P20 ranking-weight presets, VERBATIM (scripts/run_phase20_experiment.sh header).
ENGAGEMENT_PATCH_JSON='{"duration_match_weight": 0.0, "emotional_intensity_weight": 0.08, "emotional_match_weight": 0.1, "freshness_weight": 0.0, "impression_penalty_weight": 0.0, "music_match_weight": 0.12, "popularity_weight": 0.05, "quality_weight": 0.0, "repetition_penalty": 0.0, "similarity_weight": 0.6, "trending_weight": 0.0, "visual_match_weight": 0.04}'
PROXY_PATCH_JSON='{"clickbait_weight": -0.15, "emotional_intensity_weight": -0.05, "information_density_weight": 0.06, "language_match_weight": 0.05, "production_quality_weight": 0.08, "usefulness_weight": 0.12}'

# emit_patch <kind> <lambda>  -> prints the ranking patch JSON for one arm on stdout.
#   kind=engagement  -> the engagement preset;   kind=proxy -> the proxy preset;
#   kind=lambda      -> the interpolated preset at <lambda> (blend rule in the header).
emit_patch() {
    local kind="$1" lam="${2:-0}"
    ENGAGEMENT_PATCH_JSON="$ENGAGEMENT_PATCH_JSON" PROXY_PATCH_JSON="$PROXY_PATCH_JSON" \
        python3 - "$kind" "$lam" <<'PYEOF'
import json, os, sys
kind, lam = sys.argv[1], float(sys.argv[2])
ENG = json.loads(os.environ["ENGAGEMENT_PATCH_JSON"])
PROXY = json.loads(os.environ["PROXY_PATCH_JSON"])
# Resolved BASE ranking defaults (configs/realism-medium-retention.json + config.hpp defaults) used
# to fill a key missing from one patch.
BASE = {"similarity_weight": 0.50, "quality_weight": 0.10, "freshness_weight": 0.08,
        "popularity_weight": 0.07, "trending_weight": 0.08, "repetition_penalty": 0.15,
        "duration_match_weight": 0.05, "impression_penalty_weight": 0.05,
        "visual_match_weight": 0.0, "music_match_weight": 0.0, "emotional_match_weight": 0.0,
        "clickbait_weight": 0.0, "emotional_intensity_weight": 0.0, "usefulness_weight": 0.0,
        "production_quality_weight": 0.0, "information_density_weight": 0.0,
        "language_match_weight": 0.0}
if kind == "engagement":
    print(json.dumps(ENG)); sys.exit(0)
if kind == "proxy":
    print(json.dumps(PROXY)); sys.exit(0)
# interpolate over the UNION of both patch keys
union = sorted(set(ENG) | set(PROXY))
def endpoint(patch, k):
    return patch[k] if k in patch else BASE[k]
patch = {k: round((1.0 - lam) * endpoint(ENG, k) + lam * endpoint(PROXY, k), 10) for k in union}
print(json.dumps(patch))
PYEOF
}

# The arm matrix: parallel arrays keyed by a global index.  ARM_SCN / ARM_NAME / ARM_KIND /
# ARM_LAMBDA describe every arm across every requested scenario.
ARM_SCN=(); ARM_NAME=(); ARM_KIND=(); ARM_LAMBDA=()
add_arm() { ARM_SCN+=("$1"); ARM_NAME+=("$2"); ARM_KIND+=("$3"); ARM_LAMBDA+=("$4"); }

for scn in $SCENARIOS; do
    case "$scn" in
        ragebait_amplification)
            add_arm "$scn" engagement engagement 0
            add_arm "$scn" proxy      proxy      0
            ;;
        satisfaction_vs_retention)
            add_arm "$scn" lambda00  lambda 0.0
            add_arm "$scn" lambda25  lambda 0.25
            add_arm "$scn" lambda50  lambda 0.5
            add_arm "$scn" lambda75  lambda 0.75
            add_arm "$scn" lambda100 lambda 1.0
            ;;
        *) echo "error: unknown scenario: $scn" >&2; exit 2 ;;
    esac
done

NARMS=${#ARM_SCN[@]}
if [[ $NARMS -eq 0 ]]; then
    echo "error: no arms selected (SCENARIOS='$SCENARIOS')" >&2; exit 2
fi

# Generate every arm's config up front (fast, fail-fast on a bad base config).
declare -a ARM_CFG ARM_OUT ARM_LOG
for i in $(seq 0 $((NARMS - 1))); do
    scn="${ARM_SCN[$i]}"; name="${ARM_NAME[$i]}"
    base_cfg="$CONFIGS_DIR/$scn.json"
    if [[ ! -f "$base_cfg" ]]; then
        echo "ERROR: scenario config not found: $base_cfg" >&2; exit 1
    fi
    gen_dir="$RESULTS_ROOT/$scn/generated-configs"
    mkdir -p "$gen_dir" "$RESULTS_ROOT/$scn/logs"
    out_cfg="$gen_dir/$name.json"
    patch="$(emit_patch "${ARM_KIND[$i]}" "${ARM_LAMBDA[$i]}")"
    python3 - "$base_cfg" "$out_cfg" "$patch" <<'PYEOF'
import json, sys
with open(sys.argv[1]) as f:
    cfg = json.load(f)
patch = json.loads(sys.argv[3])
cfg["ranking"].update(patch)  # per-key update, never a wholesale block replacement
with open(sys.argv[2], "w") as f:
    json.dump(cfg, f, indent=2); f.write("\n")
PYEOF
    ARM_CFG[$i]="$out_cfg"
    ARM_OUT[$i]="$RESULTS_ROOT/$scn/$name"
    ARM_LOG[$i]="$RESULTS_ROOT/$scn/logs/$name.log"
done

echo "=== Phase 21 Package B experiment ==="
echo "  binary      : $SIMULATE_BIN"
echo "  scenarios   : $SCENARIOS"
echo "  arms        : $NARMS (max $MAXPAR concurrent)"
echo "  algorithm   : $ALGORITHM   seed: $SEED"
echo "  out         : $RESULTS_ROOT"
echo "  start       : $(date)"
echo

# run_arm <index> : run one arm to completion, log to its file, echo status. Never fatal.
run_arm() {
    local i="$1"
    local name="${ARM_SCN[$i]}/${ARM_NAME[$i]}"
    mkdir -p "${ARM_OUT[$i]}"; : > "${ARM_LOG[$i]}"
    echo ">>> [$name] config=${ARM_CFG[$i]} seed=$SEED -> ${ARM_OUT[$i]}"
    local rc=0
    "$SIMULATE_BIN" --config "${ARM_CFG[$i]}" --algorithm "$ALGORITHM" --seed "$SEED" \
        --out "${ARM_OUT[$i]}" >> "${ARM_LOG[$i]}" 2>&1 || rc=$?
    if [[ $rc -eq 0 ]]; then echo "<<< [$name] done"; else echo "<<< [$name] FAILED (exit $rc) -- see ${ARM_LOG[$i]}"; fi
    return $rc
}

# Wave scheduler: launch up to MAXPAR arms, wait for the whole wave, repeat (bash 3.2 safe).
declare -a ARM_RC
for ((base = 0; base < NARMS; base += MAXPAR)); do
    pids=(); idxs=()
    for ((k = 0; k < MAXPAR && base + k < NARMS; k++)); do
        i=$((base + k))
        run_arm "$i" &
        pids+=($!); idxs+=("$i")
    done
    for j in "${!pids[@]}"; do
        rc=0; wait "${pids[$j]}" || rc=$?
        ARM_RC[${idxs[$j]}]=$rc
    done
done

echo
echo "=== ALL ARMS DONE: $(date) ==="
overall_rc=0
for i in $(seq 0 $((NARMS - 1))); do
    resolved_dir="$(awk '/^ *out /{print $2; exit}' "${ARM_LOG[$i]}" 2>/dev/null || true)"
    if [[ "${ARM_RC[$i]:-1}" -eq 0 ]]; then status="OK"; else status="FAILED"; overall_rc=1; fi
    printf '  %-45s %s\n' "${ARM_SCN[$i]}/${ARM_NAME[$i]}" "$status  ${resolved_dir:-${ARM_LOG[$i]}}"
done
exit $overall_rc
