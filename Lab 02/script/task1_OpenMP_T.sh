#!/bin/bash

if [ "$#" -eq 0 ]; then
    echo "Error: No thread count provided."
    echo "Usage: bash script/task3_OpenMP_T.sh <thread_count1> [thread_count2 ...]"
    exit 1
fi

BASE_DIR=$(pwd)
PROGRAM="$BASE_DIR/compile/task3_OpenMP"

mkdir -p "$BASE_DIR/time"
CSV_FILE="$BASE_DIR/time/task3_OpenMP_T.csv"

if [ ! -f "$CSV_FILE" ]; then
    echo "N,Threads,Computational_Time_sec,Overall_Time_sec" > "$CSV_FILE"
fi

N=10000000
echo "Starting OpenMP Thread Benchmark with N = $N..."
echo "--------------------------------------------------------"

for THREADS in "$@"
do
    # Command line mode (Mode 2)
    OUTPUT=$("$PROGRAM" $N $THREADS)
    
    COMP_TIME=$(echo "$OUTPUT" | grep "Computational Time:" | awk '{print $3}')
    OVERALL_TIME=$(echo "$OUTPUT" | grep "Overall Time:" | awk '{print $3}')
    
    echo "Tested N = $N | Threads: $THREADS | Comp: $COMP_TIME s | Overall: $OVERALL_TIME s"
    echo "$N,$THREADS,$COMP_TIME,$OVERALL_TIME" >> "$CSV_FILE"
done

rm -f task3_OpenMP.txt
echo "--------------------------------------------------------"
echo "Benchmarking complete! Data saved to $CSV_FILE."