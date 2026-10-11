#!/usr/bin/env python3
"""Summarize the opt-in runtime timeline; never align independent CPU/GPU clocks."""
import argparse
import collections
import gzip
import json
import math
from pathlib import Path
import statistics


def stats(values):
    values = sorted(values)
    if not values:
        return None
    return dict(samples=len(values), p50_ms=statistics.median(values),
                p95_ms=values[math.ceil(.95 * len(values))-1])


def summarize(path, warmup):
    with (gzip.open if path.suffix == '.gz' else open)(path, 'rt') as stream:
        raw = json.load(stream)
    if raw['dropped_events']:
        raise ValueError(f'{path}: timeline was truncated')
    events = raw['traceEvents']
    frames = sorted((e for e in events if e['name'] == 'render/frame'), key=lambda e:e['ts'])
    steady = frames[warmup:]
    ids = {e['id'] for e in steady}
    if not steady:
        raise ValueError('No steady rendered frames')
    low, high = steady[0]['ts'], max(e['ts']+e['dur'] for e in steady)
    cpu = collections.defaultdict(list)
    for e in events:
        if e['gpu']:
            continue
        if (e['id'] in ids if e['id'] else low <= e['ts'] <= high):
            cpu[e['name']].append(e['dur']/1000)
    starts = [e for e in events if e['name']=='render/acquire-and-flight-wait' and e['id'] in ids]
    # Count native submissions per interval between consecutive water surfaces.
    # This is independent of CPU timestamps and excludes startup-only uploads.
    surfaces = sorted((e for e in events if e['name']=='water/surface/fragment'), key=lambda e:e['ts'])
    submissions = sorted((e for e in events if e['name']=='submission/submission'), key=lambda e:e['ts'])
    surface_steady = surfaces[warmup:]
    counts, gaps, durations, periods = [], [], [], []
    for left, right in zip(surface_steady, surface_steady[1:]):
        a, b = left['ts']+left['dur'], right['ts']+right['dur']
        counts.append(sum(a <= s['ts']+s['dur'] < b for s in submissions))
        periods.append((b-a)/1000)
    if surface_steady:
        a, b = surface_steady[0]['ts'], surface_steady[-1]['ts']+surface_steady[-1]['dur']
        selected = [s for s in submissions if a <= s['ts']+s['dur'] <= b]
        durations = [s['dur']/1000 for s in selected]
        gaps = [max(0, y['ts']-x['ts']-x['dur'])/1000 for x,y in zip(selected,selected[1:])]
    return dict(source=str(path), cpu_clock='steady_clock; completion acknowledgement includes owner-thread polling',
                gpu_clock=raw['gpu_clock'], rendered_frames=len(frames), water_surface_frames=len(surfaces),
                warmup_frames=warmup, steady_cpu_wall_ms=(high-low)/1000,
                steady_cpu_frame_period_ms=(high-low)/1000/len(steady),
                cpu_scopes={k:stats(v) for k,v in sorted(cpu.items())},
                frame_start_retirement_and_flight_wait=stats([e['a'] for e in starts]),
                frame_start_native_acquire=stats([e['b'] for e in starts]),
                native_submissions_between_surfaces=dict(samples=len(counts), counts=dict(collections.Counter(counts)), mean=statistics.mean(counts) if counts else None),
                gpu_surface_completion_period=stats(periods),
                native_submission_duration=stats(durations), between_native_submission_gaps=stats(gaps))


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('inputs', nargs='+', type=Path)
    p.add_argument('--output', required=True, type=Path)
    p.add_argument('--warmup', type=int, default=8)
    a=p.parse_args()
    report={'scope':'CPU blocked intervals are not active CPU work. Native command-buffer gaps do not reveal intra-shader occupancy or bubbles. CPU and GPU clocks are independent.',
            'runs':[summarize(path,a.warmup) for path in a.inputs]}
    a.output.write_text(json.dumps(report,indent=2)+'\n')
    for r in report['runs']:
        print(r['source'], 'CPU period', round(r['steady_cpu_frame_period_ms'],2),
              'GPU surface period', r['gpu_surface_completion_period'],
              'submissions per surface',r['native_submissions_between_surfaces'])


if __name__=='__main__':
    main()
