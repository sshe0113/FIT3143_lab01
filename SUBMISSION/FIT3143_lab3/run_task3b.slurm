#!/bin/bash
#SBATCH --job-name=task3b_line
#SBATCH --partition=defq
#SBATCH --time=00:20:00
#SBATCH --cpus-per-task=2
#SBATCH --output=%x-%A_%a.out
set -euo pipefail

# INPUT FILES ONLY: this script, evcns_v1.c, evcns_v2.c, evcns_v3.c, utils.c, utils.h.
# No Python, external helper scripts, or changes to the C/header files.
# Launch from the folder containing those files:
#   bash run_task3b.slurm
# Or: sbatch run_task3b.slurm (submit from that folder).
# Optional: CPUS_PER_TASK=2 REPETITIONS=3 PARTITION=defq bash run_task3b.slurm
# One array covers all versions/repeats. Each element loops all 30 grid sizes for one version.

suite=line
script_name=run_task3b.slurm
metric_header='version,suite,rows,cols,stations,processes,replicate,predefined_period,tag,message,sent,received,send_bytes,receive_bytes,send_call_seconds,receive_call_seconds,observed_send_seconds,observed_receive_seconds,mean_send_api_us,mean_receive_api_us,mean_observed_send_us,mean_observed_receive_us'
run_header='version,suite,rows,cols,stations,processes,replicate,predefined_period,base_overall_seconds,base_mpi_call_seconds,base_computation_section_seconds,station_avg_overall_seconds,station_avg_mpi_call_seconds,station_avg_computation_section_seconds,alerts,total_messages,total_send_bytes'

sizes=(1x2 1x3 1x4 1x5 1x6 1x7 1x8 1x9 1x10 1x11 1x12 1x13 1x14 1x15 1x16 1x17 1x18 1x19 1x20 1x21 1x22 1x23 1x24 1x25 1x26 1x27 1x28 1x29 1x30 1x31)

# Run only after all main-array elements finish (indices need not execute in order).
summarise_batch() {
    local batch=$1 repeats=$2 count records
    count=$(awk 'END {print NR-1}' "$batch/${suite}_runs.csv")
    records=$(awk 'END {print NR-1}' "$batch/${suite}_messages.csv")
    if ((count!=30*repeats*3 || records!=30*repeats*26)); then
        echo "INCOMPLETE: runs=$count messages=$records. Check jobs.csv, FAILED.txt and job output." | tee "$batch/INCOMPLETE.txt" >&2
        exit 1
    fi
    # Verify every planned shape/version/repetition, not just the total row count.
    if ! awk -F, -v shapes="${sizes[*]}" -v repeats="$repeats" '
        BEGIN {n=split(shapes,a," ");for(i=1;i<=n;i++)allowed[a[i]]=1}
        NR>1 {
            shape=$3 "x" $4
            if(!allowed[shape] || $1 !~ /^v[123]$/ || $7<1 || $7>repeats || seen[$1,shape,$7]++) bad=1
            groups[$1,shape]++
        }
        END {for(g in groups){count++;if(groups[g]!=repeats)bad=1}exit (bad || count!=90)}
    ' "$batch/${suite}_runs.csv"; then
        echo 'INCOMPLETE: duplicate/missing grid, version or repetition' | tee "$batch/INCOMPLETE.txt" >&2
        exit 1
    fi
    awk -F, '
        BEGIN {OFS=","; print "version,suite,rows,cols,stations,predefined_period,tag,message,total_messages,send_api_us_mean,send_api_us_sd,send_api_sampled_repeats,receive_api_us_mean,receive_api_us_sd,receive_api_sampled_repeats,observed_send_us_mean,observed_send_us_sd,observed_send_sampled_repeats,observed_receive_us_mean,observed_receive_us_sd,observed_receive_sampled_repeats"}
        NR>1 {key=$1 FS $2 FS $3 FS $4 FS $5 FS $8 FS $9 FS $10; keys[key]=1;msgs[key]+=$11
            for(i=19;i<=22;i++)if($i!=""){n[key,i]++;s[key,i]+=$i;q[key,i]+=$i*$i}}
        END {for(key in keys){printf "%s,%d",key,msgs[key];for(i=19;i<=22;i++){c=n[key,i];if(!c){printf ",,,0";continue}mean=s[key,i]/c;printf ",%.6f,",mean;if(c>1){var=(q[key,i]-s[key,i]*s[key,i]/c)/(c-1);printf "%.6f",sqrt(var>0?var:0)}printf ",%d",c}printf "\n"}}
    ' "$batch/${suite}_messages.csv" > "$batch/${suite}_message_summary.csv"
    awk -F, '
        BEGIN {OFS=","}
        NR==1 {printf "version,suite,rows,cols,stations,predefined_period,repeats";for(i=9;i<=17;i++)printf ",%s_mean,%s_sd",$i,$i;printf "\n";next}
        {key=$1 FS $2 FS $3 FS $4 FS $5 FS $8;keys[key]=1;n[key]++;for(i=9;i<=17;i++){s[key,i]+=$i;q[key,i]+=$i*$i}}
        END {for(key in keys){c=n[key];printf "%s,%d",key,c;for(i=9;i<=17;i++){printf ",%.6f,",s[key,i]/c;if(c>1){var=(q[key,i]-s[key,i]*s[key,i]/c)/(c-1);printf "%.6f",sqrt(var>0?var:0)}}printf "\n"}}
    ' "$batch/${suite}_runs.csv" > "$batch/${suite}_run_summary.csv"
    printf 'COMPLETE: 30 sizes, 3 versions, %d repeats; %d validated simulations.\n' "$repeats" "$count" | tee "$batch/COMPLETE.txt"
    echo 'CSV timings are MPI API time / observed completion, not isolated one-way latency.'
    echo 'Comp sums computation-section thread CPU seconds; excludes output, lock acquisition, sleep and MPI communication.'
}

if [[ "${1:-}" == --summary ]]; then
    [[ $# -eq 3 ]] || exit 2
    summarise_batch "$2" "$3"
    exit 0
fi

if [[ "${1:-}" != --batch ]]; then
    [[ $# -eq 0 ]] || { echo "Usage: bash $script_name" >&2; exit 2; }
    if [[ -n "${SLURM_JOB_ID:-}" ]]; then
        source_dir=${SLURM_SUBMIT_DIR:?}
    else
        source_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
    fi
    for file in evcns_v1.c evcns_v2.c evcns_v3.c utils.c utils.h "$script_name"; do
        [[ -f "$source_dir/$file" ]] || { echo "Missing $file: place the five latest final-folder source files beside this Slurm file." >&2; exit 1; }
    done
    repeats=${REPETITIONS:-3}
    [[ "$repeats" =~ ^[1-9][0-9]*$ ]] || { echo 'REPETITIONS must be positive' >&2; exit 1; }
    cpus_per_task=${CPUS_PER_TASK:-2}
    [[ "$cpus_per_task" =~ ^[1-9][0-9]*$ ]] || { echo 'CPUS_PER_TASK must be positive' >&2; exit 1; }
    period=${EVCNS_PRED_PERIOD:-1.5}
    awk -v x="$period" 'BEGIN {exit !(x ~ /^[0-9]+([.][0-9]+)?$/ && x+0>0)}' || { echo 'Use a positive decimal EVCNS_PRED_PERIOD' >&2; exit 1; }
    partition=${PARTITION:-defq}

    # Eligible total capacity, not a promise of immediately idle nodes.
    node_count=0
    per_node=2147483647
    inventory=$(sinfo -N -h -p "$partition" -o '%N %c %t')
    declare -A seen_nodes=()
    while read -r node cores state; do
        [[ -n "${node:-}" && -z "${seen_nodes[$node]:-}" ]] || continue
        seen_nodes[$node]=1
        case "$state" in *down*|*drain*|*fail*|*maint*|*unk*) continue;; esac
        [[ "$cores" =~ ^[0-9]+$ ]] || { echo "Invalid CPU count for $node" >&2; exit 1; }
        slots=$((cores/cpus_per_task))
        ((slots>0)) || continue
        if ((slots<per_node)); then per_node=$slots; fi
        node_count=$((node_count+1))
    done <<< "$inventory"
    max_tasks=0
    for shape in "${sizes[@]}"; do
        r=${shape%x*}; c=${shape#*x}; tasks=$((r*c+1))
        if ((tasks>max_tasks)); then max_tasks=$tasks; fi
    done
    if ((node_count==0 || max_tasks>node_count*per_node)); then
        echo "Insufficient eligible capacity for all 30 $suite sizes with $cpus_per_task CPUs/task; no jobs submitted." >&2
        exit 1
    fi

    echo "Eligible capacity: $node_count nodes, up to $per_node MPI tasks/node, $cpus_per_task CPUs/task."
    if ((cpus_per_task<7)); then
        echo "Each station keeps 7 OpenMP threads sharing $cpus_per_task allocated CPUs; use the same setting for V1/V2/V3."
    fi

    # Exact source copies freeze this batch; originals are never edited.
    batch=$(mktemp -d "$source_dir/results_${suite}_XXXXXX")
    cp -- "$source_dir/evcns_v1.c" "$source_dir/evcns_v2.c" "$source_dir/evcns_v3.c" "$source_dir/utils.c" "$source_dir/utils.h" "$batch/"
    cp -- "$source_dir/$script_name" "$batch/$script_name"
    (cd "$batch"; sha256sum evcns_v1.c evcns_v2.c evcns_v3.c utils.c utils.h > source_sha256.txt)
    printf '%s\n' "$metric_header" > "$batch/${suite}_messages.csv"
    printf '%s\n' "$run_header" > "$batch/${suite}_runs.csv"
    printf 'suite=%s\nrepetitions=%s\nperiod=%s\npartition=%s\ncpus_per_task=%s\nomp_threads_per_station=7\n' "$suite" "$repeats" "$period" "$partition" "$cpus_per_task" > "$batch/settings.txt"
    # One main array ID: V1/V2/V3 each run all 30 sizes, one repeat per element.
    # Each element needs ~15 minutes of simulation, so reserve the permitted 20 minutes.
    nodes=$(((max_tasks+per_node-1)/per_node))
    elements=$((3*repeats))
    printf 'suite=%s versions=V1,V2,V3 sizes=30 repeats=%s CPUs/task=%s OpenMP_threads/station=7\n' "$suite" "$repeats" "$cpus_per_task" > "$batch/all_${suite}.log"
    raw=$(sbatch --parsable --partition="$partition" --chdir="$batch" --nodes="$nodes" --ntasks="$max_tasks" \
        --ntasks-per-node="$per_node" --cpus-per-task="$cpus_per_task" --time=00:20:00 --array="1-${elements}%1" \
        --job-name="${suite}_all_versions" --output="$batch/${suite}_%A_%a.out" \
        "$batch/$script_name" --batch "$batch" "$repeats" "$period" "$cpus_per_task" "$per_node")
    previous=${raw%%;*}
    [[ "$previous" =~ ^[0-9]+$ ]] || { echo "Unexpected sbatch result: $raw" >&2; exit 1; }
    printf 'array,%s\n' "$previous" >> "$batch/jobs.csv"
    echo "Submitted array $previous: $elements elements; each runs one version across all 30 $suite sizes."
    # A lightweight summary job checks coverage after the ENTIRE main array.
    final_raw=$(sbatch --parsable --partition="$partition" --chdir="$batch" --nodes=1 --ntasks=1 \
        --ntasks-per-node=1 --cpus-per-task=1 --dependency="afterany:$previous" \
        --job-name="${suite}_summary" --output="$batch/summary_%j.out" \
        "$batch/$script_name" --summary "$batch" "$repeats")
    printf 'summary,%s\n' "$final_raw" >> "$batch/jobs.csv"
    echo "Results: $batch"
    echo 'Array elements run sequentially. Each element has a 20-minute limit (~15 minutes simulation).'
    echo 'Inspect COMPLETE.txt, raw logs and CSVs after all arrays finish. Scheduler/QOS limits may constrain submission.'
    exit 0
fi

# Array element: one version and one repetition, looping all 30 sizes.
[[ $# -eq 6 ]] || { echo 'Invalid batch arguments' >&2; exit 2; }
batch=$2; repeats=$3; period=$4; cpus_per_task=$5; per_node=$6
element=${SLURM_ARRAY_TASK_ID:?}
rep=$(((element-1)/3+1))
offset=$(((element-1)%3))
version=$(((rep-1+offset)%3+1))
((rep>=1 && rep<=repeats)) || { echo 'Invalid repetition index' >&2; exit 1; }
[[ "${SLURM_CPUS_PER_TASK:-0}" -eq "$cpus_per_task" ]] || { echo 'Incorrect CPU allocation' >&2; exit 1; }
job_dir="$batch/version_$version/rep_$rep"
mkdir -p "$(dirname -- "$job_dir")"
mkdir "$job_dir"
trap 'code=$?; printf "FAILED exit=%s version=%s\n" "$code" "${version:-setup}" > "$job_dir/FAILED.txt"; exit "$code"' ERR
if [[ -n "${EVCNS_MPI_MODULE-openmpi/4.1.5-gcc-11.2.0-ux65npg}" ]]; then
    module load "${EVCNS_MPI_MODULE-openmpi/4.1.5-gcc-11.2.0-ux65npg}"
fi
# Seven application threads are required by the unchanged C code, independently of allocated CPUs.
export OMP_NUM_THREADS=7 OMP_DYNAMIC=FALSE OMP_PLACES=cores OMP_PROC_BIND=close
export EVCNS_PRED_PERIOD=$period
mpicc --version > "$job_dir/compiler.txt"
mpirun --version > "$job_dir/mpi.txt"
scontrol show job "$SLURM_JOB_ID" > "$job_dir/allocation.txt"
mpicc -std=gnu11 -O2 -Wall -Wextra -fopenmp "$batch/evcns_v${version}.c" "$batch/utils.c" -o "$job_dir/evcns_v${version}" > "$job_dir/build.txt" 2>&1

for shape in "${sizes[@]}"; do
    rows=${shape%x*}; cols=${shape#*x}
    stations=$((rows*cols)); tasks=$((stations+1))
    ((tasks<=SLURM_NTASKS)) || { echo 'Insufficient MPI allocation' >&2; exit 1; }
    step_nodes=$(((tasks+per_node-1)/per_node))
    step_per_node=$per_node
    if ((tasks<step_per_node)); then step_per_node=$tasks; fi
    run_dir="$batch/${rows}x${cols}/rep_$rep/v$version"
    mkdir -p "$(dirname -- "$run_dir")"
    mkdir "$run_dir"
    cd "$run_dir"
    printf '\n===== V%s | Grid %sx%s | Repeat %s | CPUs/task %s =====\n' "$version" "$rows" "$cols" "$rep" "$cpus_per_task" >> "$batch/all_${suite}.log"
    # The application still launches exactly one process per station plus one base.
    srun --exact --kill-on-bad-exit=1 --cpu-bind=cores --nodes="$step_nodes" --ntasks-per-node="$step_per_node" --cpus-per-task="$cpus_per_task" -n "$tasks" "$job_dir/evcns_v${version}" "$rows" "$cols" > stdout.txt 2> stderr.txt
    cat log.txt >> "$batch/all_${suite}.log"
    printf '\n[Application stdout]\n' >> "$batch/all_${suite}.log"
    cat stdout.txt >> "$batch/all_${suite}.log"
    if [[ -s stderr.txt ]]; then
        printf '\n[Application stderr]\n' >> "$batch/all_${suite}.log"
        cat stderr.txt >> "$batch/all_${suite}.log"
    fi
    grep -Fxq "Shutdown confirmed: READY=$stations TERMINATED=$stations" log.txt

    # Message count/byte validation and per-message API/observed-completion means.
    awk -v header="$metric_header" -v suite="$suite" -v rows="$rows" -v cols="$cols" -v rep="$rep" -v period="$period" -v v="$version" '
        BEGIN {
            print header; expected=(v==1 ? 10 : 8)
            names[1]="C1_REQUEST"; names[2]="C2_RESPONSE"; names[3]="C3_REPORT"; names[4]="C4_BASE_REPLY"
            names[5]="TERMINATE"; names[6]="DISABLE"; names[7]="READY"; names[8]="TERMINATED"
            names[9]="QUERY_GRANT"; names[10]="QUERY_DONE"
        }
        $1=="METRIC" {
            tag=$5
            if(NF!=13 || $2!=v || $3!=rows || $4!=cols || tag<1 || tag>expected || seen[tag]++ || $6!=$7 || $8!=$9) {bad=1; next}
            for(i=6;i<=13;i++) if($i<0) bad=1
            if(tag>=5 && tag<=8 && $6!=rows*cols) bad=1
            sent[tag]=$6; n++
            sc=($6>0 ? sprintf("%.6f",$10*1e6/$6) : "")
            rc=($7>0 ? sprintf("%.6f",$11*1e6/$7) : "")
            sl=($6>0 ? sprintf("%.6f",$12*1e6/$6) : "")
            rl=($7>0 ? sprintf("%.6f",$13*1e6/$7) : "")
            printf "v%d,%s,%d,%d,%d,%d,%d,%s,%d,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s\n",v,suite,rows,cols,rows*cols,rows*cols+1,rep,period,tag,names[tag],$6,$7,$8,$9,$10,$11,$12,$13,sc,rc,sl,rl
        }
        END {if(bad || n!=expected || sent[1]!=sent[2] || (v==1 && sent[9]!=sent[10])) {print "Invalid message metrics" > "/dev/stderr";exit 1}}
    ' log.txt > metrics.csv

    # Overall/Comm are wall times; Comp is accumulated thread CPU time, not their difference.
    awk -v header="$run_header" -v suite="$suite" -v rows="$rows" -v cols="$cols" -v rep="$rep" -v period="$period" -v v="$version" '
        BEGIN {print header}
        /^\[Base Station\] Overall:/ {line=$0; sub(/^.*Overall: /,"",line);gsub(/s|[|]/,"",line);split(line,a);bo=a[1];bc=a[3];bx=a[5];nb++}
        /^\[Charging Nodes Average\] Overall:/ {line=$0;sub(/^.*Overall: /,"",line);gsub(/s|[|]/,"",line);split(line,a);so=a[1];sc=a[3];sx=a[5];ns++}
        /^Total Congestion Alerts:/ {alerts=$4;na++}
        /^Total Simulation Time:/ {duration=$4}
        $1=="METRIC" {messages+=$6;bytes+=$8;if($5==3) reports=$6}
        END {
            if(nb!=1 || ns!=1 || na!=1 || duration<30 || alerts!=reports || bo<0 || bc<0 || bx<0 || so<0 || sc<0 || sx<0) {print "Missing/invalid overall timing or alert metrics" > "/dev/stderr";exit 1}
            printf "v%d,%s,%d,%d,%d,%d,%d,%s,%s,%s,%s,%s,%s,%s,%d,%d,%d\n",v,suite,rows,cols,rows*cols,rows*cols+1,rep,period,bo,bc,bx,so,sc,sx,alerts,messages,bytes
        }
    ' stdout.txt log.txt > run.csv
    printf 'exit=0\nREADY=%s\nTERMINATED=%s\nmessage_counts_and_bytes=balanced\n' "$stations" "$stations" > PASSED.txt
    # Array concurrency is one, so there is only one CSV/log writer per batch.
    tail -n +2 metrics.csv >> "$batch/${suite}_messages.csv"
    tail -n +2 run.csv >> "$batch/${suite}_runs.csv"
    echo "PASS $suite ${rows}x${cols}, repeat $rep/$repeats, V$version"
done

