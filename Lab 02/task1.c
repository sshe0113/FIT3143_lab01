/**
 * @file      task1.c
 * @brief     Prime Number Generator and Performance Benchmarking
 *
 * @details   Extend from week 04 lab 1 but utilize MPI processes with
 *            AWS environment work distribution.
 *
 * @author    Shee Seng Cheng (34612467) - sshe0113@student.monash.edu
 * @author    Tay Chee Hsian (34612513) - ctay0040@student.monash.edu
 * @note      Task 1
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <math.h>
#include <mpi.h>
#include <time.h>

//To check if this candidate is a prime, true if it is and false otherwise
bool IsPrime(long candidate);

//To write the final sorted prime array into a file
void WritePrimesToFile(const char *filename, const long *primes, int count);

//To help qsort() determine how long values should be ordered
int CompareLong(const void *a, const void *b);

//Calculate the elapsed time
double ElapsedSeconds(struct timespec start, struct timespec end);

int main(int argc, char *argv[])
{
    MPI_Init(&argc, &argv);

    //Start overall timer after MPI initialization
    struct timespec overallStart, overallEnd;
    clock_gettime(CLOCK_MONOTONIC, &overallStart);

    int rank;
    int processCount;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &processCount);

    double localCommunicationTime = 0.0;
    struct timespec communicationStart, communicationEnd;

    /*
     * It initially contains -1 as an invalid sentinel value.
     * Only rank 0 will replace it by reading argv[1].
     */
    long n = -1;

    //This is the default number of odd candidates in one block-cyclic chunk
    long chunkSize = 64;
    int validInput = 1;

    /*
     * Only rank 0 reads the command-line arguments.
     * Usage: ./task1 <n> [chunk-size]
     */
    if (rank == 0) {
        if (argc < 2 || argc > 3) {
            fprintf(stderr, "Usage: %s <n> [chunk-size]\n", argv[0]);
            validInput = 0;
        }
        else {
            //Convert string to long format, the third argument indicates decimal base
            char *endPointer = NULL;
            //endPointer points to the first character strtol could not convert
            n = strtol(argv[1], &endPointer, 10);

            if (endPointer == argv[1] || *endPointer != '\0' || n < 2) {
                fprintf(stderr, "Invalid value of n.\n");
                validInput = 0;
            }

            if (argc == 3) {
                endPointer = NULL;
                chunkSize = strtol(argv[2], &endPointer, 10);

                if (endPointer == argv[2] || *endPointer != '\0' || chunkSize < 1) {
                    fprintf(stderr, "Invalid chunk size.\n");
                    validInput = 0;
                }
            }
        }
    }

    //First tell every rank whether the input was valid
    clock_gettime(CLOCK_MONOTONIC, &communicationStart);
    MPI_Bcast(&validInput, 1, MPI_INT, 0, MPI_COMM_WORLD);
    clock_gettime(CLOCK_MONOTONIC, &communicationEnd);
    localCommunicationTime += ElapsedSeconds(communicationStart, communicationEnd);

    if (!validInput) {
        MPI_Finalize();
        return EXIT_FAILURE;
    }

    //Broadcast n and chunk size to every rank
    clock_gettime(CLOCK_MONOTONIC, &communicationStart);
    MPI_Bcast(&n, 1, MPI_LONG, 0, MPI_COMM_WORLD);
    clock_gettime(CLOCK_MONOTONIC, &communicationEnd);
    localCommunicationTime += ElapsedSeconds(communicationStart, communicationEnd);

    clock_gettime(CLOCK_MONOTONIC, &communicationStart);
    MPI_Bcast(&chunkSize, 1, MPI_LONG, 0, MPI_COMM_WORLD);
    clock_gettime(CLOCK_MONOTONIC, &communicationEnd);
    localCommunicationTime += ElapsedSeconds(communicationStart, communicationEnd);

    //Number of odd candidates from 3 to n - 1
    long candidateCount = (n - 2) / 2;
    long chunkCount = (candidateCount + chunkSize - 1) / chunkSize;

    //Calculate an upper bound for the number of candidates allocated to one rank
    long maximumChunksPerRank = (chunkCount + processCount - 1) / processCount;
    long localCapacity = maximumChunksPerRank * chunkSize + 1;

    if (localCapacity < 1) {
        localCapacity = 1;
    }

    //calloc is used for the initial local prime array
    long *localPrimes = calloc((size_t)localCapacity, sizeof(long));

    if (localPrimes == NULL) {
        fprintf(stderr, "Rank %d: memory allocation failed.\n", rank);
        MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
    }

    int localCount = 0;

    struct timespec computationStart, computationEnd;
    clock_gettime(CLOCK_MONOTONIC, &computationStart);

    //Rank 0 handles the only even prime
    if (rank == 0 && n > 2) {
        localPrimes[localCount++] = 2;
    }

    /*
     * Block-cyclic workload distribution.
     * Each rank receives chunk numbers:
     * rank, rank + processCount, rank + 2 * processCount...
     */
    for (long chunk = rank; chunk < chunkCount; chunk += processCount) {
        long startIndex = chunk * chunkSize;
        long endIndex = startIndex + chunkSize;

        if (endIndex > candidateCount) {
            endIndex = candidateCount;
        }

        for (long index = startIndex; index < endIndex; index++) {
            long candidate = 2 * index + 3;

            if (IsPrime(candidate)) {
                if (localCount >= localCapacity) {
                    fprintf(stderr, "Rank %d: local array is full.\n", rank);
                    MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
                }

                localPrimes[localCount++] = candidate;
            }
        }
    }

    clock_gettime(CLOCK_MONOTONIC, &computationEnd);
    double localComputationTime = ElapsedSeconds(computationStart, computationEnd);

    int *receiveCounts = NULL;
    int *displacements = NULL;

    if (rank == 0) {
        receiveCounts = calloc((size_t)processCount, sizeof(int));
        displacements = calloc((size_t)processCount, sizeof(int));

        if (receiveCounts == NULL || displacements == NULL) {
            fprintf(stderr, "Rank 0: gathering allocation failed.\n");
            MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
        }
    }

    //Gather the number of primes found by each rank
    clock_gettime(CLOCK_MONOTONIC, &communicationStart);
    MPI_Gather(&localCount, 1, MPI_INT, receiveCounts, 1, MPI_INT, 0, MPI_COMM_WORLD);
    clock_gettime(CLOCK_MONOTONIC, &communicationEnd);
    localCommunicationTime += ElapsedSeconds(communicationStart, communicationEnd);

    int totalCount = 0;
    long *allPrimes = NULL;

    if (rank == 0) {
        for (int i = 0; i < processCount; i++) {
            displacements[i] = totalCount;
            totalCount += receiveCounts[i];
        }

        int allocationCount = totalCount > 0 ? totalCount : 1;
        allPrimes = calloc((size_t)allocationCount, sizeof(long));

        if (allPrimes == NULL) {
            fprintf(stderr, "Rank 0: final allocation failed.\n");
            MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
        }
    }

    //Collect every variable-sized local prime array
    clock_gettime(CLOCK_MONOTONIC, &communicationStart);
    MPI_Gatherv(localPrimes, localCount, MPI_LONG, allPrimes, receiveCounts,
                displacements, MPI_LONG, 0, MPI_COMM_WORLD);
    clock_gettime(CLOCK_MONOTONIC, &communicationEnd);
    localCommunicationTime += ElapsedSeconds(communicationStart, communicationEnd);

    if (rank == 0) {
        qsort(allPrimes, (size_t)totalCount, sizeof(long), CompareLong);
        WritePrimesToFile("task1_output.txt", allPrimes, totalCount);

        //Also print small test results
        if (n < 100) {
            printf("Prime numbers less than %ld:\n", n);

            for (int i = 0; i < totalCount; i++) {
                printf("%ld\n", allPrimes[i]);
            }
        }
    }

    /*
     * Other ranks wait for rank 0 to finish sorting and writing.
     * Overall time includes setup, computation, communication,
     * sorting and file writing.
     */
    MPI_Barrier(MPI_COMM_WORLD);

    clock_gettime(CLOCK_MONOTONIC, &overallEnd);
    double localOverallTime = ElapsedSeconds(overallStart, overallEnd);
    double localTimes[3] = {
        localComputationTime,
        localCommunicationTime,
        localOverallTime
    };

    double maximumTimes[3] = {0.0, 0.0, 0.0};

    /*
    * Find the maximum computation, communication and overall
    * times across all MPI ranks.
    */
    MPI_Reduce(localTimes, maximumTimes, 3, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);

    if (rank == 0) {
        printf("\nProcesses: %d\n", processCount);
        printf("Chunk size: %ld\n", chunkSize);
        printf("Primes found: %d\n", totalCount);
        printf("Maximum computation time: %.9f seconds\n", maximumTimes[0]);
        printf("Maximum MPI communication time: %.9f seconds\n", maximumTimes[1]);
        printf("Overall time: %.9f seconds\n", maximumTimes[2]);
    }

    free(localPrimes);

    if (rank == 0) {
        free(allPrimes);
        free(receiveCounts);
        free(displacements);
    }

    MPI_Finalize();
    return EXIT_SUCCESS;
}

/*
 * Helper function to determine whether a number is prime.
 * The concept is the same as lab 01.
 */
bool IsPrime(long candidate)
{
    if (candidate < 2) {return false;}
    if (candidate == 2) {return true;}
    if (candidate % 2 == 0) {return false;}

    long limit = (long)sqrt((double)candidate);

    for (long divisor = 3; divisor <= limit; divisor += 2) {
        if (candidate % divisor == 0) {return false;}
    }

    return true;
}

//Helper function to write the sorted primes to a file
void WritePrimesToFile(const char *filename, const long *primes, int count)
{
    FILE *pFile = fopen(filename, "w");

    if (pFile == NULL) {
        fprintf(stderr, "Unable to open %s.\n", filename);
        MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
    }

    for (int i = 0; i < count; i++) {
        fprintf(pFile, "%ld\n", primes[i]);
    }

    fclose(pFile);
    printf("Result has been written into %s\n", filename);
}

//Helper function that helps qsort() determine how long values should be ordered
int CompareLong(const void *a, const void *b)
{
    long first = *(const long *)a;
    long second = *(const long *)b;

    if (first < second) {
        return -1;
    }

    if (first > second) {
        return 1;
    }

    return 0;
}

//Helper function to calculate the elapsed seconds
double ElapsedSeconds(struct timespec start, struct timespec end)
{
    return (double)(end.tv_sec - start.tv_sec) +
           (double)(end.tv_nsec - start.tv_nsec) / 1e9;
}
