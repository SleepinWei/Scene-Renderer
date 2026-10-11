#!/usr/bin/env python3
"""Additive ocean radiance AOVs and matched raw PT diagnostics."""
import argparse
import json
import os
from pathlib import Path
import subprocess
from analyze_ocean_glints import digest, film, metrics


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--render', action='store_true')
    p.add_argument('--directory', type=Path, required=True)
    p.add_argument('--angular-moments', action='store_true', help='Previous angular-Gaussian solar and centre sky ablation')
    p.add_argument('--center-sky', action='store_true', help='Keep centre sky while testing slope-space solar')
    p.add_argument('--executable', type=Path, default=Path('build/pt-native-rt/runtime/Scene-Renderer'))
    args = p.parse_args()
    root = args.directory; root.mkdir(parents=True, exist_ok=True)
    shader_dir = args.executable.parent/'rhi/shaders'
    signature = {'binary_sha256': digest(args.executable), 'shader_sha256':
                 {str(v): digest(v) for v in sorted(shader_dir.iterdir()) if v.suffix in ('.spv', '.metallib')}}
    components = ['body', 'reflection', 'sun', 'foam', 'border']
    names = ['beauty']+components+['downward-reflection']
    commands = []
    if args.render:
        for name in names:
            cmd = [str(args.executable), '--path-trace-gpu', 'ocean-sea-state', '--backend', 'Metal',
                   '--pt-traversal', 'native', '--pt-size', '640x360', '--pt-samples', '512' if name == 'beauty' else '1',
                   '--pt-bounces', '24' if name == 'beauty' else '2', '--pt-time', '8', '--pt-ocean-grid', '513',
                   '--pt-fixed', '--pt-compare-ocean', '--pt-water-surface-roughness', '0', '--pt-output', str(root/name)]
            if name == 'beauty': cmd += ['--pt-denoise']
            env = dict(os.environ)
            for key in ('SCENERENDERER_WATER_GLINT_BOX', 'SCENERENDERER_WATER_UNFILTERED',
                        'SCENERENDERER_WATER_TAA_LEGACY', 'SCENERENDERER_WATER_TAA_RESAMPLE',
                        'SCENERENDERER_OCEAN_REFERENCE_AOV', 'SCENERENDERER_OCEAN_UNCACHED',
                        'SCENERENDERER_WATER_ANGULAR_MOMENTS', 'SCENERENDERER_WATER_CENTER_SKY'):
                env.pop(key, None)
            overrides = {'SCENERENDERER_OCEAN_REFERENCE_AOV': name}
            if args.angular_moments: overrides['SCENERENDERER_WATER_ANGULAR_MOMENTS'] = '1'
            if args.center_sky: overrides['SCENERENDERER_WATER_CENTER_SKY'] = '1'
            env.update(overrides)
            print('Rendering', name, flush=True)
            with (root/(name+'.log')).open('w') as log:
                subprocess.run(cmd, env=env, stdout=log, stderr=subprocess.STDOUT, check=True)
            assert signature['binary_sha256'] == digest(args.executable)
            assert all(digest(path) == value for path, value in signature['shader_sha256'].items())
            commands.append({'case': name, 'args': cmd, 'environment': overrides, **signature})
            (root/'commands.json').write_text(json.dumps(commands, indent=2)+'\n')
    else:
        commands = json.loads((root/'commands.json').read_text())
    assert len(commands) == len(names)
    assert all({k: c[k] for k in signature} == signature for c in commands), 'Stale captures: rerun with --render'
    mask = film(root/'beauty-water-mask.pfm'); pt = film(root/'beauty.pfm')
    pt_report = json.loads((root/'beauty.json').read_text())
    assert pt_report['average_spp'] == 512 and pt_report['non_finite_samples'] == 0
    report = {'scope': '640x360, time=8, 513 grid. Above-water raster AOVs are additive only inside water. Raw 512 spp PT before OIDN; finite-sample transport reference.',
              **signature, 'commands': commands, 'pt_report': pt_report, 'cases': {}, 'raw_sha256': {},
              'raster_reports': {n: json.loads((root/(n+'-comparison.json')).read_text()) for n in names}}
    for suffix in ('-raster', '-raster-taa'):
        beauty = film(str(root/'beauty')+suffix+'.pfm')
        aovs = {n: film(str(root/n)+suffix+'.pfm') for n in components}
        down = film(str(root/'downward-reflection')+suffix+'.pfm')
        indices = [i for i in range(0, len(mask[2]), 3) if mask[2][i] > .5]
        if suffix == '-raster':
            error = max(abs(beauty[2][i+c]-sum(a[2][i+c] for a in aovs.values())) for i in indices for c in range(3))
            relative = max(abs(beauty[2][i+c]-sum(a[2][i+c] for a in aovs.values()))/
                           max(abs(beauty[2][i+c]), abs(sum(a[2][i+c] for a in aovs.values())), 1e-5)
                           for i in indices for c in range(3))
            # Each AOV independently rounds through the RGBA16F HDR target.
            assert relative < .002, f'Radiance AOV sum exceeds half-float rounding: {relative}'
            report['additive_max_absolute_rgb_error'] = error
            report['additive_max_relative_rgb_error'] = relative
        # Temporal rejection/weights depend on radiance; do not assert TAA additivity.
        report['cases'][suffix] = {'pt': metrics(beauty, pt, mask),
                                  'components': {n: metrics(v, beauty, mask) for n, v in aovs.items()},
                                  'centre_normal_downward_fraction': sum(down[2][i] for i in indices)/len(indices)}
        for n in names:
            path = str(root/n)+suffix+'.pfm'; report['raw_sha256'][path] = digest(path)
            metadata = json.loads((root/(n+'-comparison.json')).read_text())
            assert metadata['raster_aov'] == n
    report['raw_sha256'][str(root/'beauty.pfm')] = digest(root/'beauty.pfm')
    report['source_sha256'] = {path: digest(path) for path in ('tools/diagnose_ocean_components.py',
        'src/PT/OceanReference.cpp', 'src/rhi/shaders/ocean-surface-common.glsl', 'src/rhi/shaders/water-surface-model.glsl')}
    (root/'validation.json').write_text(json.dumps(report, indent=2)+'\n')
    print('Additive error', report['additive_max_absolute_rgb_error'])
    for suffix, values in report['cases'].items():
        print(suffix, 'water/PT', values['pt']['water']['relative_l1'],
              'centre normal downward', values['centre_normal_downward_fraction'], flush=True)
        for n, values in values['components'].items():
            print(n, {r: round(values[r]['mean_Y'], 6) for r in ('water', 'near_water', 'far_water', 'side_water')})


if __name__ == '__main__':
    main()
