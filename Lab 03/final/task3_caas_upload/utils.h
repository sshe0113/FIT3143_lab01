/**
 * @file      utils.h
 * @brief     Common utilities and definitions for EVCNS
 */
#ifndef EVCNS_UTILS_H
#define EVCNS_UTILS_H

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <time.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <unistd.h>
#include <mpi.h>
#include <omp.h>

// Constants
#define NUM_PORTS 6    // Number of charging ports in a node
#define THRESHOLD 2    // A node is congested when 2 or fewer ports are available
#define NO_NEIGHBOR -2 // No physical neighbors

// MPI Message Tags
#define MSG_REQ_NEIGHBOR 1 // Request message tag from a node to its neighbors to check available ports
#define MSG_RES_NEIGHBOR 2 // Response message tag to reply its own available ports to requested node
#define MSG_REPORT 3       // Report message tag to base station if ports of a node and its neighbors are fully utilized (congestion)
#define MSG_BS_REPLY 4     // Reply coordinate of available ports from base staion to reported node

#define MSG_TERMINATE 5    // Terminate message tag to ensure shutdown gracefully
/* Updated shutdown: stop new enquiries, wait for every READY, then send
 * TERMINATE and wait for every cleanup acknowledgement (TERMINATED).
 * Each control message carries one integer signal. */
#define MSG_DISABLE 6      
#define MSG_READY 7        
#define MSG_TERMINATED 8   

#define MSG_QUERY_GRANT 9   // Base -> station: permission to check local congestion
#define MSG_QUERY_DONE 10   // Station -> base: enquiry and any base reply are complete

// Fixed simulation duration and update/check intervals (seconds).
extern const double SIMULATION_SEC, PORT_PERIOD, CHECK_PERIOD;
extern const int CONGEST_RATE;
// Base-station report freshness window; adjustable for the period experiments.
extern double PRED_PERIOD;

/**
 * @brief Defines a performance tracking structure for MPI communication metrics.
 *
 * @details
 * ### What it is:
 * A data structure that aggregates statistical data for specific MPI message tags. 
 * It records the total count of messages (`sent`, `received`), the data volume in bytes (`send_bytes`, `receive_bytes`), 
 * the synchronous execution time spent inside MPI API calls (`send_call_time`, `receive_call_time`), 
 * and the total asynchronous lifespan of non-blocking requests from initiation to completion (`send_lifetime`, `receive_lifetime`).
 * 
 * ### What it is used for:
 * It is used for network profiling and performance benchmarking of the distributed simulation. 
 * The system maintains an array of these structs to track the overhead per message type, ultimately 
 * writing the aggregated data into log.txt for Slurm to convert to CSV files and analyze communication bottlenecks.
 */
typedef struct {
    long long sent, received, send_bytes, receive_bytes;

    double send_call_time, receive_call_time, send_lifetime, receive_lifetime;
} MsgMetrics;

// Updated: tag 0 is unused; tags 1..10 include the V1 GRANT/DONE messages.
extern MsgMetrics message_metrics[11]; 

/**
 * @brief Encapsulates a comprehensive congestion incident report.
 *
 * @details
 * ### What it is:
 * A composite data structure that bundles all critical information regarding a 
 * congestion event at a specific charging node. It stores the exact simulation 
 * time, human-readable calendar time, the node's MPI rank and grid coordinates, 
 * port availability of both the node and its immediate neighbors, and the communication 
 * overhead generated during neighbor verification.
 * 
 * ### What it is used for:
 * It is used as the standard payload for reporting local area congestion to the Base Station. 
 * By packaging these multiple discrete variables into a single struct (which is then mapped to a 
 * custom MPI Datatype), the system can transmit the entire incident context in exactly one 
 * message (`MSG_REPORT`), significantly reducing communication latency and network overhead 
 * compared to sending each variable separately.
 *
 * @var timestamp     The exact elapsed simulation time (in seconds) when the congestion occurred.
 * @var incident_time The ISO 8601 formatted real-world calendar time of the incident.
 * @var sender_rank   The MPI rank ID of the node initiating the report.
 * @var coords        The [row, col] coordinates of the congested node within the 2D mesh.
 * @var avail_ports   The number of free ports at the reporting node (verifying its threshold).
 * @var avail_neigh   The free ports of its four neighboring nodes (verifying local area congestion).
 * @var msg_exchanged The total number of point-to-point messages sent and received to poll neighbors.
 * @var msg_size      The total data volume (in bytes) consumed by the neighbor polling process.
 */
typedef struct {
    double timestamp;
    char incident_time[32];
    int sender_rank, coords[2], avail_ports, avail_neigh[4];

    int msg_exchanged, msg_size; 
} MsgData;

/**
 * @brief State owned exclusively by the base process.
 * Grid arrays are flattened: index = row * ncols + column.
 * last_report_time changes only for a station's OWN congestion report;
 * last_update_time also changes when that station is observed by a neighbour.
 * Missing timestamps are -1. Unknown/expired availability is estimated as free.
 */
typedef struct {
    int nrows, ncols, *grid_state, ttl_alerts;

    double *last_report_time, *last_update_time, start_time;
    FILE *log_file;
} BaseState;

// Function Prototypes
double ComputationTime(void);
long ParseInteger(const char *text, long low, long high);
void ReadArguments(int argc, char **argv, int size, int *nrows, int *ncols);
void CreateDataType(MPI_Datatype *report_type);
void CreateGrid(MPI_Comm comm, int nrows, int ncols, MPI_Comm *comm2D,
                int coord[2], int neighbors[4]);
void SimulatePort(int thread_id, int ports[NUM_PORTS], bool *running_flag, double *comp_time);
int AvailablePorts(int ports[NUM_PORTS], double *comp_time);
void IncidentDateTime(char text[32]);
void BuildReport(MsgData *report, int rank, const int coord[2], int available,
                 const int avail_neigh[4], int degree, double start_time, double *comp_time);
void LocalReferral(int rank, const int coord[2], const int avail[4], double *comp_time);
void ShowReferral(int rank, int count, const int *coords);
void AnswerNeighbours(MPI_Comm comm2D, int ports[NUM_PORTS], double *comp_time);
void TrackSendMetric(int tag, int messages, int payload_bytes, double call_time, double lifetime);
void TrackReceiveMetric(int tag, int messages, int payload_bytes, double call_time, double lifetime);
void WriteMetrics(int rows, int cols, int version);
void BaseOpen(BaseState *base, int nrows, int ncols, int version);
int ProcessReport(BaseState *base, const MsgData *report, int *best_coords, double *comp_time);
void BaseClose(BaseState *base);
void PrintSlaveMetrics(MPI_Comm comm, double start_time, double comp_time);
void PrintMasterMetrics(double start_time, double comp_time);

#endif // EVCNS_UTILS_H