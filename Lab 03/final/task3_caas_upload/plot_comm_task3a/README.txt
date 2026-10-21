Task 3a: communication scaling across 30 mesh grid sizes

Run:
    python -m pip install matplotlib
    python plot_task3a.py

Keep mesh_messages.csv and plot_task3a.py together. No absolute paths are used.
VERSION = "v2" selects the implementation chosen for the main analysis.
Changing VERSION to "v1" or "v3" produces a separate output directory.

Data provenance:
mesh_messages.csv is an unchanged copy from:
task3_results.tar.gz/results_mesh_2WZEgl/mesh_messages.csv
It includes all three versions. The script filters the selected version.
One repetition per grid/version; there are no repeat error bars.

Graphs:
01_application_call_times.png: C1-C4 mean send/receive MPI call wall time.
02_application_observed_completion.png: C1-C4 mean observed request lifetime.
03_shutdown_call_times.png: DISABLE, READY, TERMINATE, TERMINATED MPI call times.
04_shutdown_observed_completion.png: observed lifetimes for those control messages.
V1 additionally produces two scheduling-message graphs.

Each panel includes all 30 mesh sizes, sorted by number of charging stations.
The grid is rectangular in some cases; the X axis is rows times columns.
The base is one extra MPI process. Each point is a per-message mean within
one simulation, not a mean across repeated simulations. Units are microseconds.
Log axes are explicitly labelled. Missing timings are gaps, not zero samples.

Timing interpretation:
MPI call time measures the local time inside the recorded send/receive API.
For a nonblocking operation, returning from the API does not mean completion.
Observed completion measures time until the program detects completion; it
includes waiting, scheduling, and polling intervals. Preposted receives also
include time before a matching message arrives. Blocking call/completion values
coincide in this instrumentation. V2 send completions are observed in batches,
so their lifetimes can be longer than the actual individual transfer times.
Neither metric is an isolated network delay, end-to-end delivery latency, or RTT.
No such latency is reconstructed from these CSV fields.

Simulation settings: 30 seconds, six ports/station, seven OpenMP threads/station,
two allocated CPUs/process, predefined period 1.5 seconds. Grid sizes change
both rank counts and placement; random congestion workloads also differ.
Interpret spikes with these factors, message counts and the allocation logs.
Do not infer a universal scaling law from one sample per size.

