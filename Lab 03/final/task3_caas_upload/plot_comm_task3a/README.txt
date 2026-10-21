Task 3a - V2 mesh communication analysis

Keep mesh_messages.csv and plot_task3a.py together.
Run: python plot_task3a.py
Dependency: python -m pip install matplotlib

The script reads the original mesh_messages.csv and selects V2.
It uses all 30 different mesh sizes and every V2 message type.
No input data is edited and no additional CSV is generated.

output_v2 contains only two figures:
01_application_call_times.png: C1 request, C2 response, C3 report, C4 base reply.
02_shutdown_call_times.png: TERMINATE, DISABLE, READY, TERMINATED.
Each figure has four panels; each panel has Send and Receive lines with 30 points.
X = number of charging stations (rows x columns); the base is one extra process.
Y = mean MPI send/receive call wall time per message, in microseconds.
Log axes are labelled. Grid dimensions and all original fields remain in the CSV.

Method: mean_send_api_us = total recorded send-call seconds / sent count * 1e6.
The receive mean is calculated similarly. A nonblocking call can return before
communication completes; these plots measure MPI call time, not delivery latency.
One run per size; random congestion and allocated node placement can affect times.
Explain trends and outliers alongside the graphs; no repeat error bars are available.
The CSV source is task3_results.tar.gz/results_mesh_2WZEgl/mesh_messages.csv.

VERSION at the top of the script defaults to v2. V3 also has two figures.
V1 adds a scheduling figure for its QUERY_GRANT and QUERY_DONE messages.
