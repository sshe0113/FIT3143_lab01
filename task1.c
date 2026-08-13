//////////////////////////////////////
//  Author  :   Shee Seng Cheng
//              Tay Chee Hsian
//  Start_dt:   13/08/2026
//////////////////////////////////////

#include <stdio.h>
#include <math.h>
#include <time.h>

int main () {
    int n;
    struct timespec start, end;
    double timetaken;

    // A pointer to the file
    FILE *output;
    
    //Ask user to input an integer
    printf("Enter an integer number: ");
    scanf("%d", &n); //Store the input to the address of 'n'

    //Recorede the execution time
    // Get current clock time. (Monotonic = always move foward)
	clock_gettime(CLOCK_MONOTONIC, &start); 

    // Decide the output destination
    if (n < 100){
        printf("All prime number lesser then %d was: \n", n);
        output = stdout; // stdout => output to terminal
    } else {
        // Output => write to a file task1_ouput.txt
        output = fopen("task1_output.txt", "w");
        fprintf(output, "All prime number lesser then %d was: \n", n);
    }
    
    // List all prime numbers until n
    for (int k = 2; k < n; k++) {
        // Boolean variable
        int is_prime = 1; //1 True, 0 False

        // Check from 2 until sqrt k 
        for (int i = 2; i <= sqrt(k); i++) {

            //if k has a divisor then k not a prime
            if (k % i == 0){
                is_prime = 0;
                break;
            }
        }
        if (is_prime){
            //Print formatted data to a specific output stream.
            fprintf(output, "%d\n", k);
        }
    }

    // Close the file if open.
    if (n >= 100) {
        fclose(output);
        printf("Result has been wrote into task1_output.txt\n");
    }

    // Get current clock time (end)
    clock_gettime(CLOCK_MONOTONIC, &end);
    
    // Duration of the process
    timetaken = (end.tv_sec - start.tv_sec) * 1e9; //* 1e9 to nanoseconds
    //include nano seconds
    timetaken = (timetaken + (end.tv_nsec - start.tv_nsec)); // In nanoseconds
    
    //%lf is for double
    printf("Time taken: %lf seconds\n", timetaken);

    return 0;
}