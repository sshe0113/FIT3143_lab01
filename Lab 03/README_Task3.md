# V1 completion and Task 3 experiments

## What changed

1. C3 now carries calendar incident time with timezone, in addition to elapsed simulation time. The custom MPI struct datatype includes the new 32-character field.
2. When a neighbour has more than two free ports, the node prints the available immediate neighbours and selects one with the most free ports for simulated EV referral. Ties use top/bottom/left/right order. This is a referral simulation, not a reservation protocol.
3. The base log lists the number and coordinates of **all** returned candidates. The original first `Nearest` field and grid snapshot remain for continuity.
4. `EVCNS_PREDEFINED_PERIOD` controls the freshness window. Four CAAS period cases are supplied: 0.5, 1.5, 3 and 6 seconds. Do not merely change the value in slides: retain the resulting logs.
5. C1-C4 retain V1's original mixed communication: Isend/Irecv neighbour enquiries, blocking neighbour responses, blocking C3, and separate C4 reception. The V1-only file removes the unused alternative blocking query and the V3 implementation. The alternative nested-loop search remains commented.
6. Timed 3.5/4-second draining is replaced by confirmed shutdown: DISABLE stops new enquiries, READY confirms a node's own work has finished, TERMINATE follows all READY messages, and TERMINATED acknowledges exit. Nodes keep answering neighbours while finishing existing work. The base continues serving C3/C4 during this phase.
7. The default random model is still 70% busy / 30% free, with six port workers and one communication thread. The original srand/rand approach is retained, with a recorded per-rank seed (default 2026 plus global_rank). Updates remain inside the original critical region. Thread scheduling can assign random draws differently, so identical seeds do not guarantee identical port traces or report counts.
8. Per-message counters, bytes, local MPI API durations and observed request-completion durations are written to CSV. Final global checks require completed sends/receives and payload bytes to balance and no tracked requests to remain.

The authoritative selected source is `v1/evcns.c`, provided as a self-contained C file with embedded per-message measurements. Existing function/variable names and manual comm_time accumulation are retained. The unused MessageAPITotal helper and unused multi-version measurement switches were removed. The historical V2/V3 folders and logs are preserved. The batch runner snapshots the same self-contained v1/evcns.c before compiling.

The previous Excel comparison describes the **earlier baseline runs**. New V1 results have a new report payload, controlled seeds and confirmed shutdown. Its total runtime no longer includes a fixed four-second drain. Do not combine old and new runs as though they used identical measurement protocols. Update the presentation shutdown diagram accordingly.

## First CAAS check

Upload the selected self-contained v1/evcns.c as evcns.c next to your existing CAAS run_evcns.slurm. No project header is needed. From that directory:

```bash
sbatch run_evcns.slurm
```

Retain the resulting log.txt, message_metrics_rank_*.csv, message_metrics_global.csv, roundtrip_rank_*.csv and scheduler stdout. Use a separate directory for each manual run to preserve outputs. Check the job and step exit states:

```bash
sacct -j JOB_ID --format=JobID,State,ExitCode
```

The base log must end with balanced actual message totals and Shutdown confirmed: READY=9 TERMINATED=9 for 3x3. Use stdout to verify local neighbour referrals. The Task 3 batch runner automatically isolates each job and repetition.

## Single-file build

All project measurement/control functions are embedded in evcns.c. A separate task3_metrics.h is no longer needed. Existing V1 names, MPI calls and manual comm_time accumulation are retained.

```bash
mpicc -std=gnu11 -O2 -fopenmp -o evcns evcns.c
srun -n 10 ./evcns 3 3 v1
```

## Task 3 measurement definition

These measurements use **each rank's own MPI_Wtime clock**; clocks from different ranks are never subtracted.

- `send_api_seconds`: time inside Send or time posting Isend. Posting time alone is not nonblocking transfer completion.
- `send_completion_seconds`: Send call duration, or time from Isend posting until completion is **observed** by Test/Wait. V1 may observe send completion only after receiving neighbour replies. Polling, progress and scheduling therefore contribute.
- `receive_completion_seconds`: time from receive start/post to observed completion. This includes waiting for the sender.
- `progress_api_seconds`: MPI Test/Wait time attributed to active requests. The original manual comm_time includes the explicitly timed probes; probe time is not assigned to a particular message type.
- `C1_C2_ENQUIRY`: time for the complete concurrent enquiry of all valid neighbours. This is an application operation, not a separate one-way delay for C1 or C2.
- `C3_C4_ROUNDTRIP`: time from beginning C3 until the full C4 count/coordinates are received.

The graphs describe **software-observed communication completion time** and **MPI call time**, not isolated one-way network latency. Eager sends can finish locally before remote receipt. Do not claim that MPI_Isend posting time is physical communication delay, or divide RTT by two without an additional symmetry assumption.

The per-type graphs include C1 request, C2 response, C3 report, C4 base reply, C5 terminate and shutdown tags 6-8 (DISABLE, READY, TERMINATED), covering all message types in the updated architecture. C4 count and coordinate arrays are separate MPI messages. Mean payload bytes are retained because variable candidate counts affect traffic. The report's old neighbour/C3 events field is explicitly labelled a subset; global counters include all completed point-to-point messages and exclude collectives.

## Experiment coverage and resources

`testing/task3/experiment_cases.csv` contains:

- 30 mesh configurations with **30 distinct station counts**, from 4 to 45 stations, choosing balanced rectangular factors where possible.
- 30 line configurations, `1x2` through `1x31`.
- Four 3x3 predefined-period configurations.

The default is three independent runs per configuration (seeds 2026, 2027 and 2028), 30 seconds each. Each configuration gets its own job with a five-minute limit, so the whole sweep is not squeezed into one five-minute allocation.

Submission reads CPU counts using `sinfo -N -h -p defq -o "%N|%c|%t"`. A line listing 14 idle nodes is not a CPU count. By default each MPI task receives seven CPUs, and each node receives no more than 16 tasks or its CPU capacity, whichever is smaller. The largest case needs 46 MPI tasks. If the partition cannot fit that allocation, the script stops rather than omitting sizes or oversubscribing silently.

You can explicitly choose a smaller `--cpus-per-task` allocation only if appropriate for the course cluster, documenting that seven OpenMP threads share fewer CPUs. Keep that allocation identical across the sweep. No node count or CPU capacity is invented.

## Submit period experiments, then mesh and line

Upload the full `testing/task3` directory as well as the self-contained `v1/evcns.c`. From CAAS:

```bash
cd testing/task3
python3 submit_task3.py --suite period
```

This is a **dry run**, showing the detected capacity and commands. If the plan is valid:

```bash
python3 submit_task3.py --suite period --submit --max-new-jobs 4
python3 submit_task3.py --suite mesh --submit
python3 submit_task3.py --suite line --submit
```

The default submits at most five new jobs per command to avoid flooding the queue. Repeating the same command submits the next batch and skips previously submitted case IDs. Use the same `--run-id` (default `v1_task3`) to resume. Submitting a job is not proof it passed: check scheduler output and final analysis coverage. Failed cases require explicit review before retry; do not mix multiple successful copies of the same case/replicate.

If rerunning the full experiment with changed source or parameters, use a new `--run-id`. Every run records source hashes, MPI version, job/node information, resources, parameters, seed and exit status. Each job snapshots its source before compiling. Failed runs are retained but excluded from analysis.

## Aggregate and plot

After copying the complete results folder back locally, or on a machine with Python and matplotlib:

```bash
python3 aggregate_task3.py task3_results/v1_task3
```

CSV output works with the Python standard library. If matplotlib is unavailable, the script still writes CSV and tells you to rerun locally for plots. Do not install packages in the school cluster without following its rules.

Outputs in `analysis/`:

- `task3_message_runs.csv`: one row per message type per run, with counts, payloads and timing definitions.
- `task3_message_summary.csv`: means and standard deviations across repetitions.
- `task3_roundtrip_runs.csv`: measured application-level enquiry/round-trip operations.
- `mesh_completion.png`, `line_completion.png`: per-type observed completion time against station count, excluding the base.
- `mesh_send_api.png`, `line_send_api.png`: per-type MPI posting/blocking-call time.
- `mesh_roundtrips.png`, `line_roundtrips.png`: application-level round trips.
- `period_results.csv`, `period_effects.png`: period experiments, if present.
- `coverage.json`: actual validated grid/line/period coverage.

Per-run means are weighted by message counts across ranks. Configuration means and SD treat repeated runs equally. A message type with no samples has a blank latency and is not plotted as zero. A single run has no sample SD. SD error bars are descriptive, not a confidence interval. Missing data/coverage must be reported, not silently filled.

The old `testing/task3a.slurm` and `task3b.slurm` and prior workbooks remain historical material. Their aggregate-only timing does not supply this workflow's per-type measurements.
