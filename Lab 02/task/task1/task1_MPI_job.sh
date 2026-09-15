#!/bin/bash
#SBATCH --job-name=task1_MPI_P_1_2_4_8_16_32_128
#SBATCH --time=00:10:00
#SBATCH --mem=16G
#SBATCH --nodes=8
#SBATCH --ntasks=128
#SBATCH --cpus-per-task=1
#SBATCH --ntasks-per-node=16
#SBATCH --partition=defq
#SBATCH --output=task1_MPI_P_%j.out
#SBATCH --error=task1_MPI_P_%j.err

module load openmpi/4.1.5-gcc-11.2.0-ux65npg

SOURCE="task1_MPI.c"
PROGRAM="./task1_MPI_Output"
CSV_FILE="task1_MPI_P_1_2_4_8_16_32_128.csv"
FIXED_N=40000000
CHUNK_SIZE=64
BALANCE_CSV="task1_MPI_balance.csv"

echo "N,Processes,Chunk_Size,Computation_Time_sec,Communication_Time_sec,Overall_Time_sec" > "$CSV_FILE"
echo "Processes,Rank,Jobs,Computation_Time_sec" > "$BALANCE_CSV"

for PROCESSES in 1 2 4 8 16 32 128; do
    echo "Running P=$PROCESSES..."

    RUN_NODES=$(((PROCESSES + 15) / 16))

    if ! OUTPUT=$(srun --nodes="$RUN_NODES" --ntasks="$PROCESSES" \
        --cpus-per-task=1 --cpu-bind=cores \
        "$PROGRAM" "$FIXED_N" "$CHUNK_SIZE" 2>&1); then
        echo "$OUTPUT"
        echo "Error: Task 1 failed for P=$PROCESSES."
        exit 1
    fi

    COMP_TIME=$(printf '%s\n' "$OUTPUT" | awk '/^Computation Time:/ {print $3; exit}')
    COMM_TIME=$(printf '%s\n' "$OUTPUT" | awk '/^Communication Time:/ {print $3; exit}')
    OVERALL_TIME=$(printf '%s\n' "$OUTPUT" | awk '/^Overall Time:/ {print $3; exit}')

    if [ -z "$COMP_TIME" ] || [ -z "$COMM_TIME" ] || [ -z "$OVERALL_TIME" ]; then
        echo "$OUTPUT"
        echo "Error: unable to extract Task 1 timings for P=$PROCESSES."
        exit 1
    fi

    echo "$FIXED_N,$PROCESSES,$CHUNK_SIZE,$COMP_TIME,$COMM_TIME,$OVERALL_TIME" >> "$CSV_FILE"
    echo "Completed P=$PROCESSES | computation=$COMP_TIME s | communication=$COMM_TIME s | overall=$OVERALL_TIME s"
    printf '%s\n' "$OUTPUT" |
        awk -F',' '/^RANK_STATS,/ {
            print $2","$3","$4","$5
        }' >> "$BALANCE_CSV"
    rm -f task1_OpenMPI.txt
done

echo "Task 1 process-scaling benchmark completed. Results saved to $CSV_FILE and $BALANCE_CSV."