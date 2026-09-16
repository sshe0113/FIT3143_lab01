#!/bin/bash
#SBATCH --job-name=task3_N
#SBATCH --time=00:30:00
#SBATCH --nodes=8                 
#SBATCH --ntasks-per-node=16             
#SBATCH --partition=defq
#SBATCH --output=task3_N-%j.out

# 1. Load Modules
module load openmpi/4.1.5-gcc-11.2.0-ux65npg

# 2. Compile Implementations
echo "Compiling source files..."
mpicc -O3 task/task1_MPI.c -o compile/task1_MPI -lm
mpicc -O3 -fopenmp task/task2_Hybrid.c -o compile/task2_Hybrid -lm

BASE_DIR=$(pwd)
# Fix potential Windows line endings in wrapper scripts
sed -i 's/\r$//' "$BASE_DIR/caas_script/"*.sh

echo "--------------------------------------------------------"
echo "Task 3: Problem Size (N) Scaling - 128 Total Workers"
echo "--------------------------------------------------------"

# 1. Pure Open MPI (128 Processes)
# Arguments: <chunk_size> <processes>
echo "Running MPI Block-Cyclic..."
bash "$BASE_DIR/caas_script/task3_MPI_N.sh" 16 128
mv time/task3_MPI_N.csv time/task3_MPI_N_128W_64.csv

echo "Running MPI Cyclic..."
bash "$BASE_DIR/caas_script/task3_MPI_N.sh" 1 128
mv time/task3_MPI_N.csv time/task3_MPI_N_128W_1.csv

# 2. Hybrid (8 Processes x 16 Threads = 128 Total Workers)
# Arguments: <chunk_size> <fixed_processes> <threads>
echo "Running Hybrid Block-Cyclic..."
bash "$BASE_DIR/caas_script/task3_Hybrid_N.sh" 16 8 16
mv time/task3_Hybrid_N.csv time/task3_Hybrid_N_128W_64.csv

echo "Running Hybrid Cyclic..."
bash "$BASE_DIR/caas_script/task3_Hybrid_N.sh" 1 8 16
mv time/task3_Hybrid_N.csv time/task3_Hybrid_N_128W_1.csv

echo "Task 3 Problem Size scaling complete!"