#!/bin/bash
# Usage: bash task3_MPI_N.sh <chunk_size> <processes>
module load openmpi/4.1.5-gcc-11.2.0-ux65npg

CHUNK_SIZE=$1
PROCS=$2
PROGRAM="$(pwd)/compile/task1_MPI"
CSV_FILE="$(pwd)/time/task3_MPI_N.csv"

# Initialize CSV headers
if [ ! -f "$CSV_FILE" ]; then
    echo "N,Processes,Computation_Time_sec,Communication_Time_sec,Overall_Time_sec" > "$CSV_FILE"
fi

echo "Starting Open MPI N Scaling (10M to 40M) with $PROCS Processes..."

# Loop N from 10,000,000 to 40,000,000 in steps of 1,000,000
for N in $(seq 10000000 1000000 40000000); do
    OUTPUT=$(srun -n $PROCS --cpu-bind=none "$PROGRAM" $N $CHUNK_SIZE)
    
    COMP_TIME=$(echo "$OUTPUT" | grep "Computation Time:" | awk '{print $3}')
    COMM_TIME=$(echo "$OUTPUT" | grep "Communication Time:" | awk '{print $3}')
    OVERALL_TIME=$(echo "$OUTPUT" | grep "Overall Time:" | awk '{print $3}')
    
    echo "Tested N = $N | Procs: $PROCS | Comp: $COMP_TIME s"
    echo "$N,$PROCS,$COMP_TIME,$COMM_TIME,$OVERALL_TIME" >> "$CSV_FILE"
done