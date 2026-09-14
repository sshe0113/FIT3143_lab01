#!/bin/bash

# Ensure the user provided a chunk size and at least one process count
if [ "$#" -lt 2 ]; then
    echo "Error: Missing arguments."
    echo "Usage: bash caas_script/task1_MPI_N.sh <chunk_size> <proc_count1> [proc_count2 ...]"
    echo "Example: bash caas_script/task1_MPI_N.sh 64 2 4 8 16"
    exit 1
fi

# Capture the chunk size, then shift it out of the array
CHUNK_SIZE=$1
shift

BASE_DIR=$(pwd)
PROGRAM="$BASE_DIR/compile/task1_MPI"

mkdir -p "$BASE_DIR/time"
CSV_FILE="$BASE_DIR/time/task1_MPI_N.csv"

# Initialize CSV with the three distinct time headers
if [ ! -f "$CSV_FILE" ]; then
    echo "N,Processes,Computation_Time_sec,Communication_Time_sec,Overall_Time_sec" > "$CSV_FILE"
fi

echo "Starting Open MPI Benchmark for N scaling with Chunk Size = $CHUNK_SIZE..."

# Loop through every process count provided by the user
for PROCS in "$@"
do
    echo "--------------------------------------------------------"
    echo "Starting tests for PROCESSES = $PROCS"
    echo "--------------------------------------------------------"
    
    # Loop through N from 10,000,000 to 40,000,000 in steps of 1,000,000
    for N in $(seq 10000000 1000000 40000000)
    do
        # Execute the MPI program, passing N and Chunk Size
        OUTPUT=$(srun -n $PROCS "$PROGRAM" $N $CHUNK_SIZE)
        
        # Extract the precise metrics using grep and awk
        COMP_TIME=$(echo "$OUTPUT" | grep "Computation Time:" | awk '{print $3}')
        COMM_TIME=$(echo "$OUTPUT" | grep "Communication Time:" | awk '{print $3}')
        OVERALL_TIME=$(echo "$OUTPUT" | grep "Overall Time:" | awk '{print $3}')
        
        echo "Tested N = $N | Procs: $PROCS | Comp: $COMP_TIME s | Comm: $COMM_TIME s | Overall: $OVERALL_TIME s"
        
        # Append the data securely to the CSV
        echo "$N,$PROCS,$COMP_TIME,$COMM_TIME,$OVERALL_TIME" >> "$CSV_FILE"
    done
done

# Clean up the output text file to save cluster storage
rm -f task1_OpenMPI.txt
echo "--------------------------------------------------------"
echo "Benchmarking complete! Data saved to $CSV_FILE."