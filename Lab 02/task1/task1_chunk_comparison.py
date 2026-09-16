from pathlib import Path
import argparse

import matplotlib.pyplot as plt
from matplotlib.ticker import FormatStrFormatter
import pandas as pd


CHUNK_SIZES = (1, 16, 32, 64)
PROCESS_COUNTS = (1, 2, 4, 8, 16, 32, 128)


def load_results(input_dir: Path):
    timing_frames = []
    balance_frames = []

    for chunk_size in CHUNK_SIZES:
        timing = pd.read_csv(input_dir / f"task1_MPI_P_{chunk_size}.csv")
        balance = pd.read_csv(input_dir / f"task1_MPI_balance_{chunk_size}.csv")
        timing["Chunk_Size"] = chunk_size
        balance["Chunk_Size"] = chunk_size
        timing_frames.append(timing)
        balance_frames.append(balance)

    timing = pd.concat(timing_frames, ignore_index=True)
    balance = pd.concat(balance_frames, ignore_index=True)

    balance_summary = (
        balance.groupby(["Chunk_Size", "Processes"], as_index=False)
        .agg(
            Job_Min=("Jobs", "min"),
            Job_Max=("Jobs", "max"),
            Job_Mean=("Jobs", "mean"),
            Rank_Time_Max=("Computation_Time_sec", "max"),
            Rank_Time_Mean=("Computation_Time_sec", "mean"),
        )
    )
    balance_summary["Computation_Imbalance"] = (
        balance_summary["Rank_Time_Max"] / balance_summary["Rank_Time_Mean"]
    )
    balance_summary["Job_Imbalance_Percent"] = (
        (balance_summary["Job_Max"] - balance_summary["Job_Min"])
        / balance_summary["Job_Mean"]
        * 100.0
    )
    return timing, balance_summary


def build_figure(timing: pd.DataFrame, balance: pd.DataFrame, output: Path):
    colors = {1: "#0072B2", 16: "#009E73", 32: "#D55E00", 64: "#CC79A7"}
    fig = plt.figure(figsize=(13, 9), constrained_layout=True)
    grid = fig.add_gridspec(2, 2, height_ratios=(1.2, 1.0))
    runtime_ax = fig.add_subplot(grid[0, :])
    time_balance_ax = fig.add_subplot(grid[1, 0])
    job_balance_ax = fig.add_subplot(grid[1, 1])

    for chunk_size in CHUNK_SIZES:
        runtime_data = timing[timing["Chunk_Size"] == chunk_size].sort_values("Processes")
        balance_data = balance[balance["Chunk_Size"] == chunk_size].sort_values("Processes")
        emphasis = 2.8 if chunk_size == 1 else 1.8
        runtime_ax.plot(
            runtime_data["Processes"],
            runtime_data["Overall_Time_sec"],
            marker="o",
            linewidth=emphasis,
            color=colors[chunk_size],
            label=f"Chunk {chunk_size}",
        )
        time_balance_ax.plot(
            balance_data["Processes"],
            balance_data["Computation_Imbalance"],
            marker="o",
            linewidth=emphasis,
            color=colors[chunk_size],
        )
        job_balance_ax.plot(
            balance_data["Processes"],
            balance_data["Job_Imbalance_Percent"],
            marker="o",
            linewidth=emphasis,
            color=colors[chunk_size],
        )

    for axis in (runtime_ax, time_balance_ax, job_balance_ax):
        axis.set_xscale("log", base=2)
        axis.set_xticks(PROCESS_COUNTS)
        axis.set_xticklabels(PROCESS_COUNTS)
        axis.set_xlabel("MPI processes")
        axis.grid(True, which="both", linestyle="--", alpha=0.35)

    runtime_ax.set_yscale("log")
    runtime_ax.set_ylabel("Overall time (seconds, log scale)")
    runtime_ax.set_title("A. Overall runtime - lower is better", loc="left", fontweight="bold")
    runtime_ax.legend(ncol=4, frameon=False, loc="upper right")

    time_balance_ax.axhline(1.0, color="#555555", linestyle=":", linewidth=1.4)
    time_balance_ax.set_ylabel("Max rank time / mean rank time")
    time_balance_ax.set_title(
        "B. Computation-time imbalance - closer to 1 is better",
        loc="left",
        fontweight="bold",
    )

    job_balance_ax.set_ylabel("Candidate-count range / mean (%)")
    job_balance_ax.yaxis.set_major_formatter(FormatStrFormatter("%.3f%%"))
    job_balance_ax.set_title(
        "C. Candidate-count imbalance - lower is better",
        loc="left",
        fontweight="bold",
    )

    fig.suptitle(
        "MPI chunk-size comparison for prime search (N = 40,000,000)",
        fontsize=17,
        fontweight="bold",
    )

    output.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(output, dpi=220, bbox_inches="tight")
    plt.close(fig)


def print_summary(timing: pd.DataFrame, balance: pd.DataFrame):
    merged = timing.merge(
        balance[["Chunk_Size", "Processes", "Computation_Imbalance", "Job_Imbalance_Percent"]],
        on=["Chunk_Size", "Processes"],
    )
    merged["Runtime_Ratio"] = merged["Overall_Time_sec"] / merged.groupby("Processes")[
        "Overall_Time_sec"
    ].transform("min")
    summary = merged.groupby("Chunk_Size").agg(
        Mean_Runtime_Ratio=("Runtime_Ratio", "mean"),
        Mean_Computation_Imbalance=("Computation_Imbalance", "mean"),
        Worst_Computation_Imbalance=("Computation_Imbalance", "max"),
        Worst_Job_Imbalance_Percent=("Job_Imbalance_Percent", "max"),
    )
    print(summary.round(6).to_string())


def main():
    parser = argparse.ArgumentParser(description="Compare MPI prime-search chunk sizes.")
    parser.add_argument(
        "--input-dir",
        type=Path,
        default=Path(
            r"C:\Users\User\Documents\MONASH\Year_3_sem_1\FIT3143\Week 04\FIT3143_lab01\Lab 02\task\task1"
        ),
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=Path(__file__).with_name("task1_chunk_comparison.png"),
    )
    args = parser.parse_args()

    timing, balance = load_results(args.input_dir)
    build_figure(timing, balance, args.output)
    print_summary(timing, balance)
    print(f"\nGraph saved to: {args.output.resolve()}")


if __name__ == "__main__":
    main()
