#!/usr/bin/env python3
"""Plan small, independent CAAS jobs. Default is a dry run; --submit queues jobs."""
import argparse, csv, json, math, os, subprocess, sys
from pathlib import Path

def read_capacity(partition):
    result=subprocess.run(['sinfo','-N','-h','-p',partition,'-o','%N|%c|%t'],capture_output=True,text=True,check=True)
    nodes={}
    for line in result.stdout.splitlines():
        fields=line.strip().split('|')
        if len(fields)!=3 or not fields[1].isdigit(): raise SystemExit('Cannot read CPU counts. Run: sinfo -N -o "%N %c %t"')
        if any(state in fields[2].lower() for state in ['down','drain','fail']): continue
        nodes[fields[0]]=int(fields[1])
    if not nodes: raise SystemExit('No usable nodes were reported for the partition.')
    return min(nodes.values()),len(nodes)

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--suite',choices=['mesh','line','period'],required=True)
    ap.add_argument('--submit',action='store_true')
    ap.add_argument('--partition',default='defq')
    ap.add_argument('--cpus-per-task',type=int,default=7)
    ap.add_argument('--cpus-per-node',type=int,help='Optional explicit capacity, otherwise read sinfo')
    ap.add_argument('--max-nodes',type=int,help='Optional node limit, otherwise use available partition inventory')
    ap.add_argument('--max-new-jobs',type=int,default=5)
    ap.add_argument('--replicates',type=int,default=3)
    ap.add_argument('--seconds',type=float,default=30)
    ap.add_argument('--run-id',default='v1_task3')
    args=ap.parse_args()
    if min(args.cpus_per_task,args.replicates,args.max_new_jobs)<1 or args.seconds<=0: ap.error('Counts and duration must be positive.')
    if args.replicates*(args.seconds+15)+30>270: ap.error('The requested repetitions leave too little margin within a five-minute job.')
    if '/' in args.run_id or '\\' in args.run_id or args.run_id in ['.','..']: ap.error('run-id must be one directory name')
    folder=Path(__file__).resolve().parent
    if args.cpus_per_node is None or args.max_nodes is None:
        detected_cores,detected_nodes=read_capacity(args.partition)
    cores=args.cpus_per_node if args.cpus_per_node is not None else detected_cores
    nodes=args.max_nodes if args.max_nodes is not None else detected_nodes
    # Retain the user's prior maximum of 16 MPI tasks/node, also respecting CPU allocation.
    per_node=min(16,cores//args.cpus_per_task)
    if per_node<1 or nodes<1: ap.error('Insufficient CPU/node capacity.')
    if args.cpus_per_task<7: print('NOTE: seven OpenMP threads will share fewer than seven allocated CPUs; record this in the analysis.')
    with (folder/'experiment_cases.csv').open(newline='') as stream: cases=[c for c in csv.DictReader(stream) if c['suite']==args.suite]
    required=max(int(c['rows'])*int(c['cols'])+1 for c in cases)
    print(f'{len(cases)} {args.suite} cases, {args.replicates} repeats/case; each job <= 00:05:00.')
    print(f'Capacity: {cores} CPUs/node, {nodes} nodes, {per_node} MPI tasks/node; largest case requires {required} tasks.')
    if required>per_node*nodes:
        raise SystemExit(f'Capacity is insufficient: need {required} tasks, have {per_node*nodes}. Adjust the recorded CPU allocation or obtain more resources; no sizes were silently omitted.')
    output=folder/'task3_results'/args.run_id
    state_path=output/f'{args.suite}_submissions.json'
    state=json.loads(state_path.read_text()) if state_path.exists() else {}
    submitted=0
    for case in cases:
        key=case['case_id']
        if key in state:
            print(f'Skip {key}: previously submitted as job {state[key]["job_id"]}')
            continue
        tasks=int(case['rows'])*int(case['cols'])+1
        job_nodes=math.ceil(tasks/per_node)
        command=['sbatch','--parsable',f'--partition={args.partition}','--time=00:05:00',f'--chdir={folder}',f'--nodes={job_nodes}',f'--ntasks={tasks}',f'--ntasks-per-node={per_node}',f'--cpus-per-task={args.cpus_per_task}',f'--job-name={key}',f'--output={output}/{key}_%j.out',str(folder/'run_task3.slurm'),args.suite,key,case['rows'],case['cols'],case['predefined_period'],str(output),str(args.replicates),str(args.seconds)]
        print(' '.join(command))
        if args.submit:
            output.mkdir(parents=True,exist_ok=True)
            job=subprocess.run(command,capture_output=True,text=True)
            if job.returncode: raise SystemExit(f'Submission failed; previous submissions were retained:\n{job.stderr}')
            state[key]={'job_id':job.stdout.strip().split(';')[0],'command':command,'cpus_per_task':args.cpus_per_task}
            state_path.write_text(json.dumps(state,indent=2))
            submitted+=1
            if submitted>=args.max_new_jobs: break
    if args.submit: print(f'Submitted {submitted} new jobs. Repeat this same command to submit the next batch; existing jobs are skipped. Failed jobs need explicit review before resubmission.')
    else: print('Dry run only. Add --submit to queue the first batch.')

if __name__=='__main__': main()
