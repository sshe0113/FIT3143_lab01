/**
 * @file      task2.c
 * @brief     Prime Number Generator using hybrid Open MPI and OpenMP
 *
 * @details   Uses block-cyclic distribution to divide odd-number candidates
 *            among MPI processes. Each MPI process then uses OpenMP threads
 *            to process its assigned chunks with dynamic scheduling.
 *            Boolean arrays and MPI_Reduce with logical OR combine the local
 *            results on the root process without requiring a sorting step.
 *
 * @author    Shee Seng Cheng (34612467) - sshe0113@student.monash.edu
 * @author    Tay Chee Hsian (34612513) - ctay0040@student.monash.edu
 * @note      Task 2
 */

#include <errno.h>
#include <limits.h>
#include <math.h>
#include <mpi.h>
#include <omp.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

//Function prototypes
bool IsPrime(long candidate);
bool ParseLongArgument(const char *text, long minimum, long maximum, long *value);
void WriteToFile(const char *filename, const bool *primeArray, long n);
double ElapsedSeconds(struct timespec start, struct timespec end);

int main(int argc, char *argv[])
{
    int provided;

    /*
     * MPI_THREAD_FUNNELED allows every OpenMP thread to perform computation,
     * but only the main thread of each process may call MPI functions.
     */
    MPI_Init_thread(&argc, &argv, MPI_THREAD_FUNNELED, &provided);

    int rank, processCount;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &processCount);

    // Check if the MPI implementation provides the required thread support
    if (provided < MPI_THREAD_FUNNELED) {
        if (rank == 0) {
            fprintf(stderr, "MPI does not provide the required thread support.\n");
        }
        MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
    }

    struct timespec start, end, startComm, endComm, startComp, endComp;
    double locCommTime = 0.0;
    long n = -1, chunkSize = 64, threadCount = 1, validInput = 1;

    //Overall Time Start
    clock_gettime(CLOCK_MONOTONIC, &start);

    /*
     * Only rank 0 reads the command-line arguments.
     * Usage: ./task2 <N> [chunk-size] [threads]
     */
    if (rank == 0) {
        if (argc < 2 || argc > 4) {
            fprintf(stderr, "Usage: %s <N> [chunk-size] [threads]\n", argv[0]);
            validInput = 0;
        }
        else {
            if (!ParseLongArgument(argv[1], 2, INT_MAX, &n)) {
                fprintf(stderr, "Invalid value of N.\n");
                validInput = 0;
            }

            if (argc >= 3 &&
                !ParseLongArgument(argv[2], 1, LONG_MAX, &chunkSize)) {
                fprintf(stderr, "Invalid chunk size.\n");
                validInput = 0;
            }

            if (argc == 4 &&
                !ParseLongArgument(argv[3], 1, INT_MAX, &threadCount)) {
                fprintf(stderr, "Invalid thread count.\n");
                validInput = 0;
            }
        }
    }

    //Store the inputs in one array so they can be broadcast together
    long broadcastData[4];
    if (rank == 0) {
        broadcastData[0] = validInput;
        broadcastData[1] = n;
        broadcastData[2] = chunkSize;
        broadcastData[3] = threadCount;
    }

    //Communication Time Start
    clock_gettime(CLOCK_MONOTONIC, &startComm);
    MPI_Bcast(broadcastData, 4, MPI_LONG, 0, MPI_COMM_WORLD);
    clock_gettime(CLOCK_MONOTONIC, &endComm);
    locCommTime += ElapsedSeconds(startComm, endComm);

    if (rank != 0) {
        validInput = broadcastData[0];
        n = broadcastData[1];
        chunkSize = broadcastData[2];
        threadCount = broadcastData[3];
    }

    if (!validInput) {
        MPI_Finalize();
        return EXIT_FAILURE;
    }

    //Use the same fixed number of OpenMP threads in every MPI process
    omp_set_dynamic(0);
    omp_set_num_threads((int)threadCount);

    //Allocate one boolean result array for each MPI process
    bool *localPrimeArray = calloc((size_t)n, sizeof(bool));
    if (localPrimeArray == NULL) {
        fprintf(stderr, "Rank %d: local memory allocation failed.\n", rank);
        MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
    }

    //Computational Time Start
    clock_gettime(CLOCK_MONOTONIC, &startComp);

    //Rank 0 handles the only even prime because the distributed work is odd-only
    if (rank == 0 && n > 2) {
        localPrimeArray[2] = true;
    }

    //Number of odd candidates from 3 to n - 1 and the number of blocks
    long candidateCount = (n - 2) / 2;
    long chunkCount = candidateCount / chunkSize;
    if (candidateCount % chunkSize != 0) {
        chunkCount++;
    }

    /*
     * MPI distributes block numbers cyclically among processes. OpenMP then
     * dynamically distributes each process's block iterations among its local
     * threads. Every candidate maps to one unique boolean-array position.
     */
    #pragma omp parallel for schedule(dynamic, 1)
    for (long chunk = rank; chunk < chunkCount; chunk += processCount) {
        long startIndex = chunk * chunkSize;
        long endIndex = startIndex + chunkSize;

        if (endIndex > candidateCount) {
            endIndex = candidateCount;
        }

        for (long index = startIndex; index < endIndex; index++) {
            long candidate = 2 * index + 3;
            if (IsPrime(candidate)) {
                localPrimeArray[candidate] = true;
            }
        }
    }

    clock_gettime(CLOCK_MONOTONIC, &endComp);
    double locCompTime = ElapsedSeconds(startComp, endComp);

    //Only the root process needs the combined result array
    bool *globalPrimeArray = NULL;
    if (rank == 0) {
        globalPrimeArray = calloc((size_t)n, sizeof(bool));
        if (globalPrimeArray == NULL) {
            fprintf(stderr, "Rank 0: global memory allocation failed.\n");
            MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
        }
    }

    //Combine corresponding boolean positions from all MPI processes
    clock_gettime(CLOCK_MONOTONIC, &startComm);
    MPI_Reduce(localPrimeArray, globalPrimeArray, (int)n, MPI_C_BOOL,
               MPI_LOR, 0, MPI_COMM_WORLD);
    clock_gettime(CLOCK_MONOTONIC, &endComm);
    locCommTime += ElapsedSeconds(startComm, endComm);

    double localTimes[2] = {locCompTime, locCommTime};
    double maximumTimes[2] = {0.0, 0.0};

    //Rank 0 receives the slowest computation and communication measurements
    MPI_Reduce(localTimes, maximumTimes, 2, MPI_DOUBLE, MPI_MAX,
               0, MPI_COMM_WORLD);

    //The indexed boolean array is visited in ascending order, so qsort is unnecessary
    if (rank == 0) {
        WriteToFile("task2_Hybrid.txt", globalPrimeArray, n);
    }

    //Ensure every rank's overall timer includes waiting for root file writing
    MPI_Barrier(MPI_COMM_WORLD);
    clock_gettime(CLOCK_MONOTONIC, &end);

    double localOverallTime = ElapsedSeconds(start, end);
    double overallTime = 0.0;
    MPI_Reduce(&localOverallTime, &overallTime, 1, MPI_DOUBLE, MPI_MAX,
               0, MPI_COMM_WORLD);

    if (rank == 0) {
        printf("MPI Processes: %d\n", processCount);
        printf("OpenMP Threads per Process: %ld\n", threadCount);
        printf("Total Workers: %ld\n", (long)processCount * threadCount);
        printf("Chunk Size: %ld\n", chunkSize);
        printf("Computation Time: %.9f seconds\n", maximumTimes[0]);
        printf("Communication Time: %.9f seconds\n", maximumTimes[1]);
        printf("Overall Time: %.9f seconds\n", overallTime);
        free(globalPrimeArray);
    }

    free(localPrimeArray);
    MPI_Finalize();
    return EXIT_SUCCESS;
}

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

bool ParseLongArgument(const char *text, long minimum, long maximum, long *value)
{
    char *endPointer = NULL;
    errno = 0;
    long parsedValue = strtol(text, &endPointer, 10);

    if (errno == ERANGE || endPointer == text || *endPointer != '\0' ||
        parsedValue < minimum || parsedValue > maximum) {
        return false;
    }

    *value = parsedValue;
    return true;
}

void WriteToFile(const char *filename, const bool *primeArray, long n)
{
    FILE *pFile = fopen(filename, "w");
    if (pFile == NULL) {
        fprintf(stderr, "Unable to open %s.\n", filename);
        MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
    }

    for (long i = 2; i < n; i++) {
        if (primeArray[i]) {
            fprintf(pFile, "%ld\n", i);
        }
    }

    fclose(pFile);
    printf("Result has been written into %s\n", filename);
}

double ElapsedSeconds(struct timespec start, struct timespec end)
{
    return (double)(end.tv_sec - start.tv_sec) +
           (double)(end.tv_nsec - start.tv_nsec) / 1e9;
}
