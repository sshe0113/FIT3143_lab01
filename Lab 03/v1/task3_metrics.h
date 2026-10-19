/* Controls and per-message timing required for Task 3. All point-to-point MPI calls below are
 * made by T6 (stations) or the one base thread; no metrics lock is necessary.
 * CSV timings use each rank's own clock. Request lifetime is NOT one-way wire
 * latency: preposted receives include idle waiting, and polling adds delay.
 */
#define MSG_DISABLE 6
#define MSG_READY 7
#define MSG_TERMINATED 8

static double simulation_seconds = 30.0, port_period = 2.0, check_period = 3.0;
static double predefined_period = 1.5;
static unsigned simulation_seed = 2026u;
static int busy_percent = 70;

static void SimulationError(const char *message) {
    fprintf(stderr, "EVCNS error: %s\n", message);
    MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
    exit(EXIT_FAILURE);
}

static long ParseInteger(const char *text, long low, long high) {
    char *end;
    errno = 0;
    long value = strtol(text, &end, 10);
    if (errno || end == text || *end || value < low || value > high)
        SimulationError("invalid integer argument or environment variable");
    return value;
}

static double ReadPeriod(const char *name, double fallback) {
    const char *text = getenv(name);
    if (!text) return fallback;
    char *end;
    errno = 0;
    double value = strtod(text, &end);
    if (errno || end == text || *end || !(value > 0.0 && value < 86400.0))
        SimulationError("period/duration must be positive, finite and below 86400s");
    return value;
}

/* Test-only fixture: one fixed free-port count (0..6) for every station rank.
 * Unset EVCNS_FREE_MAP for normal random simulations and performance runs.
 */
static int FixedFreePorts(int rank, int stations) {
    const char *text = getenv("EVCNS_FREE_MAP");
    if (!text) return -1;
    int chosen = -1;
    for (int i = 0; i < stations; ++i) {
        char *end;
        errno = 0;
        long value = strtol(text, &end, 10);
        if (errno || end == text || value < 0 || value > NUM_PORTS)
            SimulationError("EVCNS_FREE_MAP needs one 0..6 entry per station");
        if (i == rank) chosen = (int)value;
        if (i + 1 < stations) {
            if (*end != ',') SimulationError("EVCNS_FREE_MAP is too short");
            text = end + 1;
        } else if (*end) SimulationError("EVCNS_FREE_MAP has extra/invalid entries");
    }
    return chosen;
}

static void IncidentDateTime(char text[32]) {
    time_t now = time(NULL);
    struct tm calendar;
    if (!localtime_r(&now, &calendar) ||
        !strftime(text, 32, "%Y-%m-%dT%H:%M:%S%z", &calendar))
        SimulationError("cannot format incident date/time");
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
    long long sent, received, send_bytes, receive_bytes, cancelled;
    double send_api, receive_api, progress_api, send_lifetime, receive_lifetime;
} MessageMetric;

typedef struct {
    MPI_Request *slot; /* Storage identity: completed eager sends may share a handle. */
    MPI_Datatype datatype;
    int state, tag;
    long long bytes;
    bool sending;
    double started;
} TrackedRequest;

static MessageMetric message_metrics[9];
static TrackedRequest *request_table;
static size_t request_capacity, request_used, request_deleted;
static long long roundtrip_count[2];
static double roundtrip_seconds[2];

static size_t RequestHash(MPI_Request *slot) {
    const unsigned char *bytes = (const unsigned char *)&slot;
    size_t hash = 2166136261u;
    for (size_t i = 0; i < sizeof(slot); ++i) hash = (hash ^ bytes[i]) * 16777619u;
    return hash;
}

static void ResizeRequestTable(size_t capacity) {
    TrackedRequest *old = request_table;
    size_t previous = request_capacity;
    request_table = calloc(capacity, sizeof(*request_table));
    if (!request_table) SimulationError("cannot allocate metrics request table");
    request_capacity = capacity;
    request_deleted = 0;
    for (size_t i = 0; i < previous; ++i) {
        if (old[i].state != 1) continue;
        size_t slot = RequestHash(old[i].slot) & (capacity-1);
        while (request_table[slot].state == 1) slot = (slot+1) & (capacity-1);
        request_table[slot] = old[i];
    }
    free(old);
}

static TrackedRequest *FindRequest(MPI_Request *storage) {
    if (!storage || !request_capacity) return NULL;
    size_t slot = RequestHash(storage) & (request_capacity-1);
    while (request_table[slot].state != 0) {
        if (request_table[slot].state == 1 && request_table[slot].slot == storage)
            return &request_table[slot];
        slot = (slot+1) & (request_capacity-1);
    }
    return NULL;
}

static void TrackRequest(MPI_Request *storage, int tag, MPI_Datatype type,
                         int count, bool sending, double started) {
    if (*storage == MPI_REQUEST_NULL) return;
    if (tag < 1 || tag > 8) SimulationError("unrecognized message tag in metrics");
    if (!request_capacity) ResizeRequestTable(128);
    if ((request_used + request_deleted + 1) * 10 >= request_capacity * 7)
        ResizeRequestTable(request_used * 2 >= request_capacity ? request_capacity*2 : request_capacity);
    size_t slot = RequestHash(storage) & (request_capacity-1), deleted = SIZE_MAX;
    while (request_table[slot].state != 0) {
        if (request_table[slot].state == 2 && deleted == SIZE_MAX) deleted = slot;
        if (request_table[slot].state == 1 && request_table[slot].slot == storage)
            SimulationError("MPI request reused before completion");
        slot = (slot+1) & (request_capacity-1);
    }
    if (deleted != SIZE_MAX) { slot = deleted; --request_deleted; }
    int size;
    MPI_Type_size(type, &size);
    request_table[slot] = (TrackedRequest){storage, type, 1, tag, (long long)count*size, sending, started};
    ++request_used;
}

static void FinishRequest(MPI_Request *storage, MPI_Request previous, const MPI_Status *status) {
    if (previous == MPI_REQUEST_NULL) return;
    TrackedRequest *record = FindRequest(storage);
    if (!record) SimulationError("completed MPI request was not tracked");
    MessageMetric *metric = &message_metrics[record->tag];
    int cancelled;
    MPI_Test_cancelled(status, &cancelled); /* Other status fields are undefined if cancelled. */
    double elapsed = MPI_Wtime() - record->started;
    if (cancelled) ++metric->cancelled;
    else if (record->sending) {
        ++metric->sent;
        metric->send_bytes += record->bytes;
        metric->send_lifetime += elapsed;
    } else {
        int count, size;
        MPI_Get_count(status, record->datatype, &count);
        MPI_Type_size(record->datatype, &size);
        if (count == MPI_UNDEFINED) SimulationError("receive count/datatype mismatch");
        ++metric->received;
        metric->receive_bytes += (long long)count * size;
        metric->receive_lifetime += elapsed;
    }
    record->state = 2;
    --request_used;
    ++request_deleted;
}

static void AttributeProgress(MPI_Request *storage, double seconds) {
    TrackedRequest *record = FindRequest(storage);
    if (record) message_metrics[record->tag].progress_api += seconds;
}

static int MeasuredSend(const void *buffer, int count, MPI_Datatype type,
                        int peer, int tag, MPI_Comm comm) {
    double start = MPI_Wtime();
#ifdef EVCNS_SYNC_SEND
    int error = MPI_Ssend(buffer, count, type, peer, tag, comm);
#else
    int error = MPI_Send(buffer, count, type, peer, tag, comm);
#endif
    double elapsed = MPI_Wtime()-start;
    if (peer != MPI_PROC_NULL && error == MPI_SUCCESS) {
        int size;
        MPI_Type_size(type, &size);
        MessageMetric *metric = &message_metrics[tag];
        ++metric->sent;
        metric->send_bytes += (long long)count*size;
        metric->send_api += elapsed;
        metric->send_lifetime += elapsed;
    }
    return error;
}

static int MeasuredRecv(void *buffer, int count, MPI_Datatype type,
                        int peer, int tag, MPI_Comm comm, MPI_Status *status) {
    MPI_Status actual;
    double start = MPI_Wtime();
    int error = MPI_Recv(buffer, count, type, peer, tag, comm, &actual);
    double elapsed = MPI_Wtime()-start;
    if (status != MPI_STATUS_IGNORE) *status = actual;
    if (peer != MPI_PROC_NULL && error == MPI_SUCCESS) {
        int received, size;
        MPI_Get_count(&actual, type, &received);
        MPI_Type_size(type, &size);
        if (received == MPI_UNDEFINED) SimulationError("receive count/datatype mismatch");
        MessageMetric *metric = &message_metrics[actual.MPI_TAG];
        ++metric->received;
        metric->receive_bytes += (long long)received*size;
        metric->receive_api += elapsed;
        metric->receive_lifetime += elapsed;
    }
    return error;
}

static int MeasuredIsend(const void *buffer, int count, MPI_Datatype type,
                         int peer, int tag, MPI_Comm comm, MPI_Request *request) {
    double start = MPI_Wtime();
#ifdef EVCNS_SYNC_SEND
    int error = MPI_Issend(buffer, count, type, peer, tag, comm, request);
#else
    int error = MPI_Isend(buffer, count, type, peer, tag, comm, request);
#endif
    double elapsed = MPI_Wtime()-start;
    if (peer != MPI_PROC_NULL && error == MPI_SUCCESS) {
        message_metrics[tag].send_api += elapsed;
        TrackRequest(request, tag, type, count, true, start);
    }
    return error;
}

static int MeasuredIrecv(void *buffer, int count, MPI_Datatype type,
                         int peer, int tag, MPI_Comm comm, MPI_Request *request) {
    double start = MPI_Wtime();
    int error = MPI_Irecv(buffer, count, type, peer, tag, comm, request);
    double elapsed = MPI_Wtime()-start;
    if (peer != MPI_PROC_NULL && error == MPI_SUCCESS) {
        message_metrics[tag].receive_api += elapsed;
        TrackRequest(request, tag, type, count, false, start);
    }
    return error;
}

static int MeasuredTestall(int count, MPI_Request *requests, int *done, MPI_Status *statuses) {
    MPI_Request old[count > 0 ? count : 1];
    MPI_Status actual[count > 0 ? count : 1];
    int active = 0;
    for (int i=0; i<count; ++i) { old[i]=requests[i]; active += old[i]!=MPI_REQUEST_NULL; }
    double start=MPI_Wtime();
    int error=MPI_Testall(count,requests,done,actual);
    double share=active ? (MPI_Wtime()-start)/active : 0;
    for(int i=0;i<count;++i) AttributeProgress(&requests[i],share);
    if (*done && error==MPI_SUCCESS) for(int i=0;i<count;++i) {
        if(statuses!=MPI_STATUSES_IGNORE) statuses[i]=actual[i];
        FinishRequest(&requests[i],old[i],&actual[i]);
    }
    return error;
}

static int MeasuredWaitall(int count, MPI_Request *requests, MPI_Status *statuses) {
    MPI_Request old[count > 0 ? count : 1];
    MPI_Status actual[count > 0 ? count : 1];
    int active=0;
    for(int i=0;i<count;++i) { old[i]=requests[i]; active+=old[i]!=MPI_REQUEST_NULL; }
    double start=MPI_Wtime();
    int error=MPI_Waitall(count,requests,actual);
    double share=active ? (MPI_Wtime()-start)/active : 0;
    for(int i=0;i<count;++i) AttributeProgress(&requests[i],share);
    if(error==MPI_SUCCESS) for(int i=0;i<count;++i) {
        if(statuses!=MPI_STATUSES_IGNORE) statuses[i]=actual[i];
        FinishRequest(&requests[i],old[i],&actual[i]);
    }
    return error;
}

static void RecordRoundtrip(int kind, double started) {
    if (started < 0) SimulationError("reply arrived without an outstanding report");
    ++roundtrip_count[kind];
    roundtrip_seconds[kind] += MPI_Wtime()-started;
}

static void WriteMetrics(int rows,int cols,int version) {
    int rank,size;
    MPI_Comm_rank(MPI_COMM_WORLD,&rank);
    MPI_Comm_size(MPI_COMM_WORLD,&size);
    if(request_used) SimulationError("uncompleted tracked MPI requests at shutdown");
    const char *names[9]={"","C1_REQUEST","C2_RESPONSE","C3_REPORT","C4_BASE_REPLY",
                         "C5_TERMINATE","DISABLE","READY","TERMINATED"};
    char path[80];
    snprintf(path,sizeof(path),"message_metrics_rank_%d.csv",rank);
    FILE *file=fopen(path,"w");
    if(!file) SimulationError("cannot write message metrics");
    fprintf(file,"version,rank,role,rows,cols,tag,message,sent,received,send_bytes,receive_bytes,cancelled_receives,send_api_seconds,receive_api_seconds,progress_api_seconds,send_completion_seconds,receive_completion_seconds\n");
    long long counts[8*4],sums[8*4];
    for(int tag=1;tag<=8;++tag) {
        MessageMetric *m=&message_metrics[tag];
        fprintf(file,"v%d,%d,%s,%d,%d,%d,%s,%lld,%lld,%lld,%lld,%lld,%.9f,%.9f,%.9f,%.9f,%.9f\n",
                version,rank,rank==size-1?"base":"station",rows,cols,tag,names[tag],
                m->sent,m->received,m->send_bytes,m->receive_bytes,m->cancelled,
                m->send_api,m->receive_api,m->progress_api,m->send_lifetime,m->receive_lifetime);
        counts[(tag-1)*4]=m->sent;
        counts[(tag-1)*4+1]=m->received;
        counts[(tag-1)*4+2]=m->send_bytes;
        counts[(tag-1)*4+3]=m->receive_bytes;
    }
    fclose(file);
    snprintf(path,sizeof(path),"roundtrip_rank_%d.csv",rank);
    file=fopen(path,"w");
    if(!file) SimulationError("cannot write round-trip metrics");
    fprintf(file,"version,rank,operation,samples,total_seconds,mean_microseconds\n");
    for(int i=0;i<2;++i) {
        fprintf(file,"v%d,%d,%s,%lld,%.9f,",version,rank,i?"C3_C4_ROUNDTRIP":"C1_C2_ENQUIRY",
                roundtrip_count[i],roundtrip_seconds[i]);
        if(roundtrip_count[i]) fprintf(file,"%.6f\n",roundtrip_seconds[i]*1e6/roundtrip_count[i]);
        else fprintf(file,"NA\n");
    }
    fclose(file);
    MPI_Reduce(counts,sums,8*4,MPI_LONG_LONG_INT,MPI_SUM,size-1,MPI_COMM_WORLD);
    if(rank==size-1) {
        file=fopen("message_metrics_global.csv","w");
        FILE *log=fopen("log.txt","a");
        if(!file||!log) SimulationError("cannot write global metrics");
        fprintf(file,"version,rows,cols,tag,message,sent,received,send_bytes,receive_bytes\n");
        long long total=0;
        for(int tag=1;tag<=8;++tag) {
            long long *p=&sums[(tag-1)*4];
            if(p[0]!=p[1]||p[2]!=p[3]) SimulationError("sent/received metrics do not balance");
            total+=p[0];
            fprintf(file,"v%d,%d,%d,%d,%s,%lld,%lld,%lld,%lld\n",version,rows,cols,tag,names[tag],p[0],p[1],p[2],p[3]);
        }
        fprintf(log,"Actual completed point-to-point messages: %lld (includes tags 1..8; excludes MPI collectives)\n",total);
        fprintf(log,"Shutdown confirmed: READY=%lld TERMINATED=%lld\n",sums[6*4],sums[7*4]);
        fclose(file);
        fclose(log);
        printf("[Validation] All point-to-point sends/receives balanced. READY=%lld TERMINATED=%lld\n",sums[6*4],sums[7*4]);
    }
    free(request_table);
}

/* Instrumentation wrappers call the real MPI routines above. They preserve the
 * blocking/nonblocking distinction and do not add protocol messages. */
#define MPI_Send MeasuredSend
#define MPI_Recv MeasuredRecv
#define MPI_Isend MeasuredIsend
#define MPI_Irecv MeasuredIrecv
#define MPI_Testall MeasuredTestall
#define MPI_Waitall MeasuredWaitall
