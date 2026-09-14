#!/bin/bash
BASE_DIR=$(pwd)
PROGRAM="$BASE_DIR/compile/task1_OpenMP"
mkdir -p "$BASE_DIR/time"
CSV_FILE="$BASE_DIR/time/task1_OpenMP_T.csv"

if [ ! -f "$CSV_FILE" ]; then
    echo "N,Processes,Computational_Time_sec,Overall_Time_sec" > "$CSV_FILE"
fi

N=10000000
for PROCS in "$@"
do
    export OMP_NUM_THREADS=$PROCS
    OUTPUT=$("$PROGRAM" $N $PROCS)
    COMP_TIME=$(echo "$OUTPUT" | grep "Computational Time:" | awk '{print $3}')
    OVERALL_TIME=$(echo "$OUTPUT" | grep "Overall Time:" | awk '{print $3}')
    echo "Tested N = $N | Procs: $PROCS | Comp: $COMP_TIME s | Overall: $OVERALL_TIME s"
    echo "$N,$PROCS,$COMP_TIME,$OVERALL_TIME" >> "$CSV_FILE"
done
rm -f task1_OpenMP.txt