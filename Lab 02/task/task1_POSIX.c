/**
 * @file      task1_POSIX.c
 * @brief     Prime Number Generator by POSIX (Pthreads)
 * 
 * @author    Shee Seng Cheng (34612467) - sshe0113@student.monash.edu
 * @author    Tay Chee Hsian (34612513) - ctay0040@student.monash.edu
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <math.h>
#include <time.h>
#include <pthread.h>

// Global variables needed for the thread function
int n;
int numThreads;
bool *primeArray;  

// Function prototypes
void *find_prime(void *arg);
void WriteToFile(char *pFilename, bool *primeArray, int n);

int main(int argc, char *argv[])
{
    struct timespec start, end, startComp, endComp;
    double timeComp, timeOverall;

    //-------------------------------------
    // Overall Time Start
    //-------------------------------------
    clock_gettime(CLOCK_MONOTONIC, &start);

    // Input Handling
    if (argc != 3) {
        printf("Error: Invalid arguments.\n");
        printf("Usage: %s <N> <num_threads>\n", argv[0]);
        return 1;
    }

    n = atoi(argv[1]);
    numThreads = atoi(argv[2]);
    printf("N = %d, Threads = %d\n", n, numThreads);

    if (n <= 0 || numThreads < 1) {
        printf("Error: Invalid 'n' or thread count.\n");
        return 1;
    }

    // Array allocation
    primeArray = (bool *)calloc(n, sizeof(bool));
    pthread_t *threads = (pthread_t *)malloc(numThreads * sizeof(pthread_t));
    int *threadIDs = (int *)malloc(numThreads * sizeof(int));
    
    if (primeArray == NULL || threads == NULL || threadIDs == NULL)
    {
        printf("Memory allocation failed.\n");
        return 1;
    }

    //-------------------------------------
    // Computational Time Start
    //-------------------------------------
    clock_gettime(CLOCK_MONOTONIC, &startComp);

    // Create and activate threads
    for (int i = 0; i < numThreads; i++)
    {
        threadIDs[i] = i;
        pthread_create(&threads[i], NULL, find_prime, &threadIDs[i]);
    }

    // Wait for all sub-threads to complete the computation
    for (int i = 0; i < numThreads; i++)
    {
        pthread_join(threads[i], NULL);
    }

    //-------------------------------------
    // Computational Time End
    //-------------------------------------
    clock_gettime(CLOCK_MONOTONIC, &endComp);

    // Calculate Computational Time
    timeComp = (endComp.tv_sec - startComp.tv_sec) * 1e9; 
    timeComp = (timeComp + (endComp.tv_nsec - startComp.tv_nsec)) / 1e9; 

    // Output result
    if (n < 100)
    {
        printf("All prime numbers less than %d are:\n", n);
        for (int k = 2; k < n; k++)
        {
            if (primeArray[k]) {
                printf("%d\n", k);
            }
        }
    }
    else
    {
        WriteToFile("task1_POSIX.txt", primeArray, n);
    }

    //-------------------------------------
    // Overall Time End
    //-------------------------------------
    clock_gettime(CLOCK_MONOTONIC, &end);

    // Calculate Overall Time
    timeOverall = (end.tv_sec - start.tv_sec) * 1e9; 
    timeOverall = (timeOverall + (end.tv_nsec - start.tv_nsec)) / 1e9; 

    printf("Computational Time: %lf seconds\n", timeComp);
    printf("Overall Time: %lf seconds\n", timeOverall);

    // Free allocated memory
    free(primeArray);
    free(threads);
    free(threadIDs);

    return 0;
}

// Thread function for cyclic partitioning
void *find_prime(void *arg)
{
    int threadID = *(int *)arg;

    // Distribute the work by taking every numThreads-th item.
    for (int k = 2 + threadID; k < n; k += numThreads)
    {
        if (k == 2)  
        {
            primeArray[k] = true;
        } 
        else if (k % 2 != 0)  
        {
            bool isPrime = true;
            int range = (int)sqrt(k);
            
            for (int i = 3; i <= range; i += 2)  
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
    pthread_exit(NULL);  
}

void WriteToFile(char *pFilename, bool *primeArray, int n)
{
    FILE *pFile = fopen(pFilename, "w");
    if(pFile == NULL)
    {
        printf("Error: Cannot create output file %s\n", pFilename);
        return;
    }

    for (int i = 2; i < n; i++) {
        if(primeArray[i]){
            fprintf(pFile, "%d\n", i);
        }
    }

    fclose(pFile);
    printf("Result has been written into %s\n", pFilename);
}