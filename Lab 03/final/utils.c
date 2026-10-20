/**
 * @file      utils.c
 * @brief     Common utility function implementations for EVCNS
 */

#include "utils.h"

// Initialize globals
const double SIMULATION_SEC = 30.0;
const double PORT_PERIOD = 2.0;
const double CHECK_PERIOD = 3.0;

double PRED_PERIOD = 1.5;
int CONGEST_RATE = 70;

MsgMetrics message_metrics[9];

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
 * and environment variables (e.g., the port busy percentage). If an invalid 
 * input is detected, it prevents unpredictable simulation behavior by safely aborting 
 * the entire MPI environment rather than failing silently.
 *
 * @param text The null-terminated string containing the number to parse.
 * @param low  The minimum acceptable value (lower bound).
 * @param high The maximum acceptable value (upper bound).
 * @return     The successfully parsed and validated long integer.
 */
static long ParseInteger(const char *text, long low, long high)
{
    char *end;
    errno = 0; // Reset errno before conversion

    // Use strtol to convert the string to a long integer
    long value = strtol(text, &end, 10);

    // Check for errors: conversion failure, out of range, or extra characters
    if (errno != 0 || end == text || *end || value < low || value > high)
    {
        fprintf(stderr, "EVCNS error: %s\n", "invalid integer argument or environment variable");
        MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
        exit(EXIT_FAILURE);
    }
    return value;
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
static void IncidentDateTime(char text[32])
{
    time_t now = time(NULL);
    struct tm calendar;
    if (!localtime_r(&now, &calendar) ||
        !strftime(text, 32, "%Y-%m-%dT%H:%M:%S%z", &calendar))
    {
        fprintf(stderr, "EVCNS error: %s\n", "cannot format incident date/time");
        MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
        exit(EXIT_FAILURE);
    }
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
static void LocalReferral(int rank, const int coord[2], const int avail[4])
{
    const int dr[4] = {-1, 1, 0, 0}, dc[4] = {0, 0, -1, 1};
    int chosen = -1;
    printf("[Node %d] Local referral candidates:", rank);
    for (int i = 0; i < 4; ++i)
        if (avail[i] > THRESHOLD)
        {
            printf(" (%d,%d) free=%d", coord[0] + dr[i], coord[1] + dc[i], avail[i]);
            if (chosen < 0 || avail[i] > avail[chosen])
                chosen = i;
        }
    if (chosen >= 0)
        printf(" -> refer incoming EV to (%d,%d)\n", coord[0] + dr[chosen], coord[1] + dc[chosen]);
    fflush(stdout);
}

/**
 * @brief Aggregates and exports MPI communication metrics to CSV files.
 *
 * @details
 * ### What it is:
 * A performance reporting and validation function executed by all MPI processes at the end of the simulation. 
 * It writes each process's individual local metrics to a dedicated CSV file named `message_metrics_rank_<rank>.csv`. 
 * It then utilizes `MPI_Reduce` to sum the message counts and byte volumes across all nodes and 
 * sends the aggregated array to the Base Station (the highest rank). 
 * The Base Station performs a strict integrity check to ensure that the total number of 
 * sent messages exactly matches the received messages, and aborts if an imbalance is detected.
 * 
 * ### What it is used for:
 * It is used to generate comprehensive network profiling datasets for post-simulation analysis. 
 * The globally aggregated metrics are written to `message_metrics_global.csv` and summarized in `log.txt`, 
 * allowing developers to evaluate communication overhead, verify graceful shutdown sequences, 
 * and confirm that no messages were lost during the distributed execution.
 *
 * @param rows    The number of rows in the 2D node grid.
 * @param cols    The number of columns in the 2D node grid.
 * @param version The simulation architecture version appended to the output records.
 */
static void WriteMetrics(int rows, int cols, int version)
{
    int rank, size;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);
    const char *names[9] = {"", "C1_REQUEST", "C2_RESPONSE", "C3_REPORT", "C4_BASE_REPLY",
                            "C5_TERMINATE", "DISABLE", "READY", "TERMINATED"};
    char path[80];
    snprintf(path, sizeof(path), "message_metrics_rank_%d.csv", rank);
    FILE *file = fopen(path, "w");
    if (!file)
    {
        fprintf(stderr, "EVCNS error: %s\n", "cannot write message metrics");
        MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
        exit(EXIT_FAILURE);
    }
    fprintf(file, "version,rank,role,rows,cols,tag,message,sent,received,send_bytes,receive_bytes,send_call_time_seconds,receive_call_time_seconds,send_completion_seconds,receive_completion_seconds\n");
    long long counts[8 * 4], sums[8 * 4];
    for (int tag = 1; tag <= 8; ++tag)
    {
        MsgMetrics *m = &message_metrics[tag];
        fprintf(file, "v%d,%d,%s,%d,%d,%d,%s,%lld,%lld,%lld,%lld,%.9f,%.9f,%.9f,%.9f\n",
                version, rank, rank == size - 1 ? "base" : "station", rows, cols, tag, names[tag],
                m->sent, m->received, m->send_bytes, m->receive_bytes,
                m->send_call_time, m->receive_call_time, m->send_lifetime, m->receive_lifetime);
        counts[(tag - 1) * 4] = m->sent;
        counts[(tag - 1) * 4 + 1] = m->received;
        counts[(tag - 1) * 4 + 2] = m->send_bytes;
        counts[(tag - 1) * 4 + 3] = m->receive_bytes;
    }
    fclose(file);
    MPI_Reduce(counts, sums, 8 * 4, MPI_LONG_LONG_INT, MPI_SUM, size - 1, MPI_COMM_WORLD);
    if (rank == size - 1)
    {
        file = fopen("message_metrics_global.csv", "w");
        FILE *log = fopen("log.txt", "a");
        if (!file || !log)
        {
            fprintf(stderr, "EVCNS error: %s\n", "cannot write global metrics");
            MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
            exit(EXIT_FAILURE);
        }
        fprintf(file, "version,rows,cols,tag,message,sent,received,send_bytes,receive_bytes\n");
        long long total = 0;
        for (int tag = 1; tag <= 8; ++tag)
        {
            long long *p = &sums[(tag - 1) * 4];
            if (p[0] != p[1] || p[2] != p[3])
            {
                fprintf(stderr, "EVCNS error: %s\n", "sent/received metrics do not balance");
                MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
                exit(EXIT_FAILURE);
            }
            total += p[0];
            fprintf(file, "v%d,%d,%d,%d,%s,%lld,%lld,%lld,%lld\n", version, rows, cols, tag, names[tag], p[0], p[1], p[2], p[3]);
        }
        fprintf(log, "Actual completed point-to-point messages: %lld (includes tags 1..8; excludes MPI collectives)\n", total);
        fprintf(log, "Shutdown confirmed: READY=%lld TERMINATED=%lld\n", sums[6 * 4], sums[7 * 4]);
        fclose(file);
        fclose(log);
        printf("[Validation] All point-to-point sends/receives balanced. READY=%lld TERMINATED=%lld\n", sums[6 * 4], sums[7 * 4]);
    }
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
 * @param count     The number of data elements sent in the message.
 * @param bytes     The size in bytes of a single data element.
 * @param call_time The actual CPU time (in seconds) blocked inside the send function call.
 * @param lifetime  The total elapsed time (in seconds) from when the send was initiated until it completed.
 */
static void TrackSendMetric(int tag, int count, int bytes, double call_time, double lifetime)
{
    message_metrics[tag].sent += count;
    message_metrics[tag].send_bytes += (long long)count * bytes;
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
 * @param count     The number of data elements received in the message.
 * @param bytes     The size in bytes of a single data element.
 * @param call_time The actual CPU time (in seconds) blocked inside the receive function call.
 * @param lifetime  The total elapsed time (in seconds) from when the receive was initiated until it completed.
 */
static void TrackReceiveMetric(int tag, int count, int bytes, double call_time, double lifetime)
{
    message_metrics[tag].received += count;
    message_metrics[tag].receive_bytes += (long long)count * bytes;
    message_metrics[tag].receive_call_time += call_time;
    message_metrics[tag].receive_lifetime += lifetime;
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
void CreateDataType(MPI_Datatype *report_type)
{
    // Number of elements in each struct member
    int block_len[8] = {1, 1, 2, 1, 4, 1, 1, 32};

    // Array to store the memory offsets (displacements) of each member
    MPI_Aint displacements[8];

    // Primitive MPI datatypes corresponding to each struct member
    MPI_Datatype types[8] = {
        MPI_DOUBLE, MPI_INT, MPI_INT, MPI_INT, MPI_INT, MPI_INT, MPI_INT, MPI_CHAR};

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
    MPI_Get_address(dummy.incident_time, &displacements[7]);

    // Calculate the relative offset for each member to handle C memory padding safely
    for (int i = 0; i < 8; i++)
        displacements[i] -= base;

    // Construct the new custom MPI datatype using the layouts defined above
    MPI_Type_create_struct(8, block_len, displacements, types, report_type);

    // Commit the new datatype to the MPI system to make it ready for communication
    MPI_Type_commit(report_type);
}

/**
 * @brief Outputs a visual snapshot of the charging network grid to a log file.
 *
 * @details
 * ### What it is:
 * A file I/O logging utility that iterates through the global 2D state matrix of the 
 * charging network and formats it into a text-based spatial map. It uses conditional 
 * string formatting to visually tag specific nodes of interest: the node that triggered 
 * the congestion alert is marked with `[!!]`, the optimal available node recommended 
 * by the Base Station is marked with `[OK]`, and standard nodes display their normal coordinates 
 * and free port counts.
 * 
 * ### What it is used for:
 * It is used exclusively by the Base Station to generate an immediate visual record 
 * whenever a congestion incident is reported. These snapshots are written directly to 
 * `log.txt`, allowing developers and researchers to trace the exact state of the network at the time of 
 * an alert, visually verify the correctness of the diamond-scanning search algorithm, and 
 * analyze spatial load distribution.
 *
 * @param log_file   The file pointer to the active simulation log file.
 * @param timestamp  The exact simulation time the snapshot was taken.
 * @param nrows      The total number of rows in the 2D grid.
 * @param ncols      The total number of columns in the 2D grid.
 * @param grid_state The global 2D matrix containing the available port counts for all nodes.
 * @param alert_row  The row coordinate of the congested node initiating the report.
 * @param alert_col  The column coordinate of the congested node initiating the report.
 * @param best_row   The row coordinate of the nearest available node recommended by the Base Station.
 * @param best_col   The column coordinate of the nearest available node recommended by the Base Station.
 */
void PrintGrid(FILE *log_file, double timestamp, int nrows, int ncols, int grid_state[nrows][ncols], int alert_row, int alert_col, int best_row, int best_col)
{
    fprintf(log_file, "----- Grid Snapshot at %.2fs -----\n", timestamp);
    for (int r = 0; r < nrows; r++)
    {
        for (int c = 0; c < ncols; c++)
        {
            char cell_buffer[32];

            if (r == alert_row && c == alert_col)
            {
                sprintf(cell_buffer, "(%d,%d)[!!]:%d", r, c, grid_state[r][c]);
            }
            else if (r == best_row && c == best_col)
            {
                sprintf(cell_buffer, "(%d,%d)[OK]:%d", r, c, grid_state[r][c]);
            }
            else
            {
                sprintf(cell_buffer, "(%d,%d):%d", r, c, grid_state[r][c]);
            }

            fprintf(log_file, "%-16s", cell_buffer);
        }
        fprintf(log_file, "\n");
    }
    fprintf(log_file, "--------------------------------\n\n");
}
