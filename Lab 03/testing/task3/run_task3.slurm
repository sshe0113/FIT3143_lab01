#!/bin/bash
# Resource counts are supplied by submit_task3.py after reading node CPU capacity.
#SBATCH --job-name=evcns_task3
#SBATCH --time=00:05:00
#SBATCH --partition=defq
#SBATCH --mem=16G
set -euo pipefail
module load openmpi/4.1.5-gcc-11.2.0-ux65npg
export OMP_NUM_THREADS=7
export OMP_DYNAMIC=FALSE
export OMP_PLACES=cores
export OMP_PROC_BIND=close
# submit_task3.py passes --chdir to this script's original directory. Slurm
# executes a spool copy, so BASH_SOURCE[0] must not be used to locate Python files.
python3 ./run_experiment.py "$@"
