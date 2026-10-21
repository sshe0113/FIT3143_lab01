#!/bin/bash
#SBATCH --job-name=evcns_v2_mesh
#SBATCH --time=00:05:00
#SBATCH --mem=16G
#SBATCH --cpus-per-task=7
#SBATCH --partition=defq
#SBATCH --output=%x-%j.out
set -euo pipefail

suite=mesh
repetitions=1
csv_header='version,suite,rows,cols,stations,processes,replicate,tag,message,sent,received,send_bytes,receive_bytes,send_call_time_seconds,receive_call_time_seconds,send_completion_seconds,receive_completion_seconds,mean_send_call_us,mean_receive_call_us,mean_send_completion_us,mean_receive_completion_us'

# No arguments: submit one five-minute job per size, linked to run sequentially.
# Run from the directory containing evcns_v2.c, utils.c, utils.h and both Slurm scripts.
if [[ $# -eq 0 ]]; then
    if [[ -n "${SLURM_JOB_ID:-}" ]]; then
        source_dir="$SLURM_SUBMIT_DIR"
    else
        source_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
    fi
    script="$source_dir/run_${suite}.slurm"
    for file in evcns_v2.c utils.c utils.h; do
        [[ -f "$source_dir/$file" ]] || { echo "$file is missing from $source_dir" >&2; exit 1; }
    done
    sizes=(2x2 2x3 2x4 3x3 2x5 3x4 2x7 3x5 4x4 3x6 4x5 3x7 2x11 4x6 5x5 2x13 3x9 4x7 5x6 4x8 3x11 2x17 5x7 6x6 2x19 3x13 5x8 6x7 4x11 5x9)

    # Match seven CPUs to the seven OpenMP threads, using actual node capacity.
    node_count=0
    per_node=16
    inventory="$(sinfo -N -h -p defq -o '%N %c %t')"
    while read -r node cores state; do
        [[ -n "${node:-}" ]] || continue
        case "$state" in *down*|*drain*|*fail*) continue;; esac
        [[ "$cores" =~ ^[0-9]+$ ]] || { echo "Cannot read CPU count for $node" >&2; exit 1; }
        slots=$((cores / 7))
        ((slots > 0)) || continue
        if ((slots < per_node)); then per_node=$slots; fi
        node_count=$((node_count + 1))
    done <<< "$inventory"
    capacity=$((node_count * per_node))
    max_tasks=0
    for size in "${sizes[@]}"; do
        rows=${size%x*}; cols=${size#*x}
        tasks=$((rows * cols + 1))
        if ((tasks > max_tasks)); then max_tasks=$tasks; fi
    done
    if ((max_tasks > capacity)); then
        echo "Insufficient capacity for all 30 sizes: need $max_tasks MPI tasks, have $capacity with 7 CPUs/task. No jobs submitted." >&2
        exit 1
    fi

    batch_dir="$(mktemp -d "$source_dir/results_v2_${suite}_XXXXXX")"
    cp -- "$source_dir/evcns_v2.c" "$source_dir/utils.c" "$source_dir/utils.h" "$batch_dir/"
    cp -- "$script" "$batch_dir/run_${suite}.slurm"
    printf '%s\n' "$csv_header" > "$batch_dir/${suite}_results.csv"
    previous=""
    index=0
    for size in "${sizes[@]}"; do
        rows=${size%x*}; cols=${size#*x}
        tasks=$((rows * cols + 1))
        nodes=$(((tasks + per_node - 1) / per_node))
        index=$((index + 1))
        options=(--parsable --chdir="$batch_dir" --nodes="$nodes" --ntasks="$tasks"
                 --ntasks-per-node="$per_node" --cpus-per-task=7
                 --job-name="v2_${suite}_${size}" --output="$batch_dir/${size}_%j.out")
        if [[ -n "$previous" ]]; then options+=(--dependency="afterany:$previous"); fi
        job="$(sbatch "${options[@]}" "$batch_dir/run_${suite}.slurm" "$rows" "$cols" "$batch_dir" "$index")"
        previous=${job%%;*}
        [[ "$previous" =~ ^[0-9]+$ ]] || { echo "Unexpected sbatch result: $job" >&2; exit 1; }
        printf '%s %s %s\n' "$index" "$size" "$previous" >> "$batch_dir/jobs.txt"
        echo "$suite $index/30: $size, job $previous"
    done
    echo "All 30 sizes submitted. Each size runs $repetitions times; each job has a 5-minute limit."
    echo "Results: $batch_dir/${suite}_results.csv"
    exit 0
fi

# Worker mode: these four arguments are supplied automatically above.
[[ $# -eq 4 ]] || { echo 'Expected rows cols batch_directory case_number' >&2; exit 1; }
rows=$1; cols=$2; batch_dir=$3; case_number=$4
stations=$((rows * cols))
tasks=$((stations + 1))
[[ "${SLURM_NTASKS:-0}" -eq "$tasks" ]] || { echo 'MPI allocation does not match the grid' >&2; exit 1; }
job_dir="$batch_dir/${rows}x${cols}/job_${SLURM_JOB_ID}"
mkdir -p "$job_dir"
module load openmpi/4.1.5-gcc-11.2.0-ux65npg
mpicc -std=gnu11 -O2 -fopenmp -o "$job_dir/evcns" "$batch_dir/evcns_v2.c" "$batch_dir/utils.c"
export OMP_NUM_THREADS=$SLURM_CPUS_PER_TASK
export OMP_DYNAMIC=FALSE
export OMP_PLACES=cores
export OMP_PROC_BIND=close
# Match ReadArguments in utils.c; override this for the predefined-period tests.
export EVCNS_PRED_PERIOD="${EVCNS_PRED_PERIOD:-1.5}"

for ((rep=1; rep<=repetitions; rep++)); do
    run_dir="$job_dir/rep_$rep"
    mkdir "$run_dir"
    cd "$run_dir"
    srun --exact --kill-on-bad-exit=1 --cpu-bind=cores -n "$tasks" "$job_dir/evcns" "$rows" "$cols" > stdout.txt 2>&1
    grep -Fq "Shutdown confirmed: READY=$stations TERMINATED=$stations" log.txt

    # Convert the eight plain METRIC records from C into CSV.
    awk -v header="$csv_header" -v suite="$suite" -v rows="$rows" -v cols="$cols" -v rep="$rep" '
        BEGIN {
            print header
            names[1]="C1_REQUEST"; names[2]="C2_RESPONSE"; names[3]="C3_REPORT"; names[4]="C4_BASE_REPLY"
            names[5]="C5_TERMINATE"; names[6]="DISABLE"; names[7]="READY"; names[8]="TERMINATED"
        }
        $1 == "METRIC" {
            tag=$5
            if (NF!=13 || $2!=2 || $3!=rows || $4!=cols || tag<1 || tag>8 || seen[tag]++ || $6!=$7 || $8!=$9) { bad=1; next }
            n++
            sc=($6>0 ? sprintf("%.6f",$10*1000000/$6) : "")
            rc=($7>0 ? sprintf("%.6f",$11*1000000/$7) : "")
            sl=($6>0 ? sprintf("%.6f",$12*1000000/$6) : "")
            rl=($7>0 ? sprintf("%.6f",$13*1000000/$7) : "")
            printf "v%s,%s,%d,%d,%d,%d,%d,%d,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s\n", \
                $2,suite,rows,cols,rows*cols,rows*cols+1,rep,tag,names[tag],$6,$7,$8,$9,$10,$11,$12,$13,sc,rc,sl,rl
        }
        END { if (bad || n!=8) { print "Invalid or missing METRIC records" > "/dev/stderr"; exit 1 } }
    ' log.txt > metrics.csv
    # Jobs in this batch run sequentially, so only one job appends at a time.
    tail -n +2 metrics.csv >> "$batch_dir/${suite}_results.csv"
    echo "$suite ${rows}x${cols}: repetition $rep/$repetitions complete"
done

if [[ "$case_number" -eq 30 ]]; then
    records=$(awk 'END { print NR-1 }' "$batch_dir/${suite}_results.csv")
    expected=$((30 * repetitions * 8))
    if [[ "$records" -ne "$expected" ]]; then
        echo "INCOMPLETE: expected $expected records, found $records. Review jobs.txt and job outputs." >&2
        exit 1
    fi
    echo "COMPLETE: 30 sizes, $repetitions repetitions each, $records message rows."
    echo "$batch_dir/${suite}_results.csv"
fi
