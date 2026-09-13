#!/bin/bash
#SBATCH --job-name=task1_MPI_N
#SBATCH --partition=compute
#SBATCH --nodes=4
#SBATCH --ntasks=8
#SBATCH --ntasks-per-node=2
#SBATCH --time=00:10:00
#SBATCH --output=task1_MPI_N_%j.out
#SBATCH --error=task1_MPI_N_%j.err

set -u

SOURCE="task1_MPI.c"
PROGRAM="task1_MPI_Output"
CSV_FILE="task1_MPI_N.csv"
PROCESSES="$SLURM_NTASKS"
CHUNK_SIZE=64

if [ ! -f "$SOURCE" ]; then
    echo "Error: $SOURCE was not found in $(pwd)."
    exit 1
fi

if ! command -v mpicc >/dev/null 2>&1; then
    echo "Error: mpicc is not available."
    exit 1
fi

if ! mpicc -Wall -Wextra -O2 "$SOURCE" -o "$PROGRAM" -lm; then
    echo "Error: $SOURCE failed to compile."
    exit 1
fi

echo "N,Processes,Chunk_Size,Computation_Time_sec,Communication_Time_sec,Overall_Time_sec" > "$CSV_FILE"
echo "Starting N benchmark with $PROCESSES MPI processes and chunk size $CHUNK_SIZE."

for N in $(seq 10000000 1000000 40000000); do
    echo "Running N=$N..."

    if ! OUTPUT=$(mpirun -np "$SLURM_NTASKS" \
    ./task1_MPI_Output "$N" "$CHUNK_SIZE" 2>&1); then
        echo "$OUTPUT"
        echo "Error: run failed for N=$N."
        exit 1
    fi

    COMP_TIME=$(printf '%s\n' "$OUTPUT" | awk '/Computation Time:/ {print $3; exit}')
    COMM_TIME=$(printf '%s\n' "$OUTPUT" | awk '/Communication Time:/ {print $3; exit}')
    OVERALL_TIME=$(printf '%s\n' "$OUTPUT" | awk '/Overall Time:/ {print $3; exit}')

    if [ -z "$COMP_TIME" ] || [ -z "$COMM_TIME" ] || [ -z "$OVERALL_TIME" ]; then
        echo "$OUTPUT"
        echo "Error: unable to extract timing results for N=$N."
        exit 1
    fi

    echo "$N,$PROCESSES,$CHUNK_SIZE,$COMP_TIME,$COMM_TIME,$OVERALL_TIME" >> "$CSV_FILE"
    echo "Completed N=$N | computation=$COMP_TIME s | communication=$COMM_TIME s | overall=$OVERALL_TIME s"

    rm -f task1_OpenMPI.txt
done

echo "Benchmark complete. Results saved to $CSV_FILE."
