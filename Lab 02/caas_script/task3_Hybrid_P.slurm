#!/bin/bash
#SBATCH --job-name=task3_hybrid_P
#SBATCH --time=00:10:00
#SBATCH --mem=16G
#SBATCH --nodes=8
#SBATCH --ntasks=64
#SBATCH --ntasks-per-node=8
#SBATCH --cpus-per-task=2
#SBATCH --partition=defq
#SBATCH --output=task3_Hybrid_P_%j.out
#SBATCH --error=task3_Hybrid_P_%j.err

set -u

module load openmpi/4.1.5-gcc-11.2.0-ux65npg

BASE_DIR=$(pwd)
SOURCE="$BASE_DIR/task/task2_Hybrid.c"
PROGRAM="$BASE_DIR/compile/task2_Hybrid"
FIXED_N=${FIXED_N:-10000000}
THREADS=2
CORES_PER_NODE=16
MAX_TOTAL_WORKERS=128

mkdir -p "$BASE_DIR/compile" "$BASE_DIR/time"

if [ ! -f "$SOURCE" ]; then
    echo "Error: source file was not found: $SOURCE"
    exit 1
fi

if ! mpicc -O3 -Wall -Wextra -fopenmp "$SOURCE" -o "$PROGRAM" -lm; then
    echo "Error: task2_Hybrid.c failed to compile."
    exit 1
fi

export OMP_DYNAMIC=FALSE
export OMP_NUM_THREADS=$THREADS
export OMP_PLACES=cores
export OMP_PROC_BIND=close

TASKS_PER_NODE=$((CORES_PER_NODE / THREADS))

for CHUNK_SIZE in 1 64; do
    CSV_FILE="$BASE_DIR/time/task3_Hybrid_P_T${THREADS}_C${CHUNK_SIZE}.csv"

    echo "N,Chunk_Size,Processes,Threads_Per_Process,Total_Workers,Computational_Time_sec,Communication_Time_sec,Overall_Time_sec" > "$CSV_FILE"
    echo "Starting hybrid process scaling: N=$FIXED_N, threads/process=$THREADS, chunk=$CHUNK_SIZE"

    for PROCESSES in 1 2 4 8 16 32 64; do
        TOTAL_WORKERS=$((PROCESSES * THREADS))
        RUN_NODES=$(((PROCESSES + TASKS_PER_NODE - 1) / TASKS_PER_NODE))

        if [ "$TOTAL_WORKERS" -gt "$MAX_TOTAL_WORKERS" ]; then
            echo "Error: P=$PROCESSES and T=$THREADS requests $TOTAL_WORKERS workers."
            exit 1
        fi

        echo "Running chunk=$CHUNK_SIZE, P=$PROCESSES, T=$THREADS, workers=$TOTAL_WORKERS, nodes=$RUN_NODES..."

        if ! OUTPUT=$(srun --nodes="$RUN_NODES" \
            --ntasks="$PROCESSES" \
            --ntasks-per-node="$TASKS_PER_NODE" \
            --cpus-per-task="$THREADS" \
            --cpu-bind=cores \
            "$PROGRAM" "$FIXED_N" "$CHUNK_SIZE" "$THREADS" 2>&1); then
            echo "$OUTPUT"
            echo "Error: hybrid run failed for chunk=$CHUNK_SIZE, P=$PROCESSES, T=$THREADS."
            exit 1
        fi

        COMP_TIME=$(printf '%s\n' "$OUTPUT" | awk '/^Computational Time:/ {value=$3} END {print value}')
        COMM_TIME=$(printf '%s\n' "$OUTPUT" | awk '/^Communication Time:/ {value=$3} END {print value}')
        OVERALL_TIME=$(printf '%s\n' "$OUTPUT" | awk '/^Overall Time:/ {value=$3} END {print value}')

        if [ -z "$COMP_TIME" ] || [ -z "$COMM_TIME" ] || [ -z "$OVERALL_TIME" ]; then
            echo "$OUTPUT"
            echo "Error: unable to extract timings for chunk=$CHUNK_SIZE, P=$PROCESSES, T=$THREADS."
            exit 1
        fi

        echo "$FIXED_N,$CHUNK_SIZE,$PROCESSES,$THREADS,$TOTAL_WORKERS,$COMP_TIME,$COMM_TIME,$OVERALL_TIME" >> "$CSV_FILE"
        echo "Completed: computation=$COMP_TIME s, communication=$COMM_TIME s, overall=$OVERALL_TIME s"

        rm -f "$BASE_DIR/task2_Hybrid.txt"
    done

    echo "Saved: $CSV_FILE"
done

echo "Hybrid process-scaling benchmark completed."
