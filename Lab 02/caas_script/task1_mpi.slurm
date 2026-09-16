#!/bin/bash
#SBATCH --job-name=mpi
#SBATCH --time=00:20:00
#SBATCH --nodes=8                 ### 8 nodes x 16 cores = 128 maximum processes
#SBATCH --ntasks=128              ### Matches your max tested processes
#SBATCH --cpus-per-task=1         
#SBATCH --partition=defq
#SBATCH --output=mpi-%j.out

module load openmpi/4.1.5-gcc-11.2.0-ux65npg
mpicc -O3 task/task1_MPI.c -o compile/task1_MPI -lm

BASE_DIR=$(pwd)

sed -i 's/\r$//' "$BASE_DIR/caas_script/"*.sh

echo "Starting CAAS MPI Benchmarks for up to 128 cores..."

echo "--------------------------------------------------------"
echo "1. Running N Scaling Benchmark"
echo "--------------------------------------------------------"
bash "$BASE_DIR/caas_script/task1_MPI_N.sh" 1 16 # Chunk size of 1
mv time/task1_MPI_N.csv time/task1_MPI_N_1.csv
bash "$BASE_DIR/caas_script/task1_MPI_N.sh" 16 16 # Chunk size of 16
mv time/task1_MPI_N.csv time/task1_MPI_N_64.csv

echo "--------------------------------------------------------"
echo "2. Running Process Scaling Benchmark"
echo "--------------------------------------------------------"
bash "$BASE_DIR/caas_script/task1_MPI_P.sh" 1 1 2 4 8 16 32 64 128
mv time/task1_MPI_P.csv time/task1_MPI_P_1.csv
bash "$BASE_DIR/caas_script/task1_MPI_P.sh" 16 1 2 4 8 16 32 64 128
mv time/task1_MPI_P.csv time/task1_MPI_P_64.csv

echo "MPI benchmarks complete!"