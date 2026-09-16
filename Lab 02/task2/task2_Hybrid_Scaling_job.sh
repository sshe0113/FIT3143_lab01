#!/bin/bash
#SBATCH --job-name=task2_Hybrid_Scaling
#SBATCH --time=00:10:00
#SBATCH --mem=16G
#SBATCH --ntasks=4
#SBATCH --cpus-per-task=4
#SBATCH --ntasks-per-node=2
#SBATCH --partition=defq
#SBATCH --output=task2_Hybrid_Scaling_%j.out
#SBATCH --error=task2_Hybrid_Scaling_%j.err

set -u

module load openmpi/4.1.5-gcc-11.2.0-ux65npg

SOURCE="task2.c"
PROGRAM="./task2_Hybrid_Output"
THREAD_CSV="task2_Hybrid_T.csv"
PROCESS_THREAD_CSV="task2_Hybrid_PT.csv"
FIXED_N=40000000
FIXED_PROCESSES=4
CHUNK_SIZE=64

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

echo "N,MPI_Processes,Threads_Per_Process,Total_Workers,Chunk_Size,Computation_Time_sec,Communication_Time_sec,Overall_Time_sec" > "$THREAD_CSV"
echo "N,MPI_Processes,Threads_Per_Process,Total_Workers,Chunk_Size,Computation_Time_sec,Communication_Time_sec,Overall_Time_sec" > "$PROCESS_THREAD_CSV"

run_hybrid()
{
    local processes="$1"
    local threads="$2"
    local csv_file="$3"
    local total_workers=$((processes * threads))
    local output comp_time comm_time overall_time

    export OMP_NUM_THREADS="$threads"
    echo "Running P=$processes, T=$threads, total workers=$total_workers..."

    if ! output=$(srun --ntasks="$processes" --cpus-per-task="$threads" --cpu-bind=cores \
        "$PROGRAM" "$FIXED_N" "$CHUNK_SIZE" "$threads" 2>&1); then
        echo "$output"
        echo "Error: Task 2 failed for P=$processes and T=$threads."
        exit 1
    fi

    comp_time=$(printf '%s\n' "$output" | awk '/^Computation Time:/ {print $3; exit}')
    comm_time=$(printf '%s\n' "$output" | awk '/^Communication Time:/ {print $3; exit}')
    overall_time=$(printf '%s\n' "$output" | awk '/^Overall Time:/ {print $3; exit}')

    if [ -z "$comp_time" ] || [ -z "$comm_time" ] || [ -z "$overall_time" ]; then
        echo "$output"
        echo "Error: unable to extract Task 2 timings for P=$processes and T=$threads."
        exit 1
    fi

    echo "$FIXED_N,$processes,$threads,$total_workers,$CHUNK_SIZE,$comp_time,$comm_time,$overall_time" >> "$csv_file"
    echo "Completed P=$processes, T=$threads | computation=$comp_time s | communication=$comm_time s | overall=$overall_time s"
    rm -f task2_Hybrid.txt
}

echo "Starting Task 2 thread-scaling benchmark with P=$FIXED_PROCESSES."
for THREADS in 1 2 4; do
    run_hybrid "$FIXED_PROCESSES" "$THREADS" "$THREAD_CSV"
done

echo "Starting Task 2 combined MPI/OpenMP scaling benchmark."
run_hybrid 1 1 "$PROCESS_THREAD_CSV"
run_hybrid 2 2 "$PROCESS_THREAD_CSV"
run_hybrid 4 4 "$PROCESS_THREAD_CSV"

echo "Task 2 scaling benchmarks complete: $THREAD_CSV and $PROCESS_THREAD_CSV"
