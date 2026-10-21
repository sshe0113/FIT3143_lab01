Predefined period experiment (Task 2)

Upload run_predefined_period.slurm beside the existing evcns_v2.c, utils.c,
and utils.h in the CAAS task3_caas_upload directory. The C code is unchanged.
Run:
    sbatch run_predefined_period.slurm

One job requests 3 nodes, 17 MPI processes, and 2 CPUs per process.
The unchanged application has 16 charging stations, each with 7 OpenMP threads,
and one base process. Three sequential simulations take at least 90 seconds
of simulation time, plus compilation/startup/shutdown. The job limit is 5 minutes.
The MPI module default matches the existing Task 3 scripts. If another module
is already loaded, submit with EVCNS_MPI_MODULE='' in the environment.

Tests: V2, 4x4 mesh, periods 1.5 / 3 / 6 seconds, one run per period.
Other simulation settings are held fixed. Period changes are passed through
EVCNS_PRED_PERIOD; each log is checked to confirm the requested value.

Outputs in a new results_period_XXXXXX directory:
- period_results.csv: one comparison row per period.
- period_1.5/, period_3/, period_6/: raw log.txt, stdout.txt, stderr.txt, PASSED.txt.
- all_periods.log: combined application logs for all three runs.
- settings.txt, source_sha256.txt, allocation.txt, compiler.txt, mpi.txt,
  build.txt, source copies and the executable: experiment provenance.
- COMPLETE.txt only after all three runs validate; FAILED.txt on detected failure.
- ev-period-JOBID.out in the submission directory: Slurm/script progress/errors.

Interpretation:
recent_neighbour_shortcuts counts decisions where all existing neighbours sent
their own reports within the chosen period, so base skips searching and returns
no candidates. reports_with/without_recommendations count base decisions, not
individual recommended nodes. total_recommended_nodes sums list lengths;
it is not a count of distinct stations. The raw logs retain every candidate list.

A longer period keeps information valid longer. It can change both the shortcut
decision and candidate expiry, affecting recommendation lists and reply payloads.
It does not change the port update/check intervals or the 30-second simulation.
Actual availability of unknown/expired candidates is estimated by the program.
MPI call time excludes some MPI/progress overhead and is not delivery latency.
The independent random workloads differ, so treat comparisons as illustrative,
not a statistically established causal effect of period alone.
