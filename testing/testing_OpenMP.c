//////////////////////////////////////
//  Author  :   Shee Seng Cheng
//              Tay Chee Hsian
//  Start Date:   15/08/2026
//  TASK 3 (Automated Benchmark - OpenMP)
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

    // Open a text file to save the computational time results
    FILE *result_file = fopen("OPENMP_computational_times.txt", "w");
    if (result_file == NULL) {
        printf("Failed to create results file.\n");
        return 1;
    }
    // Write the CSV header to the text file
    fprintf(result_file, "n, Computational_Time(sec)\n");

    //Get desired number of thread from user
    int num_threads;
    printf("Enter number of threads: ");
    scanf("%d", &num_threads);


    // Loop n from 10M to 40M with step 1M
    for (n = 10000000; n <= 40000000; n += 1000000) {

        // Allocate memory to store primes
        int *primesArray = malloc(n * sizeof(int));

        //Start timing only the computation
        // Get current clock time. (Monotonic = always move foward)
        clock_gettime(CLOCK_MONOTONIC, &start); 
        
        #pragma omp parallel num_threads(num_threads) // Set number of threads
        {
            int thread_id = omp_get_thread_num();

            // Find all prime numbers < n with cyclic partitioning
            for (int k = 2 + thread_id; k < n; k += num_threads) {

                // Boolean variable
                int is_prime = 1; // 1 = True, 0 = False

                // 2 is the only even prime number
                if (k == 2) {
                    is_prime = 1;
                }
                // Other even numbers are not prime
                else if (k % 2 == 0) {
                    is_prime = 0;
                }
                // Only check odd numbers
                else {
                    // Check from 3 until sqrt(k), skipping even numbers
                    int range = (int)sqrt(k);

                    for (int i = 3; i <= range; i += 2) {

                        // If k has a divisor, k is not prime
                        if (k % i == 0) {
                            is_prime = 0;
                            break;
                        }
                    }
                }

                // Store whether k is prime
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
        printf("n = %d | Time taken to compute: %lf seconds\n", n, timetaken);

        // Write result to the text file
        fprintf(result_file, "%lf\n", timetaken);
        
        // Free allocated memory
        free(primesArray);
    }

    printf("OpenMP benchmark completed successfully.\n");

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