/**
 * @file      evcns_v2.c
 * @brief     Electric Vehicle Charging Navigation System (EVCNS) Version 2
 *
 * @details
 *
 * @author    Shee Seng Cheng (34612467) - sshe0113@student.monash.edu
 * @author    Tay Chee Hsian (34612513) - ctay0040@student.monash.edu
 * @note      Coding Implementation for EVCNS
 */

#include "evcns_utils.h"

//===========================================
// EV Charging Node
//===========================================
void SlaveIO(MPI_Comm world_comm, MPI_Comm comm, int nrows, int ncols, MPI_Datatype report_type)
{
    /**
     * ### MPI Setup and Topology Creation
     * Initializes the MPI environment variables and constructs a 2D Cartesian grid topology for the charging nodes. 
     * It is used to establish the spatial relationships between nodes, identifying the immediate top, bottom, left, and right 
     * neighbors required for localized communication.
     */
    int global_size, global_rank;
    MPI_Comm_size(world_comm, &global_size);
    MPI_Comm_rank(world_comm, &global_rank);

    int report_bytes;
    MPI_Type_size(report_type, &report_bytes);

    srand(time(NULL) + global_rank); // Original random initialization using time and process rank.

    int dims[2] = {nrows, ncols}, coord[2], wrap_around[2] = {0, 0}; // 2D Mesh
    int nbr_i_lo, nbr_i_hi, nbr_j_lo, nbr_j_hi;                      // Top, Bottom, Left, Right
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
    for (int i = 0; i < NUM_PORTS; i++)
        ports[i] = 1;                // Initially all ports are free.
    bool running_flag = true;        // true means simulation is running, false otherwise
    MPI_Barrier(world_comm);         // Start only after all ranks have initialized
    double start_time = MPI_Wtime(); // Record time
    double node_comp_time = 0.0;
    double node_comm_time = 0.0;

    #pragma omp parallel num_threads(NUM_PORTS + 1) shared(ports, running_flag, start_time, node_comp_time, node_comm_time)
    {
        int thread_id = omp_get_thread_num();
        if (omp_get_num_threads() != NUM_PORTS + 1)
        {
            fprintf(stderr, "EVCNS error: %s\n", "seven OpenMP threads are required");
            MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
            exit(EXIT_FAILURE);
        }
        double comp_time = 0.0, comp_start_time;

        /**
         * ### Port State Simulation (Worker Threads)
         * These worker threads asynchronously simulate real-world electric vehicle behavior by randomly 
         * toggling the availability of individual charging ports based on a predefined probability.
         * This mechanism is used to generate dynamic, independent load fluctuations across the charging 
         * network without blocking the node's main communication thread.
         */
        if (thread_id < NUM_PORTS)
        {
            double last_update = MPI_Wtime();  // Record the last updated time of a port
            bool running;
            #pragma omp atomic read
            running = running_flag;

            while (running)
            {
                // Every two seconds perform once (random change a port status)
                if (MPI_Wtime() - last_update >= PORT_PERIOD)
                {
                    comp_start_time = MPI_Wtime();
                    #pragma omp critical  // Mutex lock
                    {
                        ports[thread_id] = (rand() % 100 < CONGEST_RATE) ? 0 : 1;  // Default: 70% busy / 30% free
                    }
                    comp_time += (MPI_Wtime() - comp_start_time);
                    last_update = MPI_Wtime();
                }
                usleep(1000);

                #pragma omp atomic read
                running = running_flag;
            }
        }
        else
        {   
            /**
             * ### Main Thread: Disable Signal Handling
             * Continuously polls for a globally broadcasted MSG_DISABLE signal originating from the Base Station. 
             * It is used to gracefully halt the generation of new internal port states and block new outgoing queries once the 
             * fixed simulation duration concludes.
             */
            // Main thread
            double last_check = MPI_Wtime();
            bool running = true;
            bool disabled = false, ready_sent = false, awaiting_base = false;
            int flag;                 // Used by MPI_Iprobe, 1 means a msg is waiting to be read
            MPI_Status probe_status;  // Keep track who are the sender and what tag of msg sender send
            double comm_time = 0.0, comm_start_time;
            double message_start, message_elapsed;

            while (running)
            {
                // Stop new enquiries, then finish C1-C4 while still answering peers.
                comm_start_time = MPI_Wtime();
                MPI_Iprobe(global_size - 1, MSG_DISABLE, world_comm, &flag, &probe_status);
                comm_time += (MPI_Wtime() - comm_start_time);
                if (flag)
                {
                    int signal;
                    comm_start_time = MPI_Wtime();
                    message_start = MPI_Wtime();
                    MPI_Recv(&signal, 1, MPI_INT, global_size - 1, MSG_DISABLE, world_comm, MPI_STATUS_IGNORE);
                    message_elapsed = MPI_Wtime() - message_start;
                    TrackReceiveMetric(MSG_DISABLE, 1, sizeof(int), message_elapsed, message_elapsed);
                    comm_time += (MPI_Wtime() - comm_start_time);
                    disabled = true;
                    #pragma omp atomic write
                    running_flag = false;
                }

                /**
                 * ### Main Thread: Neighbor Request Processing
                 * Listens for and intercepts incoming MSG_REQ_NEIGHBOR inquiries 
                 * from adjacent charging nodes actively looking for free slots. 
                 * It safely calculates the locally available ports using an OpenMP critical section and 
                 * immediately replies, assisting the neighbor's decentralized load-balancing efforts.
                 */
                comm_start_time = MPI_Wtime();
                MPI_Iprobe(MPI_ANY_SOURCE, MSG_REQ_NEIGHBOR, comm2D, &flag, &probe_status);  // Response to the neighbors' request
                comm_time += (MPI_Wtime() - comm_start_time);

                if (flag)
                {
                    int req_sig;
                    comm_start_time = MPI_Wtime();
                    message_start = MPI_Wtime();
                    MPI_Recv(&req_sig, 1, MPI_INT, probe_status.MPI_SOURCE, MSG_REQ_NEIGHBOR, comm2D, MPI_STATUS_IGNORE);
                    message_elapsed = MPI_Wtime() - message_start;
                    TrackReceiveMetric(MSG_REQ_NEIGHBOR, 1, sizeof(int), message_elapsed, message_elapsed);
                    comm_time += (MPI_Wtime() - comm_start_time);

                    comp_start_time = MPI_Wtime();
                    int curr_avail = 0;  // Get how many available ports
                    #pragma omp critical
                    {
                        for (int i = 0; i < NUM_PORTS; i++)
                            curr_avail += ports[i];
                    }
                    comp_time += (MPI_Wtime() - comp_start_time);

                    comm_start_time = MPI_Wtime();
                    message_start = MPI_Wtime();
                    MPI_Send(&curr_avail, 1, MPI_INT, probe_status.MPI_SOURCE, MSG_RES_NEIGHBOR, comm2D);
                    message_elapsed = MPI_Wtime() - message_start;
                    TrackSendMetric(MSG_RES_NEIGHBOR, 1, sizeof(int), message_elapsed, message_elapsed);
                    comm_time += (MPI_Wtime() - comm_start_time);
                }

                /**
                 * ### Main Thread: Base Station Recommendation Handling
                 * Receives MSG_BS_REPLY network messages containing a computed list 
                 * of alternative charging nodes identified by the Base Station. 
                 * It is used to release the node from its reporting wait state and prints the 
                 * recommended geographical coordinates to reroute incoming electric vehicles.
                 */
                comm_start_time = MPI_Wtime();
                MPI_Iprobe(global_size - 1, MSG_BS_REPLY, world_comm, &flag, &probe_status);  // Instruction from the Base Station
                comm_time += (MPI_Wtime() - comm_start_time);

                if (flag)
                {
                    int found_count;
                    comm_start_time = MPI_Wtime();
                    // The number of available nodes found by the Base Station
                    message_start = MPI_Wtime();
                    MPI_Recv(&found_count, 1, MPI_INT, global_size - 1, MSG_BS_REPLY, world_comm, MPI_STATUS_IGNORE);
                    message_elapsed = MPI_Wtime() - message_start;
                    TrackReceiveMetric(MSG_BS_REPLY, 1, sizeof(int), message_elapsed, message_elapsed);

                    if (found_count < 0 || found_count > global_size - 1)
                    {
                        fprintf(stderr, "EVCNS error: %s\n", "invalid candidate count");
                        MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
                        exit(EXIT_FAILURE);
                    }
                    if (found_count == 0)
                    {   // No available nodes
                        printf("[Node Rank %d at (%d,%d)] No available nodes nearby\n", global_rank, coord[0], coord[1]);
                        fflush(stdout);
                    }
                    else
                    {
                        int reply_coords[2 * (global_size - 1)];  // Enough for all recommendations
                        message_start = MPI_Wtime();
                        MPI_Recv(reply_coords, found_count * 2, MPI_INT, global_size - 1, MSG_BS_REPLY, world_comm, MPI_STATUS_IGNORE);  // Receive the coordinates from the same distance
                        message_elapsed = MPI_Wtime() - message_start;
                        TrackReceiveMetric(MSG_BS_REPLY, 1, (found_count * 2) * sizeof(int), message_elapsed, message_elapsed);

                        printf("[Node Rank %d at (%d,%d)] Nearest has %d available node(s): ", global_rank, coord[0], coord[1], found_count);
                        for (int i = 0; i < found_count; i++)
                        {
                            printf("(%d,%d) ", reply_coords[i * 2], reply_coords[i * 2 + 1]);
                        }
                        printf("to EVs\n");
                    }
                    fflush(stdout);
                    awaiting_base = false;
                    comm_time += (MPI_Wtime() - comm_start_time);
                }

                /**
                 * ### Main Thread: Congestion Checking & Neighbor Polling
                 * Periodically checks if the local node has reached its port congestion threshold, at which point 
                 * it utilizes non-blocking MPI_Isend and MPI_Irecv to concurrently poll all physical neighbors. 
                 * It functions as the first tier of decentralized load balancing, attempting to discover nearby 
                 * charging capacity while ensuring it can still interleave incoming requests while waiting for responses.
                 */
                // Local CONGEST_RATE checking and neighbors communication
                if (!disabled && !awaiting_base && MPI_Wtime() - last_check >= CHECK_PERIOD)
                {
                    comp_start_time = MPI_Wtime();
                    int loc_avail_ports = 0;
                    #pragma omp critical
                    {
                        for (int i = 0; i < NUM_PORTS; i++)
                            loc_avail_ports += ports[i];
                    }
                    comp_time += (MPI_Wtime() - comp_start_time);

                    if (loc_avail_ports <= THRESHOLD)
                    {
                        comp_start_time = MPI_Wtime();
                        int req = 1;  // Just a dummy variable to send something
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
                        for (int i = 0; i < 4; i++)
                        {
                            if (neighbors[i] != MPI_PROC_NULL)
                            {
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

                        if (req_count > 0)
                        {
                            bool wait_done = false;
                            while (!wait_done)
                            {
                                int recv_flag = 0;
                                comm_start_time = MPI_Wtime();
                                MPI_Testall(req_count, recv, &recv_flag, MPI_STATUSES_IGNORE);
                                comm_time += (MPI_Wtime() - comm_start_time);

                                if (recv_flag)
                                {
                                    // All C2 replies are now complete; record one sample per neighbour.
                                    double completed = MPI_Wtime();
                                    message_metrics[MSG_RES_NEIGHBOR].received += req_count;
                                    message_metrics[MSG_RES_NEIGHBOR].receive_bytes += (long long)req_count * sizeof(int);
                                    for (int i = 0; i < req_count; i++)
                                        message_metrics[MSG_RES_NEIGHBOR].receive_lifetime += completed - receive_started[i];
                                    wait_done = true;
                                }
                                else
                                {
                                    int inc_flag;
                                    MPI_Status inc_status;
                                    comm_start_time = MPI_Wtime();
                                    MPI_Iprobe(MPI_ANY_SOURCE, MSG_REQ_NEIGHBOR, comm2D, &inc_flag, &inc_status);
                                    comm_time += (MPI_Wtime() - comm_start_time);

                                    if (inc_flag)
                                    {
                                        int inc_sig;
                                        comm_start_time = MPI_Wtime();
                                        message_start = MPI_Wtime();
                                        MPI_Recv(&inc_sig, 1, MPI_INT, inc_status.MPI_SOURCE, MSG_REQ_NEIGHBOR, comm2D, MPI_STATUS_IGNORE);
                                        message_elapsed = MPI_Wtime() - message_start;
                                        TrackReceiveMetric(MSG_REQ_NEIGHBOR, 1, sizeof(int), message_elapsed, message_elapsed);
                                        comm_time += (MPI_Wtime() - comm_start_time);

                                        comp_start_time = MPI_Wtime();
                                        int curr_avail = 0;
                                        #pragma omp critical
                                        {
                                            for (int p = 0; p < NUM_PORTS; p++)
                                                curr_avail += ports[p];
                                        }
                                        comp_time += (MPI_Wtime() - comp_start_time);

                                        comm_start_time = MPI_Wtime();
                                        message_start = MPI_Wtime();
                                        MPI_Send(&curr_avail, 1, MPI_INT, inc_status.MPI_SOURCE, MSG_RES_NEIGHBOR, comm2D);
                                        message_elapsed = MPI_Wtime() - message_start;
                                        TrackSendMetric(MSG_RES_NEIGHBOR, 1, sizeof(int), message_elapsed, message_elapsed);
                                        comm_time += (MPI_Wtime() - comm_start_time);
                                    }
                                    else
                                    {
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

                        /**
                         * ### Main Thread: Congestion Reporting & Local Referral
                         * If both the current node and its surrounding neighbors are found to be severely congested, this block 
                         * packages the contextual data into a MsgData struct and dispatches a critical MSG_REPORT to the Base Station. 
                         * Conversely, if any neighbor is found to have available space, it bypasses the Base Station entirely and 
                         * directly invokes a local referral to redirect the EV traffic.
                         */
                        comp_start_time = MPI_Wtime();
                        bool all_neigh_full = true;  // Check neither it nor its immediate neighbors have any available ports
                        for (int i = 0; i < 4; i++)
                        {
                            if (neigh_ports[i] > THRESHOLD)
                                all_neigh_full = false;
                        }

                        if (all_neigh_full)
                        {
                            MsgData msg = {
                                .timestamp = MPI_Wtime() - start_time,  // Records the exact time the CONGEST_RATE happened
                                .sender_rank = global_rank,
                                .coords = {coord[0], coord[1]},
                                .avail_ports = loc_avail_ports,  // Proof that this node is full
                                .avail_neigh = {neigh_ports[0], neigh_ports[1], neigh_ports[2], neigh_ports[3]}, // Proof that the neighbors are also full
                                .msg_exchanged = req_count * 2,  // Tracking how much message was generated
                                .msg_size = (req_count * 2) * sizeof(int)};
                            comp_time += (MPI_Wtime() - comp_start_time);

                            comm_start_time = MPI_Wtime();
                            IncidentDateTime(msg.incident_time);
                            awaiting_base = true;
                            message_start = MPI_Wtime();
                            MPI_Send(&msg, 1, report_type, global_size - 1, MSG_REPORT, world_comm);
                            message_elapsed = MPI_Wtime() - message_start;
                            TrackSendMetric(MSG_REPORT, 1, report_bytes, message_elapsed, message_elapsed);
                            comm_time += (MPI_Wtime() - comm_start_time);
                        }
                        else
                        {
                            LocalReferral(global_rank, coord, neigh_ports);
                            comp_time += (MPI_Wtime() - comp_start_time);
                        }
                    }
                    last_check = MPI_Wtime();
                }

                /**
                 * ### Main Thread: Graceful Shutdown Handshake
                 * Manages a strict multi-step termination protocol by formally notifying the Base Station via MSG_READY when the node 
                 * has finished all pending inquiries. It is used to ensure a synchronized global exit, preventing the MPI environment 
                 * from tearing down while asynchronous messages or acknowledgments are still in transit.
                 */
                if (disabled && !awaiting_base && !ready_sent)
                {
                    int signal = 1;
                    comm_start_time = MPI_Wtime();
                    message_start = MPI_Wtime();
                    MPI_Send(&signal, 1, MPI_INT, global_size - 1, MSG_READY, world_comm);
                    message_elapsed = MPI_Wtime() - message_start;
                    TrackSendMetric(MSG_READY, 1, sizeof(int), message_elapsed, message_elapsed);
                    comm_time += (MPI_Wtime() - comm_start_time);
                    ready_sent = true;
                }
                if (ready_sent)
                {
                    comm_start_time = MPI_Wtime();
                    MPI_Iprobe(global_size - 1, MSG_TERMINATE, world_comm, &flag, &probe_status);
                    comm_time += (MPI_Wtime() - comm_start_time);
                    if (flag)
                    {
                        int signal;
                        comm_start_time = MPI_Wtime();
                        message_start = MPI_Wtime();
                        MPI_Recv(&signal, 1, MPI_INT, global_size - 1, MSG_TERMINATE, world_comm, MPI_STATUS_IGNORE);
                        message_elapsed = MPI_Wtime() - message_start;
                        TrackReceiveMetric(MSG_TERMINATE, 1, sizeof(int), message_elapsed, message_elapsed);

                        message_start = MPI_Wtime();
                        MPI_Send(&signal, 1, MPI_INT, global_size - 1, MSG_TERMINATED, world_comm);
                        message_elapsed = MPI_Wtime() - message_start;
                        TrackSendMetric(MSG_TERMINATED, 1, sizeof(int), message_elapsed, message_elapsed);
                        comm_time += (MPI_Wtime() - comm_start_time);
                        break;
                    }
                }
                usleep(1000);
            }
            node_comm_time = comm_time;
        }

        // Only assign comp_time and comm_time from the main thread (thread_id == NUM_PORTS)
        if (thread_id == NUM_PORTS)
        {
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

    if (comm2D_rank == 0)
    {
        double avg_overall = sum_overall / slave_size;
        double avg_comm = sum_comm / slave_size;
        double avg_comp = sum_comp / slave_size;
        printf(
            "[Charging Nodes Average] Overall: %.4fs | Comm: %.4fs | Comp: %.4fs\n",
            avg_overall, avg_comm, avg_comp);
        fflush(stdout);
    }
    MPI_Comm_free(&comm2D);
}

//===========================================
// Base Station
//===========================================
void MasterIO(MPI_Comm world_comm, MPI_Comm comm, int nrows, int ncols, MPI_Datatype report_type)
{
    /**
     * ### Variable Initialization and Memory Allocation
     * Initializes the Base Station's core variables and allocates memory for tracking the shutdown states of all worker nodes. 
     * It is used to set up the global tracking arrays and asynchronous request handles necessary for executing a synchronized 
     * simulation shutdown.
     */
    int global_size;
    int term_sig = 1, ready_count = 0, ack_count = 0;
    int report_bytes;
    MPI_Type_size(report_type, &report_bytes);
    bool disabling = false, terminating = false;
    MPI_Comm_size(world_comm, &global_size);
    (void)comm;
    bool *ready_seen = calloc((size_t)(global_size - 1), sizeof(bool));
    bool *ack_seen = calloc((size_t)(global_size - 1), sizeof(bool));
    MPI_Request control_requests[2 * (global_size - 1)];
    double control_started[2 * (global_size - 1)];
    if (!ready_seen || !ack_seen)
    {
        fprintf(stderr, "EVCNS error: %s\n", "Cannot allocate shutdown state");
        MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
        exit(EXIT_FAILURE);
    }
    for (int i = 0; i < 2 * (global_size - 1); i++)
        control_requests[i] = MPI_REQUEST_NULL;

    /**
     * ### Initialization of Global State Matrix and Log Files
     * This section initializes the global 2D matrix representing port availability and prepares the main simulation log file. 
     * It acts as the central state registry to track recent congestion events and records the initial configuration parameters 
     * before the main loop begins.
     */
    int grid_state[nrows][ncols];           // Global status matrix
    double last_report_time[nrows][ncols];  // Record the last congestion report time of each node
    for (int i = 0; i < nrows; i++)
    {
        for (int j = 0; j < ncols; j++)
        {
            grid_state[i][j] = NUM_PORTS;
            last_report_time[i][j] = -999;
        }
    }

    FILE *log_file = fopen("log.txt", "w");
    if (!log_file)
    {
        printf("Error: Cannot open log file.\n");
        MPI_Abort(world_comm, EXIT_FAILURE);
    }

    MPI_Barrier(world_comm);
    double start_time = MPI_Wtime();  // Record time
    int ttl_alerts = 0;               // Record the number of happening CONGEST_RATE
    int ttl_events = 0;               // Record the number of communications
    int flag;                         // Used by MPI_Iprobe, 1 means a msg is waiting to be read
    MPI_Status probe_status;          // Keep track who are the sender and what tag of msg sender send
    MsgData report;
    double comm_time = 0.0, comm_start_time;
    double message_start, message_elapsed;
    double comp_time = 0.0, comp_start_time;

    printf("[Base Station] Listening for reports...\n");
    fflush(stdout);
    fprintf(log_file, "----- SIMULATION STARTED -----\n");
    fprintf(log_file,
            "Configuration: version=v1_task3 rows=%d cols=%d processes=%d ports=%d threads=%d "
            "seconds=%.3f PORT_PERIOD=%.3f CHECK_PERIOD=%.3f "
            "PRED_PERIOD=%.6f CONGEST_RATE=%d\n",
            nrows, ncols, global_size, NUM_PORTS, NUM_PORTS + 1,
            SIMULATION_SEC, PORT_PERIOD, CHECK_PERIOD,
            PRED_PERIOD, CONGEST_RATE);
    fflush(log_file);

    /**
     * ### Message Listening and Termination Signal Handshake
     * This block actively probes the network for incoming messages and 
     * processes the graceful shutdown handshake signals from slave nodes. 
     * It is used to securely tally which charging nodes have ceased operations 
     * and acknowledged the termination protocol to prevent system deadlocks.
     */
    while (1)
    {
        comm_start_time = MPI_Wtime();
        MPI_Iprobe(MPI_ANY_SOURCE, MPI_ANY_TAG, world_comm, &flag, &probe_status); // Listen any incoming messages
        comm_time += (MPI_Wtime() - comm_start_time);

        if (flag)
        {
            if (probe_status.MPI_TAG == MSG_READY || probe_status.MPI_TAG == MSG_TERMINATED)
            {
                int signal, peer = probe_status.MPI_SOURCE;
                comm_start_time = MPI_Wtime();
                message_start = MPI_Wtime();
                MPI_Recv(&signal, 1, MPI_INT, peer, probe_status.MPI_TAG, world_comm, MPI_STATUS_IGNORE);
                message_elapsed = MPI_Wtime() - message_start;
                TrackReceiveMetric(probe_status.MPI_TAG, 1, sizeof(int), message_elapsed, message_elapsed);
                comm_time += (MPI_Wtime() - comm_start_time);
                if (peer < 0 || peer >= global_size - 1)
                {
                    fprintf(stderr, "EVCNS error: %s\n", "invalid shutdown sender");
                    MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
                    exit(EXIT_FAILURE);
                }
                if (probe_status.MPI_TAG == MSG_READY && !ready_seen[peer])
                {
                    ready_seen[peer] = true;
                    ready_count++;
                }
                else if (probe_status.MPI_TAG == MSG_TERMINATED && !ack_seen[peer])
                {
                    ack_seen[peer] = true;
                    ack_count++;
                }
                else
                {
                    fprintf(stderr, "EVCNS error: %s\n", "duplicate shutdown confirmation");
                    MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
                    exit(EXIT_FAILURE);
                }
            }
            else if (probe_status.MPI_TAG == MSG_REPORT)
            {   
                /**
                 * ### Congestion Report Receipt and Status Updates
                 * Intercepts congestion alerts, updates the central grid state, and validates 
                 * if the reporting node's immediate neighbors are also recently congested. 
                 * It serves as the primary ingestion point for network alerts, utilizing the 
                 * Base Station's absolute clock to eliminate distributed time drift errors.
                 */
                comm_start_time = MPI_Wtime();
                message_start = MPI_Wtime();
                MPI_Recv(&report, 1, report_type, probe_status.MPI_SOURCE, MSG_REPORT, world_comm, MPI_STATUS_IGNORE);
                message_elapsed = MPI_Wtime() - message_start;
                TrackReceiveMetric(MSG_REPORT, 1, report_bytes, message_elapsed, message_elapsed);
                comm_time += (MPI_Wtime() - comm_start_time);

                comp_start_time = MPI_Wtime();
                ttl_alerts++;
                ttl_events += report.msg_exchanged + 1;

                // Get the absolute current time at the Base Station
                // Acts as a single source of truth to eliminate distributed clock drift errors
                double curr_time = MPI_Wtime();

                // Update status of this congested node
                grid_state[report.coords[0]][report.coords[1]] = report.avail_ports;

                // Record the timestamp using the Base Station's absolute clock
                last_report_time[report.coords[0]][report.coords[1]] = curr_time;

                // Update neighbors' status of this congested node
                int nbr_coords[4][2] = {
                    {report.coords[0] - 1, report.coords[1]}, // Top
                    {report.coords[0] + 1, report.coords[1]}, // Bottom
                    {report.coords[0], report.coords[1] - 1}, // Left
                    {report.coords[0], report.coords[1] + 1}  // Right
                };

                bool all_nbrs_congested_recently = true;
                int valid_nbrs = 0;

                for (int i = 0; i < 4; i++)
                {
                    if (report.avail_neigh[i] != NO_NEIGHBOR)
                    {
                        int r = nbr_coords[i][0];
                        int c = nbr_coords[i][1];
                        if (r >= 0 && r < nrows && c >= 0 && c < ncols)
                        {
                            grid_state[r][c] = report.avail_neigh[i];
                            valid_nbrs++;

                            // Check if this neighbor has never or its last congested data has expired
                            if (last_report_time[r][c] < 0 || curr_time - last_report_time[r][c] > PRED_PERIOD)
                            {
                                all_nbrs_congested_recently = false;
                            }
                        }
                    }
                }

                /**
                 * ### Manhattan Ripple Search to find available nodes
                 * This algorithm expands outward in a diamond-shaped ripple pattern to 
                 * locate the geographically nearest charging nodes with available ports. 
                 * It is used to dynamically compile a list of optimal alternative stations for 
                 * rerouting incoming electric vehicles when a local area becomes fully saturated.
                 */
                int max_candidates = global_size - 1; // Enough for all equal-nearest stations
                int best_coords[2 * (global_size - 1)];
                int found_count = 0;

                if (valid_nbrs > 0 && all_nbrs_congested_recently)
                { // This area is heavily congested
                    fprintf(log_file, "T:%.2fs, No available nodes nearby\n", report.timestamp);
                    fflush(log_file);
                    comp_time += (MPI_Wtime() - comp_start_time);
                }
                else
                {
                    //---------------------------------
                    // Original: Brute-Force Approach
                    //---------------------------------
                    // int min_dist = -1;
                    // for (int r = 0; r < nrows; r++) {
                    //     for (int c = 0; c < ncols; c++) {
                    //         if (grid_state[r][c] > THRESHOLD &&
                    //             (r != report.coords[0] || c != report.coords[1])) {
                    //             int dist = abs(r - report.coords[0]) + abs(c - report.coords[1]);
                    //             if (min_dist < 0 || dist < min_dist) {
                    //                 min_dist = dist;
                    //                 found_count = 0;  // Discard farther candidates
                    //             }
                    //             if (dist == min_dist && found_count < max_candidates) {
                    //                 best_coords[found_count * 2] = r;
                    //                 best_coords[found_count * 2 + 1] = c;
                    //                 found_count++;
                    //             }
                    //         }
                    //     }
                    // }
                    //---------------------------------

                    //---------------------------------
                    // Optimization: A diamond scanning for available ports by expanding outward in a ripple-like manner
                    //---------------------------------
                    int max_dist = nrows + ncols;
                    bool found = false;

                    for (int d = 1; d <= max_dist && !found; d++)
                    { // Expand layer by layer
                        for (int dx = -d; dx <= d; dx++)
                        {
                            int dy = d - abs(dx); // Manhattan Distance (Diamond Scanning)

                            // Positive direction
                            int r = report.coords[0] + dx;
                            int c = report.coords[1] + dy;
                            if (r >= 0 && r < nrows && c >= 0 && c < ncols)
                            {
                                if (grid_state[r][c] > THRESHOLD && found_count < max_candidates)
                                {
                                    best_coords[found_count * 2] = r; // Mapping 2D to 1D
                                    best_coords[found_count * 2 + 1] = c;
                                    found_count++;
                                    found = true;
                                }
                            }

                            // Negative direction
                            if (dy != 0)
                            {
                                c = report.coords[1] - dy;
                                if (r >= 0 && r < nrows && c >= 0 && c < ncols)
                                {
                                    if (grid_state[r][c] > THRESHOLD && found_count < max_candidates)
                                    {
                                        best_coords[found_count * 2] = r;
                                        best_coords[found_count * 2 + 1] = c;
                                        found_count++;
                                        found = true;
                                    }
                                }
                            }
                        }
                    }
                    comp_time += (MPI_Wtime() - comp_start_time);
                    //---------------------------------
                }

                /**
                 * ### Sending Dispatch Responses and Grid Status Records
                 * Transmits the discovered alternative coordinates back to the reporting node 
                 * and records a detailed visual snapshot of the grid. 
                 * It ensures the congested node receives immediate routing instructions while permanently 
                 * logging the incident's spatial context for analytical review.
                 */
                comm_start_time = MPI_Wtime();
                message_start = MPI_Wtime();
                MPI_Send(&found_count, 1, MPI_INT, probe_status.MPI_SOURCE, MSG_BS_REPLY, world_comm);
                message_elapsed = MPI_Wtime() - message_start;
                TrackSendMetric(MSG_BS_REPLY, 1, sizeof(int), message_elapsed, message_elapsed);

                if (found_count > 0)
                {
                    message_start = MPI_Wtime();
                    MPI_Send(best_coords, found_count * 2, MPI_INT, probe_status.MPI_SOURCE, MSG_BS_REPLY, world_comm);
                    message_elapsed = MPI_Wtime() - message_start;
                    TrackSendMetric(MSG_BS_REPLY, 1, (found_count * 2) * sizeof(int), message_elapsed, message_elapsed);
                }
                comm_time += (MPI_Wtime() - comm_start_time);

                int best_row = (found_count > 0) ? best_coords[0] : -1;
                int best_col = (found_count > 0) ? best_coords[1] : -1;

                // Record into log
                fprintf(
                    log_file, "T:%.2fs | ALERT [%d] | Rank: %d Coord: (%d,%d) | Ports Free: %d | Neighbor Free: [T:%d, B:%d, L:%d, R:%d] | Msgs: %d | Msg Size: %d Bytes | Nearest: (%d,%d)\n",
                    report.timestamp, // CONGEST_RATE happen time
                    ttl_alerts,       // Number for this CONGEST_RATE
                    report.sender_rank,
                    report.coords[0],
                    report.coords[1],
                    report.avail_ports,    // The remaining ports of itself
                    report.avail_neigh[0], // The remaining ports of its neighbors
                    report.avail_neigh[1],
                    report.avail_neigh[2],
                    report.avail_neigh[3],
                    report.msg_exchanged, // Total communication overhead
                    report.msg_size,
                    best_row, best_col);

                fprintf(log_file, "Incident date/time: %s\n", report.incident_time);
                fprintf(log_file, "Recommended nodes: %d", found_count);
                for (int k = 0; k < found_count; k++)
                    fprintf(log_file, " (%d,%d)", best_coords[2 * k], best_coords[2 * k + 1]);
                fprintf(log_file, "\n");

                // Print the grid
                PrintGrid(log_file, report.timestamp, nrows, ncols, grid_state, report.coords[0], report.coords[1], best_row, best_col);
                fflush(log_file);
            }
        }
        else
        {
            usleep(100);
        }

        /**
         * ### Simulated Timeout Detection and Shutdown Signaling Broadcast
         * Monitors the total elapsed simulation time and broadcasts sequential 
         * non-blocking disable and terminate commands to all active nodes. 
         * It is used to strictly enforce the simulation's fixed duration and to 
         * orchestrate a two-phase graceful shutdown across the distributed cluster.
         */
        if (!disabling && MPI_Wtime() - start_time >= SIMULATION_SEC)
        {
            disabling = true;
            printf("[Base Station] Time limit reached. Finishing active enquiries...\n");
            comm_start_time = MPI_Wtime();
            for (int i = 0; i < global_size - 1; i++)
            {
                control_started[i] = MPI_Wtime();
                MPI_Isend(&term_sig, 1, MPI_INT, i, MSG_DISABLE, world_comm, &control_requests[i]);
                message_metrics[MSG_DISABLE].send_call_time += MPI_Wtime() - control_started[i];
            }
            comm_time += (MPI_Wtime() - comm_start_time);
        }
        if (disabling && ready_count == global_size - 1 && !terminating)
        {
            terminating = true;
            comm_start_time = MPI_Wtime();
            for (int i = 0; i < global_size - 1; i++)
            {
                control_started[global_size - 1 + i] = MPI_Wtime();
                MPI_Isend(&term_sig, 1, MPI_INT, i, MSG_TERMINATE, world_comm, &control_requests[global_size - 1 + i]);
                message_metrics[MSG_TERMINATE].send_call_time += MPI_Wtime() - control_started[global_size - 1 + i];
            }
            comm_time += (MPI_Wtime() - comm_start_time);
        }
        if (terminating && ack_count == global_size - 1)
            break;
    }

    /**
     * ### Final Cleanup and Performance Summary Report
     * Waits for all asynchronous termination requests to complete, 
     * frees allocated memory, and calculates aggregate performance metrics. 
     * It is used to safely clean up the MPI environment and append a comprehensive summary 
     * report regarding simulation overhead and event frequencies to the log file.
     */
    comm_start_time = MPI_Wtime();
    MPI_Waitall(2 * (global_size - 1), control_requests, MPI_STATUSES_IGNORE);
    double control_completed = MPI_Wtime();
    message_metrics[MSG_DISABLE].sent += global_size - 1;
    message_metrics[MSG_TERMINATE].sent += global_size - 1;
    message_metrics[MSG_DISABLE].send_bytes += (long long)(global_size - 1) * sizeof(int);
    message_metrics[MSG_TERMINATE].send_bytes += (long long)(global_size - 1) * sizeof(int);
    for (int i = 0; i < global_size - 1; i++)
    {
        message_metrics[MSG_DISABLE].send_lifetime += control_completed - control_started[i];
        message_metrics[MSG_TERMINATE].send_lifetime += control_completed - control_started[global_size - 1 + i];
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
    fprintf(log_file, "Total CONGEST_RATE Alerts: %d\n", ttl_alerts);
    fprintf(log_file, "Reported neighbor/C3 events (subset): %d\n", ttl_events);
    fclose(log_file);
    printf("[Base Station] Overall: %.4fs | Comm: %.4fs | Comp: %.4fs\n", overall_time, comm_time, comp_time);
    fflush(stdout);
    printf("[Base Station] Log saved successfully.\n");
    fflush(stdout);
}

int main(int argc, char **argv)
{
    // Initializes the MPI environment with multi-threading support to ensure safe, 
    // concurrent execution of MPI calls within OpenMP threads.
    int rank, size, nrows = 3, ncols = 3;
    int permission;
    MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &permission);

    if (permission < MPI_THREAD_MULTIPLE)
    {
        printf("Error: MPI_THREAD_MULTIPLE is not supported.\n");
        fflush(stdout);
        MPI_Abort(MPI_COMM_WORLD, 1);
    }

    // Retrieves the process count and strictly validates the command-line arguments to 
    // ensure the specified 2D grid dimensions exactly match the number of available charging nodes.
    MPI_Comm_size(MPI_COMM_WORLD, &size);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    if (argc != 1 && argc != 3)
    {
        fprintf(stderr, "EVCNS error: %s\n", "usage: evcns [rows cols]");
        MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
        exit(EXIT_FAILURE);
    }
    if (argc >= 3)
    {
        nrows = (int)ParseInteger(argv[1], 1, INT_MAX);
        ncols = (int)ParseInteger(argv[2], 1, INT_MAX);
    }
    if ((long long)nrows * ncols != (long long)size - 1 || size - 1 > INT_MAX / 2)
    {
        fprintf(stderr, "EVCNS error: %s\n", "rows*cols must equal process count minus one base");
        MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
        exit(EXIT_FAILURE);
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

    if (rank == 0)
    {
        printf(">>> Running Electric Vehicle Charging Navigation System Simulation <<<\n");
        fflush(stdout);
    }

    // Registers a custom MPI datatype for congestion reports and splits the global communicator 
    // to logically isolate the Base Station from the interconnected charging nodes.
    MPI_Datatype report_type;
    CreateDataType(&report_type);

    MPI_Comm new_comm;
    MPI_Comm_split(MPI_COMM_WORLD, rank == size - 1, 0, &new_comm);

    // This conditional logic assigns the highest MPI rank to act as the centralized Base Station (MasterIO) 
    // and directs all other ranks to operate as EV charging nodes (SlaveIO).
    if (rank == size - 1)
    {
        MasterIO(MPI_COMM_WORLD, new_comm, nrows, ncols, report_type);
    }
    else
    {
        SlaveIO(MPI_COMM_WORLD, new_comm, nrows, ncols, report_type);
    }

    // This finalization step synchronizes all processes, exports the gathered performance metrics, 
    // frees allocated MPI resources, and safely shuts down the MPI environment.
    MPI_Barrier(MPI_COMM_WORLD);
    WriteMetrics(nrows, ncols, 1);
    MPI_Type_free(&report_type);
    MPI_Comm_free(&new_comm);
    MPI_Finalize();
    return 0;
}
