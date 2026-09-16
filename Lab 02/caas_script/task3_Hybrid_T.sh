#!/bin/bash
#SBATCH --job-name=task3_hybrid_T
#SBATCH --time=00:10:00
#SBATCH --mem=16G
#SBATCH --nodes=8
#SBATCH --ntasks=8
#SBATCH --ntasks-per-node=1
#SBATCH --cpus-per-task=16
#SBATCH --partition=defq

set -u

module load openmpi/4.1.5-gcc-11.2.0-ux65npg

BASE_DIR="${SLURM_SUBMIT_DIR:-$PWD}"

SOURCE="$BASE_DIR/task2_Hybrid.c"
PROGRAM="$BASE_DIR/compile/task2_Hybrid"
RESULT_DIR="$BASE_DIR/result"

FIXED_N=${FIXED_N:-10000000}
FIXED_PROCESSES=8
MAX_TOTAL_WORKERS=128

cd "$BASE_DIR" || exit 1
mkdir -p "$BASE_DIR/compile" "$RESULT_DIR"

if [ ! -f "$SOURCE" ]; then
    echo "Error: source file was not found: $SOURCE"
    exit 1
fi

if ! mpicc -O3 -Wall -Wextra -fopenmp "$SOURCE" -o "$PROGRAM" -lm; then
    echo "Error: task2_Hybrid.c failed to compile."
    exit 1
fi

export OMP_DYNAMIC=FALSE
export OMP_PLACES=cores
export OMP_PROC_BIND=close

# Chunk 1 represents cyclic distribution.
# Chunk 32 is the selected block-cyclic distribution.
for CHUNK_SIZE in 1 16 32; do
    CSV_FILE="$RESULT_DIR/task3_Hybrid_T_P${FIXED_PROCESSES}_C${CHUNK_SIZE}.csv"

    echo "N,Chunk_Size,Processes,Threads_Per_Process,Total_Workers,Computational_Time_sec,Communication_Time_sec,Overall_Time_sec" > "$CSV_FILE"

    echo "Starting hybrid thread scaling: N=$FIXED_N, processes=$FIXED_PROCESSES, chunk=$CHUNK_SIZE"

    for THREADS in 1 2 4 8 16; do
        TOTAL_WORKERS=$((FIXED_PROCESSES * THREADS))

        if [ "$TOTAL_WORKERS" -gt "$MAX_TOTAL_WORKERS" ]; then
            echo "Error: P=$FIXED_PROCESSES and T=$THREADS requests $TOTAL_WORKERS workers."
            exit 1
        fi

        export OMP_NUM_THREADS=$THREADS

        echo "Running chunk=$CHUNK_SIZE, P=$FIXED_PROCESSES, T=$THREADS, workers=$TOTAL_WORKERS..."

        if ! OUTPUT=$(srun \
            --nodes=8 \
            --ntasks="$FIXED_PROCESSES" \
            --ntasks-per-node=1 \
            --cpus-per-task="$THREADS" \
            --cpu-bind=cores \
            "$PROGRAM" "$FIXED_N" "$CHUNK_SIZE" "$THREADS" 2>&1); then
            echo "$OUTPUT"
            echo "Error: hybrid run failed for chunk=$CHUNK_SIZE, P=$FIXED_PROCESSES, T=$THREADS."
            exit 1
        fi

        COMP_TIME=$(printf '%s\n' "$OUTPUT" |
            awk '/^Computational Time:/ {value=$3} END {print value}')

        COMM_TIME=$(printf '%s\n' "$OUTPUT" |
            awk '/^Communication Time:/ {value=$3} END {print value}')

        OVERALL_TIME=$(printf '%s\n' "$OUTPUT" |
            awk '/^Overall Time:/ {value=$3} END {print value}')

        if [ -z "$COMP_TIME" ] ||
           [ -z "$COMM_TIME" ] ||
           [ -z "$OVERALL_TIME" ]; then
            echo "$OUTPUT"
            echo "Error: unable to extract timings for chunk=$CHUNK_SIZE, P=$FIXED_PROCESSES, T=$THREADS."
            exit 1
        fi

        echo "$FIXED_N,$CHUNK_SIZE,$FIXED_PROCESSES,$THREADS,$TOTAL_WORKERS,$COMP_TIME,$COMM_TIME,$OVERALL_TIME" >> "$CSV_FILE"

        echo "Completed: computation=$COMP_TIME s, communication=$COMM_TIME s, overall=$OVERALL_TIME s"

        rm -f "$BASE_DIR/task2_Hybrid.txt"
    done

    echo "Saved: $CSV_FILE"
done

echo "Hybrid thread-scaling benchmark completed."