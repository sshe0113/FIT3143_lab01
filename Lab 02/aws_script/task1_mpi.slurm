#!/bin/bash
#SBATCH --job-name=mpi
#SBATCH --partition=compute
#SBATCH --nodes=4                 ### 4 AWS nodes
#SBATCH --ntasks=8                ### 8 total processes maximum
#SBATCH --ntasks-per-node=2       ### 2 cores per AWS node
#SBATCH --time=00:30:00
#SBATCH --output=mpi-%j.out

BASE_DIR=$(pwd)
# Clean Windows line endings to prevent execution errors
sed -i 's/\r$//' "$BASE_DIR/aws_script/"*.sh

# Compile with Optimization Level 3
mpicc -O3 task/task1_MPI.c -o compile/task1_MPI -lm

echo "Starting AWS MPI Benchmarks"

echo "--------------------------------------------------------"
echo "1. Running N Scaling Benchmark (Processes = 2)"
echo "--------------------------------------------------------"
bash "$BASE_DIR/aws_script/task1_MPI_N.sh" 1 2
mv time/task1_MPI_N.csv time/task1_MPI_N_1.csv
bash "$BASE_DIR/aws_script/task1_MPI_N.sh" 64 2
mv time/task1_MPI_N.csv time/task1_MPI_N_64.csv

echo "--------------------------------------------------------"
echo "2. Running Process Scaling Benchmark (Processes = 1 - 8)"
echo "--------------------------------------------------------"
bash "$BASE_DIR/aws_script/task1_MPI_P.sh" 1 1 2 4 6 8
mv time/task1_MPI_P.csv time/task1_MPI_P_1.csv
bash "$BASE_DIR/aws_script/task1_MPI_P.sh" 64 1 2 4 6 8
mv time/task1_MPI_P.csv time/task1_MPI_P_64.csv

echo "AWS MPI benchmarks complete!"