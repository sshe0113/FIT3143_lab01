//////////////////////////////////////
//  Author  :   Shee Seng Cheng 34612467 sshe0113@student.monash.edu
//              Tay Chee Hsian  34612513 ctay0040@student.monash.edu
//  TASK 1
//////////////////////////////////////

#include <stdio.h>
#include <math.h>
#include <time.h>
#include <stdlib.h> 
#include <stdbool.h>

// To notify the program that there is a function called WritePrimesToFile, 
// and this is what its parameters look like. 
// The actual implementation will come later.
void WritePrimesToFile(char *filename, bool *primeArray, int n);

int main () {
    int n;
    struct timespec start, end, startW, endW;
    double timetaken, timeWrite;
    
    //Ask user to input an integer
    printf("Enter an integer number: ");
    scanf("%d", &n); //Store the input to the address of 'n'

    // Allocate memory to store primes
    bool *primeArray = (bool *)calloc(n, sizeof(bool));

    //Start timing only the computation
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
    timetaken = (end.tv_sec - start.tv_sec) * 1e9; //* 1e9 to nanoseconds
    //include nano seconds
    timetaken = (timetaken + (end.tv_nsec - start.tv_nsec)) / 1e9; // turn into seconds
    
    //%lf is for double computation time
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
        // Large n output to the file
        WritePrimesToFile("task1_output.txt", primeArray, n);
    }
    clock_gettime(CLOCK_MONOTONIC, &endW);

    // Duration of the computation process
    timeWrite = (endW.tv_sec - startW.tv_sec) * 1e9; //* 1e9 to nanoseconds
    //include nano seconds
    timeWrite = (timeWrite + (endW.tv_nsec - startW.tv_nsec)) / 1e9; // turn into seconds
    
    //%lf is for double computation time
    printf("Writing Time %lf seconds\n", timeWrite);
    printf("Overall Time: %lf seconds\n", timetaken + timeWrite);

    // Free allocated memory
    free(primeArray);

    return 0;
}

//Helper function to write to the file (idea from lab 3 "Vector_Cell_Product.c")
void WritePrimesToFile(char *filename, bool *primeArray, int n)
{
    FILE *pFile = fopen(filename, "w"); //pFile is a pointer to a file stream

    // Start from 2 to skip unnecessary checks for 0 and 1
    for (int i = 2; i < n; i++) {
        if(primeArray[i]){
            fprintf(pFile, "%d\n", i);
        }
    }

    fclose(pFile);
    printf("Result has been written into %s\n", filename);
}