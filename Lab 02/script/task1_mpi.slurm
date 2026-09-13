#!/bin/bash
#SBATCH --job-name=mpi
#SBATCH --time=02:00:00
#SBATCH --mem-per-cpu=1G          
#SBATCH --nodes=8                 ### 8 nodes x 16 cores = 128 maximum processes
#SBATCH --ntasks=128              ### Matches your max tested processes
#SBATCH --cpus-per-task=1         
#SBATCH --partition=defq
#SBATCH --output=mpi-%j.out

module load openmpi/4.1.5-gcc-11.2.0-ux65npg

BASE_DIR=$(pwd)

echo "Starting CAAS MPI Benchmarks for up to 128 cores..."

echo "--------------------------------------------------------"
echo "1. Running N Scaling Benchmark"
echo "--------------------------------------------------------"
bash "$BASE_DIR/script/task1_MPI_N.sh" 8 32

echo "--------------------------------------------------------"
echo "2. Running Process Scaling Benchmark"
echo "--------------------------------------------------------"
bash "$BASE_DIR/script/task1_MPI_P.sh" 1 2 4 8 16 32 64 128

echo "MPI benchmarks complete!"