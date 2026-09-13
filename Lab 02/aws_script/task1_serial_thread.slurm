#!/bin/bash
#SBATCH --job-name=aws_serial_thread
#SBATCH --time=01:00:00           
#SBATCH --nodes=1                 ### Must be exactly 1 for shared memory
#SBATCH --ntasks=1                
#SBATCH --cpus-per-task=2         ### Lowered to the AWS physical limit
#SBATCH --output=aws_serial_thread-%j.out

# Lock onto the current directory
BASE_DIR=$(pwd)

# Ensure OpenMP uses the allocated hardware threads
export OMP_NUM_THREADS=$SLURM_CPUS_PER_TASK

echo "Starting Unified Benchmarks on 2-core AWS node..."

echo "--------------------------------------------------------"
echo "1. Running Serial Benchmark"
echo "--------------------------------------------------------"
bash "$BASE_DIR/aws_script/task1_Serial.sh"

echo "--------------------------------------------------------"
echo "2. Running POSIX Benchmarks"
echo "--------------------------------------------------------"
bash "$BASE_DIR/aws_script/task1_POSIX_N.sh" 2
bash "$BASE_DIR/aws_script/task1_POSIX_T.sh" 1 2 4 8 16

echo "--------------------------------------------------------"
echo "3. Running OpenMP Benchmarks"
echo "--------------------------------------------------------"
bash "$BASE_DIR/aws_script/task1_OpenMP_N.sh" 2
bash "$BASE_DIR/aws_script/task1_OpenMP_T.sh" 1 2 4 8 16

echo "All shared-memory benchmarks complete!"