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
#include <unistd.h>
#include <string.h>
#include <limits.h>
#include <errno.h>

// Constants
#define SHIFT_R0W 0         // Row dimension in a 2D grid
#define SHIFT_COL 1         // Column dimension in a 2D grid
#define DISP 1              // Displacement
#define NO_NEIGHBOR -2      // No physical neighbors
#define NUM_PORTS 6         // Number of charging ports in a node
#define THRESHOLD 2         // A node is congested when 2 or fewer ports are available

// MPI Message Tags
#define MSG_REQ_NEIGHBOR 1  // Request message tag from a node to its neighbors to check available ports
#define MSG_RES_NEIGHBOR 2  // Response message tag to reply its own available ports to requested node
#define MSG_REPORT 3        // Report message tag to base station if ports of a node and its neighbors are fully utilized (congestion)
#define MSG_BS_REPLY 4      // Reply coordinate of available ports from base staion to reported node
#define MSG_TERMINATE 5     // Terminate message tag to ensure shutdown gracefully

//=============================================================================
// Task 3: per-message MPI measurements and the required simulation controls.
// C1=neighbour request, 
// C2=neighbour reply, 
// C3=congestion report.
// C4=base reply, 
// C5=terminate; tags 6-8 coordinate confirmed shutdown.
// All timing uses MPI_Wtime(). No separate project header is required.
//=============================================================================
/* Controls and per-message timing required for Task 3. All point-to-point MPI calls below are
 * made by T6 (stations) or the one base thread.
 * Timing differences use each rank's own clock. Request lifetime is NOT one-way wire
 * latency: preposted receives include idle waiting, and polling adds delay.
 */
#define MSG_DISABLE 6
#define MSG_READY 7
#define MSG_TERMINATED 8

// Fixed simulation duration and update/check intervals (seconds).
static const double simulation_seconds = 30.0, port_period = 2.0, check_period = 3.0;

// Base-station report freshness window; adjustable for the period experiments.
static double predefined_period = 1.5;
static int busy_percent = 70;

/**
 * @brief     Parse an integer from a string and validate its range.
 * @param text The string to parse.
 * @param low The lower bound of the valid range.
 * @param high The upper bound of the valid range.
 * @return The parsed integer if valid, otherwise aborts the program.
 */
static long ParseInteger(const char *text, long low, long high) {
    char *end;
    errno = 0; // Reset errno before conversion

    // Use strtol to convert the string to a long integer
    long value = strtol(text, &end, 10);

    // Check for errors: conversion failure, out of range, or extra characters
    if (errno != 0|| end == text || *end || value < low || value > high) {
        fprintf(stderr, "EVCNS error: %s\n", "invalid integer argument or environment variable");
        MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
        exit(EXIT_FAILURE);
    }
    return value;
}

static void IncidentDateTime(char text[32]) {
    time_t now = time(NULL);
    struct tm calendar;
    if (!localtime_r(&now, &calendar) ||
        !strftime(text, 32, "%Y-%m-%dT%H:%M:%S%z", &calendar)) {
        fprintf(stderr, "EVCNS error: %s\n", "cannot format incident date/time");
        MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
        exit(EXIT_FAILURE);
    }
}

static void LocalReferral(int rank, const int coord[2], const int avail[4]) {
    const int dr[4]={-1,1,0,0}, dc[4]={0,0,-1,1};
    int chosen = -1;
    printf("[Node %d] Local referral candidates:", rank);
    for(int i=0;i<4;++i) if(avail[i]>THRESHOLD) {
        printf(" (%d,%d) free=%d",coord[0]+dr[i],coord[1]+dc[i],avail[i]);
        if (chosen < 0 || avail[i] > avail[chosen]) chosen = i;
    }
    if (chosen >= 0) printf(" -> refer incoming EV to (%d,%d)\n",coord[0]+dr[chosen],coord[1]+dc[chosen]);
    fflush(stdout);
}

typedef struct {
    long long sent, received, send_bytes, receive_bytes;
    double send_call_time;    // Total time inside MPI_Send / MPI_Isend.
    double receive_call_time; // Total time inside MPI_Recv / MPI_Irecv.
    double send_lifetime, receive_lifetime; // Start until completion is observed.
} MessageMetric;

// One set of totals per message tag, local to this MPI process.
static MessageMetric message_metrics[9];

/* Sum per-message statistics and append plain records to log.txt.
 * The Slurm scripts convert these records to CSV; C does not write CSV files.
 * All measurements use local MPI_Wtime differences before this reduction.
 */
static void WriteMetrics(int rows, int cols, int version) {
    int rank, size;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);
    long long counts[8][4], sums[8][4];
    double times[8][4], total_times[8][4];
    for (int tag = 1; tag <= 8; tag++) {
        MessageMetric *m = &message_metrics[tag];
        counts[tag-1][0] = m->sent;
        counts[tag-1][1] = m->received;
        counts[tag-1][2] = m->send_bytes;
        counts[tag-1][3] = m->receive_bytes;
        times[tag-1][0] = m->send_call_time;
        times[tag-1][1] = m->receive_call_time;
        times[tag-1][2] = m->send_lifetime;
        times[tag-1][3] = m->receive_lifetime;
    }
    MPI_Reduce(counts, sums, 8*4, MPI_LONG_LONG_INT, MPI_SUM, size-1, MPI_COMM_WORLD);
    MPI_Reduce(times, total_times, 8*4, MPI_DOUBLE, MPI_SUM, size-1, MPI_COMM_WORLD);
    if (rank == size-1) {
        FILE *log = fopen("log.txt", "a");
        if (!log) {
            fprintf(stderr, "EVCNS error: cannot append message statistics\n");
            MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
            exit(EXIT_FAILURE);
        }
        long long total = 0;
        fprintf(log, "METRIC_FIELDS version rows cols tag sent received send_bytes receive_bytes send_call_time receive_call_time send_lifetime receive_lifetime\n");
        for (int tag = 1; tag <= 8; tag++) {
            long long *p = sums[tag-1];
            double *t = total_times[tag-1];
            if (p[0] != p[1] || p[2] != p[3]) {
                fprintf(stderr, "EVCNS error: sent/received statistics do not balance\n");
                MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
                exit(EXIT_FAILURE);
            }
            fprintf(log, "METRIC %d %d %d %d %lld %lld %lld %lld %.9f %.9f %.9f %.9f\n",
                    version, rows, cols, tag, p[0], p[1], p[2], p[3], t[0], t[1], t[2], t[3]);
            total += p[0];
        }
        fprintf(log, "Actual completed point-to-point messages: %lld (includes tags 1..8; excludes MPI collectives)\n", total);
        fprintf(log, "Shutdown confirmed: READY=%lld TERMINATED=%lld\n", sums[6][0], sums[7][0]);
        fclose(log);
    }
}

// Measurements below use the real MPI routines and MPI_Wtime directly.
//=============================================================================
// Existing V1 report type, node simulation and base-station implementation.
//=============================================================================

// Package all necessary status for sending once to eliminate communication overhead
typedef struct {
    double timestamp;   // Elapsed simulation time in seconds
    char incident_time[32]; // Incident calendar time with timezone
    int sender_rank;    // Rank ID of the congested node
    int coords[2];      // [row, col] coordinates in the 2D grid
    int avail_ports;    // Number of free ports at this node
    int avail_neigh[4]; // Free ports of neighbors
    int msg_exchanged;  // Total messages sent/received to check neighbors
    int msg_size;       // Total data volume of messages exchanged (bytes)
} MsgData;

void CreateDataType(MPI_Datatype *report_type) {
    /**
     * Registers a custom MPI Datatype to send the entire MsgData struct in a single message
     */

    // Number of elements in each struct member
    int block_len[8] = {1, 1, 2, 1, 4, 1, 1, 32};

    // Array to store the memory offsets (displacements) of each member
    MPI_Aint displacements[8];

    // Primitive MPI datatypes corresponding to each struct member
    MPI_Datatype types[8] = {
        MPI_DOUBLE, MPI_INT, MPI_INT, MPI_INT, MPI_INT, MPI_INT, MPI_INT, MPI_CHAR
    };

    // Dummy instance used solely to calculate memory address
    MsgData dummy = {0};
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
    MPI_Get_address(dummy.incident_time, &displacements[7]);

    // Calculate the relative offset for each member to handle C memory padding safely
    for (int i = 0; i < 8; i++) displacements[i] -= base;

    // Construct the new custom MPI datatype using the layouts defined above
    MPI_Type_create_struct(8, block_len, displacements, types, report_type);

    // Commit the new datatype to the MPI system to make it ready for communication
    MPI_Type_commit(report_type);
}

void PrintGrid(FILE *log_file, double timestamp, int nrows, int ncols, int grid_state[nrows][ncols], int alert_row, int alert_col, int best_row, int best_col) {
    /**
     * Snapshot for the mesh topology.
     */

    fprintf(log_file, "----- Grid Snapshot at %.2fs -----\n", timestamp);
    for (int r = 0; r < nrows; r++) {
        for (int c = 0; c < ncols; c++) {
            char cell_buffer[32];

            if (r == alert_row && c == alert_col) {
                sprintf(cell_buffer, "(%d,%d)[!!]:%d", r, c, grid_state[r][c]);
            } else if (r == best_row && c == best_col) {
                sprintf(cell_buffer, "(%d,%d)[OK]:%d", r, c, grid_state[r][c]);
            } else {
                sprintf(cell_buffer, "(%d,%d):%d", r, c, grid_state[r][c]);
            }

            fprintf(log_file, "%-16s", cell_buffer);
        }
        fprintf(log_file, "\n");
    }
    fprintf(log_file, "--------------------------------\n\n");
}

//===========================================
// EV Charging Node
//===========================================
void SlaveIO(MPI_Comm world_comm, MPI_Comm comm, int nrows, int ncols, MPI_Datatype report_type) {
    int global_size, global_rank;
    MPI_Comm_size(world_comm, &global_size);
    MPI_Comm_rank(world_comm, &global_rank);

    int report_bytes;
    MPI_Type_size(report_type, &report_bytes);

    srand(time(NULL) + global_rank); // Original random initialization using time and process rank.

    int dims[2] = {nrows, ncols}, coord[2], wrap_around[2] = {0, 0};  // 2D Mesh
    int nbr_i_lo, nbr_i_hi, nbr_j_lo, nbr_j_hi;  // Top, Bottom, Left, Right
    MPI_Comm comm2D;

    MPI_Cart_create(comm, 2, dims, wrap_around, 0, &comm2D);
    MPI_Cart_coords(comm2D, global_rank, 2, coord);
    MPI_Cart_shift(comm2D, SHIFT_R0W, DISP, &nbr_i_lo, &nbr_i_hi);
    MPI_Cart_shift(comm2D, SHIFT_COL, DISP, &nbr_j_lo, &nbr_j_hi);

    int ports[NUM_PORTS];
    for (int i = 0; i < NUM_PORTS; i++) ports[i] = 1; // Initially all ports are free.
    bool running_flag = true;  // true means simulation is running, false otherwise
    MPI_Barrier(world_comm); // Start only after all ranks have initialized
    double start_time = MPI_Wtime();  // Record time
    double node_comp_time = 0.0;
    double node_comm_time = 0.0;

    #pragma omp parallel num_threads(NUM_PORTS + 1) shared(ports, running_flag, start_time, node_comp_time, node_comm_time) 
    {
        int thread_id = omp_get_thread_num();
        if (omp_get_num_threads() != NUM_PORTS + 1) {
            fprintf(stderr, "EVCNS error: %s\n", "seven OpenMP threads are required");
            MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
            exit(EXIT_FAILURE);
        }
        double comp_time = 0.0, comp_start_time;

        if (thread_id < NUM_PORTS) {
            double last_update = MPI_Wtime();  // Record the last updated time of a port
            bool running;
            #pragma omp atomic read
            running = running_flag;

            while (running) {
                // Every two seconds perform once (random change a port status)
                if (MPI_Wtime() - last_update >= port_period) {
                    comp_start_time = MPI_Wtime();
                    #pragma omp critical  // Mutex lock
                    {  
                        ports[thread_id] = (rand() % 100 < busy_percent) ? 0 : 1;  // Default: 70% busy / 30% free
                    }
                    comp_time += (MPI_Wtime() - comp_start_time);
                    last_update = MPI_Wtime();
                }
                usleep(1000);

                #pragma omp atomic read
                running = running_flag;
            }
        } else {  // Main thread: Port 6
            double last_check = MPI_Wtime();
            bool running = true;
            bool disabled = false, ready_sent = false, awaiting_base = false;
            int flag;  // Used by MPI_Iprobe, 1 means a msg is waiting to be read
            MPI_Status probe_status;  // Keep track who are the sender and what tag of msg sender send
            double comm_time = 0.0, comm_start_time;
            double message_start, message_elapsed;

            while (running) {
                // Stop new enquiries, then finish C1-C4 while still answering peers.
                comm_start_time = MPI_Wtime();
                MPI_Iprobe(global_size - 1, MSG_DISABLE, world_comm, &flag, &probe_status);
                comm_time += (MPI_Wtime() - comm_start_time);
                if (flag) {
                    int signal;
                    comm_start_time = MPI_Wtime();
                    message_start = MPI_Wtime();
                    MPI_Recv(&signal,1,MPI_INT,global_size-1,MSG_DISABLE,world_comm,MPI_STATUS_IGNORE);
                    message_elapsed = MPI_Wtime() - message_start;
                    message_metrics[MSG_DISABLE].received++;
                    message_metrics[MSG_DISABLE].receive_bytes += (long long)(1) * sizeof(int);
                    message_metrics[MSG_DISABLE].receive_call_time += message_elapsed;
                    message_metrics[MSG_DISABLE].receive_lifetime += message_elapsed;
                    comm_time += (MPI_Wtime() - comm_start_time);
                    disabled = true;
                    #pragma omp atomic write
                    running_flag = false;
                }

                comm_start_time = MPI_Wtime();
                // Response to the neighbors' request
                MPI_Iprobe(MPI_ANY_SOURCE, MSG_REQ_NEIGHBOR, comm2D, &flag, &probe_status);
                comm_time += (MPI_Wtime() - comm_start_time);

                if (flag) {
                    int req_sig;
                    comm_start_time = MPI_Wtime();
                    message_start = MPI_Wtime();
                    MPI_Recv(&req_sig, 1, MPI_INT, probe_status.MPI_SOURCE, MSG_REQ_NEIGHBOR, comm2D, MPI_STATUS_IGNORE);
                    message_elapsed = MPI_Wtime() - message_start;
                    message_metrics[MSG_REQ_NEIGHBOR].received++;
                    message_metrics[MSG_REQ_NEIGHBOR].receive_bytes += (long long)(1) * sizeof(int);
                    message_metrics[MSG_REQ_NEIGHBOR].receive_call_time += message_elapsed;
                    message_metrics[MSG_REQ_NEIGHBOR].receive_lifetime += message_elapsed;
                    comm_time += (MPI_Wtime() - comm_start_time);

                    comp_start_time = MPI_Wtime();
                    int curr_avail = 0;  // Get how many available ports
                    #pragma omp critical 
                    {
                        for (int i = 0; i < NUM_PORTS; i++) curr_avail += ports[i];
                    }
                    comp_time += (MPI_Wtime() - comp_start_time);

                    comm_start_time = MPI_Wtime();
                    message_start = MPI_Wtime();
                    MPI_Send(&curr_avail, 1, MPI_INT, probe_status.MPI_SOURCE, MSG_RES_NEIGHBOR, comm2D);
                    message_elapsed = MPI_Wtime() - message_start;
                    message_metrics[MSG_RES_NEIGHBOR].sent++;
                    message_metrics[MSG_RES_NEIGHBOR].send_bytes += (long long)(1) * sizeof(int);
                    message_metrics[MSG_RES_NEIGHBOR].send_call_time += message_elapsed;
                    message_metrics[MSG_RES_NEIGHBOR].send_lifetime += message_elapsed;
                    comm_time += (MPI_Wtime() - comm_start_time);
                }

                comm_start_time = MPI_Wtime();
                // Instruction from the Base Station
                MPI_Iprobe(global_size - 1, MSG_BS_REPLY, world_comm, &flag, &probe_status);
                comm_time += (MPI_Wtime() - comm_start_time);

                if (flag) {
                    int found_count;
                    comm_start_time = MPI_Wtime();
                    // The number of available nodes found by the Base Station
                    message_start = MPI_Wtime();
                    MPI_Recv(&found_count, 1, MPI_INT, global_size - 1, MSG_BS_REPLY, world_comm, MPI_STATUS_IGNORE);
                    message_elapsed = MPI_Wtime() - message_start;
                    message_metrics[MSG_BS_REPLY].received++;
                    message_metrics[MSG_BS_REPLY].receive_bytes += (long long)(1) * sizeof(int);
                    message_metrics[MSG_BS_REPLY].receive_call_time += message_elapsed;
                    message_metrics[MSG_BS_REPLY].receive_lifetime += message_elapsed;

                    if (found_count < 0 || found_count > global_size-1) {
                        fprintf(stderr, "EVCNS error: %s\n", "invalid candidate count");
                        MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
                        exit(EXIT_FAILURE);
                    }
                    if (found_count == 0) {  // No available nodes
                        printf("[Node Rank %d at (%d,%d)] No available nodes nearby\n", global_rank, coord[0], coord[1]);
                        fflush(stdout);
                    } else {
                        int reply_coords[2 * (global_size - 1)]; // Enough for all recommendations
                        message_start = MPI_Wtime();
                        MPI_Recv(reply_coords, found_count * 2, MPI_INT, global_size - 1, MSG_BS_REPLY, world_comm, MPI_STATUS_IGNORE);  // Receive the coordinates from the same distance
                        message_elapsed = MPI_Wtime() - message_start;
                        message_metrics[MSG_BS_REPLY].received++;
                        message_metrics[MSG_BS_REPLY].receive_bytes += (long long)(found_count * 2) * sizeof(int);
                        message_metrics[MSG_BS_REPLY].receive_call_time += message_elapsed;
                        message_metrics[MSG_BS_REPLY].receive_lifetime += message_elapsed;

                        printf("[Node Rank %d at (%d,%d)] Nearest has %d available node(s): ", global_rank, coord[0], coord[1], found_count);
                        for (int i = 0; i < found_count; i++) {
                            printf("(%d,%d) ", reply_coords[i * 2], reply_coords[i * 2 + 1]);
                        }
                        printf("to EVs\n");
                    }
                    fflush(stdout);
                    awaiting_base = false;
                    comm_time += (MPI_Wtime() - comm_start_time);
                }

                // Local congestion checking and neighbors communication
                if (!disabled && !awaiting_base && MPI_Wtime() - last_check >= check_period) {
                    comp_start_time = MPI_Wtime();
                    int loc_avail_ports = 0;
                    #pragma omp critical 
                    {
                        for (int i = 0; i < NUM_PORTS; i++) loc_avail_ports += ports[i];
                    }
                    comp_time += (MPI_Wtime() - comp_start_time);

                    if (loc_avail_ports <= THRESHOLD) {
                        comp_start_time = MPI_Wtime();
                        int req = 1; // Just a dummy variable to send something
                        int neighbors[4] = {nbr_i_lo, nbr_i_hi, nbr_j_lo, nbr_j_hi};
                        int neigh_ports[4] = {NO_NEIGHBOR, NO_NEIGHBOR, NO_NEIGHBOR, NO_NEIGHBOR};
                        int req_count = 0;
                        comp_time += (MPI_Wtime() - comp_start_time);

                        //---------------------------------
                        // Optimization: Using MPI_Isend & MPI_Irecv
                        //---------------------------------
                        MPI_Request reqs[4], recv[4];
                        double send_started[4], receive_started[4];
                        comm_start_time = MPI_Wtime();
                        for (int i = 0; i < 4; i++) {
                            if (neighbors[i] != MPI_PROC_NULL) {
                                send_started[req_count] = MPI_Wtime();
                                MPI_Isend(&req, 1, MPI_INT, neighbors[i], MSG_REQ_NEIGHBOR, comm2D, &reqs[req_count]);
                                message_metrics[MSG_REQ_NEIGHBOR].send_call_time += MPI_Wtime() - send_started[req_count];
                                receive_started[req_count] = MPI_Wtime();
                                MPI_Irecv(&neigh_ports[i], 1, MPI_INT, neighbors[i], MSG_RES_NEIGHBOR, comm2D, &recv[req_count]);
                                message_metrics[MSG_RES_NEIGHBOR].receive_call_time += MPI_Wtime() - receive_started[req_count];
                                req_count++;
                            }
                        }
                        comm_time += MPI_Wtime() - comm_start_time;

                        if (req_count > 0) {
                            bool wait_done = false;
                            while (!wait_done) {
                                int recv_flag = 0;
                                comm_start_time = MPI_Wtime();
                                MPI_Testall(req_count, recv, &recv_flag, MPI_STATUSES_IGNORE);
                                comm_time += (MPI_Wtime() - comm_start_time);

                                if (recv_flag) {
                                    // All C2 replies are now complete; record one sample per neighbour.
                                    double completed = MPI_Wtime();
                                    message_metrics[MSG_RES_NEIGHBOR].received += req_count;
                                    message_metrics[MSG_RES_NEIGHBOR].receive_bytes += (long long)req_count * sizeof(int);
                                    for (int i = 0; i < req_count; i++)
                                        message_metrics[MSG_RES_NEIGHBOR].receive_lifetime += completed - receive_started[i];
                                    wait_done = true;
                                } else {
                                    int inc_flag;
                                    MPI_Status inc_status;
                                    comm_start_time = MPI_Wtime();
                                    MPI_Iprobe(MPI_ANY_SOURCE, MSG_REQ_NEIGHBOR, comm2D, &inc_flag, &inc_status);
                                    comm_time += (MPI_Wtime() - comm_start_time);

                                    if (inc_flag) {
                                        int inc_sig;
                                        comm_start_time = MPI_Wtime();
                                        message_start = MPI_Wtime();
                                        MPI_Recv(&inc_sig, 1, MPI_INT, inc_status.MPI_SOURCE, MSG_REQ_NEIGHBOR, comm2D, MPI_STATUS_IGNORE);
                                        message_elapsed = MPI_Wtime() - message_start;
                                        message_metrics[MSG_REQ_NEIGHBOR].received++;
                                        message_metrics[MSG_REQ_NEIGHBOR].receive_bytes += (long long)(1) * sizeof(int);
                                        message_metrics[MSG_REQ_NEIGHBOR].receive_call_time += message_elapsed;
                                        message_metrics[MSG_REQ_NEIGHBOR].receive_lifetime += message_elapsed;
                                        comm_time += (MPI_Wtime() - comm_start_time);

                                        comp_start_time = MPI_Wtime();
                                        int curr_avail = 0;
                                        #pragma omp critical 
                                        {
                                            for (int p = 0; p < NUM_PORTS; p++) curr_avail += ports[p];
                                        }
                                        comp_time += (MPI_Wtime() - comp_start_time);

                                        comm_start_time = MPI_Wtime();
                                        message_start = MPI_Wtime();
                                        MPI_Send(&curr_avail, 1, MPI_INT, inc_status.MPI_SOURCE, MSG_RES_NEIGHBOR, comm2D);
                                        message_elapsed = MPI_Wtime() - message_start;
                                        message_metrics[MSG_RES_NEIGHBOR].sent++;
                                        message_metrics[MSG_RES_NEIGHBOR].send_bytes += (long long)(1) * sizeof(int);
                                        message_metrics[MSG_RES_NEIGHBOR].send_call_time += message_elapsed;
                                        message_metrics[MSG_RES_NEIGHBOR].send_lifetime += message_elapsed;
                                        comm_time += (MPI_Wtime() - comm_start_time);
                                    } else {
                                        usleep(1000);
                                    }
                                }
                            }
                            comm_start_time = MPI_Wtime();
                            MPI_Waitall(req_count, reqs, MPI_STATUSES_IGNORE);
                            double completed = MPI_Wtime();
                            message_metrics[MSG_REQ_NEIGHBOR].sent += req_count;
                            message_metrics[MSG_REQ_NEIGHBOR].send_bytes += (long long)req_count * sizeof(int);
                            for (int i = 0; i < req_count; i++)
                                message_metrics[MSG_REQ_NEIGHBOR].send_lifetime += completed - send_started[i];
                            comm_time += (MPI_Wtime() - comm_start_time);
                        }
                        //---------------------------------

                        comp_start_time = MPI_Wtime();
                        // Check neither it nor its immediate neighbors have any available ports
                        bool all_neigh_full = true;
                        for (int i = 0; i < 4; i++) {
                            if (neigh_ports[i] > THRESHOLD) all_neigh_full = false;
                        }

                        if (all_neigh_full) {
                            MsgData msg = {
                                .timestamp = MPI_Wtime() - start_time,  // Records the exact time the congestion happened
                                .sender_rank = global_rank,
                                .coords = {coord[0], coord[1]},
                                .avail_ports = loc_avail_ports, // Proof that this node is full
                                .avail_neigh = {neigh_ports[0], neigh_ports[1], neigh_ports[2], neigh_ports[3]}, // Proof that the neighbors are also full
                                .msg_exchanged = req_count * 2,  // Tracking how much message was generated
                                .msg_size = (req_count * 2) * sizeof(int)
                            };
                            comp_time += (MPI_Wtime() - comp_start_time);

                            comm_start_time = MPI_Wtime();
                            IncidentDateTime(msg.incident_time);
                            awaiting_base = true;
                            message_start = MPI_Wtime();
                            MPI_Send(&msg, 1, report_type, global_size - 1, MSG_REPORT, world_comm);
                            message_elapsed = MPI_Wtime() - message_start;
                            message_metrics[MSG_REPORT].sent++;
                            message_metrics[MSG_REPORT].send_bytes += (long long)(1) * report_bytes;
                            message_metrics[MSG_REPORT].send_call_time += message_elapsed;
                            message_metrics[MSG_REPORT].send_lifetime += message_elapsed;
                            comm_time += (MPI_Wtime() - comm_start_time);
                        } else {
                            LocalReferral(global_rank, coord, neigh_ports);
                            comp_time += (MPI_Wtime() - comp_start_time);
                        }
                    }
                    last_check = MPI_Wtime();
                }
                if (disabled && !awaiting_base && !ready_sent) {
                    int signal = 1;
                    comm_start_time = MPI_Wtime();
                    message_start = MPI_Wtime();
                    MPI_Send(&signal,1,MPI_INT,global_size-1,MSG_READY,world_comm);
                    message_elapsed = MPI_Wtime() - message_start;
                    message_metrics[MSG_READY].sent++;
                    message_metrics[MSG_READY].send_bytes += (long long)(1) * sizeof(int);
                    message_metrics[MSG_READY].send_call_time += message_elapsed;
                    message_metrics[MSG_READY].send_lifetime += message_elapsed;
                    comm_time += (MPI_Wtime() - comm_start_time);
                    ready_sent = true;
                }
                if (ready_sent) {
                    comm_start_time = MPI_Wtime();
                    MPI_Iprobe(global_size-1,MSG_TERMINATE,world_comm,&flag,&probe_status);
                    comm_time += (MPI_Wtime() - comm_start_time);
                    if (flag) {
                        int signal;
                        comm_start_time = MPI_Wtime();
                        message_start = MPI_Wtime();
                        MPI_Recv(&signal,1,MPI_INT,global_size-1,MSG_TERMINATE,world_comm,MPI_STATUS_IGNORE);
                        message_elapsed = MPI_Wtime() - message_start;
                        message_metrics[MSG_TERMINATE].received++;
                        message_metrics[MSG_TERMINATE].receive_bytes += (long long)(1) * sizeof(int);
                        message_metrics[MSG_TERMINATE].receive_call_time += message_elapsed;
                        message_metrics[MSG_TERMINATE].receive_lifetime += message_elapsed;
                        message_start = MPI_Wtime();
                        MPI_Send(&signal,1,MPI_INT,global_size-1,MSG_TERMINATED,world_comm);
                        message_elapsed = MPI_Wtime() - message_start;
                        message_metrics[MSG_TERMINATED].sent++;
                        message_metrics[MSG_TERMINATED].send_bytes += (long long)(1) * sizeof(int);
                        message_metrics[MSG_TERMINATED].send_call_time += message_elapsed;
                        message_metrics[MSG_TERMINATED].send_lifetime += message_elapsed;
                        comm_time += (MPI_Wtime() - comm_start_time);
                        break;
                    }
                }
                usleep(1000);
            }
            node_comm_time = comm_time;
        }

        #pragma omp atomic
        node_comp_time += comp_time;
    }

    // EV Charging Node internal overall time, computational time, communication time
    double overall_time = MPI_Wtime() - start_time;
    double comp_time = node_comp_time;
    double comm_time = node_comm_time;
    
    // Gather all times to rank 0 of the charging nodes
    int comm2D_rank, slave_size;
    MPI_Comm_rank(comm2D, &comm2D_rank);
    MPI_Comm_size(comm2D, &slave_size);

    double sum_overall = 0.0, sum_comm = 0.0, sum_comp = 0.0;
    MPI_Reduce(&overall_time, &sum_overall, 1, MPI_DOUBLE, MPI_SUM, 0, comm2D);
    MPI_Reduce(&comm_time, &sum_comm, 1, MPI_DOUBLE, MPI_SUM, 0, comm2D);
    MPI_Reduce(&comp_time, &sum_comp, 1, MPI_DOUBLE, MPI_SUM, 0, comm2D);

    if (comm2D_rank == 0) {
        double avg_overall = sum_overall / slave_size;
        double avg_comm = sum_comm / slave_size;
        double avg_comp = sum_comp / slave_size;
        printf(
            "[Charging Nodes Average] Overall: %.4fs | Comm: %.4fs | Comp: %.4fs\n",
            avg_overall, avg_comm, avg_comp
        );
        fflush(stdout);
    }
    MPI_Comm_free(&comm2D);
}

//===========================================
// Base Station
//===========================================
void MasterIO(MPI_Comm world_comm, MPI_Comm comm, int nrows, int ncols, MPI_Datatype report_type) {
    int global_size;
    int term_sig=1, ready_count=0, ack_count=0;
    int report_bytes;
    MPI_Type_size(report_type, &report_bytes);
    bool disabling=false, terminating=false;
    MPI_Comm_size(world_comm, &global_size);
    (void)comm;
    bool *ready_seen=calloc((size_t)(global_size-1),sizeof(bool));
    bool *ack_seen=calloc((size_t)(global_size-1),sizeof(bool));
    MPI_Request control_requests[2*(global_size-1)];
    double control_started[2*(global_size-1)];
    if (!ready_seen || !ack_seen) {
        fprintf(stderr, "EVCNS error: %s\n", "cannot allocate shutdown state");
        MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
        exit(EXIT_FAILURE);
    }
    for(int i=0;i<2*(global_size-1);i++) control_requests[i]=MPI_REQUEST_NULL;

    int grid_state[nrows][ncols];  // Global status matrix
    double last_report_time[nrows][ncols];  // Record the last congestion report time of each node
    double PREDEFINED_PERIOD = predefined_period;  // Predefined period values
    for (int i = 0; i < nrows; i++) {
        for (int j = 0; j < ncols; j++) {
            grid_state[i][j] = NUM_PORTS;
            last_report_time[i][j] = -999;
        }
    }

    FILE *log_file = fopen("log.txt", "w");
    if (!log_file) {
        printf("Error: Cannot open log file.\n");
        MPI_Abort(world_comm,EXIT_FAILURE);
    }

    MPI_Barrier(world_comm);
    double start_time = MPI_Wtime();  // Record time
    int ttl_alerts = 0;  // Record the number of happening congestion
    int ttl_events = 0;  // Record the number of communications
    int flag;  // Used by MPI_Iprobe, 1 means a msg is waiting to be read
    MPI_Status probe_status;  // Keep track who are the sender and what tag of msg sender send
    MsgData report;
    double comm_time = 0.0, comm_start_time;
    double message_start, message_elapsed;
    double comp_time = 0.0, comp_start_time;

    printf("[Base Station] Listening for reports...\n");
    fflush(stdout);
    fprintf(log_file, "----- SIMULATION STARTED -----\n");
    fprintf(log_file,
            "Configuration: version=v1_task3 rows=%d cols=%d processes=%d ports=%d threads=%d "
            "seconds=%.3f port_period=%.3f check_period=%.3f "
            "predefined_period=%.6f busy_percent=%d\n",
            nrows, ncols, global_size, NUM_PORTS, NUM_PORTS + 1,
            simulation_seconds, port_period, check_period,
            predefined_period, busy_percent);
    fflush(log_file);

    while(1) {
        comm_start_time = MPI_Wtime();
        MPI_Iprobe(MPI_ANY_SOURCE, MPI_ANY_TAG, world_comm, &flag, &probe_status);  // Listen any incoming messages
        comm_time += (MPI_Wtime() - comm_start_time);

        if (flag) {
            if (probe_status.MPI_TAG == MSG_READY || probe_status.MPI_TAG == MSG_TERMINATED) {
                int signal, peer=probe_status.MPI_SOURCE;
                comm_start_time = MPI_Wtime();
                message_start = MPI_Wtime();
                MPI_Recv(&signal,1,MPI_INT,peer,probe_status.MPI_TAG,world_comm,MPI_STATUS_IGNORE);
                message_elapsed = MPI_Wtime() - message_start;
                message_metrics[probe_status.MPI_TAG].received++;
                message_metrics[probe_status.MPI_TAG].receive_bytes += (long long)(1) * sizeof(int);
                message_metrics[probe_status.MPI_TAG].receive_call_time += message_elapsed;
                message_metrics[probe_status.MPI_TAG].receive_lifetime += message_elapsed;
                comm_time += (MPI_Wtime() - comm_start_time);
                if (peer<0 || peer>=global_size-1) {
                    fprintf(stderr, "EVCNS error: %s\n", "invalid shutdown sender");
                    MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
                    exit(EXIT_FAILURE);
                }
                if (probe_status.MPI_TAG==MSG_READY && !ready_seen[peer]) { ready_seen[peer]=true; ready_count++; }
                else if (probe_status.MPI_TAG==MSG_TERMINATED && !ack_seen[peer]) { ack_seen[peer]=true; ack_count++; }
                else {
                    fprintf(stderr, "EVCNS error: %s\n", "duplicate shutdown confirmation");
                    MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
                    exit(EXIT_FAILURE);
                }
            } else if (probe_status.MPI_TAG == MSG_REPORT) {  // If receive congestion message
                comm_start_time = MPI_Wtime();
                message_start = MPI_Wtime();
                MPI_Recv(&report, 1, report_type, probe_status.MPI_SOURCE, MSG_REPORT, world_comm, MPI_STATUS_IGNORE);
                message_elapsed = MPI_Wtime() - message_start;
                message_metrics[MSG_REPORT].received++;
                message_metrics[MSG_REPORT].receive_bytes += (long long)(1) * report_bytes;
                message_metrics[MSG_REPORT].receive_call_time += message_elapsed;
                message_metrics[MSG_REPORT].receive_lifetime += message_elapsed;
                comm_time += (MPI_Wtime() - comm_start_time);

                comp_start_time = MPI_Wtime();
                ttl_alerts++;
                ttl_events += report.msg_exchanged + 1;

                // Get the absolute current time at the Base Station
                // Acts as a single source of truth to eliminate distributed clock drift errors
                double curr_time = MPI_Wtime();

                // Update status of this congestion node
                grid_state[report.coords[0]][report.coords[1]] = report.avail_ports;

                // Record the timestamp using the Base Station's absolute clock 
                last_report_time[report.coords[0]][report.coords[1]] = curr_time;

                // Update neighbors' status of this congestion node
                int nbr_coords[4][2] = {
                    {report.coords[0] - 1, report.coords[1]},  // Top
                    {report.coords[0] + 1, report.coords[1]},  // Bottom
                    {report.coords[0], report.coords[1] - 1},  // Left
                    {report.coords[0], report.coords[1] + 1}   // Right
                };

                bool all_nbrs_congested_recently = true;
                int valid_nbrs = 0;

                for (int i = 0; i < 4; i++) {
                    if (report.avail_neigh[i] != NO_NEIGHBOR) {
                        int r = nbr_coords[i][0];
                        int c = nbr_coords[i][1];
                        if (r >= 0 && r < nrows && c >= 0 && c < ncols) {
                            grid_state[r][c] = report.avail_neigh[i];
                            valid_nbrs++;

                            // Check if this neighbor has never or its last congestion data has expired
                            if (last_report_time[r][c] < 0 || curr_time - last_report_time[r][c] > PREDEFINED_PERIOD) {
                                all_nbrs_congested_recently = false;
                            }
                        }
                    }
                }

                int max_candidates = global_size - 1; // Enough for all equal-nearest stations
                int best_coords[2 * (global_size - 1)];
                int found_count = 0;

                if (valid_nbrs > 0 && all_nbrs_congested_recently) {  // This area is heavily congested
                    fprintf(log_file, "T:%.2fs, No available nodes nearby\n", report.timestamp);
                    fflush(log_file);
                    comp_time += (MPI_Wtime() - comp_start_time);
                } else {  // The nodes may have free ports
                    //---------------------------------
                    // Original: Brute-Force (alternative to Diamond Scanning)
                    // Enable only ONE search method. Both use found_count / best_coords.
                    /*
                    int min_dist = -1;
                    for (int r = 0; r < nrows; r++) {
                        for (int c = 0; c < ncols; c++) {
                            if (grid_state[r][c] > THRESHOLD &&
                                (r != report.coords[0] || c != report.coords[1])) {
                                int dist = abs(r - report.coords[0]) + abs(c - report.coords[1]);
                                if (min_dist < 0 || dist < min_dist) {
                                    min_dist = dist;
                                    found_count = 0;  // Discard farther candidates
                                }
                                if (dist == min_dist && found_count < max_candidates) {
                                    best_coords[found_count * 2] = r;
                                    best_coords[found_count * 2 + 1] = c;
                                    found_count++;
                                }
                            }
                        }
                    }
                    */
                    //---------------------------------
                    //---------------------------------
                    // Optimization: A BFS for available ports by expanding outward in a ripple-like manner
                    //---------------------------------
                    int max_dist = nrows + ncols;
                    bool found = false;

                    for (int d = 1; d <= max_dist && !found; d++) {  // Expand layer by layer
                        for (int dx = -d; dx <= d; dx++) {
                            int dy = d - abs(dx);  // Manhattan Distance (Diamond Scanning)

                            // Positive direction
                            int r = report.coords[0] + dx;
                            int c = report.coords[1] + dy;
                            if (r >= 0 && r < nrows && c >= 0 && c < ncols) {
                                if (grid_state[r][c] > THRESHOLD && found_count < max_candidates) {
                                    best_coords[found_count * 2] = r;  // Mapping 2D to 1D
                                    best_coords[found_count * 2 + 1] = c;
                                    found_count++;
                                    found = true;
                                }
                            }

                            // Negative direction
                            if (dy != 0) {
                                c = report.coords[1] - dy;
                                if (r >= 0 && r < nrows && c >= 0 && c < ncols) {
                                    if (grid_state[r][c] > THRESHOLD && found_count < max_candidates) {
                                        best_coords[found_count * 2] = r;
                                        best_coords[found_count * 2 + 1] = c;
                                        found_count++;
                                        found = true;
                                    }
                                }
                            }
                        }
                    }
                    //---------------------------------
                    comp_time += (MPI_Wtime() - comp_start_time);
                }

                // Reply back to that congestion node for available ports
                comm_start_time = MPI_Wtime();
                message_start = MPI_Wtime();
                MPI_Send(&found_count, 1, MPI_INT, probe_status.MPI_SOURCE, MSG_BS_REPLY, world_comm);
                message_elapsed = MPI_Wtime() - message_start;
                message_metrics[MSG_BS_REPLY].sent++;
                message_metrics[MSG_BS_REPLY].send_bytes += (long long)(1) * sizeof(int);
                message_metrics[MSG_BS_REPLY].send_call_time += message_elapsed;
                message_metrics[MSG_BS_REPLY].send_lifetime += message_elapsed;
                if (found_count > 0) {
                    message_start = MPI_Wtime();
                    MPI_Send(best_coords, found_count * 2, MPI_INT, probe_status.MPI_SOURCE, MSG_BS_REPLY, world_comm);
                    message_elapsed = MPI_Wtime() - message_start;
                    message_metrics[MSG_BS_REPLY].sent++;
                    message_metrics[MSG_BS_REPLY].send_bytes += (long long)(found_count * 2) * sizeof(int);
                    message_metrics[MSG_BS_REPLY].send_call_time += message_elapsed;
                    message_metrics[MSG_BS_REPLY].send_lifetime += message_elapsed;
                }
                comm_time += (MPI_Wtime() - comm_start_time);
                
                int best_row = (found_count > 0) ? best_coords[0] : -1;
                int best_col = (found_count > 0) ? best_coords[1] : -1;

                // Record into log
                fprintf(
                    log_file, "T:%.2fs | ALERT [%d] | Rank: %d Coord: (%d,%d) | Ports Free: %d | Neighbor Free: [T:%d, B:%d, L:%d, R:%d] | Msgs: %d | Msg Size: %d Bytes | Nearest: (%d,%d)\n",
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
                    report.msg_size,
                    best_row, best_col
                );

                fprintf(log_file,"Incident date/time: %s\n",report.incident_time);
                fprintf(log_file,"Recommended nodes: %d",found_count);
                for(int k=0;k<found_count;k++) fprintf(log_file," (%d,%d)",best_coords[2*k],best_coords[2*k+1]);
                fprintf(log_file,"\n");

                // Print the grid
                PrintGrid(log_file, report.timestamp, nrows, ncols, grid_state, report.coords[0], report.coords[1], best_row, best_col);
                fflush(log_file);
            }
        } else {
            usleep(100);
        }

        if (!disabling && MPI_Wtime()-start_time>=simulation_seconds) {
            disabling=true;
            printf("[Base Station] Time limit reached. Finishing active enquiries...\n");
            comm_start_time = MPI_Wtime();
            for(int i=0;i<global_size-1;i++) {
                control_started[i] = MPI_Wtime();
                MPI_Isend(&term_sig,1,MPI_INT,i,MSG_DISABLE,world_comm,&control_requests[i]);
                message_metrics[MSG_DISABLE].send_call_time += MPI_Wtime() - control_started[i];
            }
            comm_time += (MPI_Wtime() - comm_start_time);
        }
        if (disabling && ready_count==global_size-1 && !terminating) {
            terminating=true;
            comm_start_time = MPI_Wtime();
            for(int i=0;i<global_size-1;i++) {
                control_started[global_size-1+i] = MPI_Wtime();
                MPI_Isend(&term_sig,1,MPI_INT,i,MSG_TERMINATE,world_comm,&control_requests[global_size-1+i]);
                message_metrics[MSG_TERMINATE].send_call_time += MPI_Wtime() - control_started[global_size-1+i];
            }
            comm_time += (MPI_Wtime() - comm_start_time);
        }
        if (terminating && ack_count==global_size-1) break;
    }
    comm_start_time = MPI_Wtime();
    MPI_Waitall(2*(global_size-1),control_requests,MPI_STATUSES_IGNORE);
    double control_completed = MPI_Wtime();
    message_metrics[MSG_DISABLE].sent += global_size - 1;
    message_metrics[MSG_TERMINATE].sent += global_size - 1;
    message_metrics[MSG_DISABLE].send_bytes += (long long)(global_size - 1) * sizeof(int);
    message_metrics[MSG_TERMINATE].send_bytes += (long long)(global_size - 1) * sizeof(int);
    for (int i = 0; i < global_size - 1; i++) {
        message_metrics[MSG_DISABLE].send_lifetime += control_completed - control_started[i];
        message_metrics[MSG_TERMINATE].send_lifetime += control_completed - control_started[global_size-1+i];
    }
    comm_time += (MPI_Wtime() - comm_start_time);
    free(ready_seen);
    free(ack_seen);

    // Base Station overall time, computational time, communication time
    double overall_time = MPI_Wtime() - start_time;

    fprintf(log_file, "----- SIMULATION SUMMARY -----\n");
    fprintf(log_file, "Total Simulation Time: %.4f seconds\n", overall_time);
    fprintf(log_file, "Total Comm Time: %.4f seconds\n", comm_time);
    fprintf(log_file, "Total Comp Time: %.4f seconds\n", comp_time);
    fprintf(log_file, "Total Congestion Alerts: %d\n", ttl_alerts);
    fprintf(log_file, "Reported neighbor/C3 events (subset): %d\n", ttl_events);
    fclose(log_file);
    printf("[Base Station] Overall: %.4fs | Comm: %.4fs | Comp: %.4fs\n", overall_time, comm_time, comp_time);
    fflush(stdout);
    printf("[Base Station] Log saved successfully.\n");
    fflush(stdout);
}

int main(int argc, char **argv) {
    int rank, size, nrows = 3, ncols = 3;
    int permission;
    MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &permission);

    if (permission < MPI_THREAD_MULTIPLE) {
        printf("Error: MPI_THREAD_MULTIPLE is not supported.\n");
        fflush(stdout);
        MPI_Abort(MPI_COMM_WORLD, 1);
    }

    MPI_Comm_size(MPI_COMM_WORLD, &size);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    if(argc!=1 && argc!=3) {
        fprintf(stderr, "EVCNS error: %s\n", "usage: evcns [rows cols]");
        MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
        exit(EXIT_FAILURE);
    }
    if(argc==3) {
        nrows=(int)ParseInteger(argv[1],1,INT_MAX);
        ncols=(int)ParseInteger(argv[2],1,INT_MAX);
    }
    if((long long)nrows*ncols!=(long long)size-1 || size-1>INT_MAX/2) {
        fprintf(stderr, "EVCNS error: %s\n", "rows*cols must equal process count minus one base");
        MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
        exit(EXIT_FAILURE);
    }

    // Only the report freshness window varies between period experiments.
    const char *period_text = getenv("EVCNS_PREDEFINED_PERIOD");
    if (period_text != NULL) {
        char *end;
        errno = 0;
        double value = strtod(period_text, &end);
        if (errno != 0 || end == period_text || *end ||
            !(value > 0.0 && value < 86400.0)) {
            fprintf(stderr, "EVCNS error: predefined period must be positive, finite and below 86400s\n");
            MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
            exit(EXIT_FAILURE);
        }
        predefined_period = value;
    }

    if(getenv("EVCNS_BUSY_PERCENT")) busy_percent=(int)ParseInteger(getenv("EVCNS_BUSY_PERCENT"),0,100);

    omp_set_dynamic(0);

    MPI_Datatype report_type;
    CreateDataType(&report_type);

    MPI_Comm new_comm;
    MPI_Comm_split(MPI_COMM_WORLD, rank == size - 1, 0, &new_comm);

    if (rank == size - 1) {
        MasterIO(MPI_COMM_WORLD, new_comm, nrows, ncols, report_type);
    } else {
        SlaveIO(MPI_COMM_WORLD, new_comm, nrows, ncols, report_type);
    }

    MPI_Barrier(MPI_COMM_WORLD);
    WriteMetrics(nrows,ncols,1);
    MPI_Type_free(&report_type);
    MPI_Comm_free(&new_comm);
    MPI_Finalize();
    return 0;
}