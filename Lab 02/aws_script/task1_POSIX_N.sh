#!/bin/bash
BASE_DIR=$(pwd)
PROGRAM="$BASE_DIR/compile/task1_POSIX"
mkdir -p "$BASE_DIR/time"
CSV_FILE="$BASE_DIR/time/task1_POSIX_N.csv"

if [ ! -f "$CSV_FILE" ]; then
    echo "N,Threads,Computational_Time_sec,Overall_Time_sec" > "$CSV_FILE"
fi

for THREADS in "$@"
do
    for N in $(seq 10000000 1000000 40000000)
    do
        OUTPUT=$("$PROGRAM" $N $THREADS)
        COMP_TIME=$(echo "$OUTPUT" | grep "Computational Time:" | awk '{print $3}')
        OVERALL_TIME=$(echo "$OUTPUT" | grep "Overall Time:" | awk '{print $3}')
        echo "Tested N = $N | Threads: $THREADS | Comp: $COMP_TIME s | Overall: $OVERALL_TIME s"
        echo "$N,$THREADS,$COMP_TIME,$OVERALL_TIME" >> "$CSV_FILE"
    done
done
rm -f task1_POSIX.txt