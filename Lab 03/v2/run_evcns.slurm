#!/bin/bash
#SBATCH --job-name=mesh_3x3_v2
#SBATCH --time=00:05:00
#SBATCH --mem=16G
#SBATCH --ntasks=10
#SBATCH --cpus-per-task=7
#SBATCH --ntasks-per-node=2
#SBATCH --partition=defq

module load openmpi/4.1.5-gcc-11.2.0-ux65npg

mpicc -fopenmp -o evcns evcns.c || exit $?

export OMP_NUM_THREADS=$SLURM_CPUS_PER_TASK

echo "===== Starting EVCNS V2 Blocking Simulation: 3x3 Mesh ====="
srun ./evcns 3 3 || exit $?
echo "===== Simulation Completed ====="