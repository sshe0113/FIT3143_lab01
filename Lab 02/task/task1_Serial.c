/**
 * @file      task1_Serial.c
 * @brief     Prime Number Generator by Serial code approach
 * 
 * @author    Shee Seng Cheng (34612467) - sshe0113@student.monash.edu
 * @author    Tay Chee Hsian (34612513) - ctay0040@student.monash.edu
 */

#include <stdio.h>
#include <math.h>
#include <time.h>
#include <stdlib.h> 
#include <stdbool.h>

// Function prototype
void WriteToFile(char *pFilename, bool *primeArray, int n);

int main (int argc, char *argv[]) {
    int n;
    struct timespec start, end, startComp, endComp;
    double timeComp, timeOverall;
    
    // Check for command line arguments
    if (argc != 2) {
        printf("Error: Invalid arguments.\n");
        printf("Usage: %s <N>\n", argv[0]);
        return 1;
    }

    //-------------------------------------
    // Overall Time Start
    //-------------------------------------
    clock_gettime(CLOCK_MONOTONIC, &start);

    // Read N directly from the command line
    n = atoi(argv[1]);
    if (n <= 0) {
        printf("Error: Invalid 'n'.\n");
        return 1;
    }
    printf("Mode: Command Line | n = %d\n", n);

    // Allocate memory to store primes
    bool *primeArray = (bool *)calloc(n, sizeof(bool));

    //-------------------------------------
    // Computational Time Start
    //-------------------------------------
    clock_gettime(CLOCK_MONOTONIC, &startComp); 
    
    // Compute prime numbers
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

    if (n < 100) {
        printf("All prime numbers less than %d are:\n", n);
        for (int k = 2; k < n; k++) {
            if (primeArray[k]){
                printf("%d\n", k);
            }
        }
    } else {
        WriteToFile("task1_Serial.txt", primeArray, n);
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

    free(primeArray);
    return 0;
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