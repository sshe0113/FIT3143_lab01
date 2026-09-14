#!/bin/bash

# Ensure the user provided a chunk size and at least one process count
if [ "$#" -lt 2 ]; then
    echo "Error: Missing arguments."
    echo "Usage: bash caas_script/task1_MPI_P.sh <chunk_size> <proc_count1> [proc_count2 ...]"
    echo "Example: bash caas_script/task1_MPI_P.sh 64 1 2 4 8 16 32"
    exit 1
fi

# Capture the chunk size, then shift it out of the array
CHUNK_SIZE=$1
shift

BASE_DIR=$(pwd)
PROGRAM="$BASE_DIR/compile/task1_MPI"

mkdir -p "$BASE_DIR/time"
CSV_FILE="$BASE_DIR/time/task1_MPI_P.csv"

# Initialize CSV headers
if [ ! -f "$CSV_FILE" ]; then
    echo "N,Processes,Computation_Time_sec,Communication_Time_sec,Overall_Time_sec" > "$CSV_FILE"
fi

# Set the fixed value for N
N=10000000

echo "Starting Open MPI Process Scaling Benchmark with N = $N and Chunk Size = $CHUNK_SIZE..."
echo "--------------------------------------------------------"

for PROCS in "$@"
do
    # Execute the MPI program, passing N and Chunk Size
    OUTPUT=$(srun -n $PROCS "$PROGRAM" $N $CHUNK_SIZE)
    
    # Isolate metrics
    COMP_TIME=$(echo "$OUTPUT" | grep "Computation Time:" | awk '{print $3}')
    COMM_TIME=$(echo "$OUTPUT" | grep "Communication Time:" | awk '{print $3}')
    OVERALL_TIME=$(echo "$OUTPUT" | grep "Overall Time:" | awk '{print $3}')
    
    echo "Tested N = $N | Procs: $PROCS | Comp: $COMP_TIME s | Comm: $COMM_TIME s | Overall: $OVERALL_TIME s"
    
    echo "$N,$PROCS,$COMP_TIME,$COMM_TIME,$OVERALL_TIME" >> "$CSV_FILE"
done

# Clean up
rm -f task1_OpenMPI.txt
echo "--------------------------------------------------------"
echo "Benchmarking complete! Data saved to $CSV_FILE."