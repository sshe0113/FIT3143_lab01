# Local validation (2026-10-20)

Environment: real Open MPI under local WSL Ubuntu. No CAAS job was submitted from this machine. Slurm submission was mocked only for testing command construction/resume/capacity handling; all simulation checks below used real MPI processes.

Functional checks:

- Default 3x3, full 30-second simulation: clean exit, calendar timestamps, report and candidate logs, balanced point-to-point counts/bytes and all shutdown acknowledgements.
- Synchronous-send stress: 3x3, 1x5, 5x1, 1x1 and 2x4, all ports busy.
- Threshold 2: reports produced; threshold 3: no congestion reports.
- 3x3 with four equal-nearest candidates: all four logged; available neighbours produce an explicit local EV referral.
- Distinct predefined windows 0.0001, 1.5 and 30 seconds: the very short window bypassed the recent-report shortcut; longer windows exercised it. These are accelerated fixed-state correctness checks, not the proposed normal CAAS period experiments.
- 5x9 (45 charging nodes + base), all busy with synchronous sends: sends/receives and bytes balanced, all nodes acknowledged shutdown.
- 5x7 with ten equal-nearest candidates: all ten returned/logged, confirming no eight-candidate cap.
- One real-MPI workflow run using a mocked srun launcher: source snapshot, run manifest, CSV aggregation and plotting checked. The small smoke run has incomplete message-type coverage, correctly represented as missing samples rather than zero delay.
- Final exact source compiled and passed a short 1x2 synchronous MPI smoke check after the MPI timer boundary was tightened.

Workflow checks:

- 30 mesh configurations, 30 line configurations and four period configurations generated.
- Mesh configurations have 30 distinct station counts, maximum 45 stations.
- Every submitted job requests 00:05:00 or less.
- CPU-capacity rejection tested: an insufficient allocation is reported and no configurations are silently omitted.
- Resume tested: after two mocked submissions, the next dry run skips those two and lists the remaining 28 mesh configurations.
- Python syntax, shell syntax, CSV aggregation and plot rendering checked.

The shutdown and protocol were exercised with MPI_Ssend / MPI_Issend in test builds, so the checks do not depend on eager message buffering. Production builds retain V1 MPI_Send / MPI_Isend.

Historical v1/v2/v3 logs and their Excel comparison remain baseline evidence. The new V1 revision changes report payload size, seed handling and shutdown; its timings should be collected as a separate experiment group.
