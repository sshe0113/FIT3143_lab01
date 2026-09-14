#!/bin/bash

# Ensure the user provided process counts
if [ "$#" -eq 0 ]; then
    echo "Error: No process count provided."
    echo "Usage: bash script/task1_OpenMP_T.sh <proc_count1> [proc_count2 ...]"
    echo "Example: bash script/task1_OpenMP_T.sh 1 2 4 8 16 32"
    exit 1
fi

BASE_DIR=$(pwd)
# UPDATED: Pointing to the compile directory
PROGRAM="$BASE_DIR/compile/task1_OpenMP"

mkdir -p "$BASE_DIR/time"
CSV_FILE="$BASE_DIR/time/task1_OpenMP_T.csv"

# Initialize CSV headers
if [ ! -f "$CSV_FILE" ]; then
    echo "N,Processes,Computational_Time_sec,Overall_Time_sec" > "$CSV_FILE"
fi

# Set the fixed value for N
N=10000000

echo "Starting Open OpenMP Process Scaling Benchmark with N = $N..."
echo "--------------------------------------------------------"

for PROCS in "$@"
do
    export OMP_NUM_THREADS=$PROCS
    OUTPUT=$("$PROGRAM" $N $PROCS)
    
    # Isolate metrics
    COMP_TIME=$(echo "$OUTPUT" | grep "Computational Time:" | awk '{print $3}')
    OVERALL_TIME=$(echo "$OUTPUT" | grep "Overall Time:" | awk '{print $3}')
    
    echo "Tested N = $N | Procs: $PROCS | Comp: $COMP_TIME s | Overall: $OVERALL_TIME s"
    
    echo "$N,$PROCS,$COMP_TIME,$OVERALL_TIME" >> "$CSV_FILE"
done

# Clean up
rm -f task1_OpenMP.txt
echo "--------------------------------------------------------"
echo "Benchmarking complete! Data saved to $CSV_FILE."