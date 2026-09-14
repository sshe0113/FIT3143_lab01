#!/bin/bash

# Ensure the user provided at least one process count argument
if [ "$#" -eq 0 ]; then
    echo "Error: No process count provided."
    echo "Usage: bash script/task1_OpenMP_N.sh <proc_count1> [proc_count2 ...]"
    echo "Example: bash script/task1_OpenMP_N.sh 2 4 8 16"
    exit 1
fi

BASE_DIR=$(pwd)
# UPDATED: Pointing to the compile directory
PROGRAM="$BASE_DIR/compile/task1_OpenMP"

mkdir -p "$BASE_DIR/time"
CSV_FILE="$BASE_DIR/time/task1_OpenMP_N.csv"

# Initialize CSV with the three distinct time headers
if [ ! -f "$CSV_FILE" ]; then
    echo "N,Processes,Computational_Time_sec,Overall_Time_sec" > "$CSV_FILE"
fi

echo "Starting Open OpenMP Benchmark for N scaling..."

# Loop through every process count provided by the user
for PROCS in "$@"
do
    export OMP_NUM_THREADS=$PROCS

    echo "--------------------------------------------------------"
    echo "Starting tests for THREADS = $PROCS"
    echo "--------------------------------------------------------"
    
    # Loop through N from 10,000,000 to 40,000,000 in steps of 1,000,000
    for N in $(seq 10000000 1000000 40000000)
    do
        OUTPUT=$("$PROGRAM" $N $PROCS)
        
        # Extract the precise metrics using grep and awk
        COMP_TIME=$(echo "$OUTPUT" | grep "Computational Time:" | awk '{print $3}')
        OVERALL_TIME=$(echo "$OUTPUT" | grep "Overall Time:" | awk '{print $3}')
        
        echo "Tested N = $N | Procs: $PROCS | Comp: $COMP_TIME s | Overall: $OVERALL_TIME s"
        
        # Append the data securely to the CSV
        echo "$N,$PROCS,$COMP_TIME,$OVERALL_TIME" >> "$CSV_FILE"
    done
done

# Clean up the output text file to save cluster storage
rm -f task1_OpenMP.txt
echo "--------------------------------------------------------"
echo "Benchmarking complete! Data saved to $CSV_FILE."