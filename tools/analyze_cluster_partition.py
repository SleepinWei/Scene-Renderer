#!/usr/bin/env python3
"""Summarize same-build partition comparisons and show actual native captures."""
import argparse
import json
from pathlib import Path
from PIL import Image
import numpy as np
from analyze_cluster_performance import windows_by_frame, paired_windows, round_medians
from visualize_render_diagnostics import panel, grid


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('capture', type=Path)
    args = parser.parse_args()
    root = args.capture
    data = json.loads((root / 'comparison.json').read_text())
    before, after = (data['hierarchy'][k] for k in ('legacy', 'optimized'))
    names = data.get('builder_names', {'legacy': 'Attribute BFS', 'optimized': 'Position topology / spatial fill'})
    summary = {'scene': data['scene'], 'hierarchy': {k: {x: v for x, v in h.items() if x != 'meshes'} for k, h in data['hierarchy'].items()},
               'quality': data['quality'], 'temporal_quality': data.get('temporal_quality', {}),
               'builder_names': names, 'benchmark': {}, 'paired': {}}
    for name, record in data['benchmark'].items():
        windows = windows_by_frame(record)
        summary['benchmark'][name] = {k: v for k, v in record.items() if k != 'raw'}
        summary['benchmark'][name]['round_medians_ms'] = round_medians(windows)
    pairs = [('optimized_0px', 'legacy_0px'), ('optimized_4px', 'legacy_4px'),
             ('optimized_4px_hiz', 'legacy_4px_hiz'), ('optimized_4px_hiz', 'optimized_4px')]
    if 'source' in data['benchmark']:
        pairs += [('optimized_0px', 'source'), ('optimized_4px', 'source'), ('optimized_4px_hiz', 'source')]
    for optimized, baseline in pairs:
        if optimized in data['benchmark']:
            summary['paired'][f'{optimized}_vs_{baseline}'] = paired_windows(windows_by_frame(data['benchmark'][optimized]), windows_by_frame(data['benchmark'][baseline]))
    summary['changes_percent'] = {key: 100*(after[key]/before[key]-1) for key in ('leaf_clusters', 'all_clusters', 'roots', 'resident_bytes', 'build_seconds_sum')}
    (root / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
    panels = []
    for version, title in names.items():
        h = data['hierarchy'][version]
        panels.append(panel(Image.open(root / version / 'flow/0-clusters.png'), title + ': leaf partition',
                            f"{h['leaf_clusters']:,} instanced leaves / {h['leaf_utilization_percent']:.1f}% utilization", width=660, height=430))
    for version, title in [('legacy', 'Before'), ('optimized', 'After')]:
        q = data['quality'][version + '_4px']
        panels.append(panel(Image.open(root / version / 'flow/1-levels.png'), title + ': GPU group LOD / 4 px',
                            f"{q['triangles']:,} submitted triangles / CPU cut exactly matched", width=660, height=430))
    for version, title in [('legacy', 'Before'), ('optimized', 'After')]:
        q = data['quality'][version + '_4px']
        panels.append(panel(Image.open(root / (version + '_4px.png')), title + ': PBR / 4 px',
                            f"HDR relative RMSE vs source: {100*q['hdr_relative_rmse']:.3f}%", width=660, height=430))
    source = np.asarray(Image.open(root / 'source.png').convert('RGB')).astype(np.int16)
    optimized = np.asarray(Image.open(root / 'optimized_4px.png').convert('RGB')).astype(np.int16)
    difference = np.clip(np.abs(optimized - source)*8, 0, 255).astype(np.uint8)
    panels.append(panel(Image.fromarray(source.astype(np.uint8)), 'Original source geometry / PBR', 'Same camera, material, exposure; no UI', width=660, height=430))
    panels.append(panel(Image.fromarray(difference), 'After 4 px vs source / display difference x8', 'Shows LOD approximation; Hi-Z vs same cut is exact.', width=660, height=430))
    grid(panels, 2, f"{data['scene']}: cluster construction before / after", 'Native GPU membership and PBR; partition colors omit alpha masks and non-cluster meshes.', root / 'comparison.png')
    if data['scene'] != 'sponza':
        ids = np.asarray(Image.open(root / 'optimized/flow/0-clusters.png').convert('RGB'))
        yy, xx = np.nonzero(np.any(ids != ids[0, 0], axis=2))
        if not len(xx):
            raise ValueError('dense model partition capture is empty')
        box = (max(0, int(xx.min())-12), max(0, int(yy.min())-12),
               min(ids.shape[1], int(xx.max())+13), min(ids.shape[0], int(yy.max())+13))
        detail = []
        for version, title in [('legacy', 'Before'), ('optimized', 'After')]:
            detail.append(panel(Image.open(root/version/'flow/0-clusters.png').crop(box), title + ': leaf partition / native crop',
                                f"{data['hierarchy'][version]['leaf_utilization_percent']:.1f}% leaf utilization", width=550, height=460))
        for version, title in [('legacy', 'Before'), ('optimized', 'After')]:
            detail.append(panel(Image.open(root/(version+'_4px.png')).crop(box), title + ': PBR / 4 px / same crop',
                                f"Source HDR relative RMSE: {100*data['quality'][version+'_4px']['hdr_relative_rmse']:.3f}%", width=550, height=460))
        detail.append(panel(Image.fromarray(source.astype(np.uint8)).crop(box), 'Original source geometry / same crop', 'Full detail, same materials and camera', width=550, height=460))
        detail.append(panel(Image.fromarray(difference).crop(box), 'After 4 px vs source / difference x8', 'Highlights expose remaining LOD shading error', width=550, height=460))
        grid(detail, 2, f"{data['scene']}: native model details / before and after",
             f"Same screen crop {box}; original full captures are retained.", root/'detail.png')
    print(json.dumps({k: v for k, v in summary.items() if k not in ('quality', 'benchmark')}, indent=2))


if __name__ == '__main__':
    main()
