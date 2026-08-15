///////////////////////////////////////
//
// Dynamic Task Pool
//
///////////////////////////////////////

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
int curr_num = 2;
pthread_mutex_t lock;

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

    prime_array = (bool *)calloc(n, sizeof(bool));
    int *primes = (int *)malloc(n * sizeof(int));
    
    if (prime_array == NULL || primes == NULL)
    {
        printf("Memory allocation failed.\n");
        return 1;
    }

    pthread_t *threads = (pthread_t *)malloc(num_threads * sizeof(pthread_t));
    pthread_mutex_init(&lock, NULL);  // Initialise mutex

    clock_gettime(CLOCK_MONOTONIC, &startComp);

    for (int i = 0; i < num_threads; i++)
    {
        pthread_create(&threads[i], NULL, find_prime, NULL);
    }

    for (int i = 0; i < num_threads; i++)
    {
        pthread_join(threads[i], NULL);
    }

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

    free(prime_array);
    free(primes);
    free(threads);
    pthread_mutex_destroy(&lock);

    return 0;
}

void *find_prime(void *arg)
{
    while (1)
    {
        int k;

        // Apply lock
        pthread_mutex_lock(&lock);
        k = curr_num;
        curr_num++;
        pthread_mutex_unlock(&lock);

        if (k >= n)
        {
            break;
        }

        // Finding prime
        if (k == 2)
        {
            prime_array[k] = true;
        }
        else if (k % 2 != 0)
        {
            bool is_prime = true;
            int range = (int)sqrt(k);
            for (int i = 3; i <= range; i += 2)
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
    pthread_exit(NULL);
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
