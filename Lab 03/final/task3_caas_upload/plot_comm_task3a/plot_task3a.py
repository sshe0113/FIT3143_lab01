"""Task 3a: per-message communication times across all 30 mesh sizes.

Keep mesh_messages.csv next to this script.
Install: python -m pip install matplotlib
Run:     python plot_task3a.py
The source CSV is read only. All generated files go into output_v2/.
"""

import csv
import math
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt


# V2 is the selected implementation. Change to "v1" or "v3" if needed.
VERSION = "v2"
LOG_SCALE = True
MESSAGE_NAMES = {
    1: "C1: Neighbour request",
    2: "C2: Neighbour response",
    3: "C3: Congestion report",
    4: "C4: Base reply",
    5: "TERMINATE",
    6: "DISABLE",
    7: "READY",
    8: "TERMINATED",
    9: "QUERY_GRANT (V1)",
    10: "QUERY_DONE (V1)",
}


def load_data(folder):
    with (folder / "mesh_messages.csv").open(encoding="utf-8-sig", newline="") as file:
        records = list(csv.DictReader(file))
    selected = [r for r in records if r["suite"] == "mesh" and r["version"] == VERSION]
    expected_tags = set(range(1, 11 if VERSION == "v1" else 9))
    shapes = {(int(r["rows"]), int(r["cols"])) for r in selected}
    if VERSION not in {"v1", "v2", "v3"} or len(shapes) != 30:
        raise ValueError("Choose v1/v2/v3; exactly 30 mesh sizes are required.")
    if len({r["predefined_period"] for r in selected}) != 1:
        raise ValueError("The predefined period must be the same for all selected runs.")
    seen = set()
    for r in selected:
        key = (int(r["rows"]), int(r["cols"]), int(r["tag"]))
        if key in seen or int(r["tag"]) not in expected_tags:
            raise ValueError("Duplicate grid/tag or unexpected message type. Use one repetition.")
        seen.add(key)
        if int(r["stations"]) != key[0] * key[1] or int(r["processes"]) != key[0] * key[1] + 1:
            raise ValueError("Station/process count does not match the grid.")
        if r["sent"] != r["received"] or r["send_bytes"] != r["receive_bytes"]:
            raise ValueError("Unbalanced message counts or payload bytes.")
        for field in ["mean_send_api_us", "mean_receive_api_us",
                      "mean_observed_send_us", "mean_observed_receive_us"]:
            if r[field] and (not math.isfinite(float(r[field])) or float(r[field]) < 0):
                raise ValueError(f"Invalid timing in {field}.")
    expected = {(rows, cols, tag) for rows, cols in shapes for tag in expected_tags}
    if seen != expected:
        raise ValueError("At least one grid/message record is missing.")
    if len({r["replicate"] for r in selected}) != 1:
        raise ValueError("Use a single repetition for these plots.")
    return sorted(selected, key=lambda r: (int(r["tag"]), int(r["stations"]),
                                            int(r["rows"]), int(r["cols"])))


def plot_messages(records, tags, title, metric, output, filename):
    fig, axes = plt.subplots(2 if len(tags) > 2 else 1, 2, figsize=(12, 7.5 if len(tags) > 2 else 4.5),
                             squeeze=False)
    if metric == "call":
        send_field, receive_field = "mean_send_api_us", "mean_receive_api_us"
        ylabel = "Mean MPI call time (us)"
    else:
        send_field, receive_field = "mean_observed_send_us", "mean_observed_receive_us"
        ylabel = "Mean observed completion time (us)"
    for ax, tag in zip(axes.flat, tags):
        rows = [r for r in records if int(r["tag"]) == tag]
        stations = [int(r["stations"]) for r in rows]
        # An empty timing means no samples, not a measured zero: show a gap.
        send = [float(r[send_field]) if r[send_field] else math.nan for r in rows]
        receive = [float(r[receive_field]) if r[receive_field] else math.nan for r in rows]
        ax.plot(stations, send, "o-", color="#4477AA", label="Send", markersize=3, linewidth=1.3)
        ax.plot(stations, receive, "s-", color="#EE7733", label="Receive", markersize=3, linewidth=1.3)
        finite_values = [v for v in send + receive if math.isfinite(v)]
        # If a measured zero exists, retain it and use a linear axis.
        use_log = LOG_SCALE and finite_values and min(finite_values) > 0
        if use_log:
            ax.set_yscale("log")
        else:
            ax.set_ylim(bottom=0)
        ax.set_title(MESSAGE_NAMES[tag])
        ax.set_xlabel("Charging stations (rows x cols)")
        ax.set_ylabel(ylabel + (", log scale" if use_log else ""))
        ax.set_xticks([4, 10, 20, 30, 40, 45])
        ax.set_axisbelow(True)
        ax.grid(axis="both", which="major", alpha=0.25)
        ax.spines[["top", "right"]].set_visible(False)
        ax.legend(frameon=False, fontsize=9)
    fig.suptitle(f"Task 3a - {VERSION.upper()} Mesh: {title}", fontsize=16)
    fig.tight_layout(rect=(0, 0, 1, 0.95))
    fig.savefig(output / filename, dpi=300, bbox_inches="tight")
    plt.close(fig)


def main():
    folder = Path(__file__).resolve().parent
    records = load_data(folder)
    output = folder / f"output_{VERSION}"
    output.mkdir(exist_ok=True)
    plt.rcParams.update({"font.size": 10, "axes.titlesize": 12})
    plot_messages(records, [1, 2, 3, 4], "Application MPI Call Times", "call", output,
                  "01_application_call_times.png")
    plot_messages(records, [1, 2, 3, 4], "Application Observed Completion Times", "completion", output,
                  "02_application_observed_completion.png")
    plot_messages(records, [5, 6, 7, 8], "Shutdown MPI Call Times", "call", output,
                  "03_shutdown_call_times.png")
    plot_messages(records, [5, 6, 7, 8], "Shutdown Observed Completion Times", "completion", output,
                  "04_shutdown_observed_completion.png")
    if VERSION == "v1":
        plot_messages(records, [9, 10], "Scheduling MPI Call Times", "call", output,
                      "05_scheduling_call_times.png")
        plot_messages(records, [9, 10], "Scheduling Observed Completion Times", "completion", output,
                      "06_scheduling_observed_completion.png")
    print(f"Generated {VERSION.upper()} graphs for all 30 mesh sizes and {len(records)} message records.")
    print(f"Output folder: {output}")


if __name__ == "__main__":
    main()
