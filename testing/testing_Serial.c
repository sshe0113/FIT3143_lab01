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
        int *primes = malloc(n * sizeof(int));
        if (primes == NULL) {
            printf("Memory allocation failed for n = %d.\n", n);
            return 1;
        }
        
        int count = 0;

        // Start timing only the computation
        // Get current clock time. (Monotonic = always move foward)
        clock_gettime(CLOCK_MONOTONIC, &start); 
        
        // List all prime numbers until n
        for (int k = 2; k < n; k++) {
            // Boolean variable
            int is_prime = 1; //1 True, 0 False

            // Check from 2 until sqrt k 
            for (int i = 2; i <= sqrt(k); i++) {

                //if k has a divisor then k not a prime
                if (k % i == 0){
                    is_prime = 0;
                    break;
                }
            }
            if (is_prime){
                // Store prime in memory
                primes[count] = k;
                count ++;
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
        free(primes);
    }

    fclose(result_file);
    printf("\nBenchmark completed successfully.\n");

    return 0;
}