/**
 * @file      task1_OpenMP.c
 * @brief     Multithreaded Prime Number Generator using OpenMP (Dual-Mode)
 * 
 * @details   This program computes all prime numbers up to 'n' leveraging OpenMP.
 *            It utilizes dynamic scheduling (chunk size 500) to distribute workloads.
 *            Execution supports both file input and command-line arguments.
 *
 * @author    Shee Seng Cheng (34612467) - sshe0113@student.monash.edu
 * @author    Tay Chee Hsian (34612513) - ctay0040@student.monash.edu
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <math.h>
#include <time.h>
#include <omp.h>

// Global variables
int n;
int numThreads;
bool *primeArray;

// Function prototypes
void ReadFromFile(char *pFilename, int *pN, int *pThreads);
void WriteToFile(char *pFilename, bool *primeArray, int n);

int main(int argc, char *argv[]) 
{
    struct timespec startOverall, endOverall, startComp, endComp;
    double timeComp, timeOverall;

    //-------------------------------------
    // Overall Time Start
    //-------------------------------------
    clock_gettime(CLOCK_MONOTONIC, &startOverall);

    // Input Handling
    if (argc == 2) {
        // MODE 1: File Input
        ReadFromFile(argv[1], &n, &numThreads);
        printf("Mode: File Input | Read n = %d, Threads = %d from %s\n", n, numThreads, argv[1]);
    } 
    else if (argc == 3) {
        // MODE 2: Direct Command Line
        n = atoi(argv[1]);
        numThreads = atoi(argv[2]);
        printf("Mode: Command Line | Read n = %d, Threads = %d\n", n, numThreads);
    } 
    else {
        printf("Error: Invalid arguments.\n");
        printf("Usage 1 (File): %s <input_file.txt>\n", argv[0]);
        printf("Usage 2 (Direct): %s <N> <num_threads>\n", argv[0]);
        return 1;
    }

    if (n <= 0 || numThreads < 1) {
        printf("Error: Invalid 'n' or thread count.\n");
        return 1;
    }

    // Allocate memory using bool to save memory footprint
    primeArray = (bool *)calloc(n, sizeof(bool));
    if (primeArray == NULL) {
        printf("Memory allocation failed.\n");
        return 1;
    }

    //-------------------------------------
    // Computational Time Start
    //-------------------------------------
    clock_gettime(CLOCK_MONOTONIC, &startComp); 
    
    // Dynamic scheduling with a chunk size of 500 to minimize scheduling overhead
    #pragma omp parallel for num_threads(numThreads) default(none) shared(primeArray, n) schedule(dynamic, 500)
    for (int k = 2; k < n; k++) {
        if (k == 2) {
            primeArray[k] = true;
        }
        else if (k % 2 == 0) {
            continue;
        }
        else {
            bool isPrime = true;
            int range = (int)sqrt(k);

            for (int i = 3; i <= range; i += 2) {
                if (k % i == 0) {
                    isPrime = false;
                    break;
                }
            }
            primeArray[k] = isPrime;
        }
    }
    
    //-------------------------------------
    // Computational Time End
    //-------------------------------------
    clock_gettime(CLOCK_MONOTONIC, &endComp);
    
    // Calculate Computational Time
    timeComp = (endComp.tv_sec - startComp.tv_sec) * 1e9; 
    timeComp = (timeComp + (endComp.tv_nsec - startComp.tv_nsec)) / 1e9; 

    // Output result
    if (n < 100) {
        printf("All prime numbers less than %d are:\n", n);
        for (int k = 2; k < n; k++) {
            if (primeArray[k]){
                printf("%d\n", k);
            }
        }
    } else {
        WriteToFile("task1_OpenMP.txt", primeArray, n);
    }

    //-------------------------------------
    // Overall Time End
    //-------------------------------------
    clock_gettime(CLOCK_MONOTONIC, &endOverall);
    
    // Calculate Overall Time
    timeOverall = (endOverall.tv_sec - startOverall.tv_sec) * 1e9; 
    timeOverall = (timeOverall + (endOverall.tv_nsec - startOverall.tv_nsec)) / 1e9; 

    printf("Computational Time: %lf seconds\n", timeComp);
    printf("Overall Time: %lf seconds\n", timeOverall);

    free(primeArray);
    return 0;
}

void ReadFromFile(char *pFilename, int *pN, int *pThreads)
{
    FILE *pFile = fopen(pFilename, "r");
    if(pFile == NULL)
    {
        printf("Error: Cannot open file %s\n", pFilename);
        exit(1);
    }

    if (fscanf(pFile, "%d %d", pN, pThreads) != 2) {
        printf("Error: File must contain two integers (N and Threads).\n");
        exit(1);
    }
    
    fclose(pFile);
}

void WriteToFile(char *pFilename, bool *primeArray, int n)
{
    FILE *pFile = fopen(pFilename, "w"); 
    if (pFile == NULL) return;

    for (int i = 2; i < n; i++) {
        if(primeArray[i]){
            fprintf(pFile, "%d\n", i);
        }
    }

    fclose(pFile);
    printf("Result has been written into %s\n", pFilename);
}