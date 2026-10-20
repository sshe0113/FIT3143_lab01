#!/usr/bin/env python3
"""One five-minute Slurm job: compile a source snapshot and run three repetitions."""
import csv, hashlib, json, os, shutil, subprocess, sys, time
from pathlib import Path

def sha(path): return hashlib.sha256(path.read_bytes()).hexdigest()

def validate_run(folder,processes):
    ranks=list(folder.glob('message_metrics_rank_*.csv'))
    if len(ranks)!=processes: raise RuntimeError(f'Expected {processes} rank files, found {len(ranks)}')
    totals={tag:[0,0,0,0] for tag in range(1,9)}
    for path in ranks:
        for row in csv.DictReader(path.open()):
            for i,key in enumerate(['sent','received','send_bytes','receive_bytes']): totals[int(row['tag'])][i]+=int(row[key])
    global_rows=list(csv.DictReader((folder/'message_metrics_global.csv').open()))
    if len(global_rows)!=8: raise RuntimeError('Missing global metrics rows')
    for row in global_rows:
        expected=totals[int(row['tag'])]
        if expected!=[int(row[k]) for k in ['sent','received','send_bytes','receive_bytes']]: raise RuntimeError('Rank/global metric mismatch')
        if expected[0]!=expected[1] or expected[2]!=expected[3]: raise RuntimeError('Send/receive counts or payload bytes do not balance')
    log=(folder/'log.txt').read_text()
    stations=processes-1
    if f'Shutdown confirmed: READY={stations} TERMINATED={stations}' not in log: raise RuntimeError('Shutdown acknowledgements missing')
    return totals

def main():
    if len(sys.argv)!=8: raise SystemExit('Expected: suite case_id rows cols period output_dir replicates')
    suite,case_id,row_text,col_text,period,output,reps_text=sys.argv[1:]
    rows,cols,reps=int(row_text),int(col_text),int(reps_text)
    seconds=30.0  # Fixed in v1/evcns.c, together with port_period=2 and check_period=3.
    processes=rows*cols+1
    if int(os.environ.get('SLURM_NTASKS','0'))!=processes: raise SystemExit('Slurm process allocation does not match the grid')
    if reps*(seconds+15)+30>270: raise SystemExit('Requested runs exceed the safe five-minute budget')
    lab=Path(__file__).resolve().parents[2]
    job_id=os.environ.get('SLURM_JOB_ID','manual')
    job_dir=Path(output).resolve()/suite/case_id/f'job_{job_id}'
    job_dir.mkdir(parents=True,exist_ok=False)
    snapshot=job_dir/'source'
    snapshot.mkdir()
    shutil.copy2(lab/'v1'/'evcns.c',snapshot/'evcns.c')
    executable=snapshot/'evcns'
    compile_result=subprocess.run(['mpicc','-std=gnu11','-O2','-fopenmp',str(snapshot/'evcns.c'),'-o',str(executable)],capture_output=True,text=True)
    (job_dir/'compile.txt').write_text(compile_result.stdout+compile_result.stderr)
    compile_result.check_returncode()
    mpi_version=subprocess.run(['mpicc','--showme:version'],capture_output=True,text=True).stdout.strip()
    for replicate in range(1,reps+1):
        run_dir=job_dir/f'rep_{replicate:02d}'
        run_dir.mkdir()
        env=dict(os.environ,EVCNS_PREDEFINED_PERIOD=period,EVCNS_BUSY_PERCENT='70')
        command=['srun','--exact','--kill-on-bad-exit=1','--cpu-bind=cores','-n',str(processes),str(executable),str(rows),str(cols)]
        manifest={'suite':suite,'case_id':case_id,'rows':rows,'cols':cols,'stations':rows*cols,'processes':processes,'replicate':replicate,'predefined_period':float(period),'seconds':seconds,'port_period':2.0,'check_period':3.0,'busy_percent':70,'cpus_per_task':int(os.environ.get('SLURM_CPUS_PER_TASK','0')),'slurm_job_id':job_id,'node_list':os.environ.get('SLURM_JOB_NODELIST',''),'mpi_version':mpi_version,'source_sha256':sha(snapshot/'evcns.c'),'command':command,'started_unix':time.time(),'status':'running'}
        record=run_dir/'run_manifest.json'
        record.write_text(json.dumps(manifest,indent=2))
        try:
            run=subprocess.run(command,cwd=run_dir,env=env,capture_output=True,text=True,timeout=seconds+60)
            (run_dir/'stdout.txt').write_text(run.stdout+run.stderr)
            manifest['exit_code']=run.returncode
            if run.returncode: raise RuntimeError(f'srun returned {run.returncode}')
            validate_run(run_dir,processes)
            manifest['status']='passed'
        except Exception as error:
            manifest['status']='failed';manifest['error']=str(error)
            raise
        finally:
            manifest['finished_unix']=time.time();record.write_text(json.dumps(manifest,indent=2))
        print(f'{case_id} replicate {replicate}/{reps}: passed; results {run_dir}',flush=True)

if __name__=='__main__': main()
