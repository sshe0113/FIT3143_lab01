#!/bin/bash

BASE_DIR=$(pwd)
PROGRAM="$BASE_DIR/compile/task1_Serial"

mkdir -p "$BASE_DIR/time"
CSV_FILE="$BASE_DIR/time/task1_Serial.csv"

echo "N,Computational_Time_sec,Overall_Time_sec" > "$CSV_FILE"
echo "Starting benchmark... Results will be saved to $CSV_FILE"
echo "--------------------------------------------------------"

for N in $(seq 10000000 1000000 40000000)
do
    # Run the C program directly passing N (No temp file!)
    OUTPUT=$("$PROGRAM" $N)
    
    COMP_TIME=$(echo "$OUTPUT" | grep "Computational Time:" | awk '{print $3}')
    OVERALL_TIME=$(echo "$OUTPUT" | grep "Overall Time:" | awk '{print $3}')
    
    echo "Tested N = $N | Comp: $COMP_TIME s | Overall: $OVERALL_TIME s"
    echo "$N,$COMP_TIME,$OVERALL_TIME" >> "$CSV_FILE"
done

# Clean up only the output text file
rm -f task1_Serial.txt

echo "--------------------------------------------------------"
echo "Serial Code benchmarking complete!"