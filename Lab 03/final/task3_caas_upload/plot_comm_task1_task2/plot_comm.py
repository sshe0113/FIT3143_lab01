"""Plot selected mesh communication times for V1, V2 and V3.

Keep this script with mesh_runs.csv in the same folder.
Install: python -m pip install matplotlib
Run:     python plot_comm.py

mesh_runs.csv is copied unchanged from the CAAS results.
The script reads it and filters each version; the input file is not modified.
"""

import csv
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt


# Select the same mesh sizes for all three versions.
GRID_SIZES = [(2, 2), (4, 4), (5, 6)]
VERSIONS = ["v1", "v2", "v3"]
COLORS = ["#4477AA", "#EE7733", "#228833"]


def main():
    # Resolve files relative to this script, so the whole folder can be moved.
    folder = Path(__file__).resolve().parent
    with (folder / "mesh_runs.csv").open(encoding="utf-8-sig", newline="") as file:
        records = list(csv.DictReader(file))
    selected = []
    for version in VERSIONS:
        for rows, cols in GRID_SIZES:
            matches = [r for r in records if r["suite"] == "mesh"
                       and r["version"] == version
                       and int(r["rows"]) == rows and int(r["cols"]) == cols]
            if len(matches) != 1:
                raise ValueError(f"Need exactly one run for {version}, {rows}x{cols}.")
            seconds = float(matches[0]["station_avg_mpi_call_seconds"])
            selected.append({"grid": f"{rows}x{cols}", "version": version.upper(),
                             "station_avg_mpi_call_seconds": seconds,
                             "station_avg_mpi_call_ms": seconds * 1000})

    output = folder / "output"
    output.mkdir(exist_ok=True)
    # Save the nine plotted values separately from the original input CSV.
    with (output / "comm_comparison.csv").open("w", newline="", encoding="utf-8-sig") as file:
        writer = csv.DictWriter(file, fieldnames=list(selected[0]))
        writer.writeheader()
        writer.writerows(selected)

    plt.rcParams.update({"font.size": 11, "pdf.fonttype": 42})
    fig, ax = plt.subplots(figsize=(9, 5.5))
    width = 0.24
    for index, version in enumerate(VERSIONS):
        values = [r["station_avg_mpi_call_ms"] for r in selected
                  if r["version"] == version.upper()]
        positions = [i + (index - 1) * width for i in range(len(GRID_SIZES))]
        bars = ax.bar(positions, values, width, color=COLORS[index], label=version.upper())
        ax.bar_label(bars, labels=[f"{v:.2f}" for v in values], padding=4, fontsize=10)

    ax.set_xticks(range(len(GRID_SIZES)), [f"{r} x {c}" for r, c in GRID_SIZES])
    ax.set_xlabel("Mesh grid size")
    ax.set_ylabel("Average station MPI call time (ms, log scale)")
    ax.set_title("Mesh: Communication Call Time by Version", pad=15)
    # Log spacing makes the small V2/V3 bars visible without changing values.
    values_ms = [r["station_avg_mpi_call_ms"] for r in selected]
    if min(values_ms) <= 0:
        raise ValueError("Log scale requires positive times; a recorded zero cannot be plotted.")
    ax.set_yscale("log")
    ax.set_ylim(min(values_ms) / 3, max(values_ms) * 6)
    ax.set_yticks([0.1, 1, 10, 100], ["0.1", "1", "10", "100"])
    ax.set_axisbelow(True)
    ax.grid(axis="y", alpha=0.25)
    ax.spines[["top", "right"]].set_visible(False)
    ax.legend(ncol=3, frameon=False, loc="upper left")
    fig.tight_layout(rect=(0, 0.10, 1, 1))
    fig.savefig(output / "comm_comparison.png", dpi=300, bbox_inches="tight")
    fig.savefig(output / "comm_comparison.pdf", bbox_inches="tight")
    plt.close(fig)
    print("Generated output/comm_comparison.png, .pdf and .csv")


if __name__ == "__main__":
    main()
