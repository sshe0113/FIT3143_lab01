/**
 * @file      evcns_v1_blocking.c
 * @brief     Electric Vehicle Charging Navigation System (EVCNS) Version 1
 *
 * @details
 *
 * @author    Shee Seng Cheng (34612467) - sshe0113@student.monash.edu
 * @author    Tay Chee Hsian (34612513) - ctay0040@student.monash.edu
 * @note      Coding Implementation for EVCNS (Scheduled Blocking Architecture)
 */

#include "evcns_utils.h"

//===========================================
// EV Charging Node (Version 1: Scheduled Blocking)
//===========================================
void SlaveIO(MPI_Comm world_comm, MPI_Comm comm, int nrows, int ncols, MPI_Datatype report_type)
{
    /**
     * ### MPI Setup and Topology Creation
     * Initializes the MPI environment variables and constructs a 2D Cartesian grid topology for the charging nodes. 
     * It is used to establish the spatial relationships between nodes, identifying the immediate top, bottom, left, 
     * and right neighbors required for localized communication.
     */
    int global_size, global_rank;
    MPI_Comm_size(world_comm, &global_size);
    MPI_Comm_rank(world_comm, &global_rank);

    srand(time(NULL) + global_rank);
    int dims[2] = {nrows, ncols}, coord[2], wrap_around[2] = {0, 0};  
    int nbr_i_lo, nbr_i_hi, nbr_j_lo, nbr_j_hi;  
    MPI_Comm comm2D;

    MPI_Cart_create(comm, 2, dims, wrap_around, 0, &comm2D);
    MPI_Cart_coords(comm2D, global_rank, 2, coord);
    MPI_Cart_shift(comm2D, SHIFT_R0W, DISP, &nbr_i_lo, &nbr_i_hi);
    MPI_Cart_shift(comm2D, SHIFT_COL, DISP, &nbr_j_lo, &nbr_j_hi);

    /**
     * ### OpenMP Thread Initialization
     * Sets up the initial port states and spawns a designated number of OpenMP threads to execute concurrent tasks. 
     * It ensures the simulation runs correctly by allocating exactly one worker thread per charging port alongside 
     * a single main thread dedicated to MPI communication.
     */
    int ports[NUM_PORTS];
    for (int i = 0; i < NUM_PORTS; i++) ports[i] = 1;
    bool running_flag = true;  
    double start_time = MPI_Wtime();  
    double node_comp_time = 0.0;
    double node_comm_time = 0.0;

    #pragma omp parallel num_threads(NUM_PORTS + 1) shared(ports, running_flag, start_time, node_comp_time, node_comm_time) 
    {
        int thread_id = omp_get_thread_num();
        double comp_time = 0.0, comp_start_time;

        /**
         * ### Port State Simulation (Worker Threads)
         * These worker threads asynchronously simulate real-world electric vehicle behavior by randomly 
         * toggling the availability of individual charging ports based on a predefined probability.
         * This mechanism is used to generate dynamic, independent load fluctuations across the charging 
         * network without blocking the node's main communication thread.
         */
        if (thread_id < NUM_PORTS) {
            double last_update = MPI_Wtime();  
            bool running;
            #pragma omp atomic read
            running = running_flag;

            while (running) {
                if (MPI_Wtime() - last_update > PORT_PERIOD) {
                    comp_start_time = MPI_Wtime();
                    #pragma omp critical  
                    {  
                        ports[thread_id] = (rand() % 100 < CONGEST_RATE) ? 0 : 1;  
                    }
                    comp_time += (MPI_Wtime() - comp_start_time);
                    last_update = MPI_Wtime();
                }
                usleep(1000);

                #pragma omp atomic read
                running = running_flag;
            }
        } else {  
            /**
             * ### Main Thread: Centralized Scheduling Loop Setup
             * New.
             * ### What it is:
             * Initializes the communication timer and flags for the blocking architecture.
             * ### What it is used for:
             * It prepares the main thread to constantly poll for termination commands or incoming inquiries while awaiting its scheduled turn from the Base Station.
             */
            bool running = true;
            int flag;  
            MPI_Status probe_status;  
            double comm_time = 0.0, comm_start_time;

            while (running) {
                /**
                 * ### Main Thread: Disable Signal Handling
                 * Continuously polls for a globally broadcasted MSG_TERMINATE signal originating from the Base Station. 
                 * It is used to gracefully halt the generation of new internal port states and trigger a timed "drain mode" once the fixed simulation duration concludes.
                 */
                comm_start_time = MPI_Wtime();
                MPI_Iprobe(global_size - 1, MSG_TERMINATE, world_comm, &flag, &probe_status);
                comm_time += (MPI_Wtime() - comm_start_time);

                if (flag) {
                    int term_sig; 
                    comm_start_time = MPI_Wtime();
                    MPI_Recv(&term_sig, 1, MPI_INT, global_size - 1, MSG_TERMINATE, world_comm, MPI_STATUS_IGNORE);
                    comm_time += (MPI_Wtime() - comm_start_time);

                    #pragma omp atomic write
                    running_flag = false;

                    /**
                     * ### Main Thread: Drain Mode Processing
                     * New.
                     * ### What it is:
                     * A short-lived event loop that runs for 3.5 seconds after receiving the termination signal.
                     * ### What it is used for:
                     * It catches and discards any late-arriving requests or Base Station replies that were in-flight during shutdown, preventing deadlocks or unread message errors.
                     */
                    double drain_start = MPI_Wtime();
                    while (MPI_Wtime() - drain_start < 3.5) {
                        usleep(1000);
                        int req_flag;
                        MPI_Status drain_status;

                        comm_start_time = MPI_Wtime();
                        MPI_Iprobe(MPI_ANY_SOURCE, MSG_REQ_NEIGHBOR, comm2D, &req_flag, &drain_status);
                        comm_time += (MPI_Wtime() - comm_start_time);

                        if (req_flag) {
                            int dummy;

                            comm_start_time = MPI_Wtime();
                            MPI_Recv(&dummy, 1, MPI_INT, drain_status.MPI_SOURCE, MSG_REQ_NEIGHBOR, comm2D, MPI_STATUS_IGNORE);
                            comm_time += (MPI_Wtime() - comm_start_time);

                            comp_start_time = MPI_Wtime();
                            int curr_avail = 0;
                            #pragma omp critical 
                            {
                                for (int p = 0; p < NUM_PORTS; p++) curr_avail += ports[p];
                            }
                            comp_time += (MPI_Wtime() - comp_start_time);

                            comm_start_time = MPI_Wtime();
                            MPI_Send(&curr_avail, 1, MPI_INT, drain_status.MPI_SOURCE, MSG_RES_NEIGHBOR, comm2D);
                            comm_time += (MPI_Wtime() - comm_start_time);
                        }

                        int bs_flag;
                        comm_start_time = MPI_Wtime();
                        MPI_Iprobe(global_size - 1, MSG_BS_REPLY, world_comm, &bs_flag, MPI_STATUS_IGNORE);
                        comm_time += (MPI_Wtime() - comm_start_time);

                        if (bs_flag) {
                            int dummy_count;
                            comm_start_time = MPI_Wtime();
                            MPI_Recv(&dummy_count, 1, MPI_INT, global_size - 1, MSG_BS_REPLY, world_comm, MPI_STATUS_IGNORE);
                            if (dummy_count > 0) {
                                int dummy_coords[2 * (global_size - 1)];
                                MPI_Recv(dummy_coords, dummy_count * 2, MPI_INT, global_size - 1, MSG_BS_REPLY, world_comm, MPI_STATUS_IGNORE);
                            }
                            comm_time += (MPI_Wtime() - comm_start_time);
                        }
                    }
                    break;
                }

                /**
                 * ### Main Thread: Neighbor Request Processing
                 * Listens for and intercepts incoming MSG_REQ_NEIGHBOR inquiries 
                 * from adjacent charging nodes actively looking for free slots. 
                 * It safely calculates the locally available ports using an OpenMP critical section and 
                 * immediately replies, assisting the neighbor's decentralized load-balancing efforts.
                 */
                comm_start_time = MPI_Wtime();
                MPI_Iprobe(MPI_ANY_SOURCE, MSG_REQ_NEIGHBOR, comm2D, &flag, &probe_status);
                comm_time += (MPI_Wtime() - comm_start_time);

                if (flag) {
                    int req_sig;
                    comm_start_time = MPI_Wtime();
                    MPI_Recv(&req_sig, 1, MPI_INT, probe_status.MPI_SOURCE, MSG_REQ_NEIGHBOR, comm2D, MPI_STATUS_IGNORE);
                    comm_time += (MPI_Wtime() - comm_start_time);

                    comp_start_time = MPI_Wtime();
                    int curr_avail = 0; 
                    #pragma omp critical 
                    {
                        for (int i = 0; i < NUM_PORTS; i++) curr_avail += ports[i];
                    }
                    comp_time += (MPI_Wtime() - comp_start_time);

                    comm_start_time = MPI_Wtime();
                    MPI_Send(&curr_avail, 1, MPI_INT, probe_status.MPI_SOURCE, MSG_RES_NEIGHBOR, comm2D);
                    comm_time += (MPI_Wtime() - comm_start_time);
                }

                /**
                 * ### Main Thread: Scheduled Congestion Checking & Neighbor Polling
                 * New.
                 * ### What it is:
                 * A token-based execution block triggered only when receiving a MSG_QUERY_GRANT from the Base Station. 
                 * ### What it is used for:
                 * It ensures strictly serialized local congestion checking, forcing the node to execute blocking MPI_Send and MPI_Recv sequentially to all neighbors, replacing parallel probing.
                 */
                comm_start_time = MPI_Wtime();
                MPI_Iprobe(global_size - 1, MSG_QUERY_GRANT, world_comm, &flag, &probe_status);
                comm_time += (MPI_Wtime() - comm_start_time);
                if (flag) {
                    int grant;
                    comm_start_time = MPI_Wtime();
                    MPI_Recv(&grant, 1, MPI_INT, global_size - 1, MSG_QUERY_GRANT, world_comm, MPI_STATUS_IGNORE);
                    comm_time += (MPI_Wtime() - comm_start_time);
                    
                    comp_start_time = MPI_Wtime();
                    int loc_avail_ports = 0;
                    #pragma omp critical 
                    {
                        for (int i = 0; i < NUM_PORTS; i++) loc_avail_ports += ports[i];
                    }
                    comp_time += (MPI_Wtime() - comp_start_time);

                    if (loc_avail_ports <= THRESHOLD) {
                        comp_start_time = MPI_Wtime();
                        int req = 1; 
                        int neighbors[4] = {nbr_i_lo, nbr_i_hi, nbr_j_lo, nbr_j_hi};
                        int neigh_ports[4] = {NO_NEIGHBOR, NO_NEIGHBOR, NO_NEIGHBOR, NO_NEIGHBOR};
                        int req_count = 0;
                        comp_time += (MPI_Wtime() - comp_start_time);

                        comm_start_time = MPI_Wtime();
                        for (int i = 0; i < 4; i++) {
                            if (neighbors[i] != MPI_PROC_NULL) {
                                MPI_Send(&req, 1, MPI_INT, neighbors[i], MSG_REQ_NEIGHBOR, comm2D);
                                MPI_Recv(&neigh_ports[i], 1, MPI_INT, neighbors[i], MSG_RES_NEIGHBOR, comm2D, MPI_STATUS_IGNORE);
                                req_count++;
                            }
                        }
                        comm_time += (MPI_Wtime() - comm_start_time);

                        /**
                         * ### Main Thread: Congestion Reporting & Local Referral
                         * If both the current node and its surrounding neighbors are found to be severely congested, this block 
                         * packages the contextual data into a MsgData struct and dispatches a critical MSG_REPORT to the Base Station. 
                         * Conversely, if any neighbor is found to have available space, it bypasses the Base Station entirely and 
                         * directly invokes a local referral to redirect the EV traffic.
                         */
                        comp_start_time = MPI_Wtime();
                        bool all_neigh_full = true;
                        for (int i = 0; i < 4; i++) {
                            if (neigh_ports[i] > THRESHOLD) all_neigh_full = false;
                        }

                        if (all_neigh_full) {
                            MsgData msg = {
                                .timestamp = MPI_Wtime() - start_time,
                                .sender_rank = global_rank,
                                .coords = {coord[0], coord[1]},
                                .avail_ports = loc_avail_ports,
                                .avail_neigh = {neigh_ports[0], neigh_ports[1], neigh_ports[2], neigh_ports[3]},
                                .msg_exchanged = req_count * 2,
                                .msg_size = (req_count * 2) * sizeof(int)
                            };
                            comp_time += (MPI_Wtime() - comp_start_time);

                            comm_start_time = MPI_Wtime();
                            MPI_Send(&msg, 1, report_type, global_size - 1, MSG_REPORT, world_comm);
                            comm_time += (MPI_Wtime() - comm_start_time);

                            /**
                             * ### Main Thread: Base Station Recommendation Handling
                             * New.
                             * ### What it is:
                             * Directly executes a blocking MPI_Recv to fetch the Base Station's fallback routing list immediately after sending the report.
                             * ### What it is used for:
                             * It guarantees the entire congestion evaluation pipeline is completely resolved before releasing the node's scheduled execution turn.
                             */
                            int found_count;
                            comm_start_time = MPI_Wtime();
                            MPI_Recv(&found_count, 1, MPI_INT, global_size - 1, MSG_BS_REPLY, world_comm, MPI_STATUS_IGNORE);
                            if (found_count == 0) {
                                printf("[Node Rank %d at (%d,%d)] No available nodes nearby\n", global_rank, coord[0], coord[1]);
                                fflush(stdout);
                            } else {
                                int reply_coords[2 * (global_size - 1)];
                                MPI_Recv(reply_coords, found_count * 2, MPI_INT, global_size - 1, MSG_BS_REPLY, world_comm, MPI_STATUS_IGNORE);
                                printf("[Node Rank %d at (%d,%d)] Nearest has %d available node(s): ", global_rank, coord[0], coord[1], found_count);
                                for (int i = 0; i < found_count; i++) {
                                    printf("(%d,%d) ", reply_coords[i * 2], reply_coords[i * 2 + 1]);
                                }
                                printf("to EVs\n");
                            }
                            fflush(stdout);
                            comm_time += (MPI_Wtime() - comm_start_time);
                        } else {
                            LocalReferral(global_rank, coord, neigh_ports);
                            comp_time += (MPI_Wtime() - comp_start_time);
                        }
                    }
                    /**
                     * ### Main Thread: Completion Handshake
                     * New.
                     * ### What it is:
                     * A synchronous notification back to the Base Station signaling the end of the node's granted window.
                     * ### What it is used for:
                     * It releases the global lock, instructing the Base Station to assign the MSG_QUERY_GRANT token to the next node in the round-robin sequence.
                     */
                    int done = 1;
                    comm_start_time = MPI_Wtime();
                    MPI_Send(&done, 1, MPI_INT, global_size - 1, MSG_QUERY_DONE, world_comm);
                    comm_time += (MPI_Wtime() - comm_start_time);
                }
                usleep(1000);
            }
        } // End of else block (Main thread)

        if (thread_id == NUM_PORTS) {
            node_comp_time = comp_time;
            node_comm_time = comm_time;
        }
    }

    /**
     * ### Performance Metric Reduction
     * Extracts the independent computation and communication timings gathered 
     * throughout the node's lifecycle and aggregates them globally using MPI_Reduce. 
     * It is used to calculate and print the average operational times across all charging nodes, 
     * providing crucial data for evaluating the overhead of the parallel simulation design.
     */
    double overall_time = MPI_Wtime() - start_time;
    double comp_time = node_comp_time;
    double comm_time = node_comm_time;
    
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
// Base Station (Version 1: Scheduled Blocking)
//===========================================
void MasterIO(MPI_Comm world_comm, MPI_Comm comm, int nrows, int ncols, MPI_Datatype report_type) {
    /**
     * ### Variable Initialization and Memory Allocation
     * Initializes the Base Station's core variables and allocates memory for tracking the shutdown states of all worker nodes. 
     * It is used to set up the global tracking arrays and asynchronous request handles necessary for executing a synchronized 
     * simulation shutdown.
     */
    int global_size;
    MPI_Comm_size(world_comm, &global_size);

    /**
     * ### Initialization of Global State Matrix and Log Files
     * This section initializes the global 2D matrix representing port availability and prepares the main simulation log file. 
     * It acts as the central state registry to track recent congestion events and records the initial configuration parameters 
     * before the main loop begins.
     */
    int grid_state[nrows][ncols];  
    double last_report_time[nrows][ncols];  
    for (int i = 0; i < nrows; i++) {
        for (int j = 0; j < ncols; j++) {
            grid_state[i][j] = NUM_PORTS;
            last_report_time[i][j] = -999;
        }
    }

    FILE *log_file = fopen("log.txt", "w");
    if (!log_file) {
        printf("Error: Cannot open log file.\n");
        return;
    }

    double start_time = MPI_Wtime();  
    int ttl_alerts = 0;  
    int ttl_events = 0;  
    int flag;  
    MPI_Status probe_status;  
    MsgData report;
    double comm_time = 0.0, comm_start_time;
    double comp_time = 0.0, comp_start_time;

    /**
     * ### Base Station: Token Passing Setup
     * New.
     * ### What it is:
     * Declares variables pointing to the currently active charging node and enforces a three-second interval between global check loops.
     * ### What it is used for:
     * It establishes the centralized scheduling policy, guaranteeing that network-wide congestion queries are executed sequentially.
     */
    int active_station = -1;
    int next_station = 0;
    double next_round_time = start_time + CHECK_PERIOD;

    printf("[Base Station] Listening for reports...\n");
    fflush(stdout);
    fprintf(log_file, "----- SIMULATION STARTED -----\n");
    fflush(log_file);

    while(1) {
        /**
         * ### Message Listening and Termination Signal Handshake
         * This block actively probes the network for incoming messages and 
         * processes the graceful shutdown handshake signals from slave nodes. 
         * It is used to securely tally which charging nodes have ceased operations 
         * and acknowledged the termination protocol to prevent system deadlocks.
         */
        comm_start_time = MPI_Wtime();
        MPI_Iprobe(MPI_ANY_SOURCE, MPI_ANY_TAG, world_comm, &flag, &probe_status);  
        comm_time += (MPI_Wtime() - comm_start_time);

        if (flag) {
            /**
             * ### Base Station: Query Completion Verification
             * New.
             * ### What it is:
             * Retrieves the MSG_QUERY_DONE validation flag and systematically advances the authorization pointer to the next charging node.
             * ### What it is used for:
             * It securely revokes the current lock and cascades the polling operation through the grid topology until every node has been checked.
             */
            if (probe_status.MPI_TAG == MSG_QUERY_DONE) {
                int done;
                comm_start_time = MPI_Wtime();
                MPI_Recv(&done, 1, MPI_INT, probe_status.MPI_SOURCE, MSG_QUERY_DONE, world_comm, MPI_STATUS_IGNORE);
                comm_time += (MPI_Wtime() - comm_start_time);
                
                if (probe_status.MPI_SOURCE != active_station) {
                    fprintf(stderr, "Error: V1 completion came from a station without permission.\n");
                    MPI_Abort(world_comm, EXIT_FAILURE);
                }
                active_station = -1;
                next_station++;
                if (next_station == global_size - 1) {
                    next_station = 0;
                    next_round_time = MPI_Wtime() + CHECK_PERIOD;
                }
            } else if (probe_status.MPI_TAG == MSG_REPORT) {  
                /**
                 * ### Congestion Report Receipt and Status Updates
                 * Intercepts congestion alerts, updates the central grid state, and validates 
                 * if the reporting node's immediate neighbors are also recently congested. 
                 * It serves as the primary ingestion point for network alerts, utilizing the 
                 * Base Station's absolute clock to eliminate distributed time drift errors.
                 */
                comm_start_time = MPI_Wtime();
                MPI_Recv(&report, 1, report_type, probe_status.MPI_SOURCE, MSG_REPORT, world_comm, MPI_STATUS_IGNORE);
                comm_time += (MPI_Wtime() - comm_start_time);

                comp_start_time = MPI_Wtime();
                ttl_alerts++;
                ttl_events += report.msg_exchanged + 1;

                double curr_time = MPI_Wtime();

                grid_state[report.coords[0]][report.coords[1]] = report.avail_ports;
                last_report_time[report.coords[0]][report.coords[1]] = curr_time;

                int nbr_coords[4][2] = {
                    {report.coords[0] - 1, report.coords[1]}, 
                    {report.coords[0] + 1, report.coords[1]}, 
                    {report.coords[0], report.coords[1] - 1}, 
                    {report.coords[0], report.coords[1] + 1}  
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

                            if (last_report_time[r][c] < 0 || curr_time - last_report_time[r][c] > PRED_PERIOD) {
                                all_nbrs_congested_recently = false;
                            }
                        }
                    }
                }

                int max_candidates = global_size - 1; 
                int best_coords[2 * (global_size - 1)];
                int found_count = 0;

                if (valid_nbrs > 0 && all_nbrs_congested_recently) {  
                    fprintf(log_file, "T:%.2fs, No available nodes nearby\n", report.timestamp);
                    fflush(log_file);
                } else {  
                    /**
                     * ### Manhattan Ripple Search to find available nodes
                     * This algorithm expands outward in a diamond-shaped ripple pattern to 
                     * locate the geographically nearest charging nodes with available ports. 
                     * It is used to dynamically compile a list of optimal alternative stations for 
                     * rerouting incoming electric vehicles when a local area becomes fully saturated.
                     */
                    int max_dist = nrows + ncols;
                    bool found = false;

                    for (int d = 1; d <= max_dist && !found; d++) {  
                        for (int dx = -d; dx <= d; dx++) {
                            int dy = d - abs(dx);  

                            int r = report.coords[0] + dx;
                            int c = report.coords[1] + dy;
                            if (r >= 0 && r < nrows && c >= 0 && c < ncols) {
                                if (grid_state[r][c] > THRESHOLD && found_count < max_candidates) {
                                    best_coords[found_count * 2] = r;  
                                    best_coords[found_count * 2 + 1] = c;
                                    found_count++;
                                    found = true;
                                }
                            }

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
                }
                comp_time += (MPI_Wtime() - comp_start_time);

                /**
                 * ### Sending Dispatch Responses and Grid Status Records
                 * Transmits the discovered alternative coordinates back to the reporting node 
                 * and records a detailed visual snapshot of the grid. 
                 * It ensures the congested node receives immediate routing instructions while permanently 
                 * logging the incident's spatial context for analytical review.
                 */
                comm_start_time = MPI_Wtime();
                MPI_Send(&found_count, 1, MPI_INT, probe_status.MPI_SOURCE, MSG_BS_REPLY, world_comm);
                if (found_count > 0) {
                    MPI_Send(best_coords, found_count * 2, MPI_INT, probe_status.MPI_SOURCE, MSG_BS_REPLY, world_comm);
                }
                comm_time += (MPI_Wtime() - comm_start_time);
                
                int best_row = (found_count > 0) ? best_coords[0] : -1;
                int best_col = (found_count > 0) ? best_coords[1] : -1;

                fprintf(
                    log_file, "T:%.2fs | ALERT [%d] | Rank: %d Coord: (%d,%d) | Ports Free: %d | Neighbor Free: [T:%d, B:%d, L:%d, R:%d] | Msgs: %d | Msg Size: %d Bytes | Nearest: (%d,%d)\n",
                    report.timestamp,          
                    ttl_alerts,                
                    report.sender_rank, 
                    report.coords[0], 
                    report.coords[1], 
                    report.avail_ports,        
                    report.avail_neigh[0],     
                    report.avail_neigh[1], 
                    report.avail_neigh[2], 
                    report.avail_neigh[3], 
                    report.msg_exchanged,      
                    report.msg_size,
                    best_row, best_col
                );

                PrintGrid(log_file, report.timestamp, nrows, ncols, grid_state, report.coords[0], report.coords[1], best_row, best_col);
                fflush(log_file);
            }
        } else {
            usleep(100);
        }

        /**
         * ### Base Station: Token Dispenser
         * New.
         * ### What it is:
         * Evaluates timing constraints to emit the MSG_QUERY_GRANT token to the next node in line.
         * ### What it is used for:
         * It drives the centralized operation loop, granting individual polling permissions strictly after completing the minimum delay bounds.
         */
        double scheduler_now = MPI_Wtime();
        if (active_station < 0 && scheduler_now - start_time < SIMULATION_SEC &&
            scheduler_now >= next_round_time) {
            int grant = 1;
            active_station = next_station;
            comm_start_time = MPI_Wtime();
            MPI_Send(&grant, 1, MPI_INT, active_station, MSG_QUERY_GRANT, world_comm);
            comm_time += (MPI_Wtime() - comm_start_time);
        }

        /**
         * ### Simulated Timeout Detection and Shutdown Signaling Broadcast
         * Monitors the total elapsed simulation time and broadcasts sequential 
         * non-blocking disable and terminate commands to all active nodes. 
         * It is used to strictly enforce the simulation's fixed duration and to 
         * orchestrate a two-phase graceful shutdown across the distributed cluster.
         */
        if (MPI_Wtime() - start_time > SIMULATION_SEC && active_station < 0) {
            printf("[Base Station] Time limit reached. Shutting down...\n");
            fflush(stdout);
            int term_sig = 1;

            comm_start_time = MPI_Wtime();
            for (int i = 0; i < global_size - 1; i++) {
                MPI_Send(&term_sig, 1, MPI_INT, i, MSG_TERMINATE, world_comm); 
            }
            comm_time += (MPI_Wtime() - comm_start_time);

            /**
             * ### Base Station: Drain Mode Processing
             * New.
             * ### What it is:
             * Enforces a hard, final wait loop specifically handling terminal messages.
             * ### What it is used for:
             * It silently intercepts orphaned network artifacts before finally severing the MPI fabric execution line.
             */
            double drain_start = MPI_Wtime();
            comm_start_time = MPI_Wtime();
            while (MPI_Wtime() - drain_start < 4.0) {
                int drain_flag;
                MPI_Status drain_status;

                MPI_Iprobe(MPI_ANY_SOURCE, MPI_ANY_TAG, world_comm, &drain_flag, &drain_status);
                
                if (drain_flag) {
                    if (drain_status.MPI_TAG == MSG_REPORT) {
                        MsgData dummy_report;
                        MPI_Recv(&dummy_report, 1, report_type, drain_status.MPI_SOURCE, MSG_REPORT, world_comm, MPI_STATUS_IGNORE);
                    } else {
                        int dummy;
                        MPI_Recv(&dummy, 1, MPI_INT, drain_status.MPI_SOURCE, drain_status.MPI_TAG, world_comm, MPI_STATUS_IGNORE);
                    }
                } else {
                    usleep(1000);
                }
            }
            comm_time += (MPI_Wtime() - comm_start_time);
            break;
        }
    }

    /**
     * ### Final Cleanup and Performance Summary Report
     * Waits for all asynchronous termination requests to complete, 
     * frees allocated memory, and calculates aggregate performance metrics. 
     * It is used to safely clean up the MPI environment and append a comprehensive summary 
     * report regarding simulation overhead and event frequencies to the log file.
     */
    double overall_time = MPI_Wtime() - start_time;

    fprintf(log_file, "----- SIMULATION SUMMARY -----\n");
    fprintf(log_file, "Total Simulation Time: %.4f seconds\n", overall_time);
    fprintf(log_file, "Total Comm Time: %.4f seconds\n", comm_time);
    fprintf(log_file, "Total Comp Time: %.4f seconds\n", comp_time);
    fprintf(log_file, "Total Congestion Alerts: %d\n", ttl_alerts);
    fprintf(log_file, "Total Communication Events: %d\n", ttl_events);
    fclose(log_file);
    printf("[Base Station] Overall: %.4fs | Comm: %.4fs | Comp: %.4fs\n", overall_time, comm_time, comp_time);
    fflush(stdout);
    printf("[Base Station] Log saved successfully.\n");
    fflush(stdout);
}

int main(int argc, char **argv) {
    // Initializes the MPI environment with multi-threading support to ensure safe, 
    // concurrent execution of MPI calls within OpenMP threads.
    int rank, size, nrows = 3, ncols = 3;
    int permission;
    MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &permission);

    if (permission < MPI_THREAD_MULTIPLE) {
        printf("Error: MPI_THREAD_MULTIPLE is not supported.\n");
        fflush(stdout);
        MPI_Abort(MPI_COMM_WORLD, 1);
    }

    // Retrieves the process count and strictly validates the command-line arguments to 
    // ensure the specified 2D grid dimensions exactly match the number of available charging nodes.
    MPI_Comm_size(MPI_COMM_WORLD, &size);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    if (argc >= 3) {
        nrows = atoi(argv[1]);
        ncols = atoi(argv[2]);
    }

    if (rank == 0) {
        // New. Prints the initialization string specifically for Version 1 Scheduled Blocking.
        printf(">>> Running Electric Vehicle Charging Navigation System Simulation <<<\n");
        fflush(stdout);
    }

    if ((nrows * ncols) != (size - 1)) {  
        if (rank == 0) {
            printf("Error: nrows * ncols (%d*%d) must be equal to slave size (%d)\n", nrows, ncols, size - 1);
            fflush(stdout);
        }
        MPI_Finalize();
        return 0;
    }

    // Securely parses optional environment variables to dynamically override default 
    // simulation parameters like the report freshness window and the port congestion rate.
    const char *period_text = getenv("EVCNS_PRED_PERIOD");
    if (period_text != NULL)
    {
        char *end;
        errno = 0;
        double value = strtod(period_text, &end);
        if (errno != 0 || end == period_text || *end ||
            !(value > 0.0 && value < 86400.0))
        {
            fprintf(stderr, "EVCNS error: predefined period must be positive, finite and below 86400s\n");
            MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
            exit(EXIT_FAILURE);
        }
        PRED_PERIOD = value;
    }

    if (getenv("EVCNS_CONGEST_RATE"))
        CONGEST_RATE = (int)ParseInteger(getenv("EVCNS_CONGEST_RATE"), 0, 100);

    // Disables dynamic OpenMP thread adjustments to maintain thread stability and 
    // prints the startup message from the root process.
    omp_set_dynamic(0);

    // Registers a custom MPI datatype for congestion reports and splits the global communicator 
    // to logically isolate the Base Station from the interconnected charging nodes.
    MPI_Datatype report_type;
    CreateDataType(&report_type);

    MPI_Comm new_comm;
    MPI_Comm_split(MPI_COMM_WORLD, rank == size - 1, 0, &new_comm);

    // This conditional logic assigns the highest MPI rank to act as the centralized Base Station (MasterIO) 
    // and directs all other ranks to operate as EV charging nodes (SlaveIO).
    if (rank == size - 1) {
        MasterIO(MPI_COMM_WORLD, new_comm, nrows, ncols, report_type);
    } else {
        SlaveIO(MPI_COMM_WORLD, new_comm, nrows, ncols, report_type);
    }

    // This finalization step synchronizes all processes, exports the gathered performance metrics, 
    // frees allocated MPI resources, and safely shuts down the MPI environment.
    MPI_Type_free(&report_type);
    MPI_Comm_free(&new_comm);
    MPI_Finalize();
    return 0;
}