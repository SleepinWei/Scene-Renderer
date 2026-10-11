#!/usr/bin/env python3
"""Alternating fixed-budget native PT runs; report wall-time scopes explicitly."""
import argparse
import json
import statistics
import subprocess
from pathlib import Path


def main():
    p=argparse.ArgumentParser()
    p.add_argument('--iterations',type=int,default=3)
    p.add_argument('--output',type=Path,default=Path('build/path-tracing/pt-world-lod-benchmark'))
    a=p.parse_args()
    if not 1<=a.iterations<=10:p.error('iterations must be 1..10')
    a.output.mkdir(parents=True,exist_ok=True)
    rows=[]
    for i in range(a.iterations):
        for low in ([False,True] if i%2==0 else [True,False]):
            name=f'{"lod" if low else "full"}-{i}'
            prefix=a.output/name
            command=['./build/Scene-Renderer','--path-trace-gpu','sponza','--backend','Metal','--pt-traversal','native',
                     '--pt-size','480x270','--pt-samples','64','--pt-fixed','--pt-bounces','16','--pt-output',str(prefix)]
            if low:command+=['--pt-world-lod','--pt-lod-error','0.1','--pt-lod-ratio','0.5','--pt-lod-uv-error','0.05']
            with Path(str(prefix)+'.log').open('w') as log:subprocess.run(command,stdout=log,stderr=subprocess.STDOUT,check=True)
            j=json.loads(Path(str(prefix)+'.json').read_text())
            row={'variant':'lod' if low else 'full','iteration':i,'command':command,
                 'trace_wall_excluding_checkpoint_io_seconds':j['render_seconds']-j['timings']['checkpoint_io_seconds'],
                 'render_including_checkpoint_io_seconds':j['render_seconds'],'checkpoint_io_seconds':j['timings']['checkpoint_io_seconds'],
                 'setup_seconds':j['setup_seconds'],'scene_construct_seconds':j['timings']['scene_construct_seconds'],
                 'native_acceleration_bytes':j['native_acceleration_bytes'],'native_acceleration_build_seconds':j['native_acceleration_build_seconds'],
                 'native_ray_tracing':j['native_ray_tracing'],'samples':j['samples'],'average_spp':j['average_spp'],'rays':j['rays']}
            if low:row['lod_build_seconds']=json.loads(Path(str(prefix)+'-world-lod.json').read_text())['build_seconds']
            rows.append(row);print(name,row['trace_wall_excluding_checkpoint_io_seconds'],flush=True)
    summary={}
    for variant in ['full','lod']:
        values=[v['trace_wall_excluding_checkpoint_io_seconds'] for v in rows if v['variant']==variant]
        summary[variant]={'median_seconds':statistics.median(values),'minimum_seconds':min(values),'maximum_seconds':max(values)}
    report={'scene':'sponza','size':[480,270],'spp':64,'depth':16,'iterations':a.iterations,
            'scope':'CPU wall time including dispatch/readback waits; excludes progressive checkpoint file IO and setup; not hardware GPU timestamps. Each run is a separate process with the same frozen atmosphere. Geometry LOD can change the number of secondary paths.',
            'runs':rows,'summary':summary,'median_speedup':summary['full']['median_seconds']/summary['lod']['median_seconds']}
    (a.output/'summary.json').write_text(json.dumps(report,indent=2)+'\n')


if __name__=='__main__':main()
