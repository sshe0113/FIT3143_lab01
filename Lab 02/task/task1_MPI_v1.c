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
#include <time.h>

// Function prototypes
bool IsPrime(long candidate);
void WriteToFile(const char *filename, const bool *primeArray, long n);
double ElapsedSeconds(struct timespec start, struct timespec end);

int main(int argc, char *argv[])
{
    MPI_Init(&argc, &argv);

    struct timespec start, end;

    // ---------------------------------------------------------
    // Overall Time Start
    // ---------------------------------------------------------
    clock_gettime(CLOCK_MONOTONIC, &start);

    int rank, processCount;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &processCount);

    double localCommunicationTime = 0.0;
    struct timespec commStart, commEnd;

    long n = -1;
    long chunkSize = 64; // Default chunk size
    long validInput = 1;

    // ---------------------------------------------------------
    // Command-Line Parsing (Rank 0 Only)
    // ---------------------------------------------------------
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

    // ---------------------------------------------------------
    // 3. Optimized Broadcast (Payload Packing)
    // ---------------------------------------------------------
    // Instead of 3 separate broadcasts, we pack the variables into a single array
    // to reduce network handshake latency by 66%.
    long bcast_data[3];
    if (rank == 0) {
        bcast_data[0] = validInput;
        bcast_data[1] = n;
        bcast_data[2] = chunkSize;
    }

    clock_gettime(CLOCK_MONOTONIC, &commStart);
    MPI_Bcast(bcast_data, 3, MPI_LONG, 0, MPI_COMM_WORLD);
    clock_gettime(CLOCK_MONOTONIC, &commEnd);
    localCommunicationTime += ElapsedSeconds(commStart, commEnd);

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

    // ---------------------------------------------------------
    // 4. Computation (Block-Cyclic Distribution)
    // ---------------------------------------------------------
    // Allocate the boolean array for this specific rank
    bool *localPrimeArray = (bool *)calloc(n, sizeof(bool));
    if (localPrimeArray == NULL) {
        fprintf(stderr, "Rank %d: memory allocation failed.\n", rank);
        MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
    }

    struct timespec compStart, compEnd;
    clock_gettime(CLOCK_MONOTONIC, &compStart);

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
            if (IsPrime(candidate)) {
                // Instantly mapped to the correct index, no displacement math needed!
                localPrimeArray[candidate] = true; 
            }
        }
    }

    clock_gettime(CLOCK_MONOTONIC, &compEnd);
    double localComputationTime = ElapsedSeconds(compStart, compEnd);

    // ---------------------------------------------------------
    // 5. Data Merge (Logical OR Reduction)
    // ---------------------------------------------------------
    bool *globalPrimeArray = NULL;
    if (rank == 0) {
        globalPrimeArray = (bool *)calloc(n, sizeof(bool));
    }

    clock_gettime(CLOCK_MONOTONIC, &commStart);
    // The MPI network merges all arrays into Rank 0. If ANY rank found a prime
    // at a specific index, MPI_LOR forces the master array index to 'true'.
    MPI_Reduce(localPrimeArray, globalPrimeArray, n, MPI_C_BOOL, MPI_LOR, 0, MPI_COMM_WORLD);
    clock_gettime(CLOCK_MONOTONIC, &commEnd);
    localCommunicationTime += ElapsedSeconds(commStart, commEnd);

    // ---------------------------------------------------------
    // 6. Time Merge (Maximum Time Reduction)
    // ---------------------------------------------------------
    double localTimes[2] = {localComputationTime, localCommunicationTime};
    double maxTimes[2] = {0.0, 0.0};

    // A single collective call replaces the entire MPI_Send / MPI_Recv loop block
    MPI_Reduce(localTimes, maxTimes, 2, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);

    // ---------------------------------------------------------
    // 7. Output & Finalization
    // ---------------------------------------------------------
    if (rank == 0) {
        if (n < 100) {
            printf("Prime numbers less than %ld:\n", n);
            for (long i = 2; i < n; i++) {
                if (globalPrimeArray[i]) printf("%ld\n", i);
            }
        } else {
            // Because the array naturally counts up, it is already perfectly sorted!
            WriteToFile("task1_OpenMPI.txt", globalPrimeArray, n);
        }

        // Rank 0 measures overall time AFTER writing to the file
        clock_gettime(CLOCK_MONOTONIC, &end);
        double overallTime = ElapsedSeconds(start, end);

        printf("\nProcesses: %d\n", processCount);
        printf("Chunk size: %ld\n", chunkSize);
        printf("Maximum Computation Time: %lf seconds\n", maxTimes[0]);
        printf("Maximum Communication Time: %lf seconds\n", maxTimes[1]);
        printf("Overall Time: %lf seconds\n", overallTime);
        
        free(globalPrimeArray);
    }

    // Non-root ranks finalize immediately without waiting at a barrier
    free(localPrimeArray);
    MPI_Finalize();
    return EXIT_SUCCESS;
}

// ---------------------------------------------------------
// Helper Functions
// ---------------------------------------------------------
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

double ElapsedSeconds(struct timespec start, struct timespec end)
{
    return (double)(end.tv_sec - start.tv_sec) +
           (double)(end.tv_nsec - start.tv_nsec) / 1e9;
}