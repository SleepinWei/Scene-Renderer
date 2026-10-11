#!/usr/bin/env python3
"""Summarize DDGI GPU stage intervals without adding overlapping render ranges."""
import argparse
import gzip
import json
import statistics
from pathlib import Path


def interval_union(intervals):
    end = float('-inf')
    total = 0
    for start, stop in sorted(intervals):
        total += max(0, stop - max(start, end))
        end = max(end, stop)
    return total


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('profile', type=Path)
    parser.add_argument('--warmup', type=int, default=10)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    opener = gzip.open if args.profile.suffix == '.gz' else open
    with opener(args.profile, 'rt') as file:
        profile = json.load(file)
    if not profile['supported']:
        raise ValueError('GPU counters unavailable')
    frames = []
    group = None
    for sample in profile['samples']:
        if sample['label'] == 'ddgi/retain-field':
            group = []
            frames.append(group)
        if sample['label'].startswith('ddgi/') and group is not None:
            group.append(sample)
    if len(frames) <= args.warmup:
        raise ValueError('Insufficient DDGI frame samples')
    result = {'device': profile['device'], 'rendered_frames': len(frames), 'warmup_frames': args.warmup,
              'method': 'Union of GPU timestamp intervals within each phase and frame; phases can overlap and are not additive.',
              'phase_interval_union_ms': {}}
    for label in ('ddgi/retain-field', 'ddgi/probe-capture', 'ddgi/probe-integrate', 'ddgi/surface-query'):
        values = [interval_union([(s['start_ms'], s['end_ms']) for s in frame if s['label'] == label]) for frame in frames[args.warmup:]]
        result['phase_interval_union_ms'][label] = {'median': statistics.median(values), 'p95': sorted(values)[int(len(values)*.95)]}
    args.output.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
