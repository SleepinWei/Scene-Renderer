#!/usr/bin/env python3
"""Show parent-build ablations and native material quality at normal ceilings."""
import argparse
import json
from pathlib import Path
from PIL import Image
from visualize_render_diagnostics import panel, grid


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('selected', type=Path)
    parser.add_argument('alternative', type=Path)
    parser.add_argument('group_sweep', type=Path)
    parser.add_argument('normal_sweep', type=Path)
    args = parser.parse_args()
    selected = json.loads((args.selected / 'comparison.json').read_text())
    alternative = json.loads((args.alternative / 'comparison.json').read_text())
    summary = {'group_ablation': json.loads(args.group_sweep.read_text()),
               'normal_ablation': json.loads(args.normal_sweep.read_text()),
               'quality_sweep': {}}
    panels = [panel(Image.open(args.selected / 'source.png'), 'Original source / same material',
                    '960 x 540; fixed camera, exposure, time; no UI', width=660, height=430)]
    for capture, report, label, key in [
            (args.selected, selected, 'Pairs / global bound / normal 0.05', 'legacy'),
            (args.selected, selected, 'Connected group / endpoint bound / normal 0.15', 'optimized'),
            (args.alternative, alternative, 'Connected group / endpoint bound / normal 0.20', 'optimized')]:
        q = report['quality'][key + '_4px']
        settings = report['baseline_build_settings'] if key == 'legacy' else report['optimized_build_settings']
        summary['quality_sweep'][str(settings['normal_error'])] = {
            'settings': settings, 'quality': q,
            'quality_by_budget': {k: v for k, v in report['quality'].items() if k.startswith(key + '_')}}
        panels.append(panel(Image.open(capture / (key + '_4px.png')), label,
                            f"{q['triangles']:,} triangles / source HDR RMSE {100*q['hdr_relative_rmse']:.3f}%",
                            width=660, height=430))
    grid(panels, 2, 'Coastal Cliff: parent normal ceiling / native material comparison',
         'LOD approximates source shading; CPU-cut and same-cut Hi-Z correctness are tested separately.',
         args.selected / 'normal-quality.png')
    (args.selected / 'parent-ablation.json').write_text(json.dumps(summary, indent=2) + '\n')
    print(json.dumps(summary['quality_sweep'], indent=2))


if __name__ == '__main__':
    main()
