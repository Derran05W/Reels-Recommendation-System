#!/usr/bin/env bash
#
# run_phase20_experiment.sh — Phase 20 Tier-4 acceptance experiment 1: the POLICY-INFLUENCE
# experiment (V2 TDD §4.15-4.17 + Tier-4 acceptance item 1, plan Phase 20 task 5, contracts §6).
#
# FOUR arms, all from the SAME base config/world/seed:
#
#   engagement-on   hnsw_ranker  configs/realism-medium-retention.json + engagement ranking-weight
#                                preset (patch below)                     [gates: evolution+retention ON]
#   engagement-off  hnsw_ranker  configs/realism-medium-retention.json + engagement ranking-weight
#                                preset (patch below)                [gates: evolution OFF, retention ON]
#   proxy-on        hnsw_ranker  configs/realism-medium-retention.json + satisfaction-proxy ranking-
#                                weight preset (patch below)               [gates: evolution+retention ON]
#   proxy-off       hnsw_ranker  configs/realism-medium-retention.json + satisfaction-proxy ranking-
#                                weight preset (patch below)         [gates: evolution OFF, retention ON]
#
# The base config is `configs/realism-medium-retention.json` (this package's own new config: Phase
# 19's event-mode 10k-user/100k-reel/32-topic/64-dim medium dataset, `configs/realism-medium-events-
# drift.json` minus its `drift` block entirely — this experiment isolates ENDOGENOUS preference
# evolution from P10's exogenous scheduled drift, contracts §6 — plus `serving.prefetch_depth=10`
# [P19 finding: near-flat adaptation at markedly lower ranking cost than deeper batches],
# `simulation.horizon_seconds=777600` [9 simulated days, covering `long_term.retention_7d` with
# margin], and the three new P20 gate blocks (`realism.preference_evolution=true`,
# `retention.enabled=true`, `evolution.eta_evo=0.02`, `retention.churn_delay_threshold_seconds=
# 604800`/`retention.hazard_floor=0.02` — the latter three at their config.hpp defaults, spelled out
# explicitly). See that config file / this package's final report for the full key-by-key
# cross-check against include/rr/infrastructure/config.hpp's P20 struct fields.
#
# POLICY PATCHES (ranking-weight presets only — every other field is IDENTICAL to the base config,
# a clean one-variable-per-pair-of-arms design): extracted by diffing
# `results/published/phase15/engagement/config.json`'s and `.../proxy/config.json`'s resolved
# `ranking` blocks against `results/published/phase15/semantic/config.json`'s resolved `ranking`
# block (semantic is Phase 15's un-weighted control arm on `configs/realism-medium.json`; VERIFIED
# by direct computation in this package's own worktree that semantic's resolved 8 explicit ranking
# keys are IDENTICAL in value to this config's own base `ranking` block, so diffing against semantic
# is equivalent to diffing against this experiment's own base — the remaining ~15 RankingConfig
# fields neither config sets explicitly, so both resolve those through the SAME config.hpp defaults
# too). Every key with a differing value between preset and base is listed below; every key NOT
# listed is identical between the preset arm and this experiment's base config by construction (the
# patch only ever touches the listed keys — see the per-arm python3 heredoc below, which does
# `cfg["ranking"].update(patch)`, never a wholesale block replacement).
#
#   ENGAGEMENT preset (12 differing keys, from results/published/phase15/engagement/config.json):
#     duration_match_weight=0.0, emotional_intensity_weight=0.08, emotional_match_weight=0.1,
#     freshness_weight=0.0, impression_penalty_weight=0.0, music_match_weight=0.12,
#     popularity_weight=0.05, quality_weight=0.0, repetition_penalty=0.0, similarity_weight=0.6,
#     trending_weight=0.0, visual_match_weight=0.04
#
#   PROXY (satisfaction-proxy) preset (6 differing keys, from
#   results/published/phase15/proxy/config.json):
#     clickbait_weight=-0.15, emotional_intensity_weight=-0.05, information_density_weight=0.06,
#     language_match_weight=0.05, production_quality_weight=0.08, usefulness_weight=0.12
#
# `-off` TWINS (contracts §5 counterfactual world, INTEGRATION-CORRECTED): the SAME policy ranking
# patch as the arm's `-on` sibling, PLUS `realism.preference_evolution=false` with retention KEPT
# ON. Package C's original design set both gates off, but package B flagged the flaw: the
# distortion measure reads the counterfactual's `hidden_preference_final.csv`, which is written
# only when `long_term.configured` — a both-gates-off world emits no export. Evolution-off/
# retention-on twins keep the export AND are the cleaner controlled counterfactual: the paired
# worlds share the retention mechanics and differ ONLY in evolution, so matched-user distortion
# isolates the evolution effect. (In these twins hidden preferences never move — nothing else
# moves them with drift removed — so their export doubles as a verification that cross-run
# distortion equals the -on arm's shift-from-initial; both numbers are reported.) Retention in the
# twins runs on STATIC trust (= platformTrust; package A only writes trust under the evolution
# gate), giving the trust columns an evolution-isolated reference. Every metric group incl.
# `long_term` is populated for `-off` arms; `phase20_comparison.py` reads all arms defensively.
#
# All four arms use algorithm `hnsw_ranker` (matching Phase 15's engagement/proxy arms — ranking
# weights are inert under any non-weighted algorithm) and SEED 42 (deterministic, D8 — this
# experiment's whole point is a controlled comparison on IDENTICAL worlds, so every arm must share
# the same seed; the only free variables are the ranking-weight preset and the evolution/retention
# gate state).
#
# Configs are generated via the SAME tiny stdlib-only JSON-patch pattern established by
# scripts/run_phase17_experiment.sh and reused by scripts/run_phase19_experiment.sh (a python3
# heredoc reading the base config, applying `dict.update()` on the `ranking` sub-object plus, for
# `-off` arms, two scalar overrides, and writing the result) — never hand-duplicates the base
# config's content, so every arm is guaranteed identical to the others in every field except the
# ones this header documents. Generated configs are left on disk next to the results (not a
# throwaway mktemp) under `$RESULTS_ROOT/generated-configs/`, so `cat`/`diff` on them is a trivial
# way to confirm exactly what ran (and to re-verify the "every OTHER field is identical" claim
# above with a plain `diff`).
#
# Bash on purpose, NOT zsh (see scripts/run_phase11_load.sh / run_phase15/16/17/19_experiment.sh
# precedent): the word-splitting this script relies on is POSIX sh behaviour zsh does not do by
# default. For a long batch, run it detached:
#   nohup bash scripts/run_phase20_experiment.sh > results/phase20/nohup.log 2>&1 &
#
# CONCURRENCY: 4-concurrent by default (one wave; bounds contention to at most 4 simulate processes
# at once). --sequential runs all four arms one at a time instead. Every number this experiment's
# tooling reports except wall-clock/timing is deterministic (rng/clock-free, D8/D9) and UNAFFECTED
# by which mode you choose (same caveat as every prior phase's concurrent scripts) — EXCEPT that the
# retention/churn model draws on the "scheduling" stream and preference evolution draws zero rng
# (contracts §3: "the reserved preference-evolution stream stays reserved-unused"), so this
# determinism guarantee is unchanged from every earlier phase.
#
# SCALE AND EXPECTED WALL TIME (documented honestly, not guessed): this worktree cannot run a real
# P20 experiment (packages A/B are no-op stubs here — see the header of scripts/phase20_comparison.py
# and this package's final report), so there is no direct P20 timing measurement to cite. The closest
# available REAL, PUBLISHED reference in this repo is Phase 19's own `batch10` arm
# (results/published/phase19/batch10/): the SAME 10k-user/100k-reel/32-topic/64-dim dataset, the SAME
# event scheduler, the SAME `serving.prefetch_depth=10`/`refill_threshold=0` this config also uses,
# Release build (Apple M5, AppleClang), run 4-concurrent alongside its sibling batch-depth arms — its
# `summary.json` `timing.total_wall_seconds` is **2118.338s (~35.3 minutes)** at
# `horizon_seconds=302400` (3.5 simulated days; that arm's own drift schedule was still active, unlike
# this config, but drift adds negligible fixed overhead vs. the per-impression/session simulation
# loop it runs inside of). This experiment's horizon is 777600s (9 simulated days) = 2.5714x longer;
# assuming wall time scales roughly linearly with simulated horizon (impressions/sessions processed
# scale with horizon at roughly constant per-user rates — the same assumption
# scripts/run_phase19_experiment.sh's own header documents and the reasoning this package inherits),
# that projects to **~5,447s (~91 minutes) per arm**, Release, 4-concurrent. HONEST CAVEAT this
# estimate does NOT and CANNOT capture: `PreferenceEvolution::applyImpression` (once per impression)
# and `RetentionModel::onSessionEnd`/`nextReturnDelay` (once per session-end) are BOTH still no-op
# stubs in every worktree this package can see — the batch10 reference run above paid zero cost for
# either, so once packages A/B land their real per-impression/per-session-end computation, the true
# P20 wall time will likely run somewhat LONGER than this projection (by an amount neither this
# worktree nor the P19 reference can measure). Treat ~90-120 minutes/arm (4-concurrent, Release) as
# the honest planning range, and have the integrator record the real number the first time this
# script runs to completion against a real package A/B build.
#
# Overridable via env or flags (a flag always wins over its env var): SIMULATE_BIN, CONFIGS_DIR,
# RESULTS_ROOT, SEED, ALGORITHM. Build the Release binary first, e.g. from the reel-rank repo root:
#   cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release -DREELRANK_VDB_DIR=/path/to/vector-db
#   cmake --build build-release --target simulate -j
#   SIMULATE_BIN=./build-release/apps/simulate scripts/run_phase20_experiment.sh
#
# SMOKE-SCALE / PLUMBING-ONLY RUNS: there is no simulation-shrinking --smoke flag on this script (the
# simulate binary's own --smoke flag is a fixed 50-user/500-reel ROUND-ROBIN dataset that does not
# set scheduler=event_queue or any P20 gate, so it cannot exercise this experiment's config at all).
# To smoke-test THIS SCRIPT'S PLUMBING ONLY (config generation + the 4-arm matrix), two independent
# knobs are available and composable:
#   - `--configs-dir DIR` pointed at a directory holding your own tiny `realism-medium-retention.json`
#     (same basename, smaller simulation.users/reels/dimensions, a short horizon_seconds, the same
#     gate stack on) exercises the real JSON-patch logic against a fast-loading dataset.
#   - `--bin PATH` pointed at ANY executable (e.g. `/usr/bin/true`) lets you verify the FULL script
#     end-to-end (arg parsing, config generation, concurrency wave dispatch, the results table) with
#     ZERO real simulation work done and the real `simulate` binary never invoked at all — this is
#     the "generate configs only, do not execute simulate" smoke test this package's exit criteria
#     asks for. Combine both: `--configs-dir <tiny-dir> --bin /usr/bin/true --out <scratch-dir>`.
#
# Compare / plot / test afterward:
#   python3 scripts/phase20_comparison.py --engagement-on <dir> --engagement-off <dir> \
#       --proxy-on <dir> --proxy-off <dir> --out results/published/phase20
#   uv run --project scripts scripts/plot_results.py --phase20 <engagement-on dir> <proxy-on dir> \
#       --out results/published/phase20/figures
#       (see scripts/plot_results.py's module docstring for the exact Phase 20 plot functions and
#       their input contracts)
#   ctest --test-dir build --output-on-failure -R PolicyDivergence   # package B's in-process test

set -euo pipefail

SIMULATE_BIN="${SIMULATE_BIN:-build-release/apps/simulate}"
CONFIGS_DIR="${CONFIGS_DIR:-configs}"
RESULTS_ROOT="${RESULTS_ROOT:-results/phase20}"
SEED="${SEED:-42}"
ALGORITHM="${ALGORITHM:-hnsw_ranker}"
SEQUENTIAL=0

usage() {
    cat <<'EOF'
usage: run_phase20_experiment.sh [--bin PATH] [--configs-dir DIR] [--out DIR] [--seed N]
                                  [--algorithm NAME] [--sequential]

  --bin PATH        simulate binary (default: build-release/apps/simulate; env SIMULATE_BIN).
                     Point this at any executable (e.g. /usr/bin/true) to smoke-test config
                     generation + script plumbing without running a real simulation.
  --configs-dir DIR directory holding realism-medium-retention.json (default: configs;
                     env CONFIGS_DIR)
  --out DIR         results root; each arm writes under DIR/<arm-name>/, generated per-arm configs
                     under DIR/generated-configs/ (default: results/phase20; env RESULTS_ROOT)
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
    echo "       (For a plumbing-only smoke test with no real simulation, pass --bin /usr/bin/true.)" >&2
    exit 1
fi

if ! command -v python3 >/dev/null 2>&1; then
    echo "ERROR: python3 not found on PATH -- required for the per-arm JSON patch step." >&2
    exit 1
fi

BASE_CFG="$CONFIGS_DIR/realism-medium-retention.json"
if [[ ! -f "$BASE_CFG" ]]; then
    echo "ERROR: config not found: $BASE_CFG" >&2
    exit 1
fi

mkdir -p "$RESULTS_ROOT/logs" "$RESULTS_ROOT/generated-configs"

# Ranking-weight patches (see header for provenance): dict.update()'d onto the base config's
# "ranking" block, never a wholesale replacement, so every key NOT listed stays whatever the base
# config resolves to (identical across every arm by construction).
ENGAGEMENT_PATCH_JSON='{"duration_match_weight": 0.0, "emotional_intensity_weight": 0.08, "emotional_match_weight": 0.1, "freshness_weight": 0.0, "impression_penalty_weight": 0.0, "music_match_weight": 0.12, "popularity_weight": 0.05, "quality_weight": 0.0, "repetition_penalty": 0.0, "similarity_weight": 0.6, "trending_weight": 0.0, "visual_match_weight": 0.04}'
PROXY_PATCH_JSON='{"clickbait_weight": -0.15, "emotional_intensity_weight": -0.05, "information_density_weight": 0.06, "language_match_weight": 0.05, "production_quality_weight": 0.08, "usefulness_weight": 0.12}'

ARM_NAMES=(engagement-on engagement-off proxy-on proxy-off)
declare -A ARM_POLICY_PATCH=(
    [engagement-on]="$ENGAGEMENT_PATCH_JSON"
    [engagement-off]="$ENGAGEMENT_PATCH_JSON"
    [proxy-on]="$PROXY_PATCH_JSON"
    [proxy-off]="$PROXY_PATCH_JSON"
)
# 1 => this arm forces realism.preference_evolution=false AND retention.enabled=false (the gate-off
# counterfactual/fixed-schedule-baseline twin); 0 => leave the base config's gate state (both on) as
# generated.
declare -A ARM_GATES_OFF=(
    [engagement-on]=0
    [engagement-off]=1
    [proxy-on]=0
    [proxy-off]=1
)

declare -A ARM_CFG
for name in "${ARM_NAMES[@]}"; do
    ARM_CFG[$name]="$RESULTS_ROOT/generated-configs/realism-medium-retention-$name.json"
    python3 - "$BASE_CFG" "${ARM_CFG[$name]}" "${ARM_POLICY_PATCH[$name]}" "${ARM_GATES_OFF[$name]}" <<'PYEOF'
import json
import sys

with open(sys.argv[1]) as f:
    cfg = json.load(f)
patch = json.loads(sys.argv[3])
gates_off = sys.argv[4] == "1"

# Patch exactly the differing ranking-weight keys -- never a wholesale block replacement, so every
# OTHER ranking key stays whatever the base config resolves to (D6 default semantics).
cfg["ranking"].update(patch)

if gates_off:
    # The counterfactual twin (contracts §5): preference EVOLUTION off, retention KEPT ON.
    # Integration correction to this package's original both-gates-off design (flagged by package
    # B): the distortion measure reads the counterfactual's hidden_preference_final.csv, which is
    # written only when long_term.configured — a both-gates-off world has no export. Keeping
    # retention on (evolution off) preserves the export AND makes the twin the cleaner controlled
    # counterfactual: the two worlds share the retention mechanics and differ ONLY in evolution,
    # so per-user distortion isolates the evolution effect. (Retention-without-evolution also uses
    # static trust = platformTrust — package A only writes trust under the evolution gate — giving
    # the trust columns an evolution-isolated reference.)
    cfg["realism"]["preference_evolution"] = False

with open(sys.argv[2], "w") as f:
    json.dump(cfg, f, indent=2)
    f.write("\n")
PYEOF
done

echo "=== Phase 20 experiment: policy-influence (engagement vs. satisfaction-proxy) ==="
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

ARM_CONFIGS=()
for name in "${ARM_NAMES[@]}"; do
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
echo "=== Phase 20 experiment: ALL ARMS DONE ==="
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
    printf '  %-16s %-60s %s\n' "$name" "${resolved_dir:-$log}" "$status"
done
echo
echo "Note: engagement-off / proxy-off arms are evolution-OFF / retention-ON counterfactual twins"
echo "(integration-corrected design, see this script's header): they DO carry a long_term block and"
echo "hidden_preference_final.csv (retention keeps long_term.configured true), with preferences"
echo "static -- their exports are the matched-user baseline for the distortion measure."
echo
echo "Compare the completed arms, e.g.:"
echo "  python3 scripts/phase20_comparison.py \\"
echo "      --engagement-on <dir> --engagement-off <dir> \\"
echo "      --proxy-on <dir> --proxy-off <dir> \\"
echo "      --out results/published/phase20"
echo
echo "Plot the preference-divergence / retention / trust figures, e.g.:"
echo "  uv run --project scripts scripts/plot_results.py \\"
echo "      --phase20 <engagement-on dir> <proxy-on dir> --out results/published/phase20/figures"
echo "  (see plot_results.py's docstring for the Phase 20 plot functions and their exact inputs)"

exit $overall_rc
