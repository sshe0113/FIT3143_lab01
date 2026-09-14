#!/bin/bash
#SBATCH --job-name=serial_thread
#SBATCH --time=00:20:00           
#SBATCH --mem=16G                 
#SBATCH --nodes=1
#SBATCH --ntasks=1                
#SBATCH --cpus-per-task=16
#SBATCH --partition=defq
#SBATCH --output=serial_thread-%j.out

# Lock onto the current directory
BASE_DIR=$(pwd)

sed -i 's/\r$//' "$BASE_DIR/caas_script/"*.sh

gcc -O3 task/task1_Serial.c -o compile/task1_Serial -lm  
gcc -O3 task/task1_POSIX.c -o compile/task1_POSIX -lm -lpthread  
gcc -O3 task/task1_OpenMP.c -o compile/task1_OpenMP -lm -fopenmp

# Ensure OpenMP uses the allocated hardware threads
export OMP_NUM_THREADS=$SLURM_CPUS_PER_TASK

echo "Starting Unified Benchmarks on 16-core node..."

echo "--------------------------------------------------------"
echo "1. Running Serial Benchmark"
echo "--------------------------------------------------------"
bash "$BASE_DIR/caas_script/task1_Serial.sh"

echo "--------------------------------------------------------"
echo "2. Running POSIX Benchmarks"
echo "--------------------------------------------------------"
bash "$BASE_DIR/caas_script/task1_POSIX_N.sh" 16
bash "$BASE_DIR/caas_script/task1_POSIX_T.sh" 1 2 4 8 16 32 64 128

echo "--------------------------------------------------------"
echo "3. Running OpenMP Benchmarks"
echo "--------------------------------------------------------"
bash "$BASE_DIR/caas_script/task1_OpenMP_N.sh" 16
bash "$BASE_DIR/caas_script/task1_OpenMP_T.sh" 1 2 4 8 16 32 64 128

echo "All shared-memory benchmarks complete!"