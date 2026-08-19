/**
 * @file      task2.c
 * @brief     Multithreaded Prime Number Generator using Pthreads
 * 
 * @details   This program computes all prime numbers up to a user-specified integer 'n' utilizing POSIX threads (pthreads).
 *            Work is distributed among a user-defined number of threads using a cyclic (interleaved) distribution method.
 *            The algorithm employs optimized trial division by skipping even numbers and limiting the divisor check to the square root of the candidate.
 *            Performance benchmarking is performed using `clock_gettime()` (CLOCK_MONOTONIC) to record computation and overall execution times.
 *            Results for n < 100 are output to the console, whereas larger datasets are serialized to an external file (`task2_output.txt`).
 *
 * @author    Shee Seng Cheng (34612467) - sshe0113@student.monash.edu
 * @author    Tay Chee Hsian (34612513) - ctay0040@student.monash.edu
 * @note      TASK 2
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <math.h>
#include <time.h>
#include <pthread.h>

// Global variables
int n;
int numThreads;
bool *primeArray;  // A boolean array to record prime numbers

// Function prototype
void *find_prime(void *arg);
void WritePrimesToFile(const char *filename, int *primes, int count);

int main()
{
    struct timespec start, end, startComp, endComp;
    double timetaken;

    printf("Enter an integer number (n): ");
    if (scanf("%d", &n) != 1 || n < 2)
    {
        printf("Invalid input.\n");
        return 1;
    }

    printf("Enter number of threads: ");
    if (scanf("%d", &numThreads) != 1 || numThreads < 1)
    {
        printf("Invalid thread count.\n");
        return 1;
    }

    clock_gettime(CLOCK_MONOTONIC, &start);

    // Array allocation
    primeArray = (bool *)calloc(n, sizeof(bool));
    int *primes = (int *)malloc(n * sizeof(int));
    
    if (primeArray == NULL || primes == NULL)
    {
        printf("Memory allocation failed.\n");
        return 1;
    }

    // Assign the memory space to all threads
    pthread_t *threads = (pthread_t *)malloc(numThreads * sizeof(pthread_t));
    // Assign a unique id to each thread
    int *threadIDs = (int *)malloc(numThreads * sizeof(int));

    // Computation
    clock_gettime(CLOCK_MONOTONIC, &startComp);

    // Create and activate threads
    for (int i = 0; i < numThreads; i++)
    {
        threadIDs[i] = i;
        pthread_create(&threads[i], NULL, find_prime, &threadIDs[i]);
    }

    // Waiting all sub-threads complete the computation
    for (int i = 0; i < numThreads; i++)
    {
        pthread_join(threads[i], NULL);
    }

    // Record all results
    int count = 0;
    for (int k = 2; k < n; k++)
    {
        if (primeArray[k])
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
    free(primeArray);
    free(primes);
    free(threads);
    free(threadIDs);

    return 0;
}

// Thread function
void *find_prime(void *arg)
{
    int threadID = *(int *)arg;

    // Instead of giving each thread one continuous chunk of words,
    // distribute the work by taking every numThreads-th item.
    for (int k = 2 + threadID; k < n; k += numThreads)
    {
        if (k == 2)  // 2 is the only even prime number
        {
            primeArray[k] = true;
        } 
        else if (k % 2 != 0)  // Handles the case when k is odd number
        {
            bool isPrime = true;
            // Only needs to check whether it is divisible by any integer between 2 and sqrt(k)
            int range = (int)sqrt(k);
            for (int i = 3; i <= range; i += 2)  // Jump 2 steps to save time
            {
                if (k % i == 0)
                {
                    isPrime = false;
                    break;
                }
            }
            if (isPrime)
            {
                primeArray[k] = true;
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