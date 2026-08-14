//////////////////////////////////////
//  Author  :   Shee Seng Cheng
//              Tay Chee Hsian
//  Start Date:   15/08/2026
//  Task 3 – OpenMP - Finding Prime Numbers
//////////////////////////////////////

#include <stdio.h>
#include <math.h>
#include <time.h>
#include <stdlib.h> 
#include <omp.h>

// To notify the program that there is a function called WritePrimesToFile, 
// and this is what its parameters look like. 
// The actual implementation will come later.
void WritePrimesToFile(char *filename, int *primesArray, int n);

int main () {
    int n;
    struct timespec start, end, startW, endW;
    double timetaken, time_write;
    
    //Ask user to input an integer
    printf("Enter an integer number: ");
    scanf("%d", &n); //Store the input to the address of 'n'

    // Allocate memory to store primes
    int *primesArray = malloc(n * sizeof(int));

    //Get desired number of thread from user
    int num_threads;
    printf("Enter number of threads: ");
    scanf("%d", &num_threads);

    //Start timing only the computation
    // Get current clock time. (Monotonic = always move foward)
	clock_gettime(CLOCK_MONOTONIC, &start); 
    
    #pragma omp parallel num_threads(num_threads) // Set number of threads
    {
        int thread_id = omp_get_thread_num();

        // Find all prime number < n with way of cyclic partitioning 
        for (int k = 2 + thread_id; k < n; k += num_threads) {

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
            
            // Use boolean 1/0 to indicate it was a prime or not
            primesArray[k] = is_prime;
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
        
        // Serial loop to ensure sorted
        for (int k = 2; k < n; k++) {
            if (primesArray[k]){
                printf("%d\n", k);
            }
        }

    } else {
        // Large n output to the file
        WritePrimesToFile("task3_output.txt", primesArray, n);

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
    free(primesArray);

    printf("Overall time taken: %1f seconds\n", timetaken + time_write);
    return 0;
}

//Helper function to write to the file (idea from lab 3 "Vector_Cell_Product.c")
void WritePrimesToFile(char *filename, int *primesArray, int n)
{
    FILE *pFile = fopen(filename, "w"); //pFile is a pointer to a file stream

    for (int i = 0; i < n; i++) {
        if(primesArray[i]){
            fprintf(pFile, "%d\n", i);
        }
    }

    fclose(pFile);
}