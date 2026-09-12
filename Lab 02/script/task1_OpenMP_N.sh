#!/bin/bash

if [ "$#" -eq 0 ]; then
    echo "Error: No thread count provided."
    echo "Usage: bash script/task1_OpenMP_N.sh <thread_count1> [thread_count2 ...]"
    exit 1
fi

BASE_DIR=$(pwd)
PROGRAM="$BASE_DIR/compile/task1_OpenMP"

mkdir -p "$BASE_DIR/time"
CSV_FILE="$BASE_DIR/time/task1_OpenMP_N.csv"

if [ ! -f "$CSV_FILE" ]; then
    echo "N,Threads,Computational_Time_sec,Overall_Time_sec" > "$CSV_FILE"
fi

echo "Starting OpenMP Benchmark..."

for THREADS in "$@"
do
    echo "--------------------------------------------------------"
    echo "Starting tests for THREADS = $THREADS"
    echo "--------------------------------------------------------"
    
    for N in $(seq 10000000 1000000 40000000)
    do
        # Command line mode (Mode 2)
        OUTPUT=$("$PROGRAM" $N $THREADS)
        
        COMP_TIME=$(echo "$OUTPUT" | grep "Computational Time:" | awk '{print $3}')
        OVERALL_TIME=$(echo "$OUTPUT" | grep "Overall Time:" | awk '{print $3}')
        
        echo "Tested N = $N | Threads: $THREADS | Comp: $COMP_TIME s | Overall: $OVERALL_TIME s"
        echo "$N,$THREADS,$COMP_TIME,$OVERALL_TIME" >> "$CSV_FILE"
    done
done

rm -f task1_OpenMP.txt
echo "--------------------------------------------------------"
echo "Benchmarking complete! Data saved to $CSV_FILE."