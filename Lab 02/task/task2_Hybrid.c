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

// Function prototypes
bool IsPrime(long candidate);
bool ParseLongArgument(const char *text, long minimum, long maximum, long *value);
void WriteToFile(const char *filename, const bool *primeArray, long n);

int main(int argc, char *argv[])
{
    int provided;
    MPI_Init_thread(&argc, &argv, MPI_THREAD_FUNNELED, &provided);

    int rank, processCount;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &processCount);

    if (provided < MPI_THREAD_FUNNELED) {
        if (rank == 0) fprintf(stderr, "Error: MPI thread support not met.\n");
        MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
    }

    // ---------------------------------------------------------
    // Overall Time Start
    // ---------------------------------------------------------
    double overallStart = MPI_Wtime();
    double locCommTime = 0.0, locCompTime = 0.0;
    
    // params array: [N, chunkSize, threadCount]
    long params[3] = {-1, 64, 1}; 

    // Root Process: Parse Arguments
    if (rank == 0) {
        if (argc >= 2 && argc <= 4) {
            ParseLongArgument(argv[1], 2, INT_MAX, &params[0]);
            if (argc >= 3) ParseLongArgument(argv[2], 1, LONG_MAX, &params[1]);
            if (argc == 4) ParseLongArgument(argv[3], 1, INT_MAX, &params[2]);
        } else {
            fprintf(stderr, "Usage: %s <N> [chunk-size] [threads]\n", argv[0]);
        }
    }

    // ---------------------------------------------------------
    // Communication Time Start
    // ---------------------------------------------------------
    double startComm = MPI_Wtime();
    MPI_Bcast(params, 3, MPI_LONG, 0, MPI_COMM_WORLD);

    // ---------------------------------------------------------
    // Communication Time End
    // ---------------------------------------------------------
    locCommTime += (MPI_Wtime() - startComm);

    long n = params[0], chunkSize = params[1], threadCount = params[2];

    // If parsing failed on rank 0, n remains -1. All ranks exit gracefully.
    if (n < 0) {
        MPI_Finalize();
        return EXIT_FAILURE;
    }

    // Forcing system allocate the number of threads that users ask for
    omp_set_dynamic(0);
    omp_set_num_threads((int)threadCount);

    // Memory Allocation
    bool *localPrimeArray = calloc((size_t)n, sizeof(bool));
    if (!localPrimeArray) {
        fprintf(stderr, "Rank %d: local memory allocation failed.\n", rank);
        MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
    }

    // ---------------------------------------------------------
    // Computational Time Start
    // ---------------------------------------------------------
    double compStart = MPI_Wtime();

    if (rank == 0 && n > 2) {
        localPrimeArray[2] = true; // Handle the only even prime
    }

    long candidateCount = (n - 2) / 2;
    long chunkCount = (candidateCount + chunkSize - 1) / chunkSize;

    // The local shared-memory threads will grab exactly 1 chunk at 
    // a time from this pile on a first-come, first-served basis
    #pragma omp parallel for schedule(dynamic, 1)
    for (long chunk = rank; chunk < chunkCount; chunk += processCount) {
        long startIndex = chunk * chunkSize;
        long endIndex = (startIndex + chunkSize > candidateCount) ? candidateCount : (startIndex + chunkSize);

        for (long index = startIndex; index < endIndex; index++) {
            long candidate = 2 * index + 3;
            if (IsPrime(candidate)) {
                localPrimeArray[candidate] = true;
            }
        }
    }

    // ---------------------------------------------------------
    // Computational Time End
    // ---------------------------------------------------------
    locCompTime = MPI_Wtime() - compStart;

    // Communication: Global Reduction
    bool *globalPrimeArray = NULL;
    if (rank == 0) {
        globalPrimeArray = calloc((size_t)n, sizeof(bool));
        if (!globalPrimeArray) {
            fprintf(stderr, "Rank 0: global memory allocation failed.\n");
            MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
        }
    }

    // ---------------------------------------------------------
    // Communication Time Start
    // ---------------------------------------------------------
    startComm = MPI_Wtime();
    MPI_Reduce(localPrimeArray, globalPrimeArray, (int)n, MPI_C_BOOL, MPI_LOR, 0, MPI_COMM_WORLD);

    // ---------------------------------------------------------
    // Communication Time End
    // ---------------------------------------------------------
    locCommTime += (MPI_Wtime() - startComm);

    // Free local arrays immediately
    free(localPrimeArray); 

    // Finding the slowest execution times across the entire cluster
    // because program is only as fast as its slowest worker
    double localTimes[2] = {locCompTime, locCommTime};
    double maxTimes[2] = {0.0, 0.0};
    MPI_Reduce(localTimes, maxTimes, 2, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);

    // File I/O and Final Output
    if (rank == 0) {
        WriteToFile("task2_Hybrid.txt", globalPrimeArray, n);
        free(globalPrimeArray);
    }

    MPI_Barrier(MPI_COMM_WORLD);

    // ---------------------------------------------------------
    // Overall Time End
    // ---------------------------------------------------------
    double overallTime = MPI_Wtime() - overallStart;
    
    double maxOverallTime = 0.0;
    MPI_Reduce(&overallTime, &maxOverallTime, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);

    if (rank == 0) {
        printf("MPI Processes: %d\n", processCount);
        printf("OpenMP Threads per Process: %ld\n", threadCount);
        printf("Total Workers: %ld\n", (long)processCount * threadCount);
        printf("Chunk Size: %ld\n", chunkSize);
        printf("Computational Time: %.9f seconds\n", maxTimes[0]);
        printf("Communication Time: %.9f seconds\n", maxTimes[1]);
        printf("Overall Time: %.9f seconds\n", maxOverallTime);
    }

    MPI_Finalize();
    return EXIT_SUCCESS;
}

bool IsPrime(long candidate)
{
    if (candidate < 2) return false;
    if (candidate == 2) return true;
    if (candidate % 2 == 0) return false;

    long limit = (long)sqrt((double)candidate);
    for (long divisor = 3; divisor <= limit; divisor += 2) {
        if (candidate % divisor == 0) return false;
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
    if (!pFile) {
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