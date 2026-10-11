#!/usr/bin/env python3
"""Same-seed native/software PT timing and unfiltered linear PFM comparison.

Render sequentially. Each process builds its own scene; engine phase timings
separate AS/pipeline preparation from sampling. Driver cache state is uncontrolled.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import statistics
import struct
import subprocess
import time


def pfm(path):
    with path.open('rb') as f:
        if f.readline().strip() != b'PF':
            raise ValueError('Expected RGB PFM')
        width, height = map(int, f.readline().split())
        scale = float(f.readline())
        data = f.read()
    values = struct.unpack(('<' if scale < 0 else '>')+str(width*height*3)+'f', data)
    if not all(math.isfinite(v) for v in values):
        raise ValueError('Nonfinite raw film')
    return width, height, values


def comparison(a, b):
    return dict(exact=a == b, nrmse=math.sqrt(sum((x-y)**2 for x,y in zip(a,b))/max(1e-30,sum(y*y for y in b))),
                maximum_absolute_error=max(abs(x-y) for x,y in zip(a,b)), different_channels=sum(x!=y for x,y in zip(a,b)),
                relative_l1=sum(abs(x-y) for x,y in zip(a,b))/max(1e-30,sum(abs(y) for y in b)),
                relative_mean_difference=(sum(a)-sum(b))/max(1e-30,sum(b)),
                pixels_above_0_001=sum(max(abs(a[j+k]-b[j+k]) for k in range(3))>1e-3 for j in range(0,len(a),3)))


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--executable', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--scene', action='append', type=Path, default=[])
    p.add_argument('--fixture', action='append', default=[])
    p.add_argument('--size', default='160x90')
    p.add_argument('--samples', type=int, default=64)
    p.add_argument('--bounces', type=int, default=12)
    p.add_argument('--runs', type=int, default=3)
    args = p.parse_args()
    if args.runs < 1 or not (args.scene or args.fixture):
        p.error('Need a scene/fixture and at least one run')
    args.output.mkdir(parents=True, exist_ok=True)
    exe = args.executable.resolve()
    report = dict(executable=str(exe), executable_sha256=hashlib.sha256(exe.read_bytes()).hexdigest(), size=args.size,
                  samples=args.samples, max_depth=args.bounces, seed=1, runs=args.runs,
                  scope='sequential offline Metal processes; phase timings are CPU wall time including GPU waits; driver caches uncontrolled; raw PFM before tone mapping/denoising', scenes=[])
    scenes = [('scene', str(v.resolve())) for v in args.scene]+[('fixture', v) for v in args.fixture]
    for scene_index, (kind, source) in enumerate(scenes):
        entry = dict(kind=kind, source=source, renders=[])
        films = {}
        prefixes = {}
        for run in range(args.runs):
            modes = ['software', 'native'] if run % 2 == 0 else ['native', 'software']
            for mode in modes:
                prefix = args.output/f'scene-{scene_index}-{run}-{mode}'
                command = [str(exe), '--backend', 'Metal', '--gpu-traversal', mode, '--'+kind, source,
                           '--size', args.size, '--samples', str(args.samples), '--bounces', str(args.bounces), '--seed', '1', '--output', str(prefix)]
                start = time.monotonic()
                result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, check=True)
                wall = time.monotonic()-start
                prefix.with_suffix('.log').write_text(result.stdout)
                data = json.loads(prefix.with_suffix('.json').read_text())
                if data['native_ray_tracing'] != (mode == 'native'):
                    raise RuntimeError('Requested traversal was not executed')
                entry['renders'].append(dict(run=run, mode=mode, process_seconds=wall,
                                             render_seconds=data['render_seconds'], setup_seconds=data['setup_seconds'],
                                             as_seconds=data['native_acceleration_build_seconds'], as_bytes=data['native_acceleration_bytes'],
                                             blas_count=data['native_blas_count'], instances=data['native_instance_count'], rays=data['rays']))
                films[mode] = pfm(prefix.with_suffix('.pfm'))
                prefixes[mode] = prefix
                print(source, mode, run, data['render_seconds'], flush=True)
        if films['native'][:2] != films['software'][:2]:
            raise ValueError('Film dimensions differ')
        a, b = films['native'][2], films['software'][2]
        entry['raw_comparison'] = comparison(a,b)
        entry['aov_comparison'] = {}
        for label in ['albedo','normal']:
            a = pfm(Path(str(prefixes['native'])+'-'+label+'.pfm'))[2]
            b = pfm(Path(str(prefixes['software'])+'-'+label+'.pfm'))[2]
            entry['aov_comparison'][label] = comparison(a,b)
        medians = {mode:statistics.median(r['render_seconds'] for r in entry['renders'] if r['mode']==mode) for mode in ['native','software']}
        entry['median_render_seconds'] = medians
        entry['sampling_speedup'] = medians['software']/medians['native']
        report['scenes'].append(entry)
        (args.output/'summary.json').write_text(json.dumps(report, indent=2)+'\n')


if __name__ == '__main__':
    main()
