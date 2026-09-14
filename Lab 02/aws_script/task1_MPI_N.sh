#!/bin/bash
if [ "$#" -lt 2 ]; then
    echo "Usage: bash aws_script/task1_MPI_N.sh <chunk_size> <proc_count1> [proc_count2 ...]"
    exit 1
fi

CHUNK_SIZE=$1
shift
BASE_DIR=$(pwd)
PROGRAM="$BASE_DIR/compile/task1_MPI"
mkdir -p "$BASE_DIR/time"
CSV_FILE="$BASE_DIR/time/task1_MPI_N.csv"

if [ ! -f "$CSV_FILE" ]; then
    echo "N,Processes,Computation_Time_sec,Communication_Time_sec,Overall_Time_sec" > "$CSV_FILE"
fi

for PROCS in "$@"
do
    for N in $(seq 10000000 1000000 40000000)
    do
        # Replaced srun with mpirun for AWS compatibility
        OUTPUT=$(mpirun -np $PROCS "$PROGRAM" $N $CHUNK_SIZE)
        
        COMP_TIME=$(echo "$OUTPUT" | grep "Computation Time:" | awk '{print $3}')
        COMM_TIME=$(echo "$OUTPUT" | grep "Communication Time:" | awk '{print $3}')
        OVERALL_TIME=$(echo "$OUTPUT" | grep "Overall Time:" | awk '{print $3}')
        
        echo "Tested N = $N | Procs: $PROCS | Comp: $COMP_TIME s | Comm: $COMM_TIME s | Overall: $OVERALL_TIME s"
        echo "$N,$PROCS,$COMP_TIME,$COMM_TIME,$OVERALL_TIME" >> "$CSV_FILE"
    done
done
rm -f task1_OpenMPI.txt