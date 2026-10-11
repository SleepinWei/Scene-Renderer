#!/usr/bin/env python3
"""Analyze measured GPU intervals; keep encoder gaps separate from pass sums."""
import argparse
from collections import defaultdict
import json
from pathlib import Path
from statistics import median
import numpy as np


def windows_by_frame(record):
    frames = defaultdict(list)
    for span in record['raw']:
        frames[(span['round'], span['sample'])].append(span)
    return {key: max(s['end_ms'] for s in spans)-min(s['start_ms'] for s in spans)
            for key, spans in frames.items()}


def paired_windows(optimized, baseline):
    keys = sorted(optimized.keys() & baseline.keys())
    ratios = [100*(optimized[k]/baseline[k]-1) for k in keys]
    return {'pairs': len(keys), 'median_change_percent': median(ratios),
            'p10_p90_change_percent': np.percentile(ratios, [10, 90]).tolist(),
            'optimized_faster_pairs': sum(optimized[k] < baseline[k] for k in keys),
            'scope': 'Adjacent rotated-mode frames at identical round/sample; descriptive, not a confidence interval.'}


def round_medians(windows):
    return {str(r): median(v for (rr, _), v in windows.items() if rr == r)
            for r in sorted({key[0] for key in windows})}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('capture', type=Path)
    args = parser.parse_args()
    if (args.capture / 'dense.json').exists():
        dense = json.loads((args.capture / 'dense.json').read_text())
        summary = {'scene': dense['scene'], 'cases': {}}
        for case, records in dense['benchmark'].items():
            windows = {name: windows_by_frame(record) for name, record in records.items() if isinstance(record, dict)}
            summary['cases'][case] = {
                'round_medians_ms': {name: round_medians(w) for name, w in windows.items()},
                'paired_optimized_vs_no_occlusion': paired_windows(windows['two_stage_fused'], windows['no_occlusion'])}
        (args.capture / 'performance-summary.json').write_text(json.dumps(summary, indent=2)+'\n')
        print(json.dumps(summary, indent=2))
        return
    report = json.loads((args.capture / 'benchmark.json').read_text())
    summary = {'scope': report['scope'], 'modes': {}}
    for name, record in report.items():
        if not isinstance(record, dict):
            continue
        if not record['supported']:
            raise ValueError(f'GPU profiling unavailable for {name}')
        frames = defaultdict(list)
        for span in record['raw']:
            frames[(span['round'], span['sample'])].append(span)
        phase_samples = defaultdict(list)
        gaps, counts = [], []
        for spans in frames.values():
            phases = defaultdict(float)
            for span in spans:
                label = span['stage']
                phase = ('pyramid_and_late' if label.startswith('hiz/') and '-batch/' in label else 'geometry_early' if label == 'geometry-early' else
                         'geometry_late' if label == 'geometry-late' else
                         'geometry' if label == 'geometry' else
                         'cluster' if label.startswith('clusters/') else
                         'prepass' if label == 'hiz/occluder-depth' else
                         'pyramid' if label.startswith(('hiz/pyramid-', 'hiz/current-')) else 'object_tests')
                phases[phase] += span['ms']
            for phase, ms in phases.items():
                phase_samples[phase].append(ms)
            intervals = sorted((s['start_ms'], s['end_ms']) for s in spans)
            begin, end = intervals[0]
            first, union = begin, 0.0
            for lo, hi in intervals[1:]:
                if lo > end:
                    union += end-begin
                    begin, end = lo, hi
                else:
                    end = max(end, hi)
            union += end-begin
            gaps.append(max(0.0, end-first-union))
            counts.append(sum(s['kind'] == 'compute' for s in spans))
        result = {k: v for k, v in record.items() if k not in ('raw', 'samples_ms', 'cpu_render_samples_ms', 'gpu_window_samples_ms')}
        result.update(sample_count=len(frames), phase_medians_ms={k: median(v) for k, v in phase_samples.items()},
                      inter_scope_gap_median_ms=median(gaps), compute_encoder_counts=sorted(set(counts)),
                      pass_sum_p10_p90_ms=np.percentile(record['samples_ms'], [10, 90]).tolist(),
                      gpu_window_p10_p90_ms=np.percentile(record['gpu_window_samples_ms'], [10, 90]).tolist())
        summary['modes'][name] = result
    new = summary['modes']['batch_4px']
    hz = summary['modes'].get('batch_hiz_4px', summary['modes'].get('two_stage_original'))
    summary['changes_4px'] = {
        'hiz_triangle_reduction_percent': 100*(1-hz['selected_triangles']/new['selected_triangles']),
        'hiz_gpu_window_increase_percent': 100*(hz['gpu_window_median_ms']/new['gpu_window_median_ms']-1),
    }
    if 'legacy_4px' in summary['modes']:
        old = summary['modes']['legacy_4px']
        summary['changes_4px'].update(
            gpu_pass_sum_reduction_percent=100*(1-new['median_ms']/old['median_ms']),
            gpu_window_reduction_percent=100*(1-new['gpu_window_median_ms']/old['gpu_window_median_ms']),
            cpu_render_call_reduction_percent=100*(1-new['cpu_render_median_ms']/old['cpu_render_median_ms']))
    if 'two_stage_4px' in summary['modes']:
        two = summary['modes']['two_stage_4px']
        summary['changes_two_stage_4px'] = {
            'gpu_window_reduction_vs_reference_occlusion_percent': 100*(1-two['gpu_window_median_ms']/hz['gpu_window_median_ms']),
            'gpu_pass_sum_reduction_vs_reference_occlusion_percent': 100*(1-two['median_ms']/hz['median_ms']),
            'cpu_render_reduction_vs_reference_occlusion_percent': 100*(1-two['cpu_render_median_ms']/hz['cpu_render_median_ms']),
            'gpu_window_overhead_vs_no_occlusion_percent': 100*(two['gpu_window_median_ms']/new['gpu_window_median_ms']-1),
        }
        optimized = windows_by_frame(report['two_stage_4px'])
        summary['paired_window_comparisons'] = {}
        for reference in ('batch_4px', 'two_stage_original', 'two_stage_separate'):
            if reference not in report:
                continue
            baseline = windows_by_frame(report[reference])
            summary['paired_window_comparisons'][reference] = paired_windows(optimized, baseline)
    motion_path = args.capture / 'moving-benchmark.json'
    if motion_path.exists():
        motion = json.loads(motion_path.read_text())
        windows = {name: windows_by_frame(record) for name, record in motion.items() if isinstance(record, dict)}
        summary['moving'] = {
            'round_medians_ms': {name: round_medians(w) for name, w in windows.items()},
            'paired_optimized_vs_no_occlusion': paired_windows(windows['two_stage_4px'], windows['batch_4px'])}
    (args.capture / 'summary.json').write_text(json.dumps(summary, indent=2)+'\n')
    print('| Mode | GPU pass sum ms | GPU window ms | CPU render ms | Compute encoders | Cluster triangles | Actual prepass triangles |')
    print('| --- | ---: | ---: | ---: | ---: | ---: | ---: |')
    for name, mode in summary['modes'].items():
        print(f"| {name} | {mode['median_ms']:.3f} | {mode['gpu_window_median_ms']:.3f} | {mode['cpu_render_median_ms']:.3f} | {mode['compute_encoder_counts']} | {mode['selected_triangles']} | {mode['actual_prepass_triangles']} |")
    print(json.dumps(summary['changes_4px'], indent=2))


if __name__ == '__main__':
    main()
