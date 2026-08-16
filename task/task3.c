//////////////////////////////////////
//  Author  :   Shee Seng Cheng
//              Tay Chee Hsian
//  Start Date:   15/08/2026
//  TASK 3
//////////////////////////////////////

#include <stdio.h>
#include <math.h>
#include <time.h>
#include <stdlib.h> 
#include <stdbool.h>
#include <omp.h>

// Function prototype
void WritePrimesToFile(const char *filename, bool *primeArray, int n);

int main () {
    int n;
    struct timespec start, end, startW, endW;
    double timetaken, timeWrite;
    
    // Ask user to input an integer
    printf("Enter an integer number: ");
    if (scanf("%d", &n) != 1 || n < 2) {
        printf("Invalid input.\n");
        return 1;
    }

    // Allocate memory using bool save memory footprint
    bool *primeArray = (bool *)calloc(n, sizeof(bool));
    if (primeArray == NULL) {
        printf("Memory allocation failed.\n");
        return 1;
    }

    // Get desired number of threads from user
    int num_threads;
    printf("Enter number of threads: ");
    if (scanf("%d", &num_threads) != 1 || num_threads < 1) {
        printf("Invalid thread count.\n");
        free(primeArray);
        return 1;
    }

    // Start timing only the computation
    clock_gettime(CLOCK_MONOTONIC, &start); 
    
    // Dynamic scheduling with a chunk size of 500 to minimize scheduling overhead
    #pragma omp parallel for num_threads(num_threads) schedule(dynamic, 500)
    for (int k = 2; k < n; k++) {

        // 2 is the only even prime number
        if (k == 2) {
            primeArray[k] = true;
        }
        // Other even numbers are not prime
        else if (k % 2 == 0) {
            continue;
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
    timetaken = (end.tv_sec - start.tv_sec) * 1e9; 
    timetaken = (timetaken + (end.tv_nsec - start.tv_nsec)) / 1e9; 
    
    printf("Computational Time: %lf seconds\n", timetaken);

    clock_gettime(CLOCK_MONOTONIC, &startW);
    
    // Output result
    if (n < 100) {
        printf("All prime numbers less than %d are:\n", n);
        // Serial loop to ensure sorted
        for (int k = 2; k < n; k++) {
            if (primeArray[k]){
                printf("%d\n", k);
            }
        }
    } else {
        WritePrimesToFile("task3_output.txt", primeArray, n);
    }

    clock_gettime(CLOCK_MONOTONIC, &endW);
    
    // Duration of the write process
    timeWrite = (endW.tv_sec - startW.tv_sec) * 1e9; 
    timeWrite = (timeWrite + (endW.tv_nsec - startW.tv_nsec)) / 1e9; 
    
    printf("Writing Time: %lf seconds\n", timeWrite);
    printf("Overall Time: %lf seconds\n", timetaken + timeWrite);

    // Free allocated memory
    free(primeArray);

    return 0;
}

// Helper function to write to the file
void WritePrimesToFile(const char *filename, bool *primeArray, int n)
{
    FILE *pFile = fopen(filename, "w"); 
    if (pFile == NULL) return;

    // Start from 2 to skip unnecessary checks for 0 and 1
    for (int i = 2; i < n; i++) {
        if(primeArray[i]){
            fprintf(pFile, "%d\n", i);
        }
    }

    fclose(pFile);
    printf("Result has been written into %s\n", filename);
}