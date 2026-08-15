//////////////////////////////////////
//  Author  :   Shee Seng Cheng
//              Tay Chee Hsian
//  Start Date:   13/08/2026
//  TASK 1
//////////////////////////////////////

#include <stdio.h>
#include <math.h>
#include <time.h>
#include <stdlib.h> 

// To notify the program that there is a function called WritePrimesToFile, 
// and this is what its parameters look like. 
// The actual implementation will come later.
void WritePrimesToFile(char *filename, int *primes, int count);

int main () {
    int n;
    struct timespec start, end, startW, endW;
    double timetaken, time_write;
    
    //Ask user to input an integer
    printf("Enter an integer number: ");
    scanf("%d", &n); //Store the input to the address of 'n'

    // Allocate memory to store primes
    int *primes = malloc(n * sizeof(int));
    int count = 0;

    //Start timing only the computation
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
    //include nano seconds
    timetaken = (timetaken + (end.tv_nsec - start.tv_nsec)) / 1e9; // turn into seconds
    
    //%lf is for double computation time
    printf("Time taken to compute: %lf seconds\n", timetaken);

    clock_gettime(CLOCK_MONOTONIC, &startW);
    // Output result
    if (n < 100) {

        // Small n output to stdout (terminal)
        printf("All prime numbers less than %d are:\n", n);

        for (int i = 0; i < count; i++) {
            printf("%d\n", primes[i]);
        }

    } else {

        // Large n output to the file

        WritePrimesToFile("task1_output.txt", primes, count);

        
        printf("Result has been written to task1_output.txt\n");
        
    }
    clock_gettime(CLOCK_MONOTONIC, &endW);
    // Duration of the computation process
    time_write = (endW.tv_sec - startW.tv_sec) * 1e9; //* 1e9 to nanoseconds
    //include nano seconds
    time_write = (time_write + (endW.tv_nsec - startW.tv_nsec)) / 1e9; // turn into seconds
    
    //%lf is for double computation time
    printf("Time taken to write: %lf seconds\n", time_write);

    // Free allocated memory
    free(primes);

    printf("Overall time taken: %1f seconds\n", timetaken + time_write);
    return 0;
}

//Helper function to write to the file (idea from lab 3 "Vector_Cell_Product.c")
void WritePrimesToFile(char *filename, int *primes, int count)
{
    FILE *pFile = fopen(filename, "w"); //pFile is a pointer to a file stream

    for (int i = 0; i < count; i++) {
        fprintf(pFile, "%d\n", primes[i]);
    }

    fclose(pFile);
}