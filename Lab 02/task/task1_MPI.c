/**
 * @file      task1_OpenMPI.c
 * @brief     Prime Number Generator using Open MPI (Highly Optimized)
 *
 * @details   Utilizes Block-Cyclic distribution for perfect load balancing.
 *            Upgraded to use boolean arrays and MPI_Reduce (Logical OR) to 
 *            completely eliminate serial sorting overhead.
 *            Communication is optimized using payload packing and MPI_MAX reductions.
 *
 * @author    Shee Seng Cheng (34612467) - sshe0113@student.monash.edu
 * @author    Tay Chee Hsian (34612513) - ctay0040@student.monash.edu
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <math.h>
#include <mpi.h>

// Function prototypes
bool IsPrime(long candidate);
void WriteToFile(const char *filename, const bool *primeArray, long n);

int main(int argc, char *argv[])
{
    MPI_Init(&argc, &argv);
    
    int rank, processCount;
    double locCommTime = 0.0, locCompTime = 0.0;
    long n = -1, chunkSize = 64, validInput = 1;

    // ---------------------------------------------------------
    // Overall Time Start
    // ---------------------------------------------------------
    double overallStart = MPI_Wtime();

    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &processCount);

    // Only rank 0 reads the command-line arguments.
    // Usage: ./filename <n> [chunk-size]
    if (rank == 0) {
        if (argc < 2 || argc > 3) {
            fprintf(stderr, "Usage: %s <N> [chunk-size]\n", argv[0]);
            validInput = 0;
        } else {
            n = strtol(argv[1], NULL, 10);
            if (n < 2) {
                fprintf(stderr, "Invalid value of N.\n");
                validInput = 0;
            }
            if (argc == 3) {
                chunkSize = strtol(argv[2], NULL, 10);
                if (chunkSize < 1) {
                    fprintf(stderr, "Invalid chunk size.\n");
                    validInput = 0;
                }
            }
        }
    }

    // Packing required information to other processes need to know
    long bcast_data[3];
    if (rank == 0) {
        bcast_data[0] = validInput;
        bcast_data[1] = n;
        bcast_data[2] = chunkSize;
    }

    // ---------------------------------------------------------
    // Communication Time Start
    // ---------------------------------------------------------
    double startComm = MPI_Wtime();

    MPI_Bcast(bcast_data, 3, MPI_LONG, 0, MPI_COMM_WORLD);

    // ---------------------------------------------------------
    // Communication Time End
    // ---------------------------------------------------------
    locCommTime += (MPI_Wtime() - startComm);

    // Unpack the variables on all non-root ranks
    if (rank != 0) {
        validInput = bcast_data[0];
        n = bcast_data[1];
        chunkSize = bcast_data[2];
    }

    if (!validInput) {
        MPI_Finalize();
        return EXIT_FAILURE;
    }

    // Allocate the boolean array for this specific rank
    bool *localPrimeArray = (bool *)calloc(n, sizeof(bool));
    long localJobCount = 0;

    if (localPrimeArray == NULL) {
        fprintf(stderr, "Rank %d: memory allocation failed.\n", rank);
        MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
    }

    // ---------------------------------------------------------
    // Computational Time Start
    // ---------------------------------------------------------
    double startComp = MPI_Wtime();

    if (rank == 0 && n > 2) {
        localPrimeArray[2] = true;
    }

    long candidateCount = (n - 2) / 2;
    long chunkCount = (candidateCount + chunkSize - 1) / chunkSize;

    // Distribute chunks round-robin
    for (long chunk = rank; chunk < chunkCount; chunk += processCount) {
        long startIndex = chunk * chunkSize;
        long endIndex = startIndex + chunkSize;

        if (endIndex > candidateCount) {
            endIndex = candidateCount;
        }

        for (long index = startIndex; index < endIndex; index++) {
            long candidate = 2 * index + 3;
            localJobCount++;

            if (IsPrime(candidate)) {
                localPrimeArray[candidate] = true; 
            }
        }
    }

    // ---------------------------------------------------------
    // Computational Time End
    // ---------------------------------------------------------
    locCompTime = MPI_Wtime() - startComp;

    // Boolean array to record true (prime) or false (non-prime)
    bool *globalPrimeArray = NULL;
    
    if (rank == 0) {
        globalPrimeArray = (bool *)calloc(n, sizeof(bool));

        if (globalPrimeArray == NULL) {
            fprintf(stderr, "Rank 0: global allocation failed.\n");
            MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
        }
    }

    // ---------------------------------------------------------
    // Communication Time Start
    // ---------------------------------------------------------
    startComm = MPI_Wtime();

    // The MPI network merges all arrays into Rank 0. If any rank found a prime
    // at a specific index, MPI_LOR forces the master array index to 'true'.
    MPI_Reduce(localPrimeArray, globalPrimeArray, n, MPI_C_BOOL, MPI_LOR, 0, MPI_COMM_WORLD);

    // ---------------------------------------------------------
    // Communication Time End
    // ---------------------------------------------------------
    locCommTime += (MPI_Wtime() - startComm);
    
    // Free local arrays immediately
    free(localPrimeArray);

    double localTimes[2] = {locCompTime, locCommTime};
    double maxTimes[2] = {0.0, 0.0};
    long *allJobCounts = NULL;
    double *allCompTimes = NULL;
    double overallTime = 0.0;

    // A single collective call replaces the entire MPI_Send / MPI_Recv loop block
    MPI_Reduce(localTimes, maxTimes, 2, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);

    if (rank == 0) {
        // Because the array naturally counts up, it is already perfectly sorted!
        WriteToFile("task1_OpenMPI.txt", globalPrimeArray, n);

        // Allocate memory for job counts and computation times from all processes
        allJobCounts = calloc((size_t)processCount, sizeof(long));
        allCompTimes = calloc((size_t)processCount, sizeof(double));

        // Check for memory allocation failure
        if (allJobCounts == NULL || allCompTimes == NULL) {
            fprintf(stderr, "Unable to allocate rank statistics.\n");
            MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
        }
    }

    // Gather job counts and computation times from all processes
    MPI_Gather(
        &localJobCount, 1, MPI_LONG,
        allJobCounts, 1, MPI_LONG,
        0, MPI_COMM_WORLD
    );

    // Gather computation times from all processes
    MPI_Gather(
        &locCompTime, 1, MPI_DOUBLE,
        allCompTimes, 1, MPI_DOUBLE,
        0, MPI_COMM_WORLD
    );

    // Write result to file
    if (rank == 0) {
        for (int i = 0; i < processCount; i++) {
            printf(
                "RANK_STATS,%d,%d,%ld,%.9f\n",
                processCount,
                i,
                allJobCounts[i],
                allCompTimes[i]
            );
        }
        
        // Free allocated memory
        free(globalPrimeArray);
        free(allJobCounts);
        free(allCompTimes);
    }

    // Ensure all ranks wait here before finalizing the overall timer
    MPI_Barrier(MPI_COMM_WORLD);

    // ---------------------------------------------------------
    // Overall Time End
    // ---------------------------------------------------------
    double localOverallTime = MPI_Wtime() - overallStart;
    overallTime = 0.0;
    
    // Reduce to find the true longest overall lifespan across all nodes
    MPI_Reduce(&localOverallTime, &overallTime, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);

    if (rank == 0) {
        printf("Computation Time: %lf seconds\n", maxTimes[0]);
        printf("Communication Time: %lf seconds\n", maxTimes[1]);
        printf("Overall Time: %lf seconds\n", overallTime);
    }

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