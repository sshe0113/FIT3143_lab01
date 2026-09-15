#!/bin/bash

# Ensure the user provided chunk size, fixed process count, and at least one thread count
if [ "$#" -lt 3 ]; then
    echo "Error: Missing arguments."
    echo "Usage: bash task2_Hybrid.sh <chunk_size> <fixed_processes> <thread_count1> [thread_count2 ...]"
    echo "Example: bash task2_Hybrid.sh 64 4 1 2 4 8 16 32"
    exit 1
fi

# Capture the first two arguments, then shift them out of the array
CHUNK_SIZE=$1
FIXED_PROCS=$2
shift 2

BASE_DIR=$(pwd)
PROGRAM="$BASE_DIR/compile/task2_Hybrid"

mkdir -p "$BASE_DIR/time"
CSV_FILE="$BASE_DIR/time/task2_Hybrid.csv"

# Initialize CSV headers with Thread and Total Worker columns
if [ ! -f "$CSV_FILE" ]; then
    echo "N,Processes,Threads_Per_Proc,Total_Workers,Computational_Time_sec,Communication_Time_sec,Overall_Time_sec" > "$CSV_FILE"
fi

# Set the fixed value for N
N=10000000

echo "Starting Hybrid Scaling Benchmark with N = $N and Chunk Size = $CHUNK_SIZE..."
echo "Fixed MPI Processes: $FIXED_PROCS"
echo "--------------------------------------------------------"

# Loop through all the thread counts provided by the user
for THREADS in "$@"
do
    TOTAL_WORKERS=$((FIXED_PROCS * THREADS))
    
    # Execute the Hybrid program via SLURM with cpu-bind set to none.
    OUTPUT=$(srun -n $FIXED_PROCS --cpu-bind=none "$PROGRAM" $N $CHUNK_SIZE $THREADS)
    
    # Isolate metrics (Note: task2 prints "Computational Time", not "Computation Time")
    COMP_TIME=$(echo "$OUTPUT" | grep "Computational Time:" | awk '{print $3}')
    COMM_TIME=$(echo "$OUTPUT" | grep "Communication Time:" | awk '{print $3}')
    OVERALL_TIME=$(echo "$OUTPUT" | grep "Overall Time:" | awk '{print $3}')
    
    echo "Tested N = $N | Procs: $FIXED_PROCS | Threads: $THREADS | Total Workers: $TOTAL_WORKERS | Comp: $COMP_TIME s"
    
    echo "$N,$FIXED_PROCS,$THREADS,$TOTAL_WORKERS,$COMP_TIME,$COMM_TIME,$OVERALL_TIME" >> "$CSV_FILE"
done

# Clean up the output text file so it doesn't clutter the server
rm -f task2_Hybrid.txt
echo "--------------------------------------------------------"
echo "Benchmarking complete! Data saved to $CSV_FILE."