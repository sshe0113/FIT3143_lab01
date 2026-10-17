/**
 * @file      evcns.c
 * @brief     Electric Vehicle Charging Navigation System (EVCNS)
 *
 * @details   
 *
 * @author    Shee Seng Cheng (34612467) - sshe0113@student.monash.edu
 * @author    Tay Chee Hsian (34612513) - ctay0040@student.monash.edu
 * @note      Coding Implementation for EVCNS
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <time.h>
#include <mpi.h>
#include <omp.h>
#include <math.h>
#include <stddef.h>

// Constants
#define SHIFT_R0W 0         // Row dimension in a 2D grid
#define SHIFT_COL 1         // Column dimension in a 2D grid
#define DISP 1              // Displacement
#define NO_NEIGHBOR -2      // No physical neighbors
#define NUM_PORTS 6         // Number of charging ports in a node
#define THRESHOLD 2         // A node is congestion if it remains less than or equal to available ports

// MPI Message Tags
#define MSG_REQ_NEIGHBOR 1  // Request message tag from a node to its neighbors to check available ports
#define MSG_RES_NEIGHBOR 2  // Response message tag to reply its own available ports to requested node
#define MSG_REPORT 3        // Report message tag to base station if ports of a node and its neighbors are fully utilized (congestion)
#define MSG_BS_REPLY 4      // Reply coordinate of available ports from base staion to reported node
#define MSG_TERMINATE 5     // Terminate message tag to ensure shutdown gracefully

// Package all necessary status for sending once to eliminate communication overhead
typedef struct {
    double timestamp;   // Exact simulation time
    int sender_rank;    // Rank ID of the congested node
    int coords[2];      // [row, col] coordinates in the 2D grid
    int avail_ports;    // Number of free ports at this node
    int avail_neigh[4]; // Free ports of neighbors
    int msg_exchanged;  // Total messages sent/received to check neighbors
    int msg_size;       // Size of a single neighbor request payload (bytes)
} MsgData;

void CreateDataType(MPI_Datatype *report_type) {
    /**
     * Registers a custom MPI Datatype to send the entire MsgData struct in a single message
     */

    // Number of elements in each struct member
    int block_len[7] = {1, 1, 2, 1, 4, 1, 1};

    // Array to store the memory offsets (displacements) of each member
    MPI_Aint displacements[7];

    // Primitive MPI datatypes corresponding to each struct member
    MPI_Datatype types[7] = {
        MPI_DOUBLE, MPI_INT, MPI_INT, MPI_INT, MPI_INT, MPI_INT, MPI_INT
    };

    // Dummy instance used solely to calculate memory address
    MsgData dummy;
    MPI_Aint base;

    // Get the absolute base memory address of the struct
    MPI_Get_address(&dummy, &base);

    // Get the absolute memory address of each individual member
    MPI_Get_address(&dummy.timestamp, &displacements[0]);
    MPI_Get_address(&dummy.sender_rank, &displacements[1]);
    MPI_Get_address(&dummy.coords, &displacements[2]);
    MPI_Get_address(&dummy.avail_ports, &displacements[3]);
    MPI_Get_address(&dummy.avail_neigh, &displacements[4]);
    MPI_Get_address(&dummy.msg_exchanged, &displacements[5]);
    MPI_Get_address(&dummy.msg_size, &displacements[6]);

    // Calculate the relative offset for each member to handle C memory padding safely
    for (int i = 0; i < 7; i++) displacements[i] -= base;

    // Construct the new custom MPI datatype using the layouts defined above
    MPI_Type_create_struct(7, block_len, displacements, types, report_type);

    // Commit the new datatype to the MPI system to make it ready for communication
    MPI_Type_commit(report_type);
}

//-------------------------------------------
// EV Charging Node
//-------------------------------------------
void SlaveIO(MPI_Comm world_comm, MPI_Comm comm, int nrows, int ncols, MPI_Datatype report_type) {
    int global_size, global_rank;
    MPI_Comm_size(world_comm, &global_size);
    MPI_Comm_rank(world_comm, &global_rank);

    int dims[2] = {nrows, ncols}, coord[2], wrapAround[2] = {0, 0};  // 2D Mesh
    int nbr_i_lo, nbr_i_hi, nbr_j_lo, nbr_j_hi;  // Bottom, Top, Left, Right
    MPI_Comm comm2D;

    MPI_Cart_create(comm, 2, dims, wrapAround, 0, &comm2D);
    MPI_Cart_coords(comm2D, global_rank, 2, coord);
    MPI_Cart_shift(comm2D, SHIFT_R0W, DISP, &nbr_i_lo, &nbr_i_hi);
    MPI_Cart_shift(comm2D, SHIFT_COL, DISP, &nbr_j_lo, &nbr_j_hi);

    int ports[NUM_PORTS];
    for (int i = 0; i < NUM_PORTS; i++) ports[i] = 1;
    bool running_flag = true;  // true means simulation is running, false otherwise
    double start_time = MPI_Wtime();  // Record time

    #pragma omp parallel num_threads(NUM_PORTS + 1) shared(ports, running_flag, start_time) 
    {
        int thread_id = omp_get_thread_num();

        if (thread_id < NUM_PORTS) {
            double last_update = MPI_Wtime();  // Record the last updated time of a port
            bool running;
            #pragma omp atomic read
            running = running_flag;

            while (running) {
                // Every two seconds perform once (random change a port status)
                if (MPI_Wtime() - last_update > 2.0) {
                    #pragma omp critical 
                    {  // Mutex lock
                        ports[thread_id] = rand() % 2;
                    }
                    last_update = MPI_Wtime();
                }
                #pragma omp atomic read
                running = running_flag;
            }
        } else {  // Main thread: Port 6
            double last_check = MPI_Wtime();
            bool running = true;
            int flag;  // Used by MPI_Iprobe, 1 means a msg is waiting to be read
            MPI_Status probe_status;  // Keep track who are the sender and what tag of msg sender send

            while (running) {
                // Capture the terminate signal
                // Main thread instantly check if the Base Station has sent the command
                MPI_Iprobe(global_size - 1, MSG_TERMINATE, world_comm, &flag, &probe_status);
                if (flag) {
                    int term_sig; // Buffer required by MPI_Recv to catch the incoming payload
                    MPI_Recv(&term_sig, 1, MPI_INT, global_size - 1, MSG_TERMINATE, world_comm, MPI_STATUS_IGNORE);
                    #pragma omp atomic write
                    running_flag = false;
                    break;
                }

                // Response to the neighbors' request
                MPI_Iprobe(MPI_ANY_SOURCE, MSG_REQ_NEIGHBOR, comm2D, &flag, &probe_status);
                if (flag) {
                    int req_sig;
                    MPI_Recv(&req_sig, 1, MPI_INT, probe_status.MPI_SOURCE, MSG_REQ_NEIGHBOR, comm2D, MPI_STATUS_IGNORE);
                    int curr_avail = 0;  // Get how many available ports
                    #pragma omp critical 
                    {
                        for (int i = 0; i < NUM_PORTS; i++) curr_avail += ports[i];
                    }
                    MPI_Send(&curr_avail, 1, MPI_INT, probe_status.MPI_SOURCE, MSG_RES_NEIGHBOR, comm2D);
                }

                // Instruction from the Base Station
                MPI_Iprobe(global_size - 1, MSG_BS_REPLY, world_comm, &flag, &probe_status);
                if (flag) {
                    int reply_coords[2];
                    MPI_Recv(reply_coords, 2, MPI_INT, global_size - 1, MSG_BS_REPLY, world_comm, MPI_STATUS_IGNORE);
                }

                // Local congestion checking and neighbors communication
                if (MPI_Wtime() - last_check > 3.0) {
                    int loc_avail_ports = 0;
                    #pragma omp critical 
                    {
                        for (int i = 0; i < NUM_PORTS; i++) loc_avail_ports += ports[i];
                    }

                    if (loc_avail_ports < THRESHOLD) {
                        int req = 1; // Just a dummy variable to send something
                        int neighbors[4] = {nbr_i_lo, nbr_i_hi, nbr_j_lo, nbr_j_hi};
                        int neigh_ports[4] = {-1, -1, -1, -1};
                        int req_count = 0;

                        //---------------------------------
                        // Using MPI_Send & MPI_Recv
                        //---------------------------------
                        /*
                        for (int i = 0; i < 4; i++) {
                            if (neighbors[i] != MPI_PROC_NULL) {
                                MPI_Send(&req, 1, MPI_INT, neighbors[i], MSG_REQ_NEIGHBOR, comm2D);
                                MPI_Recv(&neigh_ports[i], 1, MPI_INT, neighbors[i], MSG_RES_NEIGHBOR, comm2D, MPI_STATUS_IGNORE);
                                req_count++;
                            }
                        }
                        */
                        //---------------------------------
                        
                        //---------------------------------
                        // Using MPI_Isend & MPI_Irecv
                        //---------------------------------
                        MPI_Request reqs[4], recv[4];
                        for (int i = 0; i < 4; i++) {
                            if (neighbors[i] != MPI_PROC_NULL) {
                                MPI_Isend(&req, 1, MPI_INT, neighbors[i], MSG_REQ_NEIGHBOR, comm2D, &reqs[req_count]);
                                MPI_Irecv(&neigh_ports[i], 1, MPI_INT, neighbors[i], MSG_RES_NEIGHBOR, comm2D, &recv[req_count]);
                                req_count++;
                            }
                        }

                        if (req_count > 0) {
                            MPI_Waitall(req_count, reqs, MPI_STATUSES_IGNORE);
                            MPI_Waitall(req_count, recv, MPI_STATUSES_IGNORE);
                        }
                        //---------------------------------

                        // Check neither it nor its immediate neighbors have any available ports
                        bool all_neigh_full = true;
                        for (int i = 0; i < 4; i++) {
                            if (neigh_ports[i] >= THRESHOLD) all_neigh_full = false;
                        }

                        if (all_neigh_full) {
                            MsgData msg = {
                                .timestamp = MPI_Wtime() - start_time,  // Records the exact time the congestion happened
                                .sender_rank = global_rank,
                                .coords = {coord[0], coord[1]},
                                .avail_ports = loc_avail_ports, // Proof that this node is full
                                .avail_neigh = {neigh_ports[0], neigh_ports[1], neigh_ports[2], neigh_ports[3]}, // Proof that the neighbors are also full
                                .msg_exchanged = req_count * 2,  // Tracking how much message was generated
                                .msg_size = sizeof(int)
                            };
                            MPI_Send(&msg, 1, report_type, global_size - 1, MSG_REPORT, world_comm);
                        }
                    }
                    last_check = MPI_Wtime();
                }

                #pragma omp atomic read
                running = running_flag;
            }
        }
    }
    MPI_Comm_free(&comm2D);
}

//-------------------------------------------
// Base Station
//-------------------------------------------
void MasterIO(MPI_Comm world_comm, MPI_Comm comm, int nrows, int ncols, MPI_Datatype report_type) {
    int global_size;
    MPI_Comm_size(world_comm, &global_size);

    int grid_state[nrows][ncols];  // Global status matrix
    for (int i = 0; i < nrows; i++) for (int j = 0; j < ncols; j++) grid_state[i][j] = NUM_PORTS;

    FILE *log_file = fopen("log.txt", "w");
    if (!log_file) {
        printf("Error: Cannot open log file.\n");
        return;
    }

    double start_time = MPI_Wtime();  // Record time
    int ttl_alerts = 0;  // Record the number of happening congestion
    int ttl_events = 0;  // Record the number of communications
    int flag;  // Used by MPI_Iprobe, 1 means a msg is waiting to be read
    MPI_Status probe_status;  // Keep track who are the sender and what tag of msg sender send
    MsgData report;

    printf("[Base Station] listening for reports...\n");
    fprintf(log_file, "----- SIMULATION STARTED -----");

    while(1) {
        MPI_Iprobe(MPI_ANY_SOURCE, MPI_ANY_TAG, world_comm, &flag, &probe_status);  // Listen any incoming messages
        if (flag) {
            if (probe_status.MPI_TAG == MSG_REPORT) {  // If receive congestion message
                MPI_Recv(&report, 1, report_type, probe_status.MPI_SOURCE, MSG_REPORT, world_comm, MPI_STATUS_IGNORE);
                ttl_alerts++;
                ttl_events += report.msg_exchanged + 1;
                grid_state[report.coords[0]][report.coords[1]] = report.avail_ports;  // Update status

                // A BFS for available ports by expanding outward in a ripple-like manner
                int best_row = -1, best_col = -1;
                int max_dist = nrows + ncols;
                bool found = false;

                for (int d = 1; d <= max_dist && !found; d++) {  // Expand layer by layer
                    for (int dx = -d; dx <= d; dx++) {
                        int dy = d - abs(dx);  // Manhattan Distance (Diamond Scanning)

                        // Positive direction
                        int r = report.coords[0] + dx;
                        int c = report.coords[1] + dy;
                        if (r >= 0 && r < nrows && c >= 0 && c < ncols) {
                            if (grid_state[r][c] >= THRESHOLD) {
                                best_row = r;
                                best_col = c;
                                found = true;
                                break;  // Once find the available ports, immediately exit the expansion
                            }
                        }

                        // Negative direction
                        if (dy != 0) {
                            c = report.coords[1] - dy;
                            if (r >= 0 && r < nrows && c >= 0 && c < ncols) {
                                if (grid_state[r][c] >= THRESHOLD) {
                                    best_row = r;
                                    best_col = c;
                                    found = true;
                                    break;  // Once find the available ports, immediately exit the expansion
                                }
                            }
                        }
                    }
                }

                // Reply back to that congestion node for available ports
                int reply_coords[2] = {best_row, best_col};
                MPI_Send(reply_coords, 2, MPI_INT, probe_status.MPI_SOURCE, MSG_BS_REPLY, world_comm);

                // Record into log
                fprintf(
                    log_file, "T:%.2fs | ALERT [%d] | Rank: %d Coord: (%d,%d) | Ports Free: %d | Neighbor Free: [T:%d, B:%d, L:%d, R:%d] | Msgs: %d | Nearest: (%d,%d)\n",
                    report.timestamp,          // Congestion happen time
                    ttl_alerts,                // Number for this congestion
                    report.sender_rank, 
                    report.coords[0], 
                    report.coords[1], 
                    report.avail_ports,        // The remaining ports of itself
                    report.avail_neigh[0],     // The remaining ports of its neighbors
                    report.avail_neigh[1], 
                    report.avail_neigh[2], 
                    report.avail_neigh[3], 
                    report.msg_exchanged,      // Total communication overhead
                    best_row, best_col
                );
                fflush(log_file);
            }
        }

        // Gracefully shutdown the whole system after 30 seconds
        if (MPI_Wtime() - start_time > 30.0) {
            printf("[Base Station] 30 seconds time limit reached. Shutting down...\n");
            int term_sig = 1;
            for (int i = 0; i < global_size - 1; i++) {
                MPI_Send(&term_sig, 1, MPI_INT, i, MSG_TERMINATE, world_comm);  // Sending terminate message
            }
            break;
        }
    }

    fprintf(log_file, "----- SIMULATION SUMMARY -----\n");
    fprintf(log_file, "Total Simulation Time: %.2f seconds\n", MPI_Wtime() - start_time);
    fprintf(log_file, "Total Congestion Alerts: %d\n", ttl_alerts);
    fprintf(log_file, "Total Communication Events: %d\n", ttl_events);
    fclose(log_file);
    printf("[Base Station] Log saved successfully.\n");
}

int main(int argc, char **argv) {
    int rank, size, nrows = 3, ncols = 3;
    int provided;
    MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &provided);

    if (provided < MPI_THREAD_MULTIPLE) {
        printf("Error: MPI_THREAD_MULTIPLE is not supported.\n");
        MPI_Abort(MPI_COMM_WORLD, 1);
    }

    MPI_Comm_size(MPI_COMM_WORLD, &size);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    if (argc == 3) {
        nrows = atoi(argv[1]);
        ncols = atoi(argv[2]);
    }

    if ((nrows * ncols) != (size - 1)) {  // Must be a node as the base station
        if (rank == 0)
            printf("Error: nrows * ncols (%d*%d) must be equal to slave size (%d)\n", nrows, ncols, size - 1);
        MPI_Finalize();
        return 0;
    }

    MPI_Datatype report_type;
    CreateDataType(&report_type);

    MPI_Comm new_comm;
    MPI_Comm_split(MPI_COMM_WORLD, rank == size - 1, 0, &new_comm);

    if (rank == size - 1) {
        MasterIO(MPI_COMM_WORLD, new_comm, nrows, ncols, report_type);
    } else {
        SlaveIO(MPI_COMM_WORLD, new_comm, nrows, ncols, report_type);
    }

    MPI_Type_free(&report_type);
    MPI_Finalize();
    return 0;
}