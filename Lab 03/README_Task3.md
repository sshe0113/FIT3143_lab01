# V1: three-file CAAS workflow

Upload only `v1/evcns.c`, `v1/run_mesh.slurm` and `v1/run_line.slurm` to one CAAS directory. No Python or project header is needed. The old Python workflow under testing/task3 is historical and does not read the new plain-log format.

From that directory, submit each complete group once:

```bash
sbatch run_mesh.slurm
sbatch run_line.slurm
```

Alternatively, `bash run_mesh.slurm` / `bash run_line.slurm` submits directly from the head node without allocating a controller job. Each group submits 30 independent five-minute jobs linked sequentially. It does not attempt to fit the complete group into five minutes. Each size runs three 30-second repetitions.

The scripts inspect CPU/node capacity before submitting workers. Each MPI process gets seven CPUs for six port workers and one communication thread. The largest mesh needs 46 MPI processes and the largest line needs 32. Insufficient capacity stops submission with an explanation; no sizes are omitted. Scheduler queue/account limits can also reject submission; consult jobs.txt for jobs already accepted.

Each submission creates a new results_mesh_XXXXXX or results_line_XXXXXX folder and freezes its source and script. Each size and repetition has a separate log.txt, stdout.txt and metrics.csv. The full group CSV is mesh_results.csv or line_results.csv. Keep the source snapshot, jobs.txt and scheduler output with the results. The final worker checks for 30 x 3 x 8 = 720 data rows; failed cases are reported rather than silently treated as successful.

C only measures communications, sums counters and times across MPI ranks, and appends eight plain METRIC records to log.txt. The Slurm scripts use awk to write CSV and calculate per-message means. No per-rank CSV or JSON manifest is needed. No-sample means are blank, not zero. Per-type means equal total time divided by the matching completed-message count, not averages of per-rank averages. Existing historical logs and comparison workbooks are not changed.

Use rows/cols or stations for the graph x-axis, and mean_send_completion_us for software-observed send completion time. For each message type, combine the three repeated-run means and show variation. mean_send_call_us measures Send/Isend function time; Isend posting time is not transfer latency. Completion times include polling and waiting; DISABLE in particular includes shutdown coordination before the final Waitall. Clocks from different ranks are never subtracted. C4 count and coordinate payloads remain separate messages. All eight message types, including shutdown controls, are retained.

The simulation retains its original time/rank random initialization, 70% busy / 30% free model, fixed 30/2/3-second periods and confirmed shutdown. The configuration and alerts remain in log.txt. Different report-freshness windows are still required for Task 2; the mesh/line sweeps both use 1.5 seconds and do not replace those experiments. The earlier V1-V3 comparison remains separate from this V1 scaling study.
