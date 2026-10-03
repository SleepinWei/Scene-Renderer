#!/usr/bin/env python3
"""Serial PT convergence comparison; training time counts towards each budget.
Uses linear PFM and an independent-seed finite-sample reference, never PNG MSE.
"""
import argparse
import array
import json
import os
from pathlib import Path
import subprocess
import sys
import time


def read_pfm(path):
    with Path(path).open('rb') as stream:
        if stream.readline().strip() != b'PF':
            raise ValueError('RGB PFM required')
        width, height = map(int, stream.readline().split())
        scale = float(stream.readline())
        data = array.array('f')
        data.frombytes(stream.read())
    if len(data) != width * height * 3:
        raise ValueError('PFM extent mismatch')
    if (scale < 0) != (sys.byteorder == 'little'):
        data.byteswap()
    rows = [data[y*width*3:(y+1)*width*3] for y in range(height-1, -1, -1)]
    return width, height, array.array('f', (value for row in rows for value in row))


def error(path, reference, roi):
    width, height, data = read_pfm(path)
    rw, rh, ref = reference
    if (width, height) != (rw, rh):
        raise ValueError('Reference dimensions must match')
    mse = sum((a-b)**2 for a, b in zip(data, ref))/len(data)
    weights = (.2126, .7152, .0722)
    y, yr = [], []
    for i in range(0, len(data), 3):
        y.append(sum(data[i+c]*weights[c] for c in range(3)))
        yr.append(sum(ref[i+c]*weights[c] for c in range(3)))
    x0, y0, x1, y1 = roi
    indices = [j*width+i for j in range(y0, y1) for i in range(x0, x1)]
    return {'rgb_mse': mse, 'roi_luminance_mse': sum((y[i]-yr[i])**2 for i in indices)/len(indices),
            'mean_luminance': sum(y)/len(y), 'reference_mean_luminance': sum(yr)/len(yr)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', default='./build/pt/Scene-Renderer')
    parser.add_argument('--scene', choices=['sponza', 'san-miguel'], required=True)
    parser.add_argument('--environment', required=True)
    parser.add_argument('--reference', required=True)
    parser.add_argument('--output', default='build/path-tracing/convergence')
    args = parser.parse_args()
    directory = Path(args.output)
    directory.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ, MTL_DEBUG_LAYER='0', MTL_SHADER_VALIDATION='0')
    reference = Path(args.reference)
    if not reference.exists():
        reference.parent.mkdir(parents=True, exist_ok=True)
        command = [args.binary, '--path-trace-gpu', args.scene, '--pt-size', '320x240',
                   '--pt-samples', '1024', '--pt-bounces', '16', '--pt-seed', '7',
                   '--pt-fixed', '--pt-exposure', '3', '--pt-environment', args.environment,
                   '--pt-output', str(reference.with_suffix(''))]
        with reference.with_suffix('.log').open('w') as log:
            subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, env=env, check=True)
        print(args.scene, 'independent 1024 spp reference ready', flush=True)
    measurements = {}
    for mode, flags, budget in [('baseline', [], 512), ('guiding', ['--pt-guiding'], 256),
                                ('cache', ['--pt-cache'], 256),
                                ('guided-cache', ['--pt-guiding', '--pt-cache'], 256)]:
        prefix = directory / (args.scene+'-'+mode)
        command = [args.binary, '--path-trace-gpu', args.scene, '--pt-size', '320x240',
                   '--pt-samples', str(budget), '--pt-bounces', '16', '--pt-seed', '1',
                   '--pt-fixed', '--pt-exposure', '3', '--pt-training-samples', '64',
                   '--pt-environment', args.environment, '--pt-output', str(prefix)] + flags
        started = time.perf_counter()
        with prefix.with_suffix('.log').open('w') as log:
            subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, env=env, check=True)
        report = json.loads(prefix.with_suffix('.json').read_text())
        report['command_wall_seconds'] = time.perf_counter()-started
        report['command'] = command
        measurements[mode] = report
        print(args.scene, mode, round(report['trace_and_training_seconds'], 3),
              's, guide/cache hits:', report['guide_hits'], report['cache_hits'], flush=True)
    ref = read_pfm(args.reference)
    roi = (164, 150, 216, 220) if args.scene == 'sponza' else (175, 155, 290, 225)
    checkpoints = [(path, json.loads(path.read_text()))
                   for path in directory.glob(args.scene+'-baseline-*spp.json')]
    comparisons = {}
    for mode in ['guiding', 'cache', 'guided-cache']:
        target = measurements[mode]['trace_and_training_seconds']
        path, baseline = min(checkpoints, key=lambda pair: abs(pair[1]['render_seconds']-target))
        comparisons[mode] = {'baseline_spp': baseline['samples'],
                             'baseline_seconds': baseline['render_seconds'], 'optimized_seconds': target,
                             'baseline_error': error(path.with_suffix('.pfm'), ref, roi),
                             'optimized_error': error(directory/(args.scene+'-'+mode+'.pfm'), ref, roi)}
    result = {'scene': args.scene, 'reference': args.reference, 'roi_xyxy': roi,
              'measurements': measurements, 'comparisons': comparisons,
              'note': 'Approximate matched-time checkpoints; finite independent reference; development load.'}
    (directory/(args.scene+'-benchmark.json')).write_text(json.dumps(result, indent=2)+'\n')
    print(json.dumps(comparisons, indent=2))


if __name__ == '__main__':
    main()
