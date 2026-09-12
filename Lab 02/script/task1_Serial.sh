#!/bin/bash

# Lock onto the exact directory your terminal is in (e.g., Lab 02)
BASE_DIR=$(pwd)

# 1. Point to the compiled executable using the absolute base path
PROGRAM="$BASE_DIR/compile/task1_Serial"

# 2. Ensure the 'time' folder exists EXACTLY in Lab 02
mkdir -p "$BASE_DIR/time"

# 3. Define the output CSV file path EXACTLY in Lab 02
CSV_FILE="$BASE_DIR/time/task1_Serial.csv"

# Initialize the CSV file with headers (Notice the quotes around "$CSV_FILE")
echo "N,Computational_Time_sec,Overall_Time_sec" > "$CSV_FILE"
echo "Starting benchmark... Results will be saved to $CSV_FILE"
echo "--------------------------------------------------------"

# Loop through 30 values of N 
for N in $(seq 10000000 1000000 40000000)
do
    # Create the temporary input file with the current N
    echo $N > temp_input.txt
    
    # Run the C program (Notice the quotes around "$PROGRAM")
    OUTPUT=$("$PROGRAM" temp_input.txt)
    
    # Extract the numerical time values using grep and awk
    COMP_TIME=$(echo "$OUTPUT" | grep "Computational Time:" | awk '{print $3}')
    OVERALL_TIME=$(echo "$OUTPUT" | grep "Overall Time:" | awk '{print $3}')
    
    # Print progress to the terminal
    echo "Tested N = $N | Comp: $COMP_TIME s | Overall: $OVERALL_TIME s"
    
    # Append the extracted data to the CSV file (Notice the quotes around "$CSV_FILE")
    echo "$N,$COMP_TIME,$OVERALL_TIME" >> "$CSV_FILE"
done

# Clean up the temporary input file
rm temp_input.txt
rm task1_Serial.txt

echo "--------------------------------------------------------"
echo "Serial Code benchmarking complete!"