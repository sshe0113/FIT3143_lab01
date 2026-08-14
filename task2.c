//////////////////////////////////////
//  Author  :   Shee Seng Cheng
//              Tay Chee Hsian
//  Start Date:   13/08/2026
//  TASK 2
//////////////////////////////////////

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
void WritePrimesToFile(const char *filename, int *primes, int count);

int main()
{
    struct timespec start, end, startComp, endComp;
    double timetaken;
    FILE *output;

    printf("Enter an integer number (n): ");
    if (scanf("%d", &n) != 1 || n < 2)
    {
        printf("Invalid input.\n");
        return 1;
    }

    printf("Enter number of threads: ");
    if (scanf("%d", &num_threads) != 1 || num_threads < 1)
    {
        printf("Invalid thread count.\n");
        return 1;
    }

    clock_gettime(CLOCK_MONOTONIC, &start);

    // Array allocation
    prime_array = (bool *)calloc(n, sizeof(bool));
    int *primes = (int *)malloc(n * sizeof(int));
    
    if (prime_array == NULL || primes == NULL)
    {
        printf("Memory allocation failed.\n");
        return 1;
    }

    // Assign the memory space to all threads
    pthread_t *threads = (pthread_t *)malloc(num_threads * sizeof(pthread_t));
    // Assign a unique id to each thread
    int *thread_ids = (int *)malloc(num_threads * sizeof(int));

    // Computation
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

    clock_gettime(CLOCK_MONOTONIC, &endComp);

    timetaken = (endComp.tv_sec - startComp.tv_sec) + (endComp.tv_nsec - startComp.tv_nsec) / 1e9;
    printf("Computational Time: %lf seconds\n", timetaken);

    // Output result
    if (n < 100)
    {
        printf("All prime numbers less than %d are:\n", n);
        for (int i = 0; i < count; i++)
        {
            printf("%d\n", primes[i]);
        }
    }
    else
    {
        WritePrimesToFile("task2_output.txt", primes, count);
    }

    clock_gettime(CLOCK_MONOTONIC, &end);

    timetaken = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
    printf("Overall Time: %lf seconds\n", timetaken);

    // Free allocated memory
    free(prime_array);
    free(primes);
    free(threads);
    free(thread_ids);

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

void WritePrimesToFile(const char *filename, int *primes, int count)
{
    FILE *pFile = fopen(filename, "w");

    for (int i = 0; i < count; i++)
    {
        fprintf(pFile, "%d\n", primes[i]);
    }

    fclose(pFile);
    printf("Result has been written into %s\n", filename);
}