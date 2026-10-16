#include <stdio.h>
#include <stdlib.h>
#include <mpi.h>
#include <omp.h>
#include <time.h>

/**
 * @file task1_2.c
 * @brief Distributed Wireless Sensor Network simulation.
 * @authors Shee Seng Cheng and Tay Chee Hsian
 */

#define NUM_NEIGHBOURS 4

/*
 * Station-to-station messages.
 * Communicator: Cartesian communicator.
 */

/* Payload: one integer containing the request ID. */
#define MSG_NEIGHBOUR_REQUEST  10

/* Payload: two integers [request_id, available_ports]. */
#define MSG_NEIGHBOUR_REPLY    11

/*
 * Messages between stations and the base station.
 * Communicator: MPI_COMM_WORLD.
 */

/* Payload: one CongestionReport structure. */
#define MSG_REPORT            20

/*
 * Payload: an integer array:
 * [request_id, number_of_recommendations, station_rank_1, ...]
 *
 * Recommended station ranks belong to the Cartesian communicator.
 * Zero recommendations means no available station was found.
 */
#define MSG_BASE_REPLY        21

/*
 * Coordinated shutdown messages.
 * Communicator: MPI_COMM_WORLD.
 * All four messages have zero-length payloads.
 */

/* Base -> stations: stop new enquiries; finish existing work. */
#define MSG_DISABLE           30

/*
 * Station -> base: own outstanding enquiries have finished.
 * Continue answering neighbours until MSG_TERMINATE arrives.
 */
#define MSG_READY             31

/* Base -> stations: begin final cleanup and shutdown. */
#define MSG_TERMINATE         32

/* Station -> base: local cleanup is complete, before MPI_Finalize. */
#define MSG_TERMINATED        33

/*
 * Report sent when this station and all its existing neighbours
 * have availability below the configured threshold.
 *
 * Neighbour array order:
 * index 0 = top
 * index 1 = bottom
 * index 2 = left
 * index 3 = right
 */
typedef struct {
    int request_id;

    int station_rank;       // Cartesian rank of reporting station
    int coords[2];          // [row, column] 2 dimension
    int available_ports;   // Reporting station's availability.

    /*
     * Missing neighbours:
     * neighbour_ranks[i]     = MPI_PROC_NULL
     * neighbour_available[i] = -1
     */
    int neighbour_ranks[NUM_NEIGHBOURS];
    int neighbour_available[NUM_NEIGHBOURS];

    /* Date and time when congestion was detected. */
    time_t timestamp;

    /*
     * Communication statistics for this neighbour enquiry.
     * Excludes the report and subsequent base reply.
     * Byte counts describe payloads, excluding network headers.
     */
    int message_count;     // Number of neighbour requests + replies.
    int request_bytes;     // Payload size of one neighbour request.
    int reply_bytes;       // Payload size of one neighbour reply.

} CongestionReport;


int main(int argc, char *argv[]) {
    int rank, size;
    int nrow = 3, ncol = 3; //Default grid dimensions
    int provided;

    MPI_Comm new_comm = MPI_COMM_NULL;

    /* Only the thread that initialises MPI will make MPI calls. */
    MPI_Init_thread(&argc, &argv, MPI_THREAD_FUNNELED, &provided);

    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    /* Verify that MPI supports our threading design. */
    if (provided < MPI_THREAD_FUNNELED) {
        if (rank == 0) {
            fprintf(stderr,
                    "Error: MPI_THREAD_FUNNELED is not supported.\n");
        }

        MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
        return EXIT_FAILURE;
    }

    /* Expect two arguments: number of rows and number of columns. */
    if (argc != 3) {
        if (rank == 0) {
            fprintf(stderr, "Usage: %s <rows> <columns>\n", argv[0]);
        }

        MPI_Finalize();
        return EXIT_FAILURE;
    }

    /* Convert argument strings to integers and detect invalid input. */
    char *row_end;
    char *col_end;

    long rows = strtol(argv[1], &row_end, 10);
    long cols = strtol(argv[2], &col_end, 10);

    /*
    * Each dimension must be a positive integer.
    * Neither dimension can exceed the number of station processes.
    */
    if (row_end == argv[1] || *row_end != '\0' ||
        col_end == argv[2] || *col_end != '\0' ||
        rows < 1 || cols < 1 ||
        rows > size - 1 || cols > size - 1) {

        if (rank == 0) {
            fprintf(stderr,
                    "Error: grid dimensions must be positive integers "
                    "not exceeding %d station processes.\n",
                    size - 1);
        }

        MPI_Finalize();
        return EXIT_FAILURE;
    }

    nrow = (int)rows;
    ncol = (int)cols;

    /* One process per charging station, plus one base station. */
    long long required_processes = (long long)nrow * ncol + 1;

    if (required_processes != size) {
        if (rank == 0) {
            fprintf(stderr,
                    "Error: a %d x %d grid requires %lld processes, "
                    "but %d were launched.\n",
                    nrow, ncol, required_processes, size);
        }

        MPI_Finalize();
        return EXIT_FAILURE;
    }

    if (rank == 0) {
        printf("Grid: %d x %d | Stations: %d | Base stations: 1\n",
            nrow, ncol, size - 1);
    }

    /* 
    World rank 0 is the base station. 
    Exclude the base from the charging-station communicator. 
    */
    MPI_Comm_split(MPI_COMM_WORLD, rank == 0, 0, &new_comm);

    /* Temporarily print the result to verify the split. */
    if (rank == 0) {
        printf("World rank %d: Base station\n", rank);
    } else {    
        int station_rank, station_size;

        MPI_Comm_rank(new_comm, &station_rank);
        MPI_Comm_size(new_comm, &station_size);

        printf("World rank %d: Station rank %d, station count %d\n",
            rank, station_rank, station_size);

        MPI_Comm_free(&new_comm);
    }




    MPI_Finalize();
    return 0;
}