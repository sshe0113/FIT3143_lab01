#!/bin/bash
#SBATCH --job-name=task2_Hybrid_N
#SBATCH --time=00:10:00
#SBATCH --mem=16G
#SBATCH --ntasks=4
#SBATCH --cpus-per-task=4
#SBATCH --ntasks-per-node=2
#SBATCH --partition=defq
#SBATCH --output=task2_Hybrid_N_%j.out
#SBATCH --error=task2_Hybrid_N_%j.err

set -u

module load openmpi/4.1.5-gcc-11.2.0-ux65npg

SOURCE="task2.c"
PROGRAM="./task2_Hybrid_Output"
CSV_FILE="task2_Hybrid_N.csv"
PROCESSES="$SLURM_NTASKS"
THREADS="$SLURM_CPUS_PER_TASK"
CHUNK_SIZE=64

export OMP_NUM_THREADS="$THREADS"
export OMP_PLACES=cores
export OMP_PROC_BIND=close

if [ ! -f "$SOURCE" ]; then
    echo "Error: $SOURCE was not found in $(pwd)."
    exit 1
fi

if ! mpicc -Wall -Wextra -O2 -fopenmp "$SOURCE" -o "$PROGRAM" -lm; then
    echo "Error: $SOURCE failed to compile."
    exit 1
fi

echo "N,MPI_Processes,Threads_Per_Process,Total_Workers,Chunk_Size,Computation_Time_sec,Communication_Time_sec,Overall_Time_sec" > "$CSV_FILE"
echo "Starting Task 2 problem-size benchmark with P=$PROCESSES and T=$THREADS."

for N in $(seq 10000000 1000000 40000000); do
    echo "Running N=$N..."

    if ! OUTPUT=$(srun --ntasks="$PROCESSES" --cpus-per-task="$THREADS" --cpu-bind=cores \
        "$PROGRAM" "$N" "$CHUNK_SIZE" "$THREADS" 2>&1); then
        echo "$OUTPUT"
        echo "Error: Task 2 failed for N=$N."
        exit 1
    fi

    COMP_TIME=$(printf '%s\n' "$OUTPUT" | awk '/^Computation Time:/ {print $3; exit}')
    COMM_TIME=$(printf '%s\n' "$OUTPUT" | awk '/^Communication Time:/ {print $3; exit}')
    OVERALL_TIME=$(printf '%s\n' "$OUTPUT" | awk '/^Overall Time:/ {print $3; exit}')

    if [ -z "$COMP_TIME" ] || [ -z "$COMM_TIME" ] || [ -z "$OVERALL_TIME" ]; then
        echo "$OUTPUT"
        echo "Error: unable to extract Task 2 timings for N=$N."
        exit 1
    fi

    TOTAL_WORKERS=$((PROCESSES * THREADS))
    echo "$N,$PROCESSES,$THREADS,$TOTAL_WORKERS,$CHUNK_SIZE,$COMP_TIME,$COMM_TIME,$OVERALL_TIME" >> "$CSV_FILE"
    echo "Completed N=$N | computation=$COMP_TIME s | communication=$COMM_TIME s | overall=$OVERALL_TIME s"
    rm -f task2_Hybrid.txt
done

echo "Task 2 problem-size benchmark complete: $CSV_FILE"
