#!/bin/bash
#SBATCH --job-name=serial_thread
#SBATCH --partition=compute
#SBATCH --nodes=1                 ### 1 Node for shared memory
#SBATCH --ntasks=1                
#SBATCH --cpus-per-task=2         ### Limited to 2 physical cores on AWS
#SBATCH --time=00:30:00
#SBATCH --output=serial_thread-%j.out

BASE_DIR=$(pwd)
sed -i 's/\r$//' "$BASE_DIR/aws_script/"*.sh

# Compile with Optimization Level 3
gcc -O3 task/task1_Serial.c -o compile/task1_Serial -lm
gcc -O3 task/task1_POSIX.c -o compile/task1_POSIX -lm -lpthread
gcc -O3 task/task1_OpenMP.c -o compile/task1_OpenMP -lm -fopenmp

# Ensure OpenMP uses the allocated hardware threads
export OMP_NUM_THREADS=$SLURM_CPUS_PER_TASK

echo "Starting AWS Serial, POSIX, OpenMP Benchmarks"

echo "--------------------------------------------------------"
echo "1. Running Serial Benchmark"
echo "--------------------------------------------------------"
bash "$BASE_DIR/aws_script/task1_Serial.sh"

echo "--------------------------------------------------------"
echo "2. Running POSIX Benchmarks"
echo "--------------------------------------------------------"
bash "$BASE_DIR/aws_script/task1_POSIX_N.sh" 2
bash "$BASE_DIR/aws_script/task1_POSIX_T.sh" 1 2 4 6 8

echo "--------------------------------------------------------"
echo "3. Running OpenMP Benchmarks"
echo "--------------------------------------------------------"
bash "$BASE_DIR/aws_script/task1_OpenMP_N.sh" 2
bash "$BASE_DIR/aws_script/task1_OpenMP_T.sh" 1 2 4 6 8

echo "AWS Shared-memory benchmarks complete!"