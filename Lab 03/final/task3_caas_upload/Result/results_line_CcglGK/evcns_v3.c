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
#include "utils.h"

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
typedef struct {
    MsgData report;
    int count, *coords, value, incoming_tag;
    bool ready_seen, ack_seen;
    MPI_Request incoming, count_send, coords_send, disable_send, terminate_send;
    double incoming_start, count_start, coords_start, disable_start, terminate_start;
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
 * @return True only for a newly completed active request; false if inactive or pending.
 * Test for NULL before MPI_Test. On completion MPI_Test clears the
 * ordinary request, so a later non-NULL condition would skip its received payload.
 */
static bool Complete(MPI_Request *request) {
    if (*request == MPI_REQUEST_NULL) return false;
    int done;
    MPI_Test(request, &done, MPI_STATUS_IGNORE);
    return done != 0;
}

//===========================================
// EV Charging Node
//===========================================
void SlaveIO(MPI_Comm world_comm, MPI_Comm comm, int nrows, int ncols, MPI_Datatype report_type) {
    /**
     * ### MPI Setup and Topology Creation
     * Initializes the MPI environment variables and constructs a 2D Cartesian grid topology for the charging nodes. 
     * It is used to establish the spatial relationships between nodes, identifying the immediate top, bottom, left, 
     * and right neighbors required for localized communication.
     */
    int rank, size, coord[2], neighbors[4], ports[NUM_PORTS], report_bytes;
    MPI_Comm comm2D;
    MPI_Comm_rank(world_comm, &rank);
    MPI_Comm_size(world_comm, &size);

    MPI_Type_size(report_type, &report_bytes);
    CreateGrid(comm, nrows, ncols, &comm2D, coord, neighbors);
    srand(time(NULL) + rank);

    /**
     * ### OpenMP Thread Initialization
     * Sets up the initial port states and spawns a designated number of OpenMP threads to execute concurrent tasks. 
     * It ensures the simulation runs correctly by allocating exactly one worker thread per charging port alongside 
     * a single main thread dedicated to MPI communication.
     */
    for (int i = 0; i < NUM_PORTS; i++) ports[i] = 1;
    bool running_flag = true;

    MPI_Barrier(world_comm);
    double start_time = MPI_Wtime();
    double node_comp_time = 0.0;

    #pragma omp parallel num_threads(NUM_PORTS + 1) shared(ports, running_flag, node_comp_time)
    {
        int thread_id = omp_get_thread_num();
        // One thread checks the team size; the same value applies to every thread.
        if (thread_id == 0 && omp_get_num_threads() != NUM_PORTS + 1) MPI_Abort(world_comm, EXIT_FAILURE);
        double comp_time = 0.0;
        /**
         * ### Port State Simulation (Worker Threads)
         * These worker threads asynchronously simulate real-world electric vehicle behavior by randomly 
         * toggling the availability of individual charging ports based on a predefined probability.
         * This mechanism is used to generate dynamic, independent load fluctuations across the charging 
         * network without blocking the node's main communication thread.
         */
        if (thread_id < NUM_PORTS) {
            SimulatePort(thread_id, ports, &running_flag, &comp_time);
        } else {

            /**
             * ### Main Thread: State Machine Setup
             * Initializes the non-blocking MPI request arrays and state variables for tracking concurrent network events. 
             * It sets up the persistent event-driven architecture, replacing synchronous polling with continuous 
             * asynchronous request tracking.
             */
            int signal = 1, requests[4], answers[4], avail_neigh[4];
            int available = 0, degree = 0, count = 0, coords_reply[2*(size-1)], reply_stage = 0;

            int disable_value, terminate_value;
            bool disabled = false, terminated = false, ready_sent = false;
            bool querying = false, awaiting_base = false;

            MPI_Request incoming[4], answering[4], sending[4], receiving[4];
            MPI_Request disable = MPI_REQUEST_NULL, terminate = MPI_REQUEST_NULL;
            MPI_Request ready = MPI_REQUEST_NULL, report_send = MPI_REQUEST_NULL, base_receive = MPI_REQUEST_NULL;
            double incoming_start[4], answering_start[4], sending_start[4], receiving_start[4];
            double disable_start = 0, terminate_start = 0, ready_start = 0, report_start = 0, base_start = 0;
            double last_check = MPI_Wtime();
            MsgData report;
            for (int i = 0; i < 4; i++)
                incoming[i] = answering[i] = sending[i] = receiving[i] = MPI_REQUEST_NULL;
            for (;;) {
                int flag;

                /**
                 * ### Main Thread: Disable Signal Handling
                 * Asynchronously tests for the MSG_DISABLE signal from the Base Station using the Complete() helper. 
                 * It gracefully halts the generation of new internal port states and blocks new outgoing queries 
                 * once the fixed simulation duration concludes.
                 */
                if (!disabled && disable == MPI_REQUEST_NULL) {
                    MPI_Iprobe(size-1, MSG_DISABLE, world_comm, &flag, MPI_STATUS_IGNORE);
                    if (flag) {
                        disable_start = MPI_Wtime();
                        MPI_Irecv(&disable_value, 1, MPI_INT, size-1, MSG_DISABLE, world_comm, &disable);
                        message_metrics[MSG_DISABLE].receive_call_time += MPI_Wtime() - disable_start;
                    }
                }
                if (Complete(&disable)) {
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
                for (int i = 0; i < 4; i++) {
                    if (neighbors[i] == MPI_PROC_NULL) continue;
                    if (Complete(&answering[i]))
                        TrackSendMetric(MSG_RES_NEIGHBOR, 1, sizeof(int), 0, MPI_Wtime() - answering_start[i]);

                    // Probe before posting, and reuse this direction's reply buffer
                    // only after its previous send completes. No idle receive needs cancellation.
                    if (incoming[i] == MPI_REQUEST_NULL && answering[i] == MPI_REQUEST_NULL) {
                        MPI_Iprobe(neighbors[i], MSG_REQ_NEIGHBOR, comm2D, &flag, MPI_STATUS_IGNORE);
                        if (flag) {
                            incoming_start[i] = MPI_Wtime();
                            MPI_Irecv(&requests[i], 1, MPI_INT, neighbors[i], MSG_REQ_NEIGHBOR, comm2D, &incoming[i]);
                            message_metrics[MSG_REQ_NEIGHBOR].receive_call_time += MPI_Wtime() - incoming_start[i];
                        }
                    }
                    if (Complete(&incoming[i])) {
                        TrackReceiveMetric(MSG_REQ_NEIGHBOR, 1, sizeof(int), 0, MPI_Wtime() - incoming_start[i]);
                        answers[i] = AvailablePorts(ports, &comp_time);
                        answering_start[i] = MPI_Wtime();
                        MPI_Isend(&answers[i], 1, MPI_INT, neighbors[i], MSG_RES_NEIGHBOR, comm2D, &answering[i]);
                        message_metrics[MSG_RES_NEIGHBOR].send_call_time += MPI_Wtime() - answering_start[i];
                    }
                }

                /**
                 * ### Main Thread: Congestion Checking & Neighbor Polling
                 * An asynchronous polling mechanism that dispatches parallel non-blocking requests to 
                 * all physical neighbors when the congestion threshold is met. 
                 * It functions as the first tier of decentralized load balancing, attempting to discover 
                 * nearby charging capacity without stalling the node's main event loop.
                 */
                if (querying) {
                    bool done = true;
                    for (int i = 0; i < 4; i++) {
                        if (Complete(&sending[i]))
                            TrackSendMetric(MSG_REQ_NEIGHBOR, 1, sizeof(int), 0, MPI_Wtime() - sending_start[i]);
                        if (Complete(&receiving[i]))
                            TrackReceiveMetric(MSG_RES_NEIGHBOR, 1, sizeof(int), 0, MPI_Wtime() - receiving_start[i]);
                        if (sending[i] != MPI_REQUEST_NULL || receiving[i] != MPI_REQUEST_NULL) done = false;
                    }
                    if (done) {
                        querying = false;
                        double comp_s = ComputationTime();
                        bool all_full = true;
                        for (int i = 0; i < 4; i++) if (avail_neigh[i] > THRESHOLD) all_full = false;
                        comp_time += ComputationTime() - comp_s;

                        /**
                         * ### Main Thread: Congestion Reporting & Local Referral
                         * Evaluates neighbor replies and either executes a local EV referral or triggers 
                         * a non-blocking MSG_REPORT to the Base Station.
                         * It dictates the routing protocol based on real-time local capacity, offloading 
                         * localized traffic directly or escalating severe congestion to the central system.
                         */
                        if (all_full) {

                            // Report the original congestion snapshot and actual neighbour count.
                            BuildReport(&report, rank, coord, available, avail_neigh, degree, start_time, &comp_time);
                            base_start = MPI_Wtime();
                            MPI_Irecv(&count, 1, MPI_INT, size-1, MSG_BS_REPLY, world_comm, &base_receive);
                            message_metrics[MSG_BS_REPLY].receive_call_time += MPI_Wtime() - base_start;
                            report_start = MPI_Wtime();
                            MPI_Isend(&report, 1, report_type, size-1, MSG_REPORT, world_comm, &report_send);
                            message_metrics[MSG_REPORT].send_call_time += MPI_Wtime() - report_start;
                            reply_stage = 1;
                            awaiting_base = true;
                        } else {
                            LocalReferral(rank, coord, avail_neigh, &comp_time);
                        }
                    }
                }

                /**
                 * ### Main Thread: Base Station Recommendation Handling
                 * A multi-stage asynchronous receiver that securely processes the recommended node count and 
                 * subsequent coordinates from the Base Station.
                 * It releases the node from its reporting wait state and prints the recommended geographical 
                 * coordinates to reroute incoming EVs without blocking execution.
                 */
                if (awaiting_base) {
                    if (Complete(&report_send))
                        TrackSendMetric(MSG_REPORT, 1, report_bytes, 0, MPI_Wtime() - report_start);
                    if (reply_stage == 1 && Complete(&base_receive)) {
                        TrackReceiveMetric(MSG_BS_REPLY, 1, sizeof(int), 0, MPI_Wtime() - base_start);
                        if (count < 0 || count > size-1) MPI_Abort(world_comm, EXIT_FAILURE);
                        if (count) {
                            base_start = MPI_Wtime();
                            MPI_Irecv(coords_reply, count*2, MPI_INT, size-1, MSG_BS_REPLY, world_comm, &base_receive);
                            message_metrics[MSG_BS_REPLY].receive_call_time += MPI_Wtime() - base_start;
                            reply_stage = 2;
                        } else reply_stage = 3;
                    }
                    if (reply_stage == 2 && Complete(&base_receive)) {
                        TrackReceiveMetric(MSG_BS_REPLY, 1, count*2*sizeof(int), 0, MPI_Wtime() - base_start);
                        reply_stage = 3;
                    }

                    // Do not reuse report/reply buffers until both C3 and the full C4 have finished.
                    if (reply_stage == 3 && report_send == MPI_REQUEST_NULL) {
                        ShowReferral(rank, count, coords_reply);
                        awaiting_base = false;
                    }
                }

                if (!disabled && !querying && !awaiting_base && MPI_Wtime() - last_check >= CHECK_PERIOD) {
                    available = AvailablePorts(ports, &comp_time);
                    if (available <= THRESHOLD) {
                        degree = 0;
                        for (int i = 0; i < 4; i++) {
                            avail_neigh[i] = NO_NEIGHBOR;
                            if (neighbors[i] == MPI_PROC_NULL) continue;
                            degree++;
                            receiving_start[i] = MPI_Wtime();
                            MPI_Irecv(&avail_neigh[i], 1, MPI_INT, neighbors[i], MSG_RES_NEIGHBOR, comm2D, &receiving[i]);
                            message_metrics[MSG_RES_NEIGHBOR].receive_call_time += MPI_Wtime() - receiving_start[i];
                            sending_start[i] = MPI_Wtime();
                            MPI_Isend(&signal, 1, MPI_INT, neighbors[i], MSG_REQ_NEIGHBOR, comm2D, &sending[i]);
                            message_metrics[MSG_REQ_NEIGHBOR].send_call_time += MPI_Wtime() - sending_start[i];
                        }
                        querying = true;
                    }
                    last_check = MPI_Wtime();
                }

                /**
                 * ### Main Thread: Graceful Shutdown Handshake
                 * A synchronized termination sequence utilizing non-blocking sends for MSG_READY and MSG_TERMINATED signals.
                 * It ensures a safe global exit by verifying the Base Station's acknowledgment before breaking the event loop 
                 * and tearing down the MPI environment.
                 */
                if (disabled && !querying && !awaiting_base && !ready_sent) {
                    ready_start = MPI_Wtime();
                    MPI_Isend(&signal, 1, MPI_INT, size-1, MSG_READY, world_comm, &ready);
                    message_metrics[MSG_READY].send_call_time += MPI_Wtime() - ready_start;
                    ready_sent = true;
                }
                if (Complete(&ready))
                    TrackSendMetric(MSG_READY, 1, sizeof(int), 0, MPI_Wtime() - ready_start);
                if (ready_sent && !terminated && terminate == MPI_REQUEST_NULL) {
                    MPI_Iprobe(size-1, MSG_TERMINATE, world_comm, &flag, MPI_STATUS_IGNORE);
                    if (flag) {
                        terminate_start = MPI_Wtime();
                        MPI_Irecv(&terminate_value, 1, MPI_INT, size-1, MSG_TERMINATE, world_comm, &terminate);
                        message_metrics[MSG_TERMINATE].receive_call_time += MPI_Wtime() - terminate_start;
                    }
                }
                if (Complete(&terminate)) {
                    TrackReceiveMetric(MSG_TERMINATE, 1, sizeof(int), 0, MPI_Wtime() - terminate_start);
                    terminated = true;
                }

                // Finish active neighbour responses and READY before leaving the loop.
                bool responses_done = true;
                for (int i = 0; i < 4; i++)
                    if (incoming[i] != MPI_REQUEST_NULL || answering[i] != MPI_REQUEST_NULL) responses_done = false;
                if (terminated && ready == MPI_REQUEST_NULL && responses_done) break;
                usleep(1000);
            }
        }
        #pragma omp atomic
        node_comp_time += comp_time;
    }

    // Workers have joined and outstanding enquiries are complete before the cleanup ACK.
    MPI_Comm_free(&comm2D);
    int signal = 1;
    MPI_Request ack;
    double start = MPI_Wtime();
    MPI_Isend(&signal, 1, MPI_INT, size-1, MSG_TERMINATED, world_comm, &ack);
    double called = MPI_Wtime() - start;
    MPI_Wait(&ack, MPI_STATUS_IGNORE);
    TrackSendMetric(MSG_TERMINATED, 1, sizeof(int), called, MPI_Wtime() - start);
    PrintSlaveMetrics(comm, start_time, node_comp_time);
}

//===========================================
// Base Station
//===========================================
void MasterIO(MPI_Comm world_comm, int nrows, int ncols, MPI_Datatype report_type) {
    /**
     * ### Variable Initialization and Memory Allocation
     * Initializes the global Base Station variables and allocates a dedicated 
     * array of persistent Peer structures for each charging node. 
     * It constructs the foundation of the event-driven architecture, enabling the 
     * Base Station to track and manage concurrent asynchronous states for the entire grid.
     */
    int size, report_bytes;
    MPI_Comm_size(world_comm, &size);
    MPI_Type_size(report_type, &report_bytes);
    int stations = size-1, signal = 1, ready_count = 0, ack_count = 0;
    bool disabling = false, terminating = false;
    Peer *peers = calloc((size_t)stations, sizeof(Peer));
    if (!peers) MPI_Abort(world_comm, EXIT_FAILURE);
    for (int i = 0; i < stations; i++) {
        Peer *p = &peers[i];
        p->coords = malloc((size_t)stations * 2 * sizeof(int));
        if (!p->coords) MPI_Abort(world_comm, EXIT_FAILURE);

        // Initialize MPI handles explicitly; calloc zero is not a portable null request.
        p->incoming = p->count_send = p->coords_send = MPI_REQUEST_NULL;
        p->disable_send = p->terminate_send = MPI_REQUEST_NULL;
    }
    BaseState base;

    /**
     * ### Initialization of Global State Matrix and Log Files
     * This section initializes the global 2D matrix representing port availability and prepares the main simulation log file. 
     * It acts as the central state registry to track recent congestion events and records the initial configuration parameters before 
     * the main loop begins.
     */
    BaseOpen(&base, nrows, ncols, 3);
    double comp_time = 0.0;

    /**
     * ### Simulated Timeout Detection and Shutdown Signaling Broadcast
     * Monitors the elapsed simulation time and dispatches non-blocking MSG_DISABLE and MSG_TERMINATE commands to all nodes.
     * It strictly enforces the simulation's fixed duration and orchestrates a secure, non-blocking two-phase graceful 
     * shutdown across the distributed cluster.
     */
    for (;;) {
        if (!disabling && MPI_Wtime() - base.start_time >= SIMULATION_SEC) {
            disabling = true;
            for (int i = 0; i < stations; i++) {
                Peer *p = &peers[i];
                p->disable_start = MPI_Wtime();
                MPI_Isend(&signal, 1, MPI_INT, i, MSG_DISABLE, world_comm, &p->disable_send);
                message_metrics[MSG_DISABLE].send_call_time += MPI_Wtime() - p->disable_start;
            }
        }

        /**
         * ### Asynchronous Message Processing
         * The core event loop that continually evaluates the completion status 
         * of non-blocking sends and receives for every registered peer.
         * It independently processes incoming congestion reports and shutdown 
         * confirmations in a fully non-blocking manner, eliminating system deadlocks.
         */
        for (int i = 0; i < stations; i++) {
            Peer *p = &peers[i];
            if (Complete(&p->count_send))
                TrackSendMetric(MSG_BS_REPLY, 1, sizeof(int), 0, MPI_Wtime() - p->count_start);
            if (Complete(&p->coords_send))
                TrackSendMetric(MSG_BS_REPLY, 1, p->count*2*sizeof(int), 0, MPI_Wtime() - p->coords_start);
            if (Complete(&p->disable_send))
                TrackSendMetric(MSG_DISABLE, 1, sizeof(int), 0, MPI_Wtime() - p->disable_start);
            if (Complete(&p->terminate_send))
                TrackSendMetric(MSG_TERMINATE, 1, sizeof(int), 0, MPI_Wtime() - p->terminate_start);

            // Preserve count/coordinate buffers until the previous referral sends complete.
            if (p->incoming == MPI_REQUEST_NULL && p->count_send == MPI_REQUEST_NULL &&
                p->coords_send == MPI_REQUEST_NULL && !p->ack_seen) {
                int flag;
                MPI_Status status;
                MPI_Iprobe(i, MPI_ANY_TAG, world_comm, &flag, &status);
                if (flag) {
                    p->incoming_tag = status.MPI_TAG;
                    p->incoming_start = MPI_Wtime();
                    if (status.MPI_TAG == MSG_REPORT) {
                        MPI_Irecv(&p->report, 1, report_type, i, MSG_REPORT, world_comm, &p->incoming);
                    } else if (status.MPI_TAG == MSG_READY || status.MPI_TAG == MSG_TERMINATED) {
                        MPI_Irecv(&p->value, 1, MPI_INT, i, status.MPI_TAG, world_comm, &p->incoming);
                    } else MPI_Abort(world_comm, EXIT_FAILURE);
                    message_metrics[p->incoming_tag].receive_call_time += MPI_Wtime() - p->incoming_start;
                }
            }

            // Process the completed payload now; the request is already NULL.
            if (Complete(&p->incoming)) {
                int tag = p->incoming_tag;
                TrackReceiveMetric(tag, 1, tag == MSG_REPORT ? report_bytes : (int)sizeof(int),
                                   0, MPI_Wtime() - p->incoming_start);
                if (tag == MSG_REPORT && !p->ready_seen) {
                    p->count = ProcessReport(&base, &p->report, p->coords, &comp_time);
                    p->count_start = MPI_Wtime();

                    /**
                     * ### Sending Dispatch Responses and Grid Status Records
                     * Triggers non-blocking MSG_BS_REPLY sends containing the optimal alternative 
                     * coordinates and commits the complete recommendation list to the log.
                     * It promptly dispatches routing instructions back to the congested node while 
                     * permanently logging the incident's spatial context without halting the event loop.
                     */
                    MPI_Isend(&p->count, 1, MPI_INT, i, MSG_BS_REPLY, world_comm, &p->count_send);
                    message_metrics[MSG_BS_REPLY].send_call_time += MPI_Wtime() - p->count_start;
                    if (p->count) {
                        p->coords_start = MPI_Wtime();
                        MPI_Isend(p->coords, p->count*2, MPI_INT, i, MSG_BS_REPLY, world_comm, &p->coords_send);
                        message_metrics[MSG_BS_REPLY].send_call_time += MPI_Wtime() - p->coords_start;
                    }
                } else if (tag == MSG_READY && !p->ready_seen) {
                    p->ready_seen = true;
                    ready_count++;
                } else if (tag == MSG_TERMINATED && !p->ack_seen && terminating) {
                    p->ack_seen = true;
                    ack_count++;
                } else MPI_Abort(world_comm, EXIT_FAILURE);
            }
        }

        if (disabling && ready_count == stations && !terminating) {
            terminating = true;
            for (int i = 0; i < stations; i++) {
                Peer *p = &peers[i];
                p->terminate_start = MPI_Wtime();
                MPI_Isend(&signal, 1, MPI_INT, i, MSG_TERMINATE, world_comm, &p->terminate_send);
                message_metrics[MSG_TERMINATE].send_call_time += MPI_Wtime() - p->terminate_start;
            }
        }

        // All station ACKs and all outstanding base sends must finish before cleanup.
        bool sends_done = true;
        for (int i = 0; i < stations; i++) {
            Peer *p = &peers[i];
            if (p->disable_send != MPI_REQUEST_NULL || p->terminate_send != MPI_REQUEST_NULL ||
                p->count_send != MPI_REQUEST_NULL || p->coords_send != MPI_REQUEST_NULL) sends_done = false;
        }
        if (ack_count == stations && sends_done) break;
        usleep(1000);
    }
    /**
     * ### Final Cleanup and Performance Summary Report
     * Waits for all station acknowledgements and all outgoing 
     * asynchronous termination commands to complete before releasing peer memory.
     * It securely finalizes the event-driven state machine, ensuring no orphaned 
     * operations remain before exporting the simulation's performance summary.
     */
    for (int i = 0; i < stations; i++) free(peers[i].coords);
    free(peers);
    
    double start_time_copy = base.start_time;
    BaseClose(&base);
    PrintMasterMetrics(start_time_copy, comp_time);
}

int main(int argc, char **argv) {
    // Initializes the MPI environment with multi-threading support to ensure safe, 
    // concurrent execution of MPI calls within OpenMP threads.
    int rank, size, nrows = 3, ncols = 3, permission;

    MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &permission);
    if (permission < MPI_THREAD_MULTIPLE) MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);

    // Retrieves the process count and strictly validates the command-line arguments to 
    // ensure the specified 2D grid dimensions exactly match the number of available charging nodes.
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);
    ReadArguments(argc, argv, size, &nrows, &ncols);

    // Registers a custom MPI datatype for congestion reports and splits the global communicator 
    // to logically isolate the Base Station from the interconnected charging nodes.
    MPI_Datatype report_type;
    CreateDataType(&report_type);
    MPI_Comm new_comm;

    MPI_Comm_split(MPI_COMM_WORLD, rank == size-1, 0, &new_comm);

    // This conditional logic assigns the highest MPI rank to act as the centralized Base Station (MasterIO) 
    // and directs all other ranks to operate as EV charging nodes (SlaveIO) utilizing the fully asynchronous logic.
    if (rank == size-1) MasterIO(MPI_COMM_WORLD, nrows, ncols, report_type);
    else SlaveIO(MPI_COMM_WORLD, new_comm, nrows, ncols, report_type);

    // This finalization step collects performance metrics from all processes, 
    // frees allocated MPI resources, and safely shuts down the MPI environment.
    WriteMetrics(nrows, ncols, 3);

    MPI_Type_free(&report_type);
    MPI_Comm_free(&new_comm);
    MPI_Finalize();
    return 0;
}