#!/usr/bin/env python3
"""Plot metric time series from one or more ReelRank experiment result directories.

Usage
-----
    uv run --project scripts scripts/plot_results.py <result-dir> [<result-dir> ...] \
        [--labels a,b,c] [--out DIR] [--drift-at N]

    <result-dir>   One or more experiment result directories (each containing
                   learning_curve.csv, regret_curve.csv, summary.json, config.json
                   as produced by the ReelRank simulator). Multiple directories are
                   overlaid on shared axes, one line/color per run.
    --labels       Comma-separated run labels, one per result-dir, in the same
                   order (default: each directory's basename).
    --out          Output directory for PNGs (default: "plots"). Created if
                   missing. Nothing inside a result directory is ever written to.
    --drift-at     Interactions-per-user value marking drift onset, used as a
                   fallback for runs whose summary.json has no
                   adaptation.first_drift_interaction (drift_recovery.png only).

Example:
    uv run --project scripts scripts/plot_results.py \
        results/published/phase9/complete-system \
        results/published/phase10/drift-adaptive \
        --labels baseline,adaptive --out plots/phase10

This script only reads result CSV/JSON files and writes PNGs to --out; it
contains no simulation logic (design decision D15).

Also produces Phase 11 benchmark plots when a result dir (or a parent of several) contains
retrieval_metrics.csv (from apps/benchmark_retrieval) and/or load_metrics.csv (from
apps/benchmark_recommender), found by recursive glob and concatenated:

  recall_vs_efsearch_*.png  Recall@10 vs efSearch, one line per M, one figure per
                            (vector_count, dimensions, data_distribution, ef_construction).
  recall_vs_latency_*.png   Recall@10 vs query p95 latency pareto scatter, colored by M,
                            each point annotated with its efSearch; one figure per
                            (vector_count, dimensions, data_distribution). Distance-counting
                            rows (distance_comps_per_query >= 0, inflated latency) are excluded.
  throughput_vs_threads.png recommendations/second vs client threads, one line per
                            (corpus_reels, dimensions), from load_metrics.csv.
  p99_vs_corpus.png         end-to-end and retrieval p99 vs corpus size at the max thread count
                            present, from load_metrics.csv.

Produces up to four simulation PNGs in --out, skipping (with a one-line stderr note) any
plot whose required inputs are missing across every run:

  reward_curve.png       mean_reward_per_impression vs. interactions_per_user,
                         one line per run (from learning_curve.csv).
  alignment_curve.png    mean_estimated_hidden_cosine vs. interactions; when a
                         run's learning_curve.csv also has drifted_alignment /
                         control_alignment columns (drift-configured runs only),
                         those are added as dashed / dotted lines in the same
                         run color.
  cumulative_regret.png  cumulative_regret vs. interactions (from
                         regret_curve.csv). The x-axis is derived by mapping
                         each row's `round` through learning_curve.csv's
                         round -> interactions_per_user pairing when available;
                         when learning_curve.csv is absent/unusable for a run,
                         interactions are instead computed as
                         (round + 1) * config.json's recommendation.feed_size.
  drift_recovery.png     Only produced when at least one run's
                         learning_curve.csv has a populated drifted_mean_reward
                         column. Plots drifted_mean_reward vs. interactions per
                         run (falling back to mean_reward_per_impression for
                         runs with no drift columns, so a non-drifted baseline
                         can be overlaid for comparison); draws a vertical
                         guide at each run's drift interaction
                         (summary.json's adaptation.first_drift_interaction,
                         else --drift-at); and a per-run horizontal dashed
                         guide at 0.95 * pre_drift_reward (from
                         adaptation.pre_drift_reward, else computed as the
                         mean reward over the rows preceding that run's drift
                         interaction).

Exit status: 0 if at least one plot was written, 1 if none were.
"""
from __future__ import annotations

import argparse
import json
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Optional

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import pandas as pd

FIGSIZE = (8, 5)
DPI = 150
GRID_ALPHA = 0.3
# Colorblind-safe qualitative cycle; index i is used for run i on every plot.
COLOR_CYCLE = plt.get_cmap("tab10").colors


def warn(message: str) -> None:
    print(f"plot_results: {message}", file=sys.stderr)


@dataclass
class RunData:
    label: str
    directory: Path
    color: tuple
    learning: Optional[pd.DataFrame]
    regret: Optional[pd.DataFrame]
    summary: Optional[dict]
    config: Optional[dict]


def _read_csv(path: Path) -> Optional[pd.DataFrame]:
    if not path.exists():
        return None
    try:
        return pd.read_csv(path)
    except Exception as exc:  # malformed CSV
        warn(f"failed to read {path}: {exc}")
        return None


def _read_json(path: Path) -> Optional[dict]:
    if not path.exists():
        return None
    try:
        with path.open() as fh:
            return json.load(fh)
    except Exception as exc:  # malformed JSON
        warn(f"failed to read {path}: {exc}")
        return None


def load_run(directory: Path, label: str, color: tuple) -> RunData:
    if not directory.is_dir():
        warn(f"{label}: result directory not found: {directory}")
        return RunData(label, directory, color, None, None, None, None)

    learning = _read_csv(directory / "learning_curve.csv")
    if learning is None:
        warn(f"{label}: learning_curve.csv missing or unreadable")

    regret = _read_csv(directory / "regret_curve.csv")
    if regret is None:
        warn(f"{label}: regret_curve.csv missing or unreadable")

    summary = _read_json(directory / "summary.json")
    if summary is None:
        warn(f"{label}: summary.json missing or unreadable")

    config = _read_json(directory / "config.json")
    if config is None:
        warn(f"{label}: config.json missing or unreadable")

    return RunData(label, directory, color, learning, regret, summary, config)


@dataclass
class BenchmarkData:
    """Benchmark result frames for one result-dir argument (Phase 11 retrieval/load sweeps).

    Distinct from RunData (simulation curves): a benchmark result dir holds
    retrieval_metrics.csv (from apps/benchmark_retrieval) and/or load_metrics.csv (from
    apps/benchmark_recommender). Either may be absent; the dependent plots warn-skip.
    """

    label: str
    directory: Path
    retrieval: Optional[pd.DataFrame]
    load: Optional[pd.DataFrame]


def load_benchmark(directory: Path, label: str) -> BenchmarkData:
    """Load retrieval_metrics.csv / load_metrics.csv from a benchmark result dir.

    The CSVs are found by recursive glob, so `directory` may be either a single experiment-id dir
    (…/hnsw_retrieval-seed42-…/) or a parent that contains several (…/results/phase11/retrieval/);
    all matches are concatenated so multi-pass sweeps can be faceted in one call.
    """
    if not directory.is_dir():
        warn(f"{label}: result directory not found: {directory}")
        return BenchmarkData(label, directory, None, None)

    def _concat(name: str) -> Optional[pd.DataFrame]:
        frames = []
        for path in sorted(directory.rglob(name)):
            df = _read_csv(path)
            if df is not None and not df.empty:
                frames.append(df)
        if not frames:
            return None
        return pd.concat(frames, ignore_index=True)

    retrieval = _concat("retrieval_metrics.csv")
    load = _concat("load_metrics.csv")
    if retrieval is None and load is None:
        warn(f"{label}: no retrieval_metrics.csv or load_metrics.csv under {directory}")
    return BenchmarkData(label, directory, retrieval, load)


def _combined_retrieval(benchruns: list[BenchmarkData]) -> Optional[pd.DataFrame]:
    frames = [b.retrieval for b in benchruns if b.retrieval is not None]
    if not frames:
        return None
    df = pd.concat(frames, ignore_index=True)
    # data_distribution / distance_comps_per_query are Phase 11 additions; default gracefully so
    # older (Phase 1) retrieval_metrics.csv files still plot.
    if "data_distribution" not in df.columns:
        df["data_distribution"] = "random"
    if "distance_comps_per_query" not in df.columns:
        df["distance_comps_per_query"] = -1.0
    return df


def _combined_load(benchruns: list[BenchmarkData]) -> Optional[pd.DataFrame]:
    frames = [b.load for b in benchruns if b.load is not None]
    if not frames:
        return None
    return pd.concat(frames, ignore_index=True)


def maybe_legend(ax, **kwargs) -> None:
    """Add a legend only when more than one labeled artist is present."""
    handles, labels = ax.get_legend_handles_labels()
    if len(handles) > 1:
        ax.legend(handles, labels, **kwargs)


def finish_figure(fig, ax, outpath: Path, xlabel: str, ylabel: str, title: str) -> None:
    ax.set_xlabel(xlabel)
    ax.set_ylabel(ylabel)
    ax.set_title(title)
    ax.grid(alpha=GRID_ALPHA)
    maybe_legend(ax, fontsize=8)
    fig.tight_layout()
    fig.savefig(outpath, dpi=DPI)
    plt.close(fig)


def plot_reward_curve(runs: list[RunData], outdir: Path) -> bool:
    fig, ax = plt.subplots(figsize=FIGSIZE)
    plotted = 0
    for run in runs:
        df = run.learning
        if df is None or "interactions_per_user" not in df.columns or "mean_reward_per_impression" not in df.columns:
            continue
        ax.plot(df["interactions_per_user"], df["mean_reward_per_impression"], color=run.color, label=run.label)
        plotted += 1
    if plotted == 0:
        plt.close(fig)
        warn("skipping reward_curve.png: no run has usable learning_curve.csv data")
        return False
    finish_figure(
        fig, ax, outdir / "reward_curve.png",
        "interactions per user", "mean reward per impression",
        "Reward per Impression vs. Interactions",
    )
    return True


def plot_alignment_curve(runs: list[RunData], outdir: Path) -> bool:
    fig, ax = plt.subplots(figsize=FIGSIZE)
    plotted = 0
    for run in runs:
        df = run.learning
        if df is None or "interactions_per_user" not in df.columns or "mean_estimated_hidden_cosine" not in df.columns:
            continue
        x = df["interactions_per_user"]
        ax.plot(x, df["mean_estimated_hidden_cosine"], color=run.color, linestyle="-", label=run.label)
        plotted += 1
        if "drifted_alignment" in df.columns:
            ax.plot(x, df["drifted_alignment"], color=run.color, linestyle="--", label=f"{run.label} (drifted)")
        if "control_alignment" in df.columns:
            ax.plot(x, df["control_alignment"], color=run.color, linestyle=":", label=f"{run.label} (control)")
    if plotted == 0:
        plt.close(fig)
        warn("skipping alignment_curve.png: no run has usable learning_curve.csv data")
        return False
    finish_figure(
        fig, ax, outdir / "alignment_curve.png",
        "interactions per user", "mean estimated-hidden cosine similarity",
        "Preference Alignment vs. Interactions",
    )
    return True


def _regret_interactions(run: RunData):
    """Return (x, y) interaction/cumulative_regret series for a run, or (None, None)."""
    regret = run.regret
    if regret is None or "round" not in regret.columns or "cumulative_regret" not in regret.columns:
        return None, None

    learning = run.learning
    if learning is not None and "round" in learning.columns and "interactions_per_user" in learning.columns:
        mapping = dict(zip(learning["round"], learning["interactions_per_user"]))
        x = regret["round"].map(mapping)
        if x.notna().all():
            return x, regret["cumulative_regret"]

    feed_size = None
    if run.config is not None:
        feed_size = run.config.get("recommendation", {}).get("feed_size")
    if feed_size:
        x = (regret["round"] + 1) * feed_size
        return x, regret["cumulative_regret"]

    return None, None


def plot_cumulative_regret(runs: list[RunData], outdir: Path) -> bool:
    fig, ax = plt.subplots(figsize=FIGSIZE)
    plotted = 0
    for run in runs:
        x, y = _regret_interactions(run)
        if x is None:
            continue
        ax.plot(x, y, color=run.color, label=run.label)
        plotted += 1
    if plotted == 0:
        plt.close(fig)
        warn("skipping cumulative_regret.png: no run has usable regret_curve.csv data")
        return False
    finish_figure(
        fig, ax, outdir / "cumulative_regret.png",
        "interactions per user", "cumulative regret (true-affinity units)",
        "Cumulative Regret vs. Interactions",
    )
    return True


def _has_drift_data(run: RunData) -> bool:
    df = run.learning
    return df is not None and "drifted_mean_reward" in df.columns and df["drifted_mean_reward"].notna().any()


def _drift_interaction(run: RunData, cli_override: Optional[int]) -> Optional[float]:
    if run.summary is not None:
        adaptation = run.summary.get("adaptation")
        if isinstance(adaptation, dict):
            value = adaptation.get("first_drift_interaction")
            if value is not None:
                return value
    return cli_override


def _pre_drift_reward(run: RunData, drift_interaction: Optional[float], reward_col: str) -> Optional[float]:
    if run.summary is not None:
        adaptation = run.summary.get("adaptation")
        if isinstance(adaptation, dict):
            value = adaptation.get("pre_drift_reward")
            if value is not None:
                return value
    if drift_interaction is None or run.learning is None:
        return None
    df = run.learning
    if "interactions_per_user" not in df.columns or reward_col not in df.columns:
        return None
    pre = df.loc[df["interactions_per_user"] < drift_interaction, reward_col].dropna()
    if pre.empty:
        return None
    return float(pre.mean())


def plot_drift_recovery(runs: list[RunData], outdir: Path, cli_drift_at: Optional[int]) -> bool:
    if not any(_has_drift_data(run) for run in runs):
        warn("skipping drift_recovery.png: no run has drift columns (drifted_mean_reward) with data")
        return False

    fig, ax = plt.subplots(figsize=FIGSIZE)
    plotted = 0
    drift_interactions: set = set()

    for run in runs:
        df = run.learning
        if df is None or "interactions_per_user" not in df.columns:
            continue
        if "drifted_mean_reward" in df.columns and df["drifted_mean_reward"].notna().any():
            reward_col = "drifted_mean_reward"
        elif "mean_reward_per_impression" in df.columns:
            reward_col = "mean_reward_per_impression"
        else:
            continue

        x = df["interactions_per_user"]
        ax.plot(x, df[reward_col], color=run.color, label=run.label)
        plotted += 1

        drift_interaction = _drift_interaction(run, cli_drift_at)
        if drift_interaction is not None:
            drift_interactions.add(drift_interaction)

        pre_drift_reward = _pre_drift_reward(run, drift_interaction, reward_col)
        if pre_drift_reward is not None:
            ax.axhline(0.95 * pre_drift_reward, color=run.color, linestyle="--", linewidth=1, alpha=0.6)

    if plotted == 0:
        plt.close(fig)
        warn("skipping drift_recovery.png: no run has usable reward data")
        return False

    for i, interaction in enumerate(sorted(drift_interactions)):
        ax.axvline(
            interaction, color="gray", linestyle=":", linewidth=1.5,
            label="drift start" if i == 0 else None,
        )

    finish_figure(
        fig, ax, outdir / "drift_recovery.png",
        "interactions per user", "mean reward per impression",
        "Preference-Drift Recovery",
    )
    return True


# --- Phase 11 benchmark plots (retrieval_metrics.csv / load_metrics.csv) ----------------------
#
# Each returns the number of PNGs written (0 => nothing usable, warn-skipped), summed in main().
# Retrieval plots facet by grid axes so "lines per M" stays unambiguous; latency plots exclude
# distance-counting rows (distance_comps_per_query >= 0), whose latency is instrumentation-inflated.


def plot_recall_vs_efsearch(retr: Optional[pd.DataFrame], outdir: Path) -> int:
    required = {"k", "ef_search", "m", "recall_at_k", "vector_count", "dimensions",
                "ef_construction"}
    if retr is None or not required <= set(retr.columns):
        warn("skipping recall_vs_efsearch: no retrieval_metrics.csv with the required columns")
        return 0
    k10 = retr[retr["k"] == 10]
    if k10.empty:
        warn("skipping recall_vs_efsearch: no k=10 rows in retrieval_metrics.csv")
        return 0

    written = 0
    # One figure per (vector_count, dimensions, data_distribution, ef_construction) so each line is
    # a clean M-sweep at a fixed efConstruction.
    for (vc, dim, dist, efc), g in k10.groupby(
        ["vector_count", "dimensions", "data_distribution", "ef_construction"]
    ):
        fig, ax = plt.subplots(figsize=FIGSIZE)
        for i, (m, gm) in enumerate(sorted(g.groupby("m"))):
            gm = gm.sort_values("ef_search")
            ax.plot(gm["ef_search"], gm["recall_at_k"], marker="o",
                    color=COLOR_CYCLE[i % len(COLOR_CYCLE)], label=f"M={m}")
        ax.set_xscale("log", base=2)
        ax.axhline(0.90, color="gray", linestyle=":", linewidth=1)  # TDD 27 Recall@10 target.
        out = outdir / f"recall_vs_efsearch_vc{vc}_d{dim}_{dist}_efc{efc}.png"
        finish_figure(fig, ax, out, "efSearch (log2)", "Recall@10",
                      f"Recall@10 vs efSearch  (N={vc}, d={dim}, {dist}, efC={efc})")
        written += 1
    return written


def plot_recall_vs_latency(retr: Optional[pd.DataFrame], outdir: Path) -> int:
    required = {"k", "recall_at_k", "query_p95_ms", "m", "ef_search", "vector_count", "dimensions"}
    if retr is None or not required <= set(retr.columns):
        warn("skipping recall_vs_latency: no retrieval_metrics.csv with the required columns")
        return 0
    df = retr[retr["k"] == 10]
    # Latency hygiene: drop distance-counting rows (flagged distance_comps_per_query >= 0) whose
    # p95 includes atomic-increment overhead.
    if "distance_comps_per_query" in df.columns:
        df = df[df["distance_comps_per_query"] < 0]
    if df.empty:
        warn("skipping recall_vs_latency: no clean-latency k=10 rows")
        return 0

    written = 0
    for (vc, dim, dist), g in df.groupby(["vector_count", "dimensions", "data_distribution"]):
        fig, ax = plt.subplots(figsize=FIGSIZE)
        for i, (m, gm) in enumerate(sorted(g.groupby("m"))):
            ax.scatter(gm["query_p95_ms"], gm["recall_at_k"],
                       color=COLOR_CYCLE[i % len(COLOR_CYCLE)], label=f"M={m}", zorder=3)
            for _, row in gm.iterrows():
                ax.annotate(f"ef{int(row['ef_search'])}",
                            (row["query_p95_ms"], row["recall_at_k"]),
                            textcoords="offset points", xytext=(3, 3), fontsize=6)
        ax.axhline(0.90, color="gray", linestyle=":", linewidth=1)
        out = outdir / f"recall_vs_latency_vc{vc}_d{dim}_{dist}.png"
        finish_figure(fig, ax, out, "query p95 latency (ms)", "Recall@10",
                      f"Recall@10 vs p95 latency  (N={vc}, d={dim}, {dist})")
        written += 1
    return written


def plot_throughput_vs_threads(load: Optional[pd.DataFrame], outdir: Path) -> int:
    required = {"threads", "rps", "corpus_reels", "dimensions"}
    if load is None or not required <= set(load.columns):
        warn("skipping throughput_vs_threads: no load_metrics.csv with the required columns")
        return 0

    fig, ax = plt.subplots(figsize=FIGSIZE)
    plotted = 0
    for i, ((corpus, dim), g) in enumerate(sorted(load.groupby(["corpus_reels", "dimensions"]))):
        g = g.sort_values("threads")
        ax.plot(g["threads"], g["rps"], marker="o",
                color=COLOR_CYCLE[i % len(COLOR_CYCLE)], label=f"N={corpus}, d={dim}")
        plotted += 1
    if plotted == 0:
        plt.close(fig)
        warn("skipping throughput_vs_threads: no usable load_metrics.csv rows")
        return 0
    finish_figure(fig, ax, outdir / "throughput_vs_threads.png", "client threads",
                  "recommendations / second", "Throughput vs Concurrent Clients")
    return 1


def plot_p99_vs_corpus(load: Optional[pd.DataFrame], outdir: Path) -> int:
    required = {"corpus_reels", "threads", "e2e_p99_ms", "retrieval_p99_ms"}
    if load is None or not required <= set(load.columns):
        warn("skipping p99_vs_corpus: no load_metrics.csv with the required columns")
        return 0
    max_threads = load["threads"].max()
    at_max = load[load["threads"] == max_threads]
    has_dim = "dimensions" in at_max.columns
    groups = at_max.groupby("dimensions") if has_dim else [(None, at_max)]

    fig, ax = plt.subplots(figsize=FIGSIZE)
    plotted = 0
    for i, (dim, g) in enumerate(groups):
        g = g.sort_values("corpus_reels")
        color = COLOR_CYCLE[i % len(COLOR_CYCLE)]
        suffix = f" (d={dim})" if dim is not None else ""
        ax.plot(g["corpus_reels"], g["e2e_p99_ms"], marker="o", color=color,
                linestyle="-", label=f"end-to-end p99{suffix}")
        ax.plot(g["corpus_reels"], g["retrieval_p99_ms"], marker="s", color=color,
                linestyle="--", label=f"retrieval p99{suffix}")
        plotted += 1
    if plotted == 0:
        plt.close(fig)
        warn("skipping p99_vs_corpus: no usable load_metrics.csv rows")
        return 0
    ax.set_xscale("log")
    finish_figure(fig, ax, outdir / "p99_vs_corpus.png", "corpus size (reels)",
                  "p99 latency (ms)",
                  f"Tail Latency vs Corpus Size  (threads={int(max_threads)})")
    return 1


def derive_labels(labels_arg: Optional[str], dirs: list[Path]) -> list[str]:
    if labels_arg:
        parts = [p.strip() for p in labels_arg.split(",")]
        if len(parts) == len(dirs):
            return parts
        warn(
            f"--labels count ({len(parts)}) does not match number of result "
            f"dirs ({len(dirs)}); using directory names instead"
        )
    return [d.name if d.name else d.resolve().name for d in dirs]


def parse_args(argv=None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Plot ReelRank experiment results (reward, alignment, regret, drift recovery).",
    )
    parser.add_argument(
        "result_dirs", nargs="+", metavar="result-dir",
        help="one or more experiment result directories",
    )
    parser.add_argument(
        "--labels", default=None,
        help="comma-separated run labels, one per result-dir (default: directory basenames)",
    )
    parser.add_argument(
        "--out", default="plots",
        help="output directory for plots (default: plots)",
    )
    parser.add_argument(
        "--drift-at", type=int, default=None, dest="drift_at",
        help="interactions_per_user marking drift onset, used when no run's "
             "summary.json has adaptation.first_drift_interaction",
    )
    return parser.parse_args(argv)


def main(argv=None) -> int:
    args = parse_args(argv)

    outdir = Path(args.out)
    outdir.mkdir(parents=True, exist_ok=True)

    dirs = [Path(d) for d in args.result_dirs]
    labels = derive_labels(args.labels, dirs)
    colors = [COLOR_CYCLE[i % len(COLOR_CYCLE)] for i in range(len(dirs))]

    runs = [load_run(d, label, color) for d, label, color in zip(dirs, labels, colors)]

    written = 0
    # Simulation curves (learning_curve.csv / regret_curve.csv). Each warn-skips when absent.
    written += plot_reward_curve(runs, outdir)
    written += plot_alignment_curve(runs, outdir)
    written += plot_cumulative_regret(runs, outdir)
    written += plot_drift_recovery(runs, outdir, args.drift_at)

    # Phase 11 benchmark plots (retrieval_metrics.csv / load_metrics.csv). Same dirs; a dir may hold
    # simulation CSVs, benchmark CSVs, or (a parent dir) several benchmark subtrees. Each warn-skips.
    benchruns = [load_benchmark(d, label) for d, label in zip(dirs, labels)]
    retr = _combined_retrieval(benchruns)
    load = _combined_load(benchruns)
    written += plot_recall_vs_efsearch(retr, outdir)
    written += plot_recall_vs_latency(retr, outdir)
    written += plot_throughput_vs_threads(load, outdir)
    written += plot_p99_vs_corpus(load, outdir)

    return 0 if written > 0 else 1


if __name__ == "__main__":
    sys.exit(main())
