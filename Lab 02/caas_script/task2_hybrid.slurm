#!/bin/bash
#SBATCH --job-name=hybrid
#SBATCH --time=00:30:00
#SBATCH --nodes=8         
#SBATCH --ntasks-per-node=16         
#SBATCH --partition=defq
#SBATCH --output=hybrid-%j.out

# 1. Load Modules
module load openmpi/4.1.5-gcc-11.2.0-ux65npg

# 2. Compile All Implementations
echo "Compiling all source files..."
mpicc -O3 task/task1_MPI.c -o compile/task1_MPI -lm
gcc -O3 -fopenmp task/task1_OpenMP.c -o compile/task1_OpenMP -lm
gcc -O3 -pthread task/task1_POSIX.c -o compile/task1_POSIX -lm
mpicc -O3 -fopenmp task/task2_Hybrid.c -o compile/task2_Hybrid -lm

BASE_DIR=$(pwd)
sed -i 's/\r$//' "$BASE_DIR/caas_script/"*.sh

echo "--------------------------------------------------------"
echo "Hybrid vs. Open MPI (Processes = 4)"
echo "--------------------------------------------------------"
# Arguments: <chunk_size> <fixed_procs> <thread1> <thread2> ...
bash "$BASE_DIR/caas_script/task2_Hybrid.sh" 64 4 1 2 4 8 16 32 64 128 # Block-Cyclic
mv time/task2_Hybrid.csv time/task2_Hybrid_64_T.csv
bash "$BASE_DIR/caas_script/task2_Hybrid.sh" 1 4 1 2 4 8 16 32 64 128 # Cyclic
mv time/task2_Hybrid.csv time/task2_Hybrid_1_T.csv

echo "--------------------------------------------------------"
echo "Hybrid vs. POSIX / OpenMP (Matching Total Workers)"
echo "--------------------------------------------------------"
# Hybrid Combinations (Processes x Threads = Total Workers)
bash "$BASE_DIR/caas_script/task2_Hybrid.sh" 64 1 1   # 1 Workers
bash "$BASE_DIR/caas_script/task2_Hybrid.sh" 64 1 2   # 2 Workers
bash "$BASE_DIR/caas_script/task2_Hybrid.sh" 64 2 2   # 4 Workers
bash "$BASE_DIR/caas_script/task2_Hybrid.sh" 64 2 4   # 8 Workers
bash "$BASE_DIR/caas_script/task2_Hybrid.sh" 64 4 4   # 16 Workers
bash "$BASE_DIR/caas_script/task2_Hybrid.sh" 64 4 8   # 32 Workers
bash "$BASE_DIR/caas_script/task2_Hybrid.sh" 64 8 8   # 64 Workers
bash "$BASE_DIR/caas_script/task2_Hybrid.sh" 64 8 16  # 128 Workers
mv time/task2_Hybrid.csv time/task2_Hybrid_64_W.csv

bash "$BASE_DIR/caas_script/task2_Hybrid.sh" 1 1 1   # 1 Workers
bash "$BASE_DIR/caas_script/task2_Hybrid.sh" 1 1 2   # 2 Workers
bash "$BASE_DIR/caas_script/task2_Hybrid.sh" 1 2 2   # 4 Workers
bash "$BASE_DIR/caas_script/task2_Hybrid.sh" 1 2 4   # 8 Workers
bash "$BASE_DIR/caas_script/task2_Hybrid.sh" 1 4 4   # 16 Workers
bash "$BASE_DIR/caas_script/task2_Hybrid.sh" 1 4 8   # 32 Workers
bash "$BASE_DIR/caas_script/task2_Hybrid.sh" 1 8 8   # 64 Workers
bash "$BASE_DIR/caas_script/task2_Hybrid.sh" 1 8 16  # 128 Workers
mv time/task2_Hybrid.csv time/task2_Hybrid_1_W.csv

echo "All benchmarks complete!"