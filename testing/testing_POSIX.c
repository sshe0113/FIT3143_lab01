///////////////////////////////////////
//
// Testing Running Record
//
///////////////////////////////////////

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <math.h>
#include <time.h>
#include <pthread.h>

// Global variables
int n;
int num_threads;
bool *prime_array;  // A boolean array to record prime numbers

// Function prototype
void *find_prime(void *arg);

int main()
{
    struct timespec startComp, endComp;
    double timetaken;

    printf("Enter number of threads: ");
    if (scanf("%d", &num_threads) != 1 || num_threads < 1)
    {
        printf("Invalid thread count.\n");
        return 1;
    }

    // Open a text file to save the computational time results
    FILE *result_file = fopen("computational_times.txt", "w");
    if (result_file == NULL)
    {
        printf("Failed to create results file.\n");
        return 1;
    }
    
    // Write the CSV header to the text file
    fprintf(result_file, "n, Computational_Time(sec)\n");

    // Loop n from 10M to 40M with step 1M
    for (n = 10000000; n <= 40000000; n += 1000000)
    {
        // Array allocation for this iteration
        prime_array = (bool *)calloc(n, sizeof(bool));
        int *primes = (int *)malloc(n * sizeof(int));
        
        if (prime_array == NULL || primes == NULL)
        {
            printf("Memory allocation failed for n = %d.\n", n);
            return 1;
        }

        // Assign the memory space to all threads
        pthread_t *threads = (pthread_t *)malloc(num_threads * sizeof(pthread_t));
        // Assign a unique id to each thread
        int *thread_ids = (int *)malloc(num_threads * sizeof(int));

        // ==========================================
        // Start Computation Time
        // ==========================================
        clock_gettime(CLOCK_MONOTONIC, &startComp);

        // Create and activate threads
        for (int i = 0; i < num_threads; i++)
        {
            thread_ids[i] = i;
            pthread_create(&threads[i], NULL, find_prime, &thread_ids[i]);
        }

        // Waiting all sub-threads complete the computation
        for (int i = 0; i < num_threads; i++)
        {
            pthread_join(threads[i], NULL);
        }

        // Record all results
        int count = 0;
        for (int k = 2; k < n; k++)
        {
            if (prime_array[k])
            {
                primes[count] = k;
                count++;
            }
        }

        // ==========================================
        // End Computation Time
        // ==========================================
        clock_gettime(CLOCK_MONOTONIC, &endComp);

        timetaken = (endComp.tv_sec - startComp.tv_sec) + (endComp.tv_nsec - startComp.tv_nsec) / 1e9;
        
        // Output to console to monitor progress
        printf("n = %d | Computational Time: %lf seconds\n", n, timetaken);
        
        // Write result to the text file
        fprintf(result_file, "%lf\n", timetaken);

        // Free allocated memory before next iteration
        free(prime_array);
        free(primes);
        free(threads);
        free(thread_ids);
    }

    fclose(result_file);
    printf("\nBenchmark completed successfully.\n");

    return 0;
}

// Thread function
void *find_prime(void *arg)
{
    int thread_id = *(int *)arg;

    // Instead of giving each thread one continuous chunk of words,
    // distribute the work by taking every num_threads-th item.
    for (int k = 2 + thread_id; k < n; k += num_threads)
    {
        if (k == 2)  // 2 is the only even prime number
        {
            prime_array[k] = true;
        } 
        else if (k % 2 != 0)  // Handles the case when k is odd number
        {
            bool is_prime = true;
            // Only needs to check whether it is divisible by any integer between 2 and sqrt(k)
            int range = (int)sqrt(k);
            for (int i = 3; i <= range; i += 2)  // Jump 2 steps to save time
            {
                if (k % i == 0)
                {
                    is_prime = false;
                    break;
                }
            }
            if (is_prime)
            {
                prime_array[k] = true;
            }
        }
    }
    pthread_exit(NULL);  // End thread
}