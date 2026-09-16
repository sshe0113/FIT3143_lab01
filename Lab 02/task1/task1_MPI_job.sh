#!/bin/bash
#SBATCH --job-name=task1_MPI_chunks
#SBATCH --time=00:10:00
#SBATCH --mem=16G
#SBATCH --nodes=8
#SBATCH --ntasks=128
#SBATCH --cpus-per-task=1
#SBATCH --partition=defq
#SBATCH --output=task1_MPI_chunks_%j.out
#SBATCH --error=task1_MPI_chunks_%j.err

module load openmpi/4.1.5-gcc-11.2.0-ux65npg

SOURCE="task1_MPI.c"
PROGRAM="./task1_MPI_Output"
FIXED_N=40000000

if ! mpicc -O3 -Wall -Wextra "$SOURCE" -o "$PROGRAM" -lm; then
    echo "Error: $SOURCE failed to compile."
    exit 1
fi

for CHUNK_SIZE in 1 16 32 64; do
    CSV_FILE="task1_MPI_P_${CHUNK_SIZE}.csv"
    BALANCE_CSV="task1_MPI_balance_${CHUNK_SIZE}.csv"

    echo "N,Processes,Chunk_Size,Computation_Time_sec,Communication_Time_sec,Overall_Time_sec" > "$CSV_FILE"
    echo "Processes,Rank,Jobs,Computation_Time_sec" > "$BALANCE_CSV"

    echo "========================================================"
    echo "Starting tests with chunk size $CHUNK_SIZE"
    echo "========================================================"

    for PROCESSES in 1 2 4 8 16 32 64; do
        echo "Running chunk=$CHUNK_SIZE with P=$PROCESSES..."

        RUN_NODES=$(((PROCESSES + 15) / 16))

        if ! OUTPUT=$(srun --nodes="$RUN_NODES" \
            --ntasks="$PROCESSES" \
            --ntasks-per-node=16 \
            --cpus-per-task=1 \
            --cpu-bind=cores \
            "$PROGRAM" "$FIXED_N" "$CHUNK_SIZE" 2>&1); then
            echo "$OUTPUT"
            echo "Error: Task 1 failed for chunk=$CHUNK_SIZE, P=$PROCESSES."
            exit 1
        fi

        COMP_TIME=$(printf '%s\n' "$OUTPUT" |
            awk '/^Computation Time:/ {print $3; exit}')
        COMM_TIME=$(printf '%s\n' "$OUTPUT" |
            awk '/^Communication Time:/ {print $3; exit}')
        OVERALL_TIME=$(printf '%s\n' "$OUTPUT" |
            awk '/^Overall Time:/ {print $3; exit}')

        if [ -z "$COMP_TIME" ] ||
           [ -z "$COMM_TIME" ] ||
           [ -z "$OVERALL_TIME" ]; then
            echo "$OUTPUT"
            echo "Error: unable to extract results for chunk=$CHUNK_SIZE, P=$PROCESSES."
            exit 1
        fi

        echo "$FIXED_N,$PROCESSES,$CHUNK_SIZE,$COMP_TIME,$COMM_TIME,$OVERALL_TIME" >> "$CSV_FILE"

        printf '%s\n' "$OUTPUT" |
            awk -F',' '/^RANK_STATS,/ {
                print $2","$3","$4","$5
            }' >> "$BALANCE_CSV"

        echo "Completed chunk=$CHUNK_SIZE, P=$PROCESSES | computation=$COMP_TIME s | communication=$COMM_TIME s | overall=$OVERALL_TIME s"

        rm -f task1_OpenMPI.txt
    done

    echo "Chunk size $CHUNK_SIZE completed."
    echo "Timing results: $CSV_FILE"
    echo "Balance results: $BALANCE_CSV"
done

echo "All Task 1 chunk-size benchmarks completed."