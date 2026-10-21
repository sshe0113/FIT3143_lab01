/**
 * @file      utils.c
 * @brief     Common utility function implementations for EVCNS
 */
#include "utils.h"

// Initialize globals
const double SIMULATION_SEC = 30.0;
const double PORT_PERIOD = 2.0;
const double CHECK_PERIOD = 3.0;
const int CONGEST_RATE = 70;
double PRED_PERIOD = 1.5;
MsgMetrics message_metrics[11];

/**
 * @brief Retrieves the precise CPU execution time consumed by the calling thread.
 *
 * @details
 * ### What it is:
 * A high-precision timing wrapper that leverages the POSIX `clock_gettime` system call alongside the `CLOCK_THREAD_CPUTIME_ID` macro. 
 * It calculates the CPU seconds used exclusively by the calling thread, returning the value as a double-precision float. 
 * Sleep periods, network waits, and OS descheduling overhead are strictly excluded from this measurement. 
 * If the system clock retrieval fails, it catches the error and safely aborts the MPI environment to prevent data corruption.
 * 
 * ### What it is used for:
 * It is used to cleanly isolate pure algorithmic processing time from absolute wall-clock time. 
 * In highly concurrent architectures where threads frequently yield the CPU (via `usleep`) or spin-wait on asynchronous network 
 * events (via `MPI_Test`), standard timers like `MPI_Wtime()` would falsely record those waiting periods as computational load. 
 * By bracketing specific business logic (such as checking port availability or executing the Diamond Scanning algorithm) with 
 * this function, the simulation accurately aggregates the exact hardware compute overhead required to mathematically justify 
 * the architectural improvements in Task 3.
 */
double ComputationTime(void) {
    struct timespec value;
    if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &value) != 0) {
        perror("clock_gettime");
        MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
        exit(EXIT_FAILURE);
    }
    return (double)value.tv_sec + (double)value.tv_nsec / 1e9;
}

/**
 * @brief Safely parses a string into a long integer with strict boundary validation.
 *
 * @details
 * ### What it is:
 * A robust type-conversion utility function that uses `strtol` to convert a string 
 * representation of a number into a `long` integer. It performs strict error checking 
 * to detect numerical overflows (`errno != 0`), invalid or trailing characters (`*end`), 
 * empty strings (`end == text`), and values outside the specified minimum and maximum bounds.
 * 
 * ### What it is used for:
 * It is used to securely process and validate external inputs configured by the user, 
 * such as command-line arguments (e.g., the number of rows and columns in the charging grid) 
 * when validating the grid dimensions. If an invalid 
 * input is detected, it prevents unpredictable simulation behavior by safely aborting 
 * the entire MPI environment rather than failing silently.
 *
 * @param text The null-terminated string containing the number to parse.
 * @param low  The minimum acceptable value (lower bound).
 * @param high The maximum acceptable value (upper bound).
 * @return     The successfully parsed and validated long integer.
 */
long ParseInteger(const char *text, long low, long high) {
    char *end;
    errno = 0;
    // Use strtol to convert the string to a long integer
    long value = strtol(text, &end, 10);
    // Check for errors: conversion failure, out of range, or extra characters
    if (errno || end == text || *end || value < low || value > high) {
        fprintf(stderr, "Invalid integer: %s\n", text);
        MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
        exit(EXIT_FAILURE);
    }
    return value;
}

/**
 * @brief Validate grid dimensions, process count and the report freshness window.
 * @param argc Number of command-line arguments: 1 for defaults, or 3 for rows/cols.
 * @param argv Program name followed optionally by rows and columns.
 * @param size Total world processes, including the one base process.
 * @param nrows In/out row count; caller supplies the default.
 * @param ncols In/out column count; caller supplies the default.
 * EVCNS_PRED_PERIOD optionally changes the window for the required period tests.
 * Other simulation constants are unchanged. Disable dynamic OpenMP team shrinking
 * because each station needs six port workers and one communication thread.
 */
void ReadArguments(int argc, char **argv, int size, int *nrows, int *ncols) {
    if (argc != 1 && argc != 3) {
        fprintf(stderr, "Usage: %s [rows cols]\n", argv[0]);
        MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
        exit(EXIT_FAILURE);
    }
    if (argc == 3) {
        *nrows = (int)ParseInteger(argv[1], 1, INT_MAX);
        *ncols = (int)ParseInteger(argv[2], 1, INT_MAX);
    }
    if ((long long)*nrows * *ncols != (long long)size - 1 || size - 1 > INT_MAX / 2) {
        fprintf(stderr, "rows * cols must equal MPI processes minus one base station\n");
        MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
        exit(EXIT_FAILURE);
    }

    // Securely parses optional environment variables to dynamically override default 
    // simulation parameters like the report freshness window.
    const char *text = getenv("EVCNS_PRED_PERIOD");
    if (text) {
        char *end;
        errno = 0;
        double value = strtod(text, &end);
        if (errno || end == text || *end || !isfinite(value) || value <= 0) {
            fprintf(stderr, "EVCNS_PRED_PERIOD must be a positive finite number\n");
            MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
            exit(EXIT_FAILURE);
        }
        PRED_PERIOD = value;
    }
    // Disables dynamic OpenMP thread adjustments to maintain thread stability.
    omp_set_dynamic(0);
}

/**
 * @brief Registers a custom MPI derived datatype for the MsgData structure.
 *
 * @details
 * ### What it is:
 * An initialization function that maps the memory layout of the heterogeneous C struct 
 * `MsgData` (containing doubles, integers, and character arrays) into a native MPI datatype. 
 * It safely calculates the exact relative byte offsets (displacements) of each struct member to 
 * account for platform-specific C memory padding, and constructs the new type using 
 * `MPI_Type_create_struct`.
 * 
 * ### What it is used for:
 * It is used to enable the seamless transmission of complex congestion reports across the network. 
 * By committing this custom datatype, the simulation can send and receive an entire `MsgData` payload 
 * in a single MPI message call. This eliminates the need for manual data serialization or making 
 * multiple separate `MPI_Send` calls, drastically reducing communication overhead and latency.
 *
 * @param report_type A pointer to the MPI_Datatype handle that will be created and committed.
 */
void CreateDataType(MPI_Datatype *report_type) {
    // Dummy instance used solely to calculate memory address
    MsgData dummy = {0};
    // Number of elements in each struct member
    int lengths[8] = {1, 32, 1, 2, 1, 4, 1, 1};
    // Primitive MPI datatypes corresponding to each struct member
    MPI_Datatype types[8] = {MPI_DOUBLE, MPI_CHAR, MPI_INT, MPI_INT,
                             MPI_INT, MPI_INT, MPI_INT, MPI_INT};
    // Array to store the memory offsets (displacements) of each member
    MPI_Aint base, offsets[8];
    // Get the absolute base memory address of the struct
    MPI_Get_address(&dummy, &base);
    // Get the absolute memory address of each individual member
    MPI_Get_address(&dummy.timestamp, &offsets[0]);
    MPI_Get_address(&dummy.incident_time, &offsets[1]);
    MPI_Get_address(&dummy.sender_rank, &offsets[2]);
    MPI_Get_address(&dummy.coords, &offsets[3]);
    MPI_Get_address(&dummy.avail_ports, &offsets[4]);
    MPI_Get_address(&dummy.avail_neigh, &offsets[5]);
    MPI_Get_address(&dummy.msg_exchanged, &offsets[6]);
    MPI_Get_address(&dummy.msg_size, &offsets[7]);

    // Calculate the relative offset for each member to handle C memory padding safely
    for (int i = 0; i < 8; i++) offsets[i] -= base;

    // Construct the new custom MPI datatype using the layouts defined above
    MPI_Type_create_struct(8, lengths, offsets, types, report_type);
    // Commit the new datatype to the MPI system to make it ready for communication
    MPI_Type_commit(report_type);
}

/**
 * @brief MPI Setup and Topology Creation
 * 
 * @details
 * ### What it is:
 * Initializes the MPI environment variables and constructs a 2D Cartesian grid topology for the charging nodes. 
 * 
 * ### What it is used for:
 * It is used to establish the spatial relationships between nodes, identifying the immediate top, bottom, left, and right 
 * neighbors required for localized communication.
 */
void CreateGrid(MPI_Comm comm, int nrows, int ncols, MPI_Comm *comm2D,
                int coord[2], int neighbors[4]) {
    int dims[2] = {nrows, ncols}, periods[2] = {0, 0}, rank;
    MPI_Cart_create(comm, 2, dims, periods, 0, comm2D);
    MPI_Comm_rank(*comm2D, &rank);
    MPI_Cart_coords(*comm2D, rank, 2, coord);
    // Row dimension: low = top, high = bottom; mesh boundaries return MPI_PROC_NULL.
    MPI_Cart_shift(*comm2D, 0, 1, &neighbors[0], &neighbors[1]);
    // Column dimension: low = left, high = right.
    MPI_Cart_shift(*comm2D, 1, 1, &neighbors[2], &neighbors[3]);
}

/**
 * @brief Port State Simulation (Worker Threads)
 * 
 * @details
 * ### What it is:
 * These worker threads asynchronously simulate real-world electric vehicle behavior by randomly 
 * toggling the availability of individual charging ports based on a predefined probability.
 * 
 * ### What it is used for:
 * This mechanism is used to generate dynamic, independent load fluctuations across the charging 
 * network without blocking the node's main communication thread.
 */
void SimulatePort(int thread_id, int ports[NUM_PORTS], bool *running_flag, double *comp_time) {
    // Record the last updated time of a port
    double last_update = MPI_Wtime();
    for (;;) {
        bool running;
        #pragma omp atomic read
        running = *running_flag;
        if (!running) break;
        // Every two seconds perform once (random change a port status)
        if (MPI_Wtime() - last_update >= PORT_PERIOD) {
            // Mutex lock
            #pragma omp critical
            {
                double c_start = ComputationTime();
                // Default: 70% busy / 30% free
                ports[thread_id] = rand() % 100 < CONGEST_RATE ? 0 : 1;
                *comp_time += ComputationTime() - c_start;
            }
            last_update = MPI_Wtime();
        }
        usleep(1000);
    }
}

/**
 * @brief Read one consistent snapshot and count free ports.
 * @param ports Shared array updated by the six port workers.
 * @return Sum of the 0/1 entries, from 0 to NUM_PORTS.
 * The same unnamed critical region as SimulatePort prevents any port write while
 * the whole array is being summed. It does not reserve or consume a port.
 */
int AvailablePorts(int ports[NUM_PORTS], double *comp_time) {
    int available = 0;
    /* Same unnamed critical region as port writes: sum a consistent snapshot. */
    #pragma omp critical
    {
        double c_start = ComputationTime();
        for (int i = 0; i < NUM_PORTS; i++) available += ports[i];
        *comp_time += ComputationTime() - c_start;
    }
    return available;
}

/**
 * @brief Retrieves and formats the current system time for incident logging.
 *
 * @details 
 * ### What it is:
 * A thread-safe utility function that captures the current local system time 
 * and formats it into an ISO 8601 compliant string (e.g., "YYYY-MM-DDThh:mm:ss+z").
 * It uses `localtime_r` for thread safety and `strftime` for formatting. If the 
 * time fetching fails, it safely aborts the MPI environment to prevent data corruption.
 * 
 * ### What it is used for:
 * It is used to generate a human-readable timestamp whenever a congestion event 
 * (an "incident") occurs at an EV charging node. This timestamp is attached to the 
 * `MsgData` structure and sent to the Base Station, allowing the system to accurately 
 * log exactly when the node and its neighbors ran out of available charging ports.
 *
 * @param text A character array (minimum size 32) where the formatted date and time 
 *             string will be stored.
 */
void IncidentDateTime(char text[32]) {
    time_t now = time(NULL);
    struct tm calendar;
    if (!localtime_r(&now, &calendar) || !strftime(text, 32, "%Y-%m-%dT%H:%M:%S%z", &calendar)) {
        fprintf(stderr, "Cannot format incident date/time\n");
        MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
        exit(EXIT_FAILURE);
    }
}

/**
 * @brief Package a confirmed congestion enquiry as the C3 report payload.
 * @param report Output report; a V3 caller keeps it alive until its send completes.
 * @param rank Reporting station's world rank.
 * @param coord Reporting station's [row, column].
 * @param available Own availability saved when this enquiry began.
 * @param avail_neigh Replies in top, bottom, left, right order; absent = NO_NEIGHBOR.
 * @param degree Actual number of existing neighbours, not a fixed four.
 * @param start_time Local simulation start from MPI_Wtime.
 * Each valid neighbour contributes one request and one reply, each one MPI_INT.
 * msg_size is their combined payload bytes; it excludes C3/C4 and network headers.
 * The own snapshot is retained even if port workers change it during the enquiry.
 */
void BuildReport(MsgData *report, int rank, const int coord[2], int available,
                 const int avail_neigh[4], int degree, double start_time, double *comp_time) {
    double timestamp = MPI_Wtime() - start_time;
    double c_start = ComputationTime();
    *report = (MsgData){.timestamp = timestamp, .sender_rank = rank,
        .coords = {coord[0], coord[1]}, .avail_ports = available,
        .msg_exchanged = degree * 2, .msg_size = degree * 2 * (int)sizeof(int)};
    for (int i = 0; i < 4; i++) report->avail_neigh[i] = avail_neigh[i];
    *comp_time += ComputationTime() - c_start;
    IncidentDateTime(report->incident_time);
}

/**
 * @brief Evaluates adjacent nodes and recommends the neighbor with the most available ports.
 *
 * @details
 * ### What it is:
 * A localized decision-making function that iterates through the availability 
 * statuses of a node's four immediate neighbors (top, bottom, left, right). 
 * It identifies all neighbors that have free ports exceeding a defined `THRESHOLD` 
 * and selects the one with the highest number of available charging ports.
 * 
 * ### What it is used for:
 * It acts as a primary, decentralized load-balancing mechanism. 
 * When an EV charging node becomes congested (runs out of available ports), 
 * it uses this function to instantly redirect an incoming electric vehicle to the 
 * most capable adjacent node, preventing unnecessary communication overhead with 
 * the Base Station.
 *
 * @param rank  The MPI rank identifier of the current congested node.
 * @param coord The 2D grid coordinates [row, column] of the current node.
 * @param avail An array containing the available port counts of the four neighboring nodes.
 */
void LocalReferral(int rank, const int coord[2], const int avail[4], double *comp_time) {
    double c_start = ComputationTime();
    const int dr[4] = {-1, 1, 0, 0}, dc[4] = {0, 0, -1, 1};
    int chosen = -1;
    for (int i = 0; i < 4; i++)
        if (avail[i] > THRESHOLD && (chosen < 0 || avail[i] > avail[chosen])) chosen = i;
    *comp_time += ComputationTime() - c_start;
    if (chosen >= 0)
        printf("[Node %d] Refer EV to neighbour (%d,%d), free=%d\n",
               rank, coord[0] + dr[chosen], coord[1] + dc[chosen], avail[chosen]);
    fflush(stdout);
}

/**
 * @brief Display the full C4 candidate list forwarded to incoming EVs.
 * @param rank Station receiving the referral.
 * @param count Number of coordinate pairs; zero means no candidate is available.
 * @param coords Flat [row0,col0,row1,col1,...] list, read only when count > 0.
 * All equally near candidates returned by the base are shown, without a fixed cap.
 */
void ShowReferral(int rank, int count, const int *coords) {
    printf("[Node %d] Base recommends %d node(s):", rank, count);
    for (int i = 0; i < count; i++) printf(" (%d,%d)", coords[2*i], coords[2*i+1]);
    printf("%s\n", count ? " -> forward to EVs" : " no available nodes nearby");
    fflush(stdout);
}

/**
 * @brief Accumulates statistical performance data for outgoing MPI messages.
 *
 * @details
 * ### What it is:
 * A local helper function that updates the `message_metrics` tracking array for a specific 
 * outgoing MPI message type (identified by its `tag`). It incrementally adds to the total 
 * number of messages sent, the total payload volume in bytes, the synchronous execution time 
 * spent inside the MPI send API (`call_time`), and the total asynchronous duration from request 
 * initiation to completion (`lifetime`).
 * 
 * ### What it is used for:
 * It is used to precisely profile the communication overhead of the charging node simulation. 
 * By calling this function immediately after an `MPI_Send` finishes or an `MPI_Isend` request 
 * is confirmed as complete, the system accurately logs the network cost of every distinct 
 * interaction (e.g., neighbor queries, congestion reports, or termination signals) for later analysis.
 *
 * @param tag       The MPI message tag, which serves as the index in the metrics array.
 * @param messages  The number of completed MPI messages sent.
 * @param payload_bytes The payload size in bytes of a single MPI message.
 * @param call_time The elapsed wall-clock time (in seconds) spent inside the send function call.
 * @param lifetime  The total elapsed time (in seconds) from when the send was initiated until completion was observed.
 */
void TrackSendMetric(int tag, int messages, int payload_bytes, double call_time, double lifetime) {
    message_metrics[tag].sent += messages;
    message_metrics[tag].send_bytes += (long long)messages * payload_bytes;
    message_metrics[tag].send_call_time += call_time;
    message_metrics[tag].send_lifetime += lifetime;
}

/**
 * @brief Accumulates statistical performance data for incoming MPI messages.
 *
 * @details
 * ### What it is:
 * A local helper function that updates the `message_metrics` tracking array for a specific 
 * incoming MPI message type (identified by its `tag`). It incrementally adds to the total 
 * number of messages received, the total payload volume in bytes, the synchronous execution time 
 * spent inside the MPI receive API (`call_time`), and the total asynchronous duration from request 
 * initiation to completion (`lifetime`).
 * 
 * ### What it is used for:
 * It is used to precisely profile the communication overhead and latency of incoming data in the 
 * charging node simulation. By calling this function immediately after an `MPI_Recv` finishes 
 * or an `MPI_Irecv` request is confirmed as complete, the system accurately logs the network cost 
 * of every distinct incoming interaction (e.g., neighbor replies, congestion reports, or base station 
 * instructions) for later performance analysis.
 *
 * @param tag       The MPI message tag, which serves as the index in the metrics array.
 * @param messages  The number of completed MPI messages received.
 * @param payload_bytes The payload size in bytes of a single MPI message.
 * @param call_time The elapsed wall-clock time (in seconds) spent inside the receive function call.
 * @param lifetime  The total elapsed time (in seconds) from when the receive was initiated until completion was observed.
 */
void TrackReceiveMetric(int tag, int messages, int payload_bytes, double call_time, double lifetime) {
    message_metrics[tag].received += messages;
    message_metrics[tag].receive_bytes += (long long)messages * payload_bytes;
    message_metrics[tag].receive_call_time += call_time;
    message_metrics[tag].receive_lifetime += lifetime;
}

/**
 * ### Main Thread: Neighbor Request Processing
 * Listens for and intercepts incoming MSG_REQ_NEIGHBOR inquiries 
 * from adjacent charging nodes actively looking for free slots. 
 * It safely calculates the locally available ports using an OpenMP critical section and 
 * immediately replies, assisting the neighbor's decentralized load-balancing efforts.
 */
void AnswerNeighbours(MPI_Comm comm2D, int ports[NUM_PORTS], double *comp_time) {
    int flag, request;
    MPI_Status status;
    MPI_Iprobe(MPI_ANY_SOURCE, MSG_REQ_NEIGHBOR, comm2D, &flag, &status);
    if (!flag) return;
    double start = MPI_Wtime();
    MPI_Recv(&request, 1, MPI_INT, status.MPI_SOURCE, MSG_REQ_NEIGHBOR, comm2D, MPI_STATUS_IGNORE);
    double elapsed = MPI_Wtime() - start;
    TrackReceiveMetric(MSG_REQ_NEIGHBOR, 1, sizeof(int), elapsed, elapsed);
    
    int available = AvailablePorts(ports, comp_time);
    
    start = MPI_Wtime();
    MPI_Send(&available, 1, MPI_INT, status.MPI_SOURCE, MSG_RES_NEIGHBOR, comm2D);
    elapsed = MPI_Wtime() - start;
    TrackSendMetric(MSG_RES_NEIGHBOR, 1, sizeof(int), elapsed, elapsed);
}

/**
 * ### Initialization of Global State Matrix and Log Files
 * This section initializes the global 2D matrix representing port availability and prepares the main simulation log file. 
 * It acts as the central state registry to track recent congestion events and records the initial configuration parameters 
 * before the main loop begins.
 */
void BaseOpen(BaseState *base, int nrows, int ncols, int version) {
    *base = (BaseState){.nrows = nrows, .ncols = ncols};
    int stations = nrows * ncols;
    base->grid_state = malloc((size_t)stations * sizeof(int));
    base->last_report_time = malloc((size_t)stations * sizeof(double));
    base->last_update_time = malloc((size_t)stations * sizeof(double));
    base->log_file = fopen("log.txt", "w");
    if (!base->grid_state || !base->last_report_time || !base->last_update_time || !base->log_file) {
        fprintf(stderr, "Cannot allocate base state or open log.txt\n");
        MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
        exit(EXIT_FAILURE);
    }
    for (int i = 0; i < stations; i++) {
        base->grid_state[i] = NUM_PORTS;
        base->last_report_time[i] = base->last_update_time[i] = -1;
    }
    fprintf(base->log_file, "Configuration: version=v%d rows=%d cols=%d processes=%d ports=%d threads=%d seconds=%.3f port_period=%.3f check_period=%.3f predefined_period=%.6f busy_percent=%d\n",
            version, nrows, ncols, stations + 1, NUM_PORTS, NUM_PORTS + 1,
            SIMULATION_SEC, PORT_PERIOD, CHECK_PERIOD, PRED_PERIOD, CONGEST_RATE);
    fprintf(base->log_file, "Search: nearest Manhattan layer over the grid; unknown/expired availability is estimated as free.\n");
    fprintf(base->log_file, "Neighbour payloads: request=%zu bytes, reply=%zu bytes; report Msg Size is their combined payload, excluding network headers.\n",
            sizeof(int), sizeof(int));
    MPI_Barrier(MPI_COMM_WORLD);
    base->start_time = MPI_Wtime();
}

/**
 * @brief Update report knowledge and compute all equal-nearest referrals.
 * @param base Base-only state, including status and receipt-time arrays.
 * @param report One completed C3 receive; it is safe to inspect its fields now.
 * @param best_coords Output [row,col,...] buffer with space for 2 * rows * cols ints.
 * @return Number of candidate pairs, or zero for the recent-neighbour shortcut
 *         or when no qualifying station is found.
 * Use a single base-clock now for every freshness comparison. Expired observations
 * use the documented availability estimate when a candidate is examined. Own report timestamps remain
 * separate from neighbour observations, as required by the report-window rule.
 * The original diamond scan checks increasing Manhattan distances across the grid
 * and returns every candidate in the first qualifying layer. It does not reserve
 * ports or contact these candidates to confirm their current availability.
 */
int ProcessReport(BaseState *base, const MsgData *report, int *best_coords, double *comp_time) {
    double c_start = ComputationTime();
    int rows = base->nrows, cols = base->ncols;
    int row = report->coords[0], col = report->coords[1];
    if (row < 0 || row >= rows || col < 0 || col >= cols || report->avail_ports > THRESHOLD) {
        fprintf(stderr, "Invalid congestion report\n");
        MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
        exit(EXIT_FAILURE);
    }

    /* All receipt times and freshness comparisons use the base clock.
     * last_report_time tracks OWN reports; last_update_time also tracks
     * neighbour observations. Check expiry only for candidates examined below. */
    double now = MPI_Wtime();

    int own = row * cols + col;
    base->grid_state[own] = report->avail_ports;
    base->last_report_time[own] = base->last_update_time[own] = now;
    const int dr[4] = {-1, 1, 0, 0}, dc[4] = {0, 0, -1, 1};
    bool recent = true;
    int degree = 0;
    for (int i = 0; i < 4; i++) {
        int r = row + dr[i], c = col + dc[i];
        if (r < 0 || r >= rows || c < 0 || c >= cols) continue;
        int index = r * cols + c;
        base->grid_state[index] = report->avail_neigh[i];
        base->last_update_time[index] = now;
        degree++;
        if (base->last_report_time[index] < 0 || now - base->last_report_time[index] > PRED_PERIOD)
            recent = false;
    }

    int found_count = 0;
    /**
     * ### Manhattan Ripple Search to find available nodes
     * This algorithm expands outward in a diamond-shaped ripple pattern to 
     * locate the geographically nearest charging nodes with available ports. 
     * It is used to dynamically compile a list of optimal alternative stations for 
     * rerouting incoming electric vehicles when a local area becomes fully saturated.
     */
    if (!(degree > 0 && recent)) {

        /* Return every candidate in the first qualifying layer, without a fixed cap.
         * C3 already confirms that distance-one neighbours are congested.
         * Unknown/expired availability remains the documented estimate, not a reservation. */
        for (int d = 2; d <= rows + cols - 2 && !found_count; d++) {
            for (int dx = -d; dx <= d; dx++) {
                int dy = d - abs(dx), r = row + dx;
                if (r < 0 || r >= rows) continue;
                for (int sign = -1; sign <= 1; sign += 2) {
                    int c = col + sign * dy;
                    if (c >= 0 && c < cols) {
                        int index = r * cols + c;
                        if (base->last_update_time[index] < 0 ||
                            now - base->last_update_time[index] > PRED_PERIOD ||
                            base->grid_state[index] > THRESHOLD) {
                            best_coords[2*found_count] = r;
                            best_coords[2*found_count+1] = c;
                            found_count++;
                        }
                    }

                    // Both signs give the same coordinate when dy is zero.
                    if (dy == 0) break;
                }
            }
        }
    }

    base->ttl_alerts++;
    *comp_time += ComputationTime() - c_start;
    fprintf(base->log_file, "T:%.6fs | ALERT [%d] | Incident: %s | Rank: %d Coord: (%d,%d) | Ports Free: %d | Neighbor Free: [T:%d, B:%d, L:%d, R:%d] | Msgs: %d | Msg Size: %d Bytes\n",
            report->timestamp, base->ttl_alerts, report->incident_time, report->sender_rank, row, col,
            report->avail_ports, report->avail_neigh[0], report->avail_neigh[1],
            report->avail_neigh[2], report->avail_neigh[3], report->msg_exchanged, report->msg_size);
    fprintf(base->log_file, "Recommended nodes: %d", found_count);
    for (int i = 0; i < found_count; i++)
        fprintf(base->log_file, " (%d,%d)", best_coords[2*i], best_coords[2*i+1]);
    fprintf(base->log_file, " | recent-neighbour shortcut=%d\n", degree > 0 && recent);
    fflush(base->log_file);
    
    return found_count;
}

/**
 * @brief Log elapsed simulation time and alert count, then release base state.
 * @param base State initialized by BaseOpen, used only by the base process.
 * Call after all station cleanup acknowledgements and local pending sends finish.
 * WriteMetrics later reopens log.txt to append the globally reduced message totals.
 */
void BaseClose(BaseState *base) {
    fprintf(base->log_file, "Total Simulation Time: %.6f seconds\nTotal Congestion Alerts: %d\n",
            MPI_Wtime() - base->start_time, base->ttl_alerts);
    fclose(base->log_file);
    free(base->grid_state);
    free(base->last_report_time);
    free(base->last_update_time);
}

/**
 * @brief Aggregates and exports MPI communication metrics to log.txt.
 *
 * @details
 * ### What it is:
 * A performance reporting and validation function executed by all MPI processes at the end of the simulation. 
 * It packs each process's individual local metrics into arrays for global reduction. 
 * It then utilizes `MPI_Reduce` to sum the message counts, byte volumes, and timings across all nodes and 
 * sends the aggregated array to the Base Station (the highest rank). 
 * The Base Station performs a strict integrity check to ensure that the total number of 
 * sent messages exactly matches the received messages, and aborts if an imbalance is detected.
 * 
 * ### What it is used for:
 * It is used to generate comprehensive network profiling datasets for post-simulation analysis. 
 * The globally aggregated metrics are written to `log.txt` for Slurm to convert to CSV, 
 * allowing developers to evaluate communication overhead, verify graceful shutdown sequences, 
 * and confirm that no messages were lost during the distributed execution.
 *
 * @param rows    The number of rows in the 2D node grid.
 * @param cols    The number of columns in the 2D node grid.
 * @param version The simulation architecture version appended to the output records.
 */
void WriteMetrics(int rows, int cols, int version) {
    int rank, size;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    long long counts[10][4], sums[10][4];
    double times[10][4], totals[10][4];
    for (int tag = 1; tag <= 10; tag++) {
        MsgMetrics *m = &message_metrics[tag];
        counts[tag-1][0] = m->sent;
        counts[tag-1][1] = m->received;
        counts[tag-1][2] = m->send_bytes;
        counts[tag-1][3] = m->receive_bytes;
        times[tag-1][0] = m->send_call_time;
        times[tag-1][1] = m->receive_call_time;
        times[tag-1][2] = m->send_lifetime;
        times[tag-1][3] = m->receive_lifetime;
    }

    // 40 values = 10 tags * 4 counters; the base is world rank size - 1.
    MPI_Reduce(counts, sums, 40, MPI_LONG_LONG_INT, MPI_SUM, size-1, MPI_COMM_WORLD);
    MPI_Reduce(times, totals, 40, MPI_DOUBLE, MPI_SUM, size-1, MPI_COMM_WORLD);
    if (rank == size-1) {
        FILE *file = fopen("log.txt", "a");
        if (!file) MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
        long long total = 0;
        // V1 includes GRANT/DONE; completed point-to-point messages are counted once.
        int tags = version == 1 ? 10 : 8;
        fprintf(file, "Timing: local MPI function time and observed completion time; includes waiting/polling, not isolated one-way network latency.\n");
        fprintf(file, "METRIC_FIELDS version rows cols tag sent received send_bytes receive_bytes send_call_time receive_call_time send_lifetime receive_lifetime\n");
        for (int tag = 1; tag <= tags; tag++) {
            long long *p = sums[tag-1];
            double *t = totals[tag-1];

            if (p[0] != p[1] || p[2] != p[3]) {
                fprintf(stderr, "Unbalanced message tag %d: sent=%lld received=%lld\n", tag, p[0], p[1]);
                MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
            }
            total += p[0];
            fprintf(file, "METRIC %d %d %d %d %lld %lld %lld %lld %.9f %.9f %.9f %.9f\n",
                    version, rows, cols, tag, p[0], p[1], p[2], p[3], t[0], t[1], t[2], t[3]);
        }
        fprintf(file, "Total Communication Events: %lld (completed point-to-point messages; collectives excluded)\n", total);
        fprintf(file, "Shutdown confirmed: READY=%lld TERMINATED=%lld\n", sums[6][0], sums[7][0]);
        fclose(file);
    }
}

/**
 * @brief Aggregates and prints the average performance metrics across all charging nodes.
 *
 * @details
 * ### What it is:
 * A performance reduction utility executed by all slave nodes (charging stations) at the end of the simulation. 
 * It records overall wall time, the instrumented MPI send/receive API wall time,
 * and CPU time accumulated only inside computation sections. The metrics are not additive. 
 * 
 * ### What it is used for:
 * It uses `MPI_Reduce` to gather these individual timing metrics from the entire distributed grid and sends them 
 * to a designated root node (`comm_rank == 0`). The root node then calculates and prints the exact average 
 * overall, communication, and computation times across the network. This provides the macro-level experimental 
 * data required to justify the efficiency of the charging nodes' architecture in Task 3.
 *
 * @param comm       The MPI communicator specifically isolating the slave nodes (charging stations).
 * @param start_time The absolute wall-clock time when the simulation started for this node.
 * @param comp_time  Computation-section CPU seconds summed across all station threads;
 *                   excludes lock acquisition, output, MPI communication and sleep.
 */
void PrintSlaveMetrics(MPI_Comm comm, double start_time, double comp_time) {
    double overall_time = MPI_Wtime() - start_time;
    double comm_time = 0.0;
    for (int i = 1; i <= 10; i++) {
        comm_time += message_metrics[i].send_call_time;
        comm_time += message_metrics[i].receive_call_time;
    }

    int comm_rank, slave_size;
    MPI_Comm_rank(comm, &comm_rank);
    MPI_Comm_size(comm, &slave_size);

    double sum_overall = 0.0, sum_comm = 0.0, sum_comp = 0.0;
    MPI_Reduce(&overall_time, &sum_overall, 1, MPI_DOUBLE, MPI_SUM, 0, comm);
    MPI_Reduce(&comm_time, &sum_comm, 1, MPI_DOUBLE, MPI_SUM, 0, comm);
    MPI_Reduce(&comp_time, &sum_comp, 1, MPI_DOUBLE, MPI_SUM, 0, comm);

    if (comm_rank == 0) {
        printf("[Charging Nodes Average] Overall: %.4fs | Comm: %.4fs | Comp: %.9fs\n",
               sum_overall / slave_size, sum_comm / slave_size, sum_comp / slave_size);
        fflush(stdout);
    }
}

/**
 * @brief Calculates and prints the standalone performance metrics for the Base Station.
 *
 * @details
 * ### What it is:
 * A local performance profiling function executed exclusively by the Base Station (master node) upon 
 * simulation completion. It reports overall wall time, recorded send/receive API wall
 * time, and separately accumulated computation-section CPU time.
 * 
 * ### What it is used for:
 * It is used to directly output the isolated operational metrics of the centralized Base Station to the console. 
 * By separating the Master's metrics from the Slaves', it allows you to precisely analyze whether the Base Station's 
 * scheduling loop (V1/V2) or asynchronous event-driven state machine (V3) is acting as a computational or communicational 
 * bottleneck under heavy grid congestion.
 *
 * @param start_time The absolute wall-clock time when the simulation started for the Base Station.
 * @param comp_time  Base computation-section CPU seconds, excluding report logging.
 */
void PrintMasterMetrics(double start_time, double comp_time) {
    double overall_time = MPI_Wtime() - start_time;
    double comm_time = 0.0;
    for (int i = 1; i <= 10; i++) {
        comm_time += message_metrics[i].send_call_time;
        comm_time += message_metrics[i].receive_call_time;
    }
    printf("[Base Station] Overall: %.4fs | Comm: %.4fs | Comp: %.9fs\n", overall_time, comm_time, comp_time);
    fflush(stdout);
}