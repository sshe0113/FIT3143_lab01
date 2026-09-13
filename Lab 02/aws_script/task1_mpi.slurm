#!/bin/bash
#SBATCH --job-name=aws_mpi
#SBATCH --time=02:00:00
#SBATCH --nodes=8                 ### Requesting all 8 AWS nodes
#SBATCH --ntasks=16               ### 8 nodes x 2 cores = 16 physical cores
#SBATCH --output=aws_mpi-%j.out

BASE_DIR=$(pwd)

echo "Starting AWS MPI Benchmarks for up to 128 cores..."

echo "--------------------------------------------------------"
echo "1. Running N Scaling Benchmark"
echo "--------------------------------------------------------"
bash "$BASE_DIR/aws_script/task1_MPI_N.sh" 2

echo "--------------------------------------------------------"
echo "2. Running Process Scaling Benchmark"
echo "--------------------------------------------------------"
bash "$BASE_DIR/aws_script/task1_MPI_P.sh" 1 2 4 8 16

echo "MPI benchmarks complete!"