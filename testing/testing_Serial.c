//////////////////////////////////////
//  Author  :   Shee Seng Cheng
//              Tay Chee Hsian
//  Start Date:   13/08/2026
//  TASK 1 (Automated Benchmark)
//////////////////////////////////////

#include <stdio.h>
#include <math.h>
#include <time.h>
#include <stdlib.h> 
#include <stdbool.h>

int main () {
    int n;
    struct timespec start, end;
    double timetaken;
    
    // Open a text file to save the computational time results
    FILE *result_file = fopen("serial_computational_times.txt", "w");
    if (result_file == NULL) {
        printf("Failed to create results file.\n");
        return 1;
    }

    // Write the CSV header to the text file
    fprintf(result_file, "n, Computational_Time(sec)\n");

    // Loop n from 10M to 40M with step 1M
    for (n = 10000000; n <= 40000000; n += 1000000) {
        
        // Allocate memory to store primes
        bool *primeArray = (bool *)calloc(n, sizeof(int));
        if (primeArray == NULL) {
            printf("Memory allocation failed for n = %d.\n", n);
            return 1;
        }

        // Start timing only the computation
        // Get current clock time. (Monotonic = always move foward)
        clock_gettime(CLOCK_MONOTONIC, &start); 
        
        // List all prime numbers until n
        for (int k = 2; k < n; k++) {

            // 2 is the only even prime number
            if (k == 2) {
                primeArray[k] = true;
            }
            // Other even numbers are not prime
            else if (k % 2 == 0) {
                primeArray[k] = false;
            }
            // Only check odd numbers
            else {
                bool isPrime = true;
                // Check from 3 until sqrt(k), skipping even numbers
                int range = (int)sqrt(k);

                for (int i = 3; i <= range; i += 2) {
                    // If k has a divisor, k is not prime
                    if (k % i == 0) {
                        isPrime = false;
                        break;
                    }
                }
                // Store whether k is prime
                primeArray[k] = isPrime;
            }
        }

        // Get current clock time (end for computation)
        clock_gettime(CLOCK_MONOTONIC, &end);
        
        // Duration of the computation process
        timetaken = (end.tv_sec - start.tv_sec) * 1e9; //* 1e9 to nanoseconds
        // include nano seconds
        timetaken = (timetaken + (end.tv_nsec - start.tv_nsec)) / 1e9; // turn into seconds
        
        // Output to console to monitor progress
        printf("n = %d | Computational Time: %lf seconds\n", n, timetaken);
        
        // Write result to the text file
        fprintf(result_file, "%lf\n", timetaken);

        // Free allocated memory before next iteration
        free(primeArray);
    }

    fclose(result_file);
    printf("\nBenchmark completed successfully.\n");

    return 0;
}