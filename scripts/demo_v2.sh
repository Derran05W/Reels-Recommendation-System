#!/usr/bin/env bash
#
# ReelRank V2 demo — "same users, same content, different objective."
#
# Runs the SAME tiny world (configs/realism-small.json, seed 42) through the SAME algorithm
# (hnsw_ranker) TWICE, differing ONLY in the ranking-weight preset:
#
#   engagement  -- the engagement-optimized preset (Phase 15/16/20's `engagement` arm patch)
#   proxy       -- the satisfaction-proxy preset   (Phase 15/16/20's `proxy` arm patch)
#
# and prints a compact side-by-side of what changes when the objective changes but nothing else
# does: reward/impression, completion rate, like rate, mean hidden satisfaction, mean regret, and
# (since realism.session_dynamics is on) U_s / session-health outcomes.
#
# Gate stack: content_v2 + latent_reactions + session_dynamics all ON (round-robin, not event
# mode -- event mode is optional per the Phase 24 brief and round-robin is faster for a demo this
# small; every other phase's engagement-vs-proxy comparisons in this repo, e.g. Phase 15/16, ALSO
# ran round-robin). Base config is configs/realism-small.json (already gates content_v2/
# latent_reactions on; 1,000 users x 10,000 reels x 50 interactions/user -- the same simulation
# scale as V1's configs/small.json). It does NOT yet turn on session_dynamics, so this script adds
# `realism.session_dynamics = true` via the SAME tiny stdlib-only JSON-patch heredoc pattern
# scripts/run_phase17/19/20_experiment.sh use, plus the ranking-weight preset patch (Phase 15's
# engagement/proxy config.json's resolved `ranking` blocks, verbatim, as documented in
# scripts/run_phase20_experiment.sh's header) -- generated into a scratch temp dir, never a new
# committed config (configs/realism-small.json already exists and is the natural base; see this
# package's final report for why no configs/demo-v2-small.json was added).
#
# Deterministic (fixed seed 42, D8/D9). Writes only to a scratch temp dir (never
# results/published/). Budget: under 2 minutes wall-clock for BOTH arms; elapsed is measured and
# printed. Override the build location with BUILD_DIR=/path/to/build.
#
# Usage:  bash scripts/demo_v2.sh

set -euo pipefail

# --- locate the repo root (this script lives in <repo>/scripts) ------------------------------
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
cd "${REPO_ROOT}"

# --- locate a build: BUILD_DIR override, else prefer build-release, else build ---------------
find_build() {
    if [[ -n "${BUILD_DIR:-}" ]]; then
        echo "${BUILD_DIR}"
        return 0
    fi
    for candidate in build-release build; do
        if [[ -x "${REPO_ROOT}/${candidate}/apps/simulate" ]]; then
            echo "${REPO_ROOT}/${candidate}"
            return 0
        fi
    done
    return 1
}

if ! BUILD="$(find_build)"; then
    cat >&2 <<'EOF'
error: no ReelRank build found (looked for build-release/apps/simulate then build/apps/simulate).

Build the Release configuration first:

    cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
    cmake --build build-release

...then re-run this script (or set BUILD_DIR=/path/to/build).
EOF
    exit 1
fi

SIMULATE="${BUILD}/apps/simulate"
if [[ ! -x "${SIMULATE}" ]]; then
    echo "error: expected binary not found or not executable: ${SIMULATE}" >&2
    echo "       rebuild with: cmake --build ${BUILD}" >&2
    exit 1
fi
if ! command -v python3 >/dev/null 2>&1; then
    echo "ERROR: python3 not found on PATH -- required for the JSON-patch config step." >&2
    exit 1
fi

BASE_CFG="${REPO_ROOT}/configs/realism-small.json"
if [[ ! -f "${BASE_CFG}" ]]; then
    echo "error: base config not found: ${BASE_CFG}" >&2
    exit 1
fi

SEED=42
OUT_DIR="$(mktemp -d "${TMPDIR:-/tmp}/reelrank-demo-v2.XXXXXX")"
trap 'rm -rf "${OUT_DIR}"' EXIT
mkdir -p "${OUT_DIR}/configs"

echo "==================================================================================="
echo " ReelRank V2 demo -- same users, same content, different objective"
echo "   build:   ${BUILD}"
echo "   config:  configs/realism-small.json + session_dynamics on (1,000 users x 10,000 reels,"
echo "            50 interactions/user, seed ${SEED}), algorithm hnsw_ranker, round-robin"
echo "   scratch: ${OUT_DIR}"
echo "   budget:  < 2 minutes wall-clock for both arms (measured below)"
echo "==================================================================================="
echo

# --- ranking-weight presets (verbatim from scripts/run_phase20_experiment.sh's header, itself ---
# extracted by diffing results/published/phase15/{engagement,proxy}/config.json's resolved
# `ranking` blocks against Phase 15's semantic control arm) -----------------------------------
ENGAGEMENT_PATCH_JSON='{"duration_match_weight": 0.0, "emotional_intensity_weight": 0.08, "emotional_match_weight": 0.1, "freshness_weight": 0.0, "impression_penalty_weight": 0.0, "music_match_weight": 0.12, "popularity_weight": 0.05, "quality_weight": 0.0, "repetition_penalty": 0.0, "similarity_weight": 0.6, "trending_weight": 0.0, "visual_match_weight": 0.04}'
PROXY_PATCH_JSON='{"clickbait_weight": -0.15, "emotional_intensity_weight": -0.05, "information_density_weight": 0.06, "language_match_weight": 0.05, "production_quality_weight": 0.08, "usefulness_weight": 0.12}'

ARM_NAMES=(engagement proxy)
declare -A ARM_PATCH=(
    [engagement]="$ENGAGEMENT_PATCH_JSON"
    [proxy]="$PROXY_PATCH_JSON"
)

declare -A ARM_CFG
for name in "${ARM_NAMES[@]}"; do
    ARM_CFG[$name]="${OUT_DIR}/configs/${name}.json"
    python3 - "$BASE_CFG" "${ARM_CFG[$name]}" "${ARM_PATCH[$name]}" <<'PYEOF'
import json
import sys

with open(sys.argv[1]) as f:
    cfg = json.load(f)
patch = json.loads(sys.argv[3])

# Same objective-only-variable design as scripts/run_phase20_experiment.sh: patch exactly the
# differing ranking-weight keys (never a wholesale block replacement) and add session_dynamics
# (the one gate configs/realism-small.json does not yet turn on) -- every other field, including
# content_v2/latent_reactions (already on in the base config) and the simulation/recommendation/
# hnsw/exploration/diversity blocks, is IDENTICAL between the two arms by construction.
cfg["ranking"].update(patch)
cfg["realism"]["session_dynamics"] = True

with open(sys.argv[2], "w") as f:
    json.dump(cfg, f, indent=2)
    f.write("\n")
PYEOF
done

# --- run both arms, timing the whole thing ----------------------------------------------------
START_TS=$(date +%s)

echo "-----------------------------------------------------------------------------------"
echo " [1/2] engagement-optimized preset"
echo "-----------------------------------------------------------------------------------"
"${SIMULATE}" --config "${ARM_CFG[engagement]}" --algorithm hnsw_ranker --seed "${SEED}" \
    --out "${OUT_DIR}/results-engagement" >/dev/null
echo "       done."
echo

echo "-----------------------------------------------------------------------------------"
echo " [2/2] satisfaction-proxy preset"
echo "-----------------------------------------------------------------------------------"
"${SIMULATE}" --config "${ARM_CFG[proxy]}" --algorithm hnsw_ranker --seed "${SEED}" \
    --out "${OUT_DIR}/results-proxy" >/dev/null
echo "       done."
echo

END_TS=$(date +%s)
ELAPSED=$((END_TS - START_TS))

# locate each arm's summary.json (simulate writes results/<experiment-id>/ under --out)
ENGAGEMENT_SUMMARY="$(find "${OUT_DIR}/results-engagement" -name summary.json | head -1)"
PROXY_SUMMARY="$(find "${OUT_DIR}/results-proxy" -name summary.json | head -1)"
for label_var in ENGAGEMENT_SUMMARY:engagement PROXY_SUMMARY:proxy; do
    var="${label_var%%:*}"; lbl="${label_var##*:}"
    if [[ -z "${!var}" ]]; then
        echo "error: simulate produced no summary.json for the ${lbl} arm under ${OUT_DIR}" >&2
        exit 1
    fi
done

echo "==================================================================================="
echo " Same users, same content, different objective -- headline comparison"
echo "==================================================================================="
python3 - "${ENGAGEMENT_SUMMARY}" "${PROXY_SUMMARY}" <<'PY'
import json, sys

eng = json.load(open(sys.argv[1]))
prx = json.load(open(sys.argv[2]))


def get(d, *path):
    cur = d
    for key in path:
        if not isinstance(cur, dict) or key not in cur:
            return None
        cur = cur[key]
    return cur


def fmt(v):
    if v is None:
        return "n/a"
    if isinstance(v, float):
        return f"{v:.4f}"
    return str(v)


rows = [
    ("reward / impression",        ("metrics", "reward_per_impression"),      "engagement proxy (TDD reward)"),
    ("completion rate",             ("metrics", "completion_rate"),           "share of feed watched to completion"),
    ("like rate",                   ("metrics", "like_rate"),                 "likes / impression"),
    ("mean hidden satisfaction",    ("welfare", "mean_immediate_satisfaction"), "hidden-preference welfare (D18 carve-out)"),
    ("mean hidden regret",          ("welfare", "mean_regret"),               "LatentReaction.regret"),
    ("U_s (mean session utility)",  ("session_health", "mean_session_utility"), "session-scoped welfare (V2 TDD 4.9)"),
    ("early-failure-exit rate",     ("session_health", "early_failure_exit_rate"), "share of closed sessions"),
    ("natural-completion rate",     ("session_health", "natural_completion_rate"), "satisfied-exit share"),
    ("mean session duration (s)",   ("session_health", "mean_duration_seconds"), "time-before-exit"),
]

name_w = max(len(r[0]) for r in rows)
print(f"   {'metric':<{name_w}}   {'engagement':>12}   {'proxy':>12}   note")
print(f"   {'-' * name_w}   {'-' * 12}   {'-' * 12}   {'-' * 40}")
for name, path, note in rows:
    ev, pv = get(eng, *path), get(prx, *path)
    print(f"   {name:<{name_w}}   {fmt(ev):>12}   {fmt(pv):>12}   {note}")
PY
echo
echo "==================================================================================="
echo " Elapsed: ${ELAPSED}s for both arms (budget: < 120s)."
if (( ELAPSED >= 120 )); then
    echo " WARNING: elapsed exceeded the 2-minute budget."
fi
echo " Scratch output was written under ${OUT_DIR} (auto-removed)."
echo "==================================================================================="
