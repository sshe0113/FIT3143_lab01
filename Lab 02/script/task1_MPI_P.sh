#!/bin/bash

# Ensure the user provided process counts
if [ "$#" -eq 0 ]; then
    echo "Error: No process count provided."
    echo "Usage: bash script/task1_MPI_P.sh <proc_count1> [proc_count2 ...]"
    echo "Example: bash script/task1_MPI_P.sh 1 2 4 8 16 32"
    exit 1
fi

BASE_DIR=$(pwd)
PROGRAM="$BASE_DIR/task1_MPI_v1"

mkdir -p "$BASE_DIR/time"
CSV_FILE="$BASE_DIR/time/task1_MPI_v1_P.csv"

# Initialize CSV headers
if [ ! -f "$CSV_FILE" ]; then
    echo "N,Processes,Computation_Time_sec,Communication_Time_sec,Overall_Time_sec" > "$CSV_FILE"
fi

# Set the fixed value for N
N=10000000

echo "Starting Open MPI Process Scaling Benchmark with N = $N..."
echo "--------------------------------------------------------"

for PROCS in "$@"
do
    OUTPUT=$(mpirun -np $PROCS "$PROGRAM" $N)
    
    # Isolate metrics
    COMP_TIME=$(echo "$OUTPUT" | grep "Computation Time:" | awk '{print $3}')
    COMM_TIME=$(echo "$OUTPUT" | grep "Communication Time:" | awk '{print $3}')
    OVERALL_TIME=$(echo "$OUTPUT" | grep "Overall Time:" | awk '{print $3}')
    
    echo "Tested N = $N | Procs: $PROCS | Comp: $COMP_TIME s | Comm: $COMM_TIME s | Overall: $OVERALL_TIME s"
    
    echo "$N,$PROCS,$COMP_TIME,$COMM_TIME,$OVERALL_TIME" >> "$CSV_FILE"
done

# Clean up
rm -f task1_MPI.txt
echo "--------------------------------------------------------"
echo "Benchmarking complete! Data saved to $CSV_FILE."