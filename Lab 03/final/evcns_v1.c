/**
 * @file      evcns_v1.c
 * @brief     Electric Vehicle Charging Navigation System (EVCNS) Version 1
 *
 * @details
 *
 * @author    Shee Seng Cheng (34612467) - sshe0113@student.monash.edu
 * @author    Tay Chee Hsian (34612513) - ctay0040@student.monash.edu
 * @note      Coding Implementation for EVCNS (Scheduled Blocking Architecture)
 */
#include "utils.h"

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

    #pragma omp parallel num_threads(NUM_PORTS + 1) shared(ports, running_flag)
    {
        int thread_id = omp_get_thread_num();
        // One thread checks the team size; the same value applies to every thread.
        if (thread_id == 0 && omp_get_num_threads() != NUM_PORTS + 1) MPI_Abort(world_comm, EXIT_FAILURE);
        /**
         * ### Port State Simulation (Worker Threads)
         * These worker threads asynchronously simulate real-world electric vehicle behavior by randomly 
         * toggling the availability of individual charging ports based on a predefined probability.
         * This mechanism is used to generate dynamic, independent load fluctuations across the charging 
         * network without blocking the node's main communication thread.
         */
        if (thread_id < NUM_PORTS) {
            SimulatePort(thread_id, ports, &running_flag);
        } else {
            /**
             * ### Main Thread: Centralized Scheduling Loop Setup
             * Initializes the control signal and flags for the blocking architecture.
             * It prepares the main thread to constantly poll for termination commands or 
             * incoming inquiries while awaiting its scheduled turn from the Base Station.
             */
            int signal = 1;
            bool ready_sent = false;
            for (;;) {

                /**
                 * ### Main Thread: Neighbor Request Processing
                 * Listens for and intercepts incoming MSG_REQ_NEIGHBOR inquiries 
                 * from adjacent charging nodes actively looking for free slots. 
                 * It safely calculates the locally available ports using an OpenMP critical section and 
                 * immediately replies, assisting the neighbor's decentralized load-balancing efforts.
                 */
                AnswerNeighbours(comm2D, ports);
                int flag;
                MPI_Status status;
                MPI_Iprobe(size-1, MPI_ANY_TAG, world_comm, &flag, &status);
                if (!flag) { usleep(1000); continue; }
                double start = MPI_Wtime();
                MPI_Recv(&signal, 1, MPI_INT, size-1, status.MPI_TAG, world_comm, MPI_STATUS_IGNORE);
                double elapsed = MPI_Wtime() - start;
                TrackReceiveMetric(status.MPI_TAG, 1, sizeof(int), elapsed, elapsed);
                if (status.MPI_TAG == MSG_TERMINATE) break;

                // DISABLE stops new work and port updates; READY confirms own
                // enquiry completion. Continue answering neighbours until TERMINATE.
                if (status.MPI_TAG == MSG_DISABLE) {
                    #pragma omp atomic write
                    running_flag = false;
                    start = MPI_Wtime();
                    MPI_Send(&signal, 1, MPI_INT, size-1, MSG_READY, world_comm);
                    elapsed = MPI_Wtime() - start;
                    TrackSendMetric(MSG_READY, 1, sizeof(int), elapsed, elapsed);
                    ready_sent = true;
                } else if (status.MPI_TAG == MSG_QUERY_GRANT && !ready_sent) {

                    /**
                     * ### Main Thread: Scheduled Congestion Checking & Neighbor Polling
                     * A token-based execution block triggered only when receiving a MSG_QUERY_GRANT from the Base Station. 
                     * It ensures strictly serialized local congestion checking, forcing the node to execute blocking MPI_Send 
                     * and MPI_Recv sequentially to all neighbors, replacing parallel probing.
                     */
                    int available = AvailablePorts(ports);
                    if (available <= THRESHOLD) {
                        int avail_neigh[4] = {NO_NEIGHBOR, NO_NEIGHBOR, NO_NEIGHBOR, NO_NEIGHBOR};
                        int degree = 0;
                        bool all_full = true;

                        for (int i = 0; i < 4; i++) {
                            if (neighbors[i] == MPI_PROC_NULL) continue;
                            start = MPI_Wtime();
                            MPI_Send(&signal, 1, MPI_INT, neighbors[i], MSG_REQ_NEIGHBOR, comm2D);
                            elapsed = MPI_Wtime() - start;
                            TrackSendMetric(MSG_REQ_NEIGHBOR, 1, sizeof(int), elapsed, elapsed);
                            start = MPI_Wtime();
                            MPI_Recv(&avail_neigh[i], 1, MPI_INT, neighbors[i], MSG_RES_NEIGHBOR, comm2D, MPI_STATUS_IGNORE);
                            elapsed = MPI_Wtime() - start;
                            TrackReceiveMetric(MSG_RES_NEIGHBOR, 1, sizeof(int), elapsed, elapsed);
                            degree++;
                            if (avail_neigh[i] > THRESHOLD) all_full = false;
                        }

                        /**
                         * ### Main Thread: Congestion Reporting & Local Referral
                         * If both the current node and its surrounding neighbors are found to be severely congested, this block 
                         * packages the contextual data into a MsgData struct and dispatches a critical MSG_REPORT to the Base Station. 
                         * Conversely, if any neighbor is found to have available space, it bypasses the Base Station entirely and 
                         * directly invokes a local referral to redirect the EV traffic.
                         */
                        if (all_full) {
                            MsgData report;
                            BuildReport(&report, rank, coord, available, avail_neigh, degree, start_time);
                            start = MPI_Wtime();
                            MPI_Send(&report, 1, report_type, size-1, MSG_REPORT, world_comm);
                            elapsed = MPI_Wtime() - start;
                            TrackSendMetric(MSG_REPORT, 1, report_bytes, elapsed, elapsed);

                            /**
                             * ### Main Thread: Base Station Recommendation Handling
                             * Directly executes a blocking MPI_Recv to fetch the Base Station's 
                             * fallback routing list immediately after sending the report.
                             * It guarantees the entire congestion evaluation pipeline is completely 
                             * resolved before releasing the node's scheduled execution turn.
                             */
                            int count, coords[2*(size-1)];
                            start = MPI_Wtime();
                            MPI_Recv(&count, 1, MPI_INT, size-1, MSG_BS_REPLY, world_comm, MPI_STATUS_IGNORE);
                            elapsed = MPI_Wtime() - start;
                            TrackReceiveMetric(MSG_BS_REPLY, 1, sizeof(int), elapsed, elapsed);
                            if (count < 0 || count > size-1) MPI_Abort(world_comm, EXIT_FAILURE);
                            if (count) {
                                start = MPI_Wtime();
                                MPI_Recv(coords, count*2, MPI_INT, size-1, MSG_BS_REPLY, world_comm, MPI_STATUS_IGNORE);
                                elapsed = MPI_Wtime() - start;
                                TrackReceiveMetric(MSG_BS_REPLY, 1, count*2*sizeof(int), elapsed, elapsed);
                            }
                            ShowReferral(rank, count, coords);
                        } else {
                            LocalReferral(rank, coord, avail_neigh);
                        }
                    }
                    start = MPI_Wtime();

                    /**
                     * ### Main Thread: Completion Handshake
                     * A synchronous notification back to the Base Station signaling the end of the node's granted window.
                     * It releases the global lock, instructing the Base Station to assign the MSG_QUERY_GRANT token to 
                     * the next node in the round-robin sequence.
                     */
                    MPI_Send(&signal, 1, MPI_INT, size-1, MSG_QUERY_DONE, world_comm);
                    elapsed = MPI_Wtime() - start;
                    TrackSendMetric(MSG_QUERY_DONE, 1, sizeof(int), elapsed, elapsed);
                } else {
                    MPI_Abort(world_comm, EXIT_FAILURE);
                }
            }
        }
    }
    MPI_Comm_free(&comm2D); 
    int signal = 1;
    double start = MPI_Wtime();
    MPI_Send(&signal, 1, MPI_INT, size-1, MSG_TERMINATED, world_comm);
    double elapsed = MPI_Wtime() - start;
    TrackSendMetric(MSG_TERMINATED, 1, sizeof(int), elapsed, elapsed);
    PrintSlaveMetrics(comm, start_time);
}

//===========================================
// Base Station
//===========================================
void MasterIO(MPI_Comm world_comm, int nrows, int ncols, MPI_Datatype report_type) {
    /**
     * ### Variable Initialization and Memory Allocation
     * Initializes the Base Station's core variables and allocates memory for tracking the shutdown states of all worker nodes. 
     * It is used to set up the global tracking arrays necessary for executing a synchronized 
     * simulation shutdown.
     */
    int size, report_bytes;
    MPI_Comm_size(world_comm, &size);
    MPI_Type_size(report_type, &report_bytes);
    int stations = size-1, signal = 1, next = 0, active = -1, ready_count = 0, ack_count = 0;
    bool disabling = false, terminating = false, ready_seen[stations], ack_seen[stations];
    for (int i = 0; i < stations; i++) ready_seen[i] = ack_seen[i] = false;
    BaseState base;
    /**
     * ### Initialization of Global State Matrix and Log Files
     * This section initializes the global 2D matrix representing port availability and prepares the main simulation log file. 
     * It acts as the central state registry to track recent congestion events and records the initial configuration parameters 
     * before the main loop begins.
     */
    BaseOpen(&base, nrows, ncols, 1);

    /**
     * ### Base Station: Token Passing Setup
     * New.
     * ### What it is:
     * Declares variables pointing to the currently active charging node and enforces a three-second interval between global check loops.
     * ### What it is used for:
     * It establishes the centralized scheduling policy, guaranteeing that network-wide congestion queries are executed sequentially.
     */
    double next_round = base.start_time + CHECK_PERIOD;
    while (ack_count < stations) {
        /**
         * ### Message Listening and Termination Signal Handshake
         * This block actively probes the network for incoming messages and 
         * processes the graceful shutdown handshake signals from slave nodes. 
         * It is used to securely tally which charging nodes have ceased operations 
         * and acknowledged the termination protocol to prevent system deadlocks.
         */
        int flag;
        MPI_Status status;
        MPI_Iprobe(MPI_ANY_SOURCE, MPI_ANY_TAG, world_comm, &flag, &status);
        if (flag) {
            double start = MPI_Wtime();
            /**
             * ### Congestion Report Receipt and Status Updates
             * Intercepts congestion alerts, updates the central grid state, and validates 
             * if the reporting node's immediate neighbors are also recently congested. 
             * It serves as the primary ingestion point for network alerts, utilizing the 
             * Base Station's absolute clock to eliminate distributed time drift errors.
             */
            if (status.MPI_TAG == MSG_REPORT) {
                MsgData report;
                MPI_Recv(&report, 1, report_type, status.MPI_SOURCE, MSG_REPORT, world_comm, MPI_STATUS_IGNORE);
                double elapsed = MPI_Wtime() - start;
                TrackReceiveMetric(MSG_REPORT, 1, report_bytes, elapsed, elapsed);

                int coords[2*stations];
                int count = ProcessReport(&base, &report, coords);
                start = MPI_Wtime();
                /**
                 * ### Sending Dispatch Responses and Grid Status Records
                 * Transmits the discovered alternative coordinates back to the reporting node 
                 * and records the complete recommendation list in the log. 
                 * It ensures the congested node receives immediate routing instructions while permanently 
                 * logging the incident's spatial context for analytical review.
                 */
                MPI_Send(&count, 1, MPI_INT, status.MPI_SOURCE, MSG_BS_REPLY, world_comm);
                elapsed = MPI_Wtime() - start;
                TrackSendMetric(MSG_BS_REPLY, 1, sizeof(int), elapsed, elapsed);
                if (count) {
                    start = MPI_Wtime();
                    MPI_Send(coords, count*2, MPI_INT, status.MPI_SOURCE, MSG_BS_REPLY, world_comm);
                    elapsed = MPI_Wtime() - start;
                    TrackSendMetric(MSG_BS_REPLY, 1, count*2*sizeof(int), elapsed, elapsed);
                }
            } else {
                MPI_Recv(&signal, 1, MPI_INT, status.MPI_SOURCE, status.MPI_TAG, world_comm, MPI_STATUS_IGNORE);
                double elapsed = MPI_Wtime() - start;
                TrackReceiveMetric(status.MPI_TAG, 1, sizeof(int), elapsed, elapsed);
                int peer = status.MPI_SOURCE;
                /**
                 * ### Base Station: Query Completion Verification
                 * Retrieves the MSG_QUERY_DONE validation flag and systematically 
                 * advances the authorization pointer to the next charging node.
                 * It securely revokes the current lock and cascades the polling operation 
                 * through the grid topology until every node has been checked.
                 */
                if (status.MPI_TAG == MSG_QUERY_DONE && peer == active) active = -1;
                else if (status.MPI_TAG == MSG_READY && !ready_seen[peer]) {
                    ready_seen[peer] = true;
                    ready_count++;
                } else if (status.MPI_TAG == MSG_TERMINATED && !ack_seen[peer]) {
                    ack_seen[peer] = true;
                    ack_count++;
                } else MPI_Abort(world_comm, EXIT_FAILURE);
            }
        }
        double now = MPI_Wtime();

        // Finish the granted enquiry before DISABLE. Wait for every READY,
        // send TERMINATE, then wait for every TERMINATED instead of a timed drain.
        if (!disabling && active < 0 && now - base.start_time >= SIMULATION_SEC) {
            disabling = true;
            for (int i = 0; i < stations; i++) {
                double start = MPI_Wtime();
                MPI_Send(&signal, 1, MPI_INT, i, MSG_DISABLE, world_comm);
                double elapsed = MPI_Wtime() - start;
                TrackSendMetric(MSG_DISABLE, 1, sizeof(int), elapsed, elapsed);
            }
        }

        /**
         * ### Base Station: Token Dispenser
         * Evaluates timing constraints to emit the MSG_QUERY_GRANT token to the next node in line.
         * It drives the centralized operation loop, granting individual polling permissions strictly 
         * after completing the minimum delay bounds.
         */
        if (!disabling && active < 0 && (next != 0 || now >= next_round)) {
            if (next == 0) next_round = now + CHECK_PERIOD;
            active = next;
            next = (next+1) % stations;
            double start = MPI_Wtime();
            MPI_Send(&signal, 1, MPI_INT, active, MSG_QUERY_GRANT, world_comm);
            double elapsed = MPI_Wtime() - start;
            TrackSendMetric(MSG_QUERY_GRANT, 1, sizeof(int), elapsed, elapsed);
        }

        if (disabling && ready_count == stations && !terminating) {
            terminating = true;
            for (int i = 0; i < stations; i++) {
                double start = MPI_Wtime();
                MPI_Send(&signal, 1, MPI_INT, i, MSG_TERMINATE, world_comm);
                double elapsed = MPI_Wtime() - start;
                TrackSendMetric(MSG_TERMINATE, 1, sizeof(int), elapsed, elapsed);
            }
        }
        usleep(1000);
    }
    /**
     * ### Final Cleanup and Performance Summary Report
     * Waits for all station cleanup acknowledgements, 
     * frees allocated memory, and calculates aggregate performance metrics. 
     * It is used to safely clean up the MPI environment and append a comprehensive summary 
     * report regarding simulation overhead and event frequencies to the log file.
     */
    double start_time_copy = base.start_time;
    BaseClose(&base);
    PrintMasterMetrics(start_time_copy);
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
    // and directs all other ranks to operate as EV charging nodes (SlaveIO).
    if (rank == size-1) MasterIO(MPI_COMM_WORLD, nrows, ncols, report_type);
    else SlaveIO(MPI_COMM_WORLD, new_comm, nrows, ncols, report_type);

    // This finalization step collects performance metrics from all processes, 
    // frees allocated MPI resources, and safely shuts down the MPI environment.
    WriteMetrics(nrows, ncols, 1);

    MPI_Type_free(&report_type);
    MPI_Comm_free(&new_comm);
    MPI_Finalize();
    return 0;
}