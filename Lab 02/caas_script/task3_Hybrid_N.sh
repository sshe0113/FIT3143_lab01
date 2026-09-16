#!/bin/bash
# Usage: bash task3_Hybrid_N.sh <chunk_size> <fixed_processes> <threads>
module load openmpi/4.1.5-gcc-11.2.0-ux65npg

CHUNK_SIZE=$1
FIXED_PROCS=$2
THREADS=$3
PROGRAM="$(pwd)/compile/task2_Hybrid"
CSV_FILE="$(pwd)/time/task3_Hybrid_N.csv"

# Initialize CSV headers
if [ ! -f "$CSV_FILE" ]; then
    echo "N,Processes,Threads,Total_Workers,Computational_Time_sec,Communication_Time_sec,Overall_Time_sec" > "$CSV_FILE"
fi

TOTAL_WORKERS=$((FIXED_PROCS * THREADS))
echo "Starting Hybrid N Scaling (10M to 40M) with $TOTAL_WORKERS Total Workers..."

# Loop N from 10,000,000 to 40,000,000 in steps of 1,000,000
for N in $(seq 10000000 1000000 40000000); do
    OUTPUT=$(srun -n $FIXED_PROCS --cpu-bind=none "$PROGRAM" $N $CHUNK_SIZE $THREADS)
    
    COMP_TIME=$(echo "$OUTPUT" | grep "Computational Time:" | awk '{print $3}')
    COMM_TIME=$(echo "$OUTPUT" | grep "Communication Time:" | awk '{print $3}')
    OVERALL_TIME=$(echo "$OUTPUT" | grep "Overall Time:" | awk '{print $3}')
    
    echo "Tested N = $N | Procs: $FIXED_PROCS | Threads: $THREADS | Comp: $COMP_TIME s"
    echo "$N,$FIXED_PROCS,$THREADS,$TOTAL_WORKERS,$COMP_TIME,$COMM_TIME,$OVERALL_TIME" >> "$CSV_FILE"
done