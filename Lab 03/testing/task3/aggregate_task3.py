#!/usr/bin/env python3
"""Aggregate validated runs. No samples means blank, never a zero-delay result."""
import argparse, csv, json, statistics, re
from collections import defaultdict
from pathlib import Path
from run_experiment import validate_run

FIELDS=['suite','case_id','rows','cols','stations','replicate','predefined_period','seconds','cpus_per_task','tag','message','sent','send_bytes','receive_bytes','send_call_time_us','receive_call_time_us','send_completion_us','receive_completion_us','mean_payload_bytes','source']

def write_csv(path,rows,fields=None):
    if fields is None: fields=list(rows[0]) if rows else []
    with path.open('w',newline='') as file:
        writer=csv.DictWriter(file,fieldnames=fields);writer.writeheader();writer.writerows(rows)

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('results',type=Path)
    ap.add_argument('--output',type=Path)
    ap.add_argument('--no-plots',action='store_true')
    args=ap.parse_args()
    output=args.output or args.results/'analysis'
    output.mkdir(parents=True,exist_ok=True)
    manifests=[]
    for path in sorted(args.results.rglob('run_manifest.json')):
        value=json.loads(path.read_text())
        if value['status']=='passed': manifests.append((path.parent,value))
    if not manifests: raise SystemExit('No validated runs found.')
    protocols={(m['source_sha256'],m.get('metrics_sha256','embedded'),m['seconds'],m['port_period'],m['check_period'],m['busy_percent'],m['cpus_per_task']) for _,m in manifests}
    if len(protocols)!=1: raise SystemExit('Runs have mixed source/configuration/resource settings. Analyse each consistent group separately.')
    seen=set();message_rows=[];period_rows=[]
    for folder,m in manifests:
        identity=(m['suite'],m['case_id'],m['replicate'])
        if identity in seen: raise SystemExit(f'Duplicate case/replicate: {identity}. Select one result group explicitly.')
        seen.add(identity)
        validate_run(folder,m['processes'])
        totals={tag:defaultdict(float) for tag in range(1,9)}
        names={}
        for path in folder.glob('message_metrics_rank_*.csv'):
            for metric in csv.DictReader(path.open()):
                for old, new in [('send_api_seconds', 'send_call_time_seconds'),
                                 ('receive_api_seconds', 'receive_call_time_seconds')]:
                    if new not in metric and old in metric:
                        metric[new] = metric[old]
                tag=int(metric['tag']);names[tag]=metric['message']
                for key in ['sent','received','send_bytes','receive_bytes','send_call_time_seconds','receive_call_time_seconds','send_completion_seconds','receive_completion_seconds']:
                    totals[tag][key]+=float(metric[key])
        for tag in range(1,9):
            metric=totals[tag];sent=int(metric['sent']);received=int(metric['received'])
            row={key:m[key] for key in FIELDS if key in m}
            row.update(tag=tag,message=names[tag],sent=sent,send_bytes=int(metric['send_bytes']),receive_bytes=int(metric['receive_bytes']),
                send_call_time_us=metric['send_call_time_seconds']*1e6/sent if sent else None,
                receive_call_time_us=metric['receive_call_time_seconds']*1e6/received if received else None,
                send_completion_us=metric['send_completion_seconds']*1e6/sent if sent else None,
                receive_completion_us=metric['receive_completion_seconds']*1e6/received if received else None,
                mean_payload_bytes=metric['send_bytes']/sent if sent else None,source=str(folder))
            message_rows.append(row)
        if m['suite']=='period':
            log=(folder/'log.txt').read_text()
            counts=[int(value) for value in re.findall(r'Recommended nodes: (\d+)',log)]
            period_rows.append({'predefined_period':m['predefined_period'],'replicate':m['replicate'],'reports':len(counts),'reports_with_candidates':sum(value>0 for value in counts),'reports_without_candidates':sum(value==0 for value in counts),'freshness_shortcuts':log.count('No available nodes nearby')})
    write_csv(output/'task3_message_runs.csv',message_rows,FIELDS)
    if period_rows: write_csv(output/'period_results.csv',period_rows)
    groups=defaultdict(list)
    for row in message_rows: groups[(row['suite'],row['case_id'],row['tag'])].append(row)
    summary=[]
    for _,items in sorted(groups.items()):
        first=items[0]
        row={key:first[key] for key in ['suite','case_id','rows','cols','stations','predefined_period','tag','message']}
        samples=[item for item in items if item['sent']>0]
        row.update(recorded_runs=len(items),runs_with_samples=len(samples),total_messages=sum(item['sent'] for item in items))
        for key in ['send_completion_us','send_call_time_us','receive_completion_us']:
            values=[item[key] for item in samples]
            row['mean_'+key]=statistics.mean(values) if values else None
            row['sd_'+key]=statistics.stdev(values) if len(values)>1 else None
        summary.append(row)
    write_csv(output/'task3_message_summary.csv',summary)
    coverage={}
    for suite in ['mesh','line','period']:
        chosen=[m for _,m in manifests if m['suite']==suite]
        sizes={(m['rows'],m['cols']) for m in chosen}
        unique_counts={m['stations'] for m in chosen}
        coverage[suite]={'validated_runs':len(chosen),'distinct_dimensions':len(sizes),'distinct_station_counts':len(unique_counts),'distinct_periods':len({m['predefined_period'] for m in chosen})}
    coverage['notes']=['Only passed runs are included. Zero message samples produce blank latency fields.','Per-run means are pooled over messages/ranks. Configuration means and SD are calculated across independent runs.','Completion times are software-observed request lifetimes, not isolated one-way network delays.','C4 counts its count and coordinate payloads as separate MPI messages. Tags 6-8 are shutdown controls.']
    (output/'coverage.json').write_text(json.dumps(coverage,indent=2))
    print(json.dumps(coverage,indent=2))
    if args.no_plots: return
    try:
        import matplotlib
        matplotlib.use('Agg')
        import matplotlib.pyplot as plt
    except ImportError:
        print('CSV files are complete. Install matplotlib on your local machine and rerun to create plots.');return
    for suite in ['mesh','line']:
        for metric,label,filename in [
            ('send_completion_us','Completion time (us/message)','completion'),
            ('send_call_time_us','MPI send function time (us/message)','send_call_time')]:
            chosen=[row for row in summary if row['suite']==suite and row['tag']<=8]
            if not chosen: continue
            fig,axes=plt.subplots(4,2,figsize=(11,13),constrained_layout=True)
            for tag,ax in zip(range(1,9),axes.flat):
                data=sorted([r for r in chosen if r['tag']==tag and r['mean_'+metric] is not None],key=lambda r:r['stations'])
                if data:
                    ax.errorbar([r['stations'] for r in data],[r['mean_'+metric] for r in data],yerr=[r['sd_'+metric] or 0 for r in data],fmt='o-',capsize=3,color='#24486b')
                else: ax.text(.5,.5,'No message samples',ha='center',transform=ax.transAxes)
                from matplotlib.ticker import MaxNLocator
                ax.xaxis.set_major_locator(MaxNLocator(integer=True))
                ax.set_title(next(r['message'] for r in chosen if r['tag']==tag));ax.set_xlabel('Charging nodes (base excluded)',fontsize=9);ax.set_ylabel(label,fontsize=9);ax.grid(alpha=.2)
            fig.suptitle(f'V1 {suite}: observed per-type timing; error bars show run SD',fontsize=13)
            fig.savefig(output/f'{suite}_{filename}.png',dpi=180);plt.close(fig)
    if period_rows:
        fig,ax=plt.subplots(figsize=(8,5),constrained_layout=True)
        for key,label in [('reports','Congestion reports'),('freshness_shortcuts','Freshness shortcuts'),('reports_with_candidates','Reports with candidates')]:
            groups=defaultdict(list)
            for row in period_rows: groups[row['predefined_period']].append(row[key])
            periods=sorted(groups)
            ax.errorbar(periods,[statistics.mean(groups[p]) for p in periods],yerr=[statistics.stdev(groups[p]) if len(groups[p])>1 else 0 for p in periods],fmt='o-',capsize=3,label=label)
        ax.set_xlabel('Predefined period (s)');ax.set_ylabel('Count per run');ax.set_title('V1 predefined-period experiment');ax.legend();ax.grid(alpha=.2)
        fig.savefig(output/'period_effects.png',dpi=180);plt.close(fig)

if __name__=='__main__': main()
