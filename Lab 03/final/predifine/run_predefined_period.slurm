#!/bin/bash
#SBATCH --job-name=ev_period_test
#SBATCH --partition=defq
#SBATCH --nodes=3
#SBATCH --ntasks=17
#SBATCH --ntasks-per-node=8
#SBATCH --cpus-per-task=2
#SBATCH --time=00:05:00
#SBATCH --output=ev-period-%j.out

# Place beside evcns_v2.c, utils.c and utils.h, then:
#   sbatch run_predefined_period.slurm
# One job runs V2 on a 4x4 mesh for each period, once per value.
# Seven application threads share two allocated CPUs per process, as before.
set -euo pipefail
: "${SLURM_JOB_ID:?Submit this script with sbatch}"
source_dir=${SLURM_SUBMIT_DIR:?}
periods=(1.5 3 6)
rows=4
cols=4
stations=$((rows*cols))
tasks=$((stations+1))
[[ ${SLURM_NTASKS:-0} -eq $tasks && ${SLURM_CPUS_PER_TASK:-0} -eq 2 ]] || {
    echo 'This test requires 17 MPI tasks with 2 CPUs per task.' >&2
    exit 1
}
for file in evcns_v2.c utils.c utils.h; do
    [[ -f "$source_dir/$file" ]] || { echo "Missing $file beside this script" >&2; exit 1; }
done

batch=$(mktemp -d "$source_dir/results_period_XXXXXX")
period=setup
trap 'code=$?; printf "FAILED period=%s exit=%s\n" "$period" "$code" | tee "$batch/FAILED.txt" >&2; exit "$code"' ERR
cp -- "$source_dir/evcns_v2.c" "$source_dir/utils.c" "$source_dir/utils.h" "$batch/"
cp -- "${BASH_SOURCE[0]}" "$batch/run_predefined_period.slurm"
cd "$batch"
sha256sum evcns_v2.c utils.c utils.h > source_sha256.txt
printf 'version=v2\nrows=4\ncols=4\nperiods_seconds=1.5 3 6\nrepetitions=1\ncpus_per_task=2\nomp_threads_per_station=7\njob_id=%s\n' "$SLURM_JOB_ID" > settings.txt

if [[ -n "${EVCNS_MPI_MODULE-openmpi/4.1.5-gcc-11.2.0-ux65npg}" ]]; then
    module load "${EVCNS_MPI_MODULE-openmpi/4.1.5-gcc-11.2.0-ux65npg}"
fi
export OMP_NUM_THREADS=7 OMP_DYNAMIC=FALSE OMP_PLACES=cores OMP_PROC_BIND=close
mpicc --version > compiler.txt
mpirun --version > mpi.txt
scontrol show job "$SLURM_JOB_ID" > allocation.txt
mpicc -std=gnu11 -O2 -Wall -Wextra -fopenmp evcns_v2.c utils.c -o evcns_v2 > build.txt 2>&1

printf 'version,rows,cols,predefined_period,alerts,recent_neighbour_shortcuts,reports_with_recommendations,reports_without_recommendations,total_recommended_nodes,total_messages,base_mpi_call_seconds,base_computation_seconds,station_avg_mpi_call_seconds,station_avg_computation_seconds\n' > period_results.csv
printf 'V2 predefined period tests: 4x4 mesh; one run per value.\n' > all_periods.log

for period in "${periods[@]}"; do
    mkdir "$batch/period_$period"
    cd "$batch/period_$period"
    export EVCNS_PRED_PERIOD=$period
    echo "Running V2 4x4: predefined period = $period seconds"
    srun --exact --kill-on-bad-exit=1 --cpu-bind=cores --nodes=3 --ntasks="$tasks" \
        --ntasks-per-node=8 --cpus-per-task=2 "$batch/evcns_v2" "$rows" "$cols" \
        > stdout.txt 2> stderr.txt
    printf '\n===== V2 | Mesh 4x4 | Period %s seconds =====\n' "$period" >> "$batch/all_periods.log"
    cat log.txt stdout.txt stderr.txt >> "$batch/all_periods.log"
    grep -Fxq "Shutdown confirmed: READY=$stations TERMINATED=$stations" log.txt

    # Summarise only after validating the actual period, report/reply counts,
    # balanced communication and the complete 16-node shutdown handshake.
    awk -v period="$period" -v stations="$stations" '
        function number(x) {return x ~ /^[0-9]+([.][0-9]+)?$/}
        /^Configuration:/ {
            configs++
            for(i=1;i<=NF;i++) if($i ~ /^predefined_period=/) {split($i,a,"=");actual=a[2]}
        }
        /\| ALERT \[/ {alert_lines++}
        /^Recommended nodes:/ {
            decisions++; candidates+=$3
            if($3>0) with_nodes++; else without_nodes++
            if($0 ~ /recent-neighbour shortcut=1$/) {shortcuts++; if($3!=0) bad=1}
        }
        /^Total Congestion Alerts:/ {alerts=$4;alert_summaries++}
        /^Total Simulation Time:/ {duration=$4}
        /^\[Base Station\] Overall:/ {
            line=$0;sub(/^.*Overall: /,"",line);gsub(/s|[|]/,"",line);split(line,a)
            base_comm=a[3];base_comp=a[5];base_summaries++
        }
        /^\[Charging Nodes Average\] Overall:/ {
            line=$0;sub(/^.*Overall: /,"",line);gsub(/s|[|]/,"",line);split(line,a)
            node_comm=a[3];node_comp=a[5];node_summaries++
        }
        $1=="METRIC" {
            tag=$5
            if(NF!=13 || $2!=2 || $3!=4 || $4!=4 || tag<1 || tag>8 || seen[tag]++ || $6!=$7 || $8!=$9) bad=1
            for(i=6;i<=13;i++) if($i<0) bad=1
            if(tag>=5 && tag<=8 && $6!=stations) bad=1
            sent[tag]=$6;messages+=$6;metrics++
        }
        END {
            if(bad || configs!=1 || !number(actual) || actual+0!=period+0 || metrics!=8 ||
               alert_summaries!=1 || alerts<=0 || alerts!=alert_lines || alerts!=decisions ||
               alerts!=sent[3] || sent[1]!=sent[2] || sent[4]!=alerts+with_nodes ||
               base_summaries!=1 || node_summaries!=1 || duration<30 ||
               !number(base_comm) || !number(base_comp) || !number(node_comm) || !number(node_comp)) {
                print "Invalid or incomplete period-test results" > "/dev/stderr";exit 1
            }
            printf "v2,4,4,%s,%d,%d,%d,%d,%d,%d,%s,%s,%s,%s\n",period,alerts,shortcuts,with_nodes,without_nodes,candidates,messages,base_comm,base_comp,node_comm,node_comp
        }
    ' log.txt stdout.txt >> "$batch/period_results.csv"
    printf 'exit=0\npredefined_period=%s\nREADY=16\nTERMINATED=16\nmessage_counts_and_bytes=balanced\n' "$period" > PASSED.txt
    echo "PASS: period $period seconds"
done

[[ $(awk 'END {print NR-1}' "$batch/period_results.csv") -eq 3 ]]
printf 'COMPLETE: V2, 4x4 mesh, periods 1.5 / 3 / 6 seconds; 3 validated simulations.\n' | tee "$batch/COMPLETE.txt"
echo "Results: $batch"
echo 'Compare period_results.csv with the recommendation details in each log.txt.'
echo 'MPI call time is not end-to-end latency. Each period uses one random workload sample.'
