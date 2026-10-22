/**
 * @file      evcns_v2.c
 * @brief     Electric Vehicle Charging Navigation System (EVCNS) Version 2
 *
 * @details   Implements an optimized Communication Overlap Architecture. 
 *            Eliminates centralized token passing by allowing concurrent, non-blocking neighbor 
 *            polling (MPI_Isend/Irecv). Nodes can process incoming peer requests while waiting 
 *            for their own queries to resolve, effectively hiding network latency.
 *
 * @author    Shee Seng Cheng (34612467) - sshe0113@student.monash.edu
 * @author    Tay Chee Hsian (34612513) - ctay0040@student.monash.edu
 * @note      Coding Implementation for EVCNS
 */
#include "utils.h"

//===========================================
// EV Charging Node
//===========================================
void SlaveIO(MPI_Comm world_comm, MPI_Comm comm, int nrows, int ncols, MPI_Datatype report_type) {
    /**
     * ### MPI Setup and Topology Creation
     * Initializes the MPI environment variables and constructs a 2D Cartesian grid topology for the charging nodes. 
     * It is used to establish the spatial relationships between nodes, identifying the immediate top, bottom, left, and right 
     * neighbors required for localized communication.
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

            bool disabled = false, ready_sent = false, awaiting_base = false;
            double last_check = MPI_Wtime();
            int signal = 1;
            for (;;) {

                /**
                 * ### Main Thread: Neighbor Request Processing
                 * Listens for and intercepts incoming MSG_REQ_NEIGHBOR inquiries 
                 * from adjacent charging nodes actively looking for free slots. 
                 * It safely calculates the locally available ports using an OpenMP critical section and 
                 * immediately replies, assisting the neighbor's decentralized load-balancing efforts.
                 */
                AnswerNeighbours(comm2D, ports, &comp_time);
                int flag;
                MPI_Status status;
                MPI_Iprobe(size-1, MPI_ANY_TAG, world_comm, &flag, &status);
                if (flag) {

                    /**
                     * ### Main Thread: Base Station Recommendation Handling
                     * Receives MSG_BS_REPLY network messages containing a computed list 
                     * of alternative charging nodes identified by the Base Station. 
                     * It is used to release the node from its reporting wait state and prints the 
                     * recommended geographical coordinates to reroute incoming electric vehicles.
                     */
                    if (status.MPI_TAG == MSG_BS_REPLY && awaiting_base) {
                        int count, coords[2*(size-1)];
                        double start = MPI_Wtime();
                        MPI_Recv(&count, 1, MPI_INT, size-1, MSG_BS_REPLY, world_comm, MPI_STATUS_IGNORE);
                        double elapsed = MPI_Wtime() - start;
                        TrackReceiveMetric(MSG_BS_REPLY, 1, sizeof(int), elapsed, elapsed);
                        if (count < 0 || count > size-1) MPI_Abort(world_comm, EXIT_FAILURE);
                        if (count) {
                            start = MPI_Wtime();
                            MPI_Recv(coords, count*2, MPI_INT, size-1, MSG_BS_REPLY, world_comm, MPI_STATUS_IGNORE);
                            elapsed = MPI_Wtime() - start;
                            TrackReceiveMetric(MSG_BS_REPLY, 1, count*2*sizeof(int), elapsed, elapsed);
                        }
                        ShowReferral(rank, count, coords);
                        awaiting_base = false;

                    /**
                     * ### Main Thread: Disable Signal Handling
                     * Continuously polls for a globally broadcasted MSG_DISABLE signal originating from the Base Station. 
                     * It is used to gracefully halt the generation of new internal port states and block new outgoing queries once the 
                     * fixed simulation duration concludes.
                     */
                    } else if (status.MPI_TAG == MSG_DISABLE || status.MPI_TAG == MSG_TERMINATE) {
                        double start = MPI_Wtime();
                        MPI_Recv(&signal, 1, MPI_INT, size-1, status.MPI_TAG, world_comm, MPI_STATUS_IGNORE);
                        double elapsed = MPI_Wtime() - start;
                        TrackReceiveMetric(status.MPI_TAG, 1, sizeof(int), elapsed, elapsed);
                        if (status.MPI_TAG == MSG_TERMINATE) break;
                        disabled = true;
                        #pragma omp atomic write
                        running_flag = false;
                    } else MPI_Abort(world_comm, EXIT_FAILURE);
                }

                /**
                 * ### Main Thread: Congestion Checking & Neighbor Polling
                 * Periodically checks if the local node has reached its port congestion threshold, at which point 
                 * it utilizes non-blocking MPI_Isend and MPI_Irecv to concurrently poll all physical neighbors. 
                 * It functions as the first tier of decentralized load balancing, attempting to discover nearby 
                 * charging capacity while ensuring it can still interleave incoming requests while waiting for responses.
                 */
                if (!disabled && !awaiting_base && MPI_Wtime() - last_check >= CHECK_PERIOD) {
                    int available = AvailablePorts(ports, &comp_time);
                    if (available <= THRESHOLD) {
                        int avail_neigh[4] = {NO_NEIGHBOR, NO_NEIGHBOR, NO_NEIGHBOR, NO_NEIGHBOR};
                        MPI_Request sends[4], receives[4];
                        double send_started[4], receive_started[4];

                        // Only valid neighbours occupy the request arrays; degree is the wait count.
                        int degree = 0;
                        for (int i = 0; i < 4; i++) {
                            if (neighbors[i] == MPI_PROC_NULL) continue;
                            send_started[degree] = MPI_Wtime();
                            MPI_Isend(&signal, 1, MPI_INT, neighbors[i], MSG_REQ_NEIGHBOR, comm2D, &sends[degree]);
                            message_metrics[MSG_REQ_NEIGHBOR].send_call_time += MPI_Wtime() - send_started[degree];
                            receive_started[degree] = MPI_Wtime();
                            MPI_Irecv(&avail_neigh[i], 1, MPI_INT, neighbors[i], MSG_RES_NEIGHBOR, comm2D, &receives[degree]);
                            message_metrics[MSG_RES_NEIGHBOR].receive_call_time += MPI_Wtime() - receive_started[degree];
                            degree++;
                        }

                        if (degree) {
                            int done = 0;
                            while (!done) {
                                MPI_Testall(degree, receives, &done, MPI_STATUSES_IGNORE);

                                // Respond while waiting: another node may be querying us.
                                if (!done) { AnswerNeighbours(comm2D, ports, &comp_time); usleep(1000); }
                            }
                            double completed = MPI_Wtime();
                            for (int i = 0; i < degree; i++)
                                TrackReceiveMetric(MSG_RES_NEIGHBOR, 1, sizeof(int), 0, completed - receive_started[i]);

                            // The request buffer must remain unchanged until these sends complete.
                            MPI_Waitall(degree, sends, MPI_STATUSES_IGNORE);
                            completed = MPI_Wtime();
                            for (int i = 0; i < degree; i++)
                                TrackSendMetric(MSG_REQ_NEIGHBOR, 1, sizeof(int), 0, completed - send_started[i]);
                        }
                        double comp_s = ComputationTime();
                        bool all_full = true;
                        for (int i = 0; i < 4; i++) if (avail_neigh[i] > THRESHOLD) all_full = false;
                        comp_time += ComputationTime() - comp_s;

                        /**
                         * ### Main Thread: Congestion Reporting & Local Referral
                         * If both the current node and its surrounding neighbors are found to be severely congested, this block 
                         * packages the contextual data into a MsgData struct and dispatches a critical MSG_REPORT to the Base Station. 
                         * Conversely, if any neighbor is found to have available space, it bypasses the Base Station entirely and 
                         * directly invokes a local referral to redirect the EV traffic.
                         */
                        if (all_full) {
                            MsgData report;
                            BuildReport(&report, rank, coord, available, avail_neigh, degree, start_time, &comp_time);
                            
                            double start = MPI_Wtime();
                            MPI_Send(&report, 1, report_type, size-1, MSG_REPORT, world_comm);
                            double elapsed = MPI_Wtime() - start;
                            TrackSendMetric(MSG_REPORT, 1, report_bytes, elapsed, elapsed);
                            awaiting_base = true;
                        } else {
                            LocalReferral(rank, coord, avail_neigh, &comp_time);
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
                if (disabled && !awaiting_base && !ready_sent) {
                    double start = MPI_Wtime();
                    MPI_Send(&signal, 1, MPI_INT, size-1, MSG_READY, world_comm);
                    double elapsed = MPI_Wtime() - start;
                    TrackSendMetric(MSG_READY, 1, sizeof(int), elapsed, elapsed);
                    ready_sent = true;
                }
                usleep(1000);
            }
        }
        #pragma omp atomic
        node_comp_time += comp_time;
    }

    // Workers have joined and outstanding enquiries are complete before the cleanup ACK.
    MPI_Comm_free(&comm2D);
    int signal = 1;
    double start = MPI_Wtime();
    MPI_Send(&signal, 1, MPI_INT, size-1, MSG_TERMINATED, world_comm);
    double elapsed = MPI_Wtime() - start;
    TrackSendMetric(MSG_TERMINATED, 1, sizeof(int), elapsed, elapsed);
    PrintSlaveMetrics(comm, start_time, node_comp_time);
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
    int stations = size-1, signal = 1, ready_count = 0, ack_count = 0;
    bool disabling = false, terminating = false, ready_seen[stations], ack_seen[stations];

    MPI_Request controls[2*stations];
    double started[2*stations];
    for (int i = 0; i < stations; i++) ready_seen[i] = ack_seen[i] = false;
    for (int i = 0; i < 2*stations; i++) controls[i] = MPI_REQUEST_NULL;
    BaseState base;

    /**
     * ### Initialization of Global State Matrix and Log Files
     * Initializes the global 2D matrix representing port availability and prepares the main simulation log file. 
     * It acts as the central state registry to track recent congestion events and records the initial configuration parameters 
     * before the main loop begins.
     */
    BaseOpen(&base, nrows, ncols, 2);
    double comp_time = 0.0;

    /**
     * ### Message Listening and Termination Signal Handshake
     * Actively probes the network for incoming messages and 
     * processes the graceful shutdown handshake signals from slave nodes. 
     * It is used to securely tally which charging nodes have ceased operations 
     * and acknowledged the termination protocol to prevent system deadlocks.
     */
    while (ack_count < stations) {
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
                int count = ProcessReport(&base, &report, coords, &comp_time);
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
                // Keep incoming control values separate from the active Isend signal buffer.
                int value;
                MPI_Recv(&value, 1, MPI_INT, status.MPI_SOURCE, status.MPI_TAG, world_comm, MPI_STATUS_IGNORE);
                double elapsed = MPI_Wtime() - start;
                TrackReceiveMetric(status.MPI_TAG, 1, sizeof(int), elapsed, elapsed);
                int peer = status.MPI_SOURCE;
                if (status.MPI_TAG == MSG_READY && !ready_seen[peer]) { ready_seen[peer] = true; ready_count++; }
                else if (status.MPI_TAG == MSG_TERMINATED && !ack_seen[peer]) { ack_seen[peer] = true; ack_count++; }
                else MPI_Abort(world_comm, EXIT_FAILURE);
            }
        }

        /**
         * ### Simulated Timeout Detection and Shutdown Signaling Broadcast
         * Monitors the total elapsed simulation time and broadcasts sequential 
         * non-blocking disable and terminate commands to all active nodes. 
         * It is used to strictly enforce the simulation's fixed duration and to 
         * orchestrate a two-phase graceful shutdown across the distributed cluster.
         */
        if (!disabling && MPI_Wtime() - base.start_time >= SIMULATION_SEC) {
            disabling = true;
            for (int i = 0; i < stations; i++) {
                started[i] = MPI_Wtime();
                MPI_Isend(&signal, 1, MPI_INT, i, MSG_DISABLE, world_comm, &controls[i]);
                message_metrics[MSG_DISABLE].send_call_time += MPI_Wtime() - started[i];
            }
        }

        if (disabling && ready_count == stations && !terminating) {
            terminating = true;
            for (int i = 0; i < stations; i++) {
                started[stations+i] = MPI_Wtime();
                MPI_Isend(&signal, 1, MPI_INT, i, MSG_TERMINATE, world_comm, &controls[stations+i]);
                message_metrics[MSG_TERMINATE].send_call_time += MPI_Wtime() - started[stations+i];
            }
        }
        usleep(1000);
    }

    /**
     * ### Final Cleanup and Performance Summary Report
     * Waits for all asynchronous termination requests to complete, 
     * frees allocated memory, and calculates aggregate performance metrics. 
     * It is used to safely clean up the MPI environment and append a comprehensive summary 
     * report regarding simulation overhead and event frequencies to the log file.
     */
    MPI_Waitall(2*stations, controls, MPI_STATUSES_IGNORE);
    double completed = MPI_Wtime();
    for (int i = 0; i < stations; i++) {
        TrackSendMetric(MSG_DISABLE, 1, sizeof(int), 0, completed - started[i]);
        TrackSendMetric(MSG_TERMINATE, 1, sizeof(int), 0, completed - started[stations+i]);
    }
    
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
    // and directs all other ranks to operate as EV charging nodes (SlaveIO).
    if (rank == size-1) MasterIO(MPI_COMM_WORLD, nrows, ncols, report_type);
    else SlaveIO(MPI_COMM_WORLD, new_comm, nrows, ncols, report_type);

    // This finalization step collects performance metrics from all processes, 
    // frees allocated MPI resources, and safely shuts down the MPI environment.
    WriteMetrics(nrows, ncols, 2);

    MPI_Type_free(&report_type);
    MPI_Comm_free(&new_comm);
    MPI_Finalize();
    return 0;
}