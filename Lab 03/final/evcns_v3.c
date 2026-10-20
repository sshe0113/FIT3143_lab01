/**
 * @file      evcns_v3.c
 * @brief     Electric Vehicle Charging Navigation System (EVCNS) Version 3
 *
 * @details
 *
 * @author    Shee Seng Cheng (34612467) - sshe0113@student.monash.edu
 * @author    Tay Chee Hsian (34612513) - ctay0040@student.monash.edu
 * @note      Coding Implementation for EVCNS
 */

#include "evcns_utils.h"

/**
 * @brief Defines a persistent state-tracking structure for each charging node.
 *
 * @details
 * ### What it is:
 * A data structure maintaining individual non-blocking MPI request handles and operation states for a specific peer. 
 * 
 * ### What it is used for:
 * It enables the Base Station to track concurrent, asynchronous communication with every charging node without blocking.
 */
typedef struct
{
    MsgData report;
    int count, *coords, ready_value, ack_value;
    bool ready_seen, ack_seen;
    MPI_Request report_recv, count_send, coords_send, ready_recv, ack_recv;
    MPI_Request disable_send, terminate_send;

    double report_recv_started, ready_recv_started, ack_recv_started;
    double count_send_started, coords_send_started;
    double disable_send_started, terminate_send_started;
} Peer;

/**
 * @brief Checks if a non-blocking MPI request has finished.
 *
 * @details
 * ### What it is:
 * A wrapper function around MPI_Test that safely evaluates the completion status of a given MPI_Request. 
 * 
 * ### What it is used for:
 * It drives the asynchronous event loop by allowing the system to continuously 
 * verify message delivery or receipt without halting execution.
 *
 * @param request Pointer to the MPI_Request handle to test.
 */
static bool Complete(MPI_Request *request)
{
    if (*request == MPI_REQUEST_NULL)
        return true;
    int done;
    MPI_Test(request, &done, MPI_STATUS_IGNORE);
    return done != 0;
}

/**
 * @brief Safely cancels a pending non-blocking receive request.
 *
 * @details
 * ### What it is:
 * A cleanup utility that invokes MPI_Cancel and MPI_Wait on an active MPI_Request. 
 * 
 * ### What it is used for:
 * It ensures that orphaned receive requests are properly terminated during the graceful shutdown phase to prevent memory leaks.
 *
 * @param request Pointer to the MPI_Request handle to cancel.
 */
static void CancelRecv(MPI_Request *request)
{
    if (*request == MPI_REQUEST_NULL)
        return;
    MPI_Cancel(request);
    MPI_Wait(request, MPI_STATUS_IGNORE);
}

//===========================================
// EV Charging Node
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

    int report_bytes;
    MPI_Type_size(report_type, &report_bytes);

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
    for (int i = 0; i < NUM_PORTS; i++)
        ports[i] = 1;
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
        if (thread_id < NUM_PORTS)
        {
            double last_update = MPI_Wtime();
            bool running;
            #pragma omp atomic read
            running = running_flag;

            while (running)
            {
                if (MPI_Wtime() - last_update >= PORT_PERIOD)
                {
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
        }
        else
        {
            /**
             * ### Main Thread: State Machine Setup
             * Initializes the non-blocking MPI request arrays and state variables for tracking concurrent network events. 
             * It sets up the persistent event-driven architecture, replacing synchronous polling with continuous 
             * asynchronous request tracking.
             */
            double comm_time = 0.0, comm_start_time;
            int signal = 1, disable_signal, terminate_signal;
            int requests[4] = {0}, reply_values[4] = {0};
            int avail[4] = {NO_NEIGHBOR, NO_NEIGHBOR, NO_NEIGHBOR, NO_NEIGHBOR};
            int neighbors[4] = {nbr_i_lo, nbr_i_hi, nbr_j_lo, nbr_j_hi};

            MPI_Request incoming[4], answering[4], sending[4], receiving[4];
            double incoming_start[4], answering_start[4], sending_start[4], receiving_start[4];

            MPI_Request disable = MPI_REQUEST_NULL, terminate = MPI_REQUEST_NULL, ready = MPI_REQUEST_NULL;
            double disable_start = 0, terminate_start = 0, ready_start = 0;

            MPI_Request report_send = MPI_REQUEST_NULL, base_receive = MPI_REQUEST_NULL;
            double report_send_start = 0, base_receive_start = 0;

            MsgData report;
            int count = 0, reply_stage = 0, coords_reply[2 * (global_size - 1)];
            bool disabled = false, ready_sent = false, querying = false, awaiting_base = false;
            double last_check = MPI_Wtime();

            comm_start_time = MPI_Wtime();
            for (int i = 0; i < 4; ++i)
            {
                incoming[i] = answering[i] = sending[i] = receiving[i] = MPI_REQUEST_NULL;
                if (neighbors[i] != MPI_PROC_NULL)
                {
                    incoming_start[i] = MPI_Wtime();
                    MPI_Irecv(&requests[i], 1, MPI_INT, neighbors[i], MSG_REQ_NEIGHBOR, comm2D, &incoming[i]);
                    message_metrics[MSG_REQ_NEIGHBOR].receive_call_time += MPI_Wtime() - incoming_start[i];
                }
            }
            disable_start = MPI_Wtime();
            MPI_Irecv(&disable_signal, 1, MPI_INT, global_size - 1, MSG_DISABLE, world_comm, &disable);
            message_metrics[MSG_DISABLE].receive_call_time += MPI_Wtime() - disable_start;

            terminate_start = MPI_Wtime();
            MPI_Irecv(&terminate_signal, 1, MPI_INT, global_size - 1, MSG_TERMINATE, world_comm, &terminate);
            message_metrics[MSG_TERMINATE].receive_call_time += MPI_Wtime() - terminate_start;
            comm_time += (MPI_Wtime() - comm_start_time);

            for (;;)
            {
                /**
                 * ### Main Thread: Disable Signal Handling
                 * Asynchronously tests for the MSG_DISABLE signal from the Base Station using the Complete() helper. 
                 * It gracefully halts the generation of new internal port states and blocks new outgoing queries 
                 * once the fixed simulation duration concludes.
                 */
                comm_start_time = MPI_Wtime();
                bool disable_Completed = Complete(&disable);
                comm_time += (MPI_Wtime() - comm_start_time);

                if (!disabled && disable_Completed)
                {
                    TrackReceiveMetric(MSG_DISABLE, 1, sizeof(int), 0, MPI_Wtime() - disable_start);
                    disabled = true;
                    #pragma omp atomic write
                    running_flag = false;
                }

                /**
                 * ### Main Thread: Neighbor Request Processing
                 * An event-driven loop that detects incoming neighbor queries and dispatches non-blocking MSG_RES_NEIGHBOR replies.
                 * It seamlessly assists neighboring nodes in their load-balancing efforts while simultaneously allowing the current 
                 * node to process its own outgoing tasks.
                 */
                for (int i = 0; i < 4; ++i)
                {
                    if (neighbors[i] == MPI_PROC_NULL)
                        continue;

                    comm_start_time = MPI_Wtime();
                    bool ans_Completed = Complete(&answering[i]);
                    comm_time += (MPI_Wtime() - comm_start_time);

                    if (!ans_Completed)
                        continue;
                    if (answering[i] != MPI_REQUEST_NULL && ans_Completed)
                    {
                        TrackSendMetric(MSG_RES_NEIGHBOR, 1, sizeof(int), 0, MPI_Wtime() - answering_start[i]);
                        answering[i] = MPI_REQUEST_NULL;
                    }

                    if (incoming[i] == MPI_REQUEST_NULL)
                    {
                        comm_start_time = MPI_Wtime();
                        incoming_start[i] = MPI_Wtime();
                        MPI_Irecv(&requests[i], 1, MPI_INT, neighbors[i], MSG_REQ_NEIGHBOR, comm2D, &incoming[i]);
                        message_metrics[MSG_REQ_NEIGHBOR].receive_call_time += MPI_Wtime() - incoming_start[i];
                        comm_time += (MPI_Wtime() - comm_start_time);
                    }

                    comm_start_time = MPI_Wtime();
                    bool inc_Completed = Complete(&incoming[i]);
                    comm_time += (MPI_Wtime() - comm_start_time);

                    if (inc_Completed && incoming[i] != MPI_REQUEST_NULL)
                    {
                        TrackReceiveMetric(MSG_REQ_NEIGHBOR, 1, sizeof(int), 0, MPI_Wtime() - incoming_start[i]);
                        incoming[i] = MPI_REQUEST_NULL;

                        int curr_avail = 0;
                        comp_start_time = MPI_Wtime();
                        #pragma omp critical
                        {
                            for (int p = 0; p < NUM_PORTS; p++)
                                curr_avail += ports[p];
                        }
                        comp_time += (MPI_Wtime() - comp_start_time);

                        reply_values[i] = curr_avail;

                        comm_start_time = MPI_Wtime();
                        answering_start[i] = MPI_Wtime();
                        MPI_Isend(&reply_values[i], 1, MPI_INT, neighbors[i], MSG_RES_NEIGHBOR, comm2D, &answering[i]);
                        message_metrics[MSG_RES_NEIGHBOR].send_call_time += MPI_Wtime() - answering_start[i];
                        comm_time += (MPI_Wtime() - comm_start_time);
                    }
                }

                /**
                 * ### Main Thread: Base Station Recommendation Handling
                 * A multi-stage asynchronous receiver that securely processes the recommended node count and 
                 * subsequent coordinates from the Base Station.
                 * It releases the node from its reporting wait state and prints the recommended geographical 
                 * coordinates to reroute incoming EVs without blocking execution.
                 */
                if (awaiting_base)
                {
                    comm_start_time = MPI_Wtime();
                    bool send_done = Complete(&report_send);
                    bool base_recv_Completed = Complete(&base_receive);
                    comm_time += (MPI_Wtime() - comm_start_time);

                    if (send_done && report_send != MPI_REQUEST_NULL)
                    {
                        TrackSendMetric(MSG_REPORT, 1, report_bytes, 0, MPI_Wtime() - report_send_start);
                        report_send = MPI_REQUEST_NULL;
                    }

                    if (reply_stage == 1 && base_recv_Completed)
                    {
                        TrackReceiveMetric(MSG_BS_REPLY, 1, sizeof(int), 0, MPI_Wtime() - base_receive_start);
                        if (count > 0)
                        {
                            comm_start_time = MPI_Wtime();
                            base_receive_start = MPI_Wtime();
                            MPI_Irecv(coords_reply, count * 2, MPI_INT, global_size - 1, MSG_BS_REPLY, world_comm, &base_receive);
                            message_metrics[MSG_BS_REPLY].receive_call_time += MPI_Wtime() - base_receive_start;
                            comm_time += (MPI_Wtime() - comm_start_time);
                            reply_stage = 2;
                        }
                        else
                        {
                            reply_stage = 3;
                        }
                    }

                    if (reply_stage == 2)
                    {
                        comm_start_time = MPI_Wtime();
                        bool stage2_Completed = Complete(&base_receive);
                        comm_time += (MPI_Wtime() - comm_start_time);
                        if (stage2_Completed)
                        {
                            TrackReceiveMetric(MSG_BS_REPLY, 1, (count * 2) * sizeof(int), 0, MPI_Wtime() - base_receive_start);
                            reply_stage = 3;
                        }
                    }

                    if (reply_stage == 3 && send_done)
                    {
                        if (count == 0)
                        {
                            printf("[Node Rank %d at (%d,%d)] No available nodes nearby\n", global_rank, coord[0], coord[1]);
                            fflush(stdout);
                        }
                        else
                        {
                            printf("[Node Rank %d at (%d,%d)] Nearest has %d available node(s): ", global_rank, coord[0], coord[1], count);
                            for (int i = 0; i < count; i++)
                            {
                                printf("(%d,%d) ", coords_reply[i * 2], coords_reply[i * 2 + 1]);
                            }
                            printf("to EVs\n");
                            fflush(stdout);
                        }

                        awaiting_base = false;
                    }
                }

                /**
                 * ### Main Thread: Congestion Checking & Neighbor Polling
                 * An asynchronous polling mechanism that dispatches parallel non-blocking requests to 
                 * all physical neighbors when the congestion threshold is met. 
                 * It functions as the first tier of decentralized load balancing, attempting to discover 
                 * nearby charging capacity without stalling the node's main event loop.
                 */
                if (querying)
                {
                    int receives_done, sends_done;
                    comm_start_time = MPI_Wtime();
                    MPI_Testall(4, receiving, &receives_done, MPI_STATUSES_IGNORE);
                    MPI_Testall(4, sending, &sends_done, MPI_STATUSES_IGNORE);
                    comm_time += (MPI_Wtime() - comm_start_time);

                    if (receives_done && sends_done)
                    {
                        for (int i = 0; i < 4; ++i)
                        {
                            if (neighbors[i] != MPI_PROC_NULL)
                            {
                                if (receiving[i] != MPI_REQUEST_NULL)
                                {
                                    TrackReceiveMetric(MSG_RES_NEIGHBOR, 1, sizeof(int), 0, MPI_Wtime() - receiving_start[i]);
                                    receiving[i] = MPI_REQUEST_NULL;
                                }
                                if (sending[i] != MPI_REQUEST_NULL)
                                {
                                    TrackSendMetric(MSG_REQ_NEIGHBOR, 1, sizeof(int), 0, MPI_Wtime() - sending_start[i]);
                                    sending[i] = MPI_REQUEST_NULL;
                                }
                            }
                        }

                        querying = false;
                        int loc_avail_ports = 0;

                        comp_start_time = MPI_Wtime();
                        #pragma omp critical
                        {
                            for (int p = 0; p < NUM_PORTS; p++)
                                loc_avail_ports += ports[p];
                        }

                        bool all_neigh_full = true;
                        for (int i = 0; i < 4; i++)
                        {
                            if (avail[i] > THRESHOLD)
                                all_neigh_full = false;
                        }
                        comp_time += (MPI_Wtime() - comp_start_time);

                        /**
                         * ### Main Thread: Congestion Reporting & Local Referral
                         * Evaluates neighbor replies and either executes a local EV referral or triggers 
                         * a non-blocking MSG_REPORT to the Base Station.
                         * It dictates the routing protocol based on real-time local capacity, offloading 
                         * localized traffic directly or escalating severe congestion to the central system.
                         */
                        if (all_neigh_full)
                        {
                            comp_start_time = MPI_Wtime();
                            report.timestamp = MPI_Wtime() - start_time;
                            report.sender_rank = global_rank;
                            report.coords[0] = coord[0];
                            report.coords[1] = coord[1];
                            report.avail_ports = loc_avail_ports;
                            for (int i = 0; i < 4; i++)
                                report.avail_neigh[i] = avail[i];
                            report.msg_exchanged = 8;
                            report.msg_size = 8 * sizeof(int);
                            IncidentDateTime(report.incident_time);
                            comp_time += (MPI_Wtime() - comp_start_time);

                            comm_start_time = MPI_Wtime();
                            base_receive_start = MPI_Wtime();
                            MPI_Irecv(&count, 1, MPI_INT, global_size - 1, MSG_BS_REPLY, world_comm, &base_receive);
                            message_metrics[MSG_BS_REPLY].receive_call_time += MPI_Wtime() - base_receive_start;

                            report_send_start = MPI_Wtime();
                            MPI_Isend(&report, 1, report_type, global_size - 1, MSG_REPORT, world_comm, &report_send);
                            message_metrics[MSG_REPORT].send_call_time += MPI_Wtime() - report_send_start;
                            comm_time += (MPI_Wtime() - comm_start_time);

                            awaiting_base = true;
                            reply_stage = 1;
                        }
                        else
                        {
                            LocalReferral(global_rank, coord, avail);
                        }
                    }
                }

                if (!disabled && !querying && !awaiting_base && MPI_Wtime() - last_check >= CHECK_PERIOD)
                {
                    last_check = MPI_Wtime();
                    int loc_avail_ports = 0;

                    comp_start_time = MPI_Wtime();
                    #pragma omp critical
                    {
                        for (int p = 0; p < NUM_PORTS; p++)
                            loc_avail_ports += ports[p];
                    }
                    comp_time += (MPI_Wtime() - comp_start_time);

                    if (loc_avail_ports <= THRESHOLD)
                    {
                        querying = true;
                        comm_start_time = MPI_Wtime();
                        for (int i = 0; i < 4; ++i)
                        {
                            avail[i] = NO_NEIGHBOR;
                            if (neighbors[i] == MPI_PROC_NULL)
                                continue;
                            receiving_start[i] = MPI_Wtime();
                            MPI_Irecv(&avail[i], 1, MPI_INT, neighbors[i], MSG_RES_NEIGHBOR, comm2D, &receiving[i]);
                            message_metrics[MSG_RES_NEIGHBOR].receive_call_time += MPI_Wtime() - receiving_start[i];

                            sending_start[i] = MPI_Wtime();
                            MPI_Isend(&signal, 1, MPI_INT, neighbors[i], MSG_REQ_NEIGHBOR, comm2D, &sending[i]);
                            message_metrics[MSG_REQ_NEIGHBOR].send_call_time += MPI_Wtime() - sending_start[i];
                        }
                        comm_time += (MPI_Wtime() - comm_start_time);
                    }
                }

                /**
                 * ### Main Thread: Graceful Shutdown Handshake
                 * A synchronized termination sequence utilizing non-blocking sends for MSG_READY and MSG_TERMINATED signals.
                 * It ensures a safe global exit by verifying the Base Station's acknowledgment before breaking the event loop 
                 * and tearing down the MPI environment.
                 */
                if (disabled && !querying && !awaiting_base && !ready_sent)
                {
                    comm_start_time = MPI_Wtime();
                    ready_start = MPI_Wtime();
                    MPI_Isend(&signal, 1, MPI_INT, global_size - 1, MSG_READY, world_comm, &ready);
                    message_metrics[MSG_READY].send_call_time += MPI_Wtime() - ready_start;
                    comm_time += (MPI_Wtime() - comm_start_time);
                    ready_sent = true;
                }

                comm_start_time = MPI_Wtime();
                bool term_Completed = Complete(&terminate);
                comm_time += (MPI_Wtime() - comm_start_time);

                if (ready_sent && term_Completed)
                {
                    TrackReceiveMetric(MSG_TERMINATE, 1, sizeof(int), 0, MPI_Wtime() - terminate_start);
                    comm_start_time = MPI_Wtime();
                    for (int i = 0; i < 4; ++i)
                        CancelRecv(&incoming[i]);
                    MPI_Waitall(4, answering, MPI_STATUSES_IGNORE);
                    MPI_Wait(&ready, MPI_STATUS_IGNORE);
                    TrackSendMetric(MSG_READY, 1, sizeof(int), 0, MPI_Wtime() - ready_start);

                    MPI_Request ack;
                    double ack_start = MPI_Wtime();
                    MPI_Isend(&signal, 1, MPI_INT, global_size - 1, MSG_TERMINATED, world_comm, &ack);
                    message_metrics[MSG_TERMINATED].send_call_time += MPI_Wtime() - ack_start;
                    MPI_Wait(&ack, MPI_STATUS_IGNORE);
                    TrackSendMetric(MSG_TERMINATED, 1, sizeof(int), 0, MPI_Wtime() - ack_start);
                    comm_time += (MPI_Wtime() - comm_start_time);
                    break;
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
     * Extracts the independent computation and communication timings gathered throughout the 
     * node's lifecycle and aggregates them globally using MPI_Reduce. 
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
     * Initializes the global Base Station variables and allocates a dedicated 
     * array of persistent Peer structures for each charging node. 
     * It constructs the foundation of the event-driven architecture, enabling the 
     * Base Station to track and manage concurrent asynchronous states for the entire grid.
     */
    int global_size;
    MPI_Comm_size(world_comm, &global_size);
    int stations = global_size - 1;

    int report_bytes;
    MPI_Type_size(report_type, &report_bytes);

    /**
     * ### Initialization of Global State Matrix and Log Files
     * This section initializes the global 2D matrix representing port availability and prepares the main simulation log file. 
     * It acts as the central state registry to track recent congestion events and records the initial configuration parameters before 
     * the main loop begins.
     */
    int grid_state[nrows][ncols];
    double last_report_time[nrows][ncols];
    for (int r = 0; r < nrows; r++)
    {
        for (int c = 0; c < ncols; c++)
        {
            grid_state[r][c] = NUM_PORTS;
            last_report_time[r][c] = -999;
        }
    }

    FILE *log_file = fopen("log.txt", "w");
    if (!log_file)
    {
        printf("Error: Cannot open log file.\n");
        return;
    }

    Peer *peers = calloc((size_t)stations, sizeof(*peers));
    if (!peers)
        MPI_Abort(world_comm, EXIT_FAILURE);
    int signal = 1, ready_count = 0, ack_count = 0;
    bool disabling = false, terminating = false;

    double start_time = MPI_Wtime();
    double comm_time = 0.0, comm_start_time;
    double comp_time = 0.0, comp_start_time;

    int ttl_alerts = 0, ttl_events = 0;

    printf("[Base Station] Listening for reports...\n");
    fflush(stdout);
    fprintf(log_file, "----- SIMULATION STARTED -----\n");
    fprintf(log_file,
            "Configuration: version=v3_task3 rows=%d cols=%d processes=%d ports=%d threads=%d "
            "seconds=%.3f PORT_PERIOD=%.3f CHECK_PERIOD=%.3f "
            "PRED_PERIOD=%.6f CONGEST_RATE=%d\n",
            nrows, ncols, global_size, NUM_PORTS, NUM_PORTS + 1,
            SIMULATION_SEC, PORT_PERIOD, CHECK_PERIOD,
            PRED_PERIOD, CONGEST_RATE);
    fflush(log_file);

    comm_start_time = MPI_Wtime();
    for (int i = 0; i < stations; ++i)
    {
        Peer *p = &peers[i];
        p->coords = malloc((size_t)stations * 2 * sizeof(int));
        if (!p->coords)
            MPI_Abort(world_comm, EXIT_FAILURE);
        p->count_send = p->coords_send = p->disable_send = p->terminate_send = MPI_REQUEST_NULL;

        p->report_recv_started = MPI_Wtime();
        MPI_Irecv(&p->report, 1, report_type, i, MSG_REPORT, world_comm, &p->report_recv);
        message_metrics[MSG_REPORT].receive_call_time += MPI_Wtime() - p->report_recv_started;

        p->ready_recv_started = MPI_Wtime();
        MPI_Irecv(&p->ready_value, 1, MPI_INT, i, MSG_READY, world_comm, &p->ready_recv);
        message_metrics[MSG_READY].receive_call_time += MPI_Wtime() - p->ready_recv_started;

        p->ack_recv_started = MPI_Wtime();
        MPI_Irecv(&p->ack_value, 1, MPI_INT, i, MSG_TERMINATED, world_comm, &p->ack_recv);
        message_metrics[MSG_TERMINATED].receive_call_time += MPI_Wtime() - p->ack_recv_started;
    }
    comm_time += (MPI_Wtime() - comm_start_time);

    /**
     * ### Simulated Timeout Detection and Shutdown Signaling Broadcast
     * Monitors the elapsed simulation time and dispatches non-blocking MSG_DISABLE and MSG_TERMINATE commands to all nodes.
     * It strictly enforces the simulation's fixed duration and orchestrates a secure, non-blocking two-phase graceful 
     * shutdown across the distributed cluster.
     */
    while (ack_count < stations)
    {
        if (!disabling && MPI_Wtime() - start_time >= SIMULATION_SEC)
        {
            printf("[Base Station] Time limit reached. Finishing active enquiries...\n");
            fflush(stdout);
            disabling = true;

            comm_start_time = MPI_Wtime();
            for (int i = 0; i < stations; ++i)
            {
                peers[i].disable_send_started = MPI_Wtime();
                MPI_Isend(&signal, 1, MPI_INT, i, MSG_DISABLE, world_comm, &peers[i].disable_send);
                message_metrics[MSG_DISABLE].send_call_time += MPI_Wtime() - peers[i].disable_send_started;
            }
            comm_time += (MPI_Wtime() - comm_start_time);
        }

        /**
         * ### Asynchronous Message Processing
         * The core event loop that continually evaluates the completion status 
         * of non-blocking sends and receives for every registered peer.
         * It independently processes incoming congestion reports and shutdown 
         * confirmations in a fully non-blocking manner, eliminating system deadlocks.
         */
        for (int i = 0; i < stations; ++i)
        {
            Peer *p = &peers[i];

            comm_start_time = MPI_Wtime();
            bool count_done = Complete(&p->count_send);
            bool coords_done = Complete(&p->coords_send);
            comm_time += (MPI_Wtime() - comm_start_time);

            if (count_done && p->count_send != MPI_REQUEST_NULL)
            {
                TrackSendMetric(MSG_BS_REPLY, 1, sizeof(int), 0, MPI_Wtime() - p->count_send_started);
                p->count_send = MPI_REQUEST_NULL;
            }

            if (coords_done && p->coords_send != MPI_REQUEST_NULL)
            {
                TrackSendMetric(MSG_BS_REPLY, 1, (p->count * 2) * sizeof(int), 0, MPI_Wtime() - p->coords_send_started);
                p->coords_send = MPI_REQUEST_NULL;
            }

            if (count_done && coords_done && !p->ready_seen)
            {
                if (p->report_recv == MPI_REQUEST_NULL)
                {
                    comm_start_time = MPI_Wtime();
                    p->report_recv_started = MPI_Wtime();
                    MPI_Irecv(&p->report, 1, report_type, i, MSG_REPORT, world_comm, &p->report_recv);
                    message_metrics[MSG_REPORT].receive_call_time += MPI_Wtime() - p->report_recv_started;
                    comm_time += (MPI_Wtime() - comm_start_time);
                }

                comm_start_time = MPI_Wtime();
                bool report_Completed = Complete(&p->report_recv);
                comm_time += (MPI_Wtime() - comm_start_time);

                if (report_Completed && p->report_recv != MPI_REQUEST_NULL)
                {
                    TrackReceiveMetric(MSG_REPORT, 1, report_bytes, 0, MPI_Wtime() - p->report_recv_started);
                    p->report_recv = MPI_REQUEST_NULL;

                    comp_start_time = MPI_Wtime();
                    ttl_alerts++;
                    ttl_events += p->report.msg_exchanged + 1;
                    double curr_time = MPI_Wtime();

                    grid_state[p->report.coords[0]][p->report.coords[1]] = p->report.avail_ports;
                    last_report_time[p->report.coords[0]][p->report.coords[1]] = curr_time;

                    int nbr_coords[4][2] = {
                        {p->report.coords[0] - 1, p->report.coords[1]},
                        {p->report.coords[0] + 1, p->report.coords[1]},
                        {p->report.coords[0], p->report.coords[1] - 1},
                        {p->report.coords[0], p->report.coords[1] + 1}};

                    bool all_nbrs_congested_recently = true;
                    int valid_nbrs = 0;

                    for (int n = 0; n < 4; n++)
                    {
                        if (p->report.avail_neigh[n] != NO_NEIGHBOR)
                        {
                            int r = nbr_coords[n][0];
                            int c = nbr_coords[n][1];
                            if (r >= 0 && r < nrows && c >= 0 && c < ncols)
                            {
                                grid_state[r][c] = p->report.avail_neigh[n];
                                valid_nbrs++;
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
                    int max_candidates = stations; // Return all equal-nearest stations
                    p->count = 0;

                    if (valid_nbrs > 0 && all_nbrs_congested_recently)
                    {
                        fprintf(log_file, "T:%.2fs, No available nodes nearby\n", p->report.timestamp);
                    }
                    else
                    {
                        int max_dist = nrows + ncols;
                        bool found = false;

                        for (int d = 1; d <= max_dist && !found; d++)
                        {
                            for (int dx = -d; dx <= d; dx++)
                            {
                                int dy = d - abs(dx);
                                int r = p->report.coords[0] + dx;
                                int c = p->report.coords[1] + dy;
                                if (r >= 0 && r < nrows && c >= 0 && c < ncols)
                                {
                                    if (grid_state[r][c] > THRESHOLD && p->count < max_candidates)
                                    {
                                        p->coords[p->count * 2] = r;
                                        p->coords[p->count * 2 + 1] = c;
                                        p->count++;
                                        found = true;
                                    }
                                }
                                if (dy != 0)
                                {
                                    c = p->report.coords[1] - dy;
                                    if (r >= 0 && r < nrows && c >= 0 && c < ncols)
                                    {
                                        if (grid_state[r][c] > THRESHOLD && p->count < max_candidates)
                                        {
                                            p->coords[p->count * 2] = r;
                                            p->coords[p->count * 2 + 1] = c;
                                            p->count++;
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
                     * Triggers non-blocking MSG_BS_REPLY sends containing the optimal alternative 
                     * coordinates and commits the visual grid snapshot to the log.
                     * It promptly dispatches routing instructions back to the congested node while 
                     * permanently logging the incident's spatial context without halting the event loop.
                     */
                    comm_start_time = MPI_Wtime();
                    p->count_send_started = MPI_Wtime();
                    MPI_Isend(&p->count, 1, MPI_INT, i, MSG_BS_REPLY, world_comm, &p->count_send);
                    message_metrics[MSG_BS_REPLY].send_call_time += MPI_Wtime() - p->count_send_started;

                    if (p->count > 0)
                    {
                        p->coords_send_started = MPI_Wtime();
                        MPI_Isend(p->coords, p->count * 2, MPI_INT, i, MSG_BS_REPLY, world_comm, &p->coords_send);
                        message_metrics[MSG_BS_REPLY].send_call_time += MPI_Wtime() - p->coords_send_started;
                    }
                    comm_time += (MPI_Wtime() - comm_start_time);

                    int best_row = (p->count > 0) ? p->coords[0] : -1;
                    int best_col = (p->count > 0) ? p->coords[1] : -1;

                    fprintf(
                        log_file, "T:%.2fs | ALERT [%d] | Rank: %d Coord: (%d,%d) | Ports Free: %d | Neighbor Free: [T:%d, B:%d, L:%d, R:%d] | Msgs: %d | Msg Size: %d Bytes | Nearest: (%d,%d)\n",
                        p->report.timestamp, ttl_alerts, p->report.sender_rank, p->report.coords[0], p->report.coords[1],
                        p->report.avail_ports, p->report.avail_neigh[0], p->report.avail_neigh[1], p->report.avail_neigh[2], p->report.avail_neigh[3],
                        p->report.msg_exchanged, p->report.msg_size, best_row, best_col);

                    fprintf(log_file, "Incident date/time: %s\n", p->report.incident_time);
                    fprintf(log_file, "Recommended nodes: %d", p->count);
                    for (int k = 0; k < p->count; k++)
                        fprintf(log_file, " (%d,%d)", p->coords[2 * k], p->coords[2 * k + 1]);
                    fprintf(log_file, "\n");

                    PrintGrid(log_file, p->report.timestamp, nrows, ncols, grid_state, p->report.coords[0], p->report.coords[1], best_row, best_col);
                    fflush(log_file);
                }
            }

            comm_start_time = MPI_Wtime();
            bool ready_Completed = Complete(&p->ready_recv);
            comm_time += (MPI_Wtime() - comm_start_time);
            if (!p->ready_seen && ready_Completed)
            {
                TrackReceiveMetric(MSG_READY, 1, sizeof(int), 0, MPI_Wtime() - p->ready_recv_started);
                p->ready_seen = true;
                ++ready_count;
            }

            comm_start_time = MPI_Wtime();
            bool ack_Completed = Complete(&p->ack_recv);
            comm_time += (MPI_Wtime() - comm_start_time);
            if (terminating && !p->ack_seen && ack_Completed)
            {
                TrackReceiveMetric(MSG_TERMINATED, 1, sizeof(int), 0, MPI_Wtime() - p->ack_recv_started);
                p->ack_seen = true;
                ++ack_count;
            }
        }

        if (disabling && ready_count == stations && !terminating)
        {
            terminating = true;
            comm_start_time = MPI_Wtime();
            for (int i = 0; i < stations; ++i)
            {
                peers[i].terminate_send_started = MPI_Wtime();
                MPI_Isend(&signal, 1, MPI_INT, i, MSG_TERMINATE, world_comm, &peers[i].terminate_send);
                message_metrics[MSG_TERMINATE].send_call_time += MPI_Wtime() - peers[i].terminate_send_started;
            }
            comm_time += (MPI_Wtime() - comm_start_time);
        }
        usleep(1000);
    }

    /**
     * ### Final Cleanup and Performance Summary Report
     * Cancels all pending receive requests and blocks until all outgoing 
     * asynchronous termination commands have successfully completed.
     * It securely finalizes the event-driven state machine, ensuring no orphaned 
     * operations remain before exporting the simulation's performance summary.
     */
    comm_start_time = MPI_Wtime();
    for (int i = 0; i < stations; ++i)
    {
        Peer *p = &peers[i];
        CancelRecv(&p->report_recv);
        MPI_Request sends[4] = {p->count_send, p->coords_send, p->disable_send, p->terminate_send};
        MPI_Waitall(4, sends, MPI_STATUSES_IGNORE);

        double current_time = MPI_Wtime();
        if (p->disable_send != MPI_REQUEST_NULL)
            TrackSendMetric(MSG_DISABLE, 1, sizeof(int), 0, current_time - p->disable_send_started);
        if (p->terminate_send != MPI_REQUEST_NULL)
            TrackSendMetric(MSG_TERMINATE, 1, sizeof(int), 0, current_time - p->terminate_send_started);
    }
    comm_time += (MPI_Wtime() - comm_start_time);

    for (int i = 0; i < stations; ++i)
        free(peers[i].coords);
    free(peers);

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
    // and directs all other ranks to operate as EV charging nodes (SlaveIO) utilizing the fully asynchronous logic.
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
    WriteMetrics(nrows, ncols, 3);
    MPI_Type_free(&report_type);
    MPI_Comm_free(&new_comm);
    MPI_Finalize();
    return 0;
}
