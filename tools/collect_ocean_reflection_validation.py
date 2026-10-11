#!/usr/bin/env python3
"""Publish a guarded same-build ablation of ocean reflection integration."""
import argparse
import json
from pathlib import Path
import shutil
from analyze_ocean_glints import digest


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--before', type=Path, required=True)
    p.add_argument('--after', type=Path, required=True)
    args = p.parse_args()
    reports = {name: json.loads((root/'validation.json').read_text())
               for name, root in (('before', args.before), ('after', args.after))}
    a, b = reports['before'], reports['after']
    assert a['binary_sha256'] == b['binary_sha256'] and a['shader_sha256'] == b['shader_sha256'], 'Different builds'
    executable = Path(b['commands'][0]['args'][0])
    assert digest(executable) == b['binary_sha256']
    assert all(digest(path) == value for path, value in b['shader_sha256'].items()), 'Stale shaders'
    for name, v in reports.items():
        assert all(digest(path) == value for path, value in v['raw_sha256'].items()), 'Modified capture'
        assert v['additive_max_relative_rgb_error'] < .002
        for cmd in v['commands']:
            assert bool(cmd['environment'].get('SCENERENDERER_WATER_ANGULAR_MOMENTS')) == (name == 'before')
            assert not cmd['environment'].get('SCENERENDERER_WATER_CENTER_SKY')
    assert digest(args.before/'beauty.pfm') == digest(args.after/'beauty.pfm'), 'PT reference changed'
    assert digest(args.before/'beauty-water-mask.pfm') == digest(args.after/'beauty-water-mask.pfm')
    images = Path('img/path-tracing'); result = {'scope': b['scope'], 'ablation': reports,
        'pt_raw_identical': True, 'images': [], 'validation_logs': {}}
    for name, root in (('before', args.before), ('after', args.after)):
        for suffix in ('-raster', '-raster-taa'):
            dst = images/f'ocean-reflection-{name}{suffix}.png'; shutil.copyfile(str(root/'beauty')+suffix+'.png', dst)
            result['images'].append({'path': str(dst), 'sha256': digest(dst)})
        for aov in ('reflection', 'sun', 'downward-reflection'):
            dst = images/f'ocean-reflection-{name}-{aov}.png'; shutil.copyfile(root/(aov+'-raster.png'), dst)
            result['images'].append({'path': str(dst), 'sha256': digest(dst)})
    for suffix in ('', '-denoised'):
        dst = images/f'ocean-reflection-pt{suffix}.png'; shutil.copyfile(str(args.after/'beauty')+suffix+'.png', dst)
        result['images'].append({'path': str(dst), 'sha256': digest(dst)})
    for name in ('metal', 'vulkan', 'water-metal', 'water-vulkan'):
        path = Path('/tmp/scene-ocean-components-'+name+'.log')
        log = path.read_text()
        assert 'Water slope-space finite sun / grazing reflection Jacobian' in log and 'PASS' in log
        assert 'RHI full Ocean spectrum' in log and 'passed' in log
        if name.startswith('water-'): assert 'RHI temporal HDR resolve' in log and 'passed' in log
        result['validation_logs'][name] = {'sha256': digest(path), 'text': log}
    result['source_sha256'] = {path: digest(path) for path in (
        'tools/collect_ocean_reflection_validation.py', 'tools/diagnose_ocean_components.py',
        'src/PT/OceanReference.cpp', 'include/renderer/rhi/OceanSurface.h', 'src/renderer/rhi/OceanSurface.cpp',
        'src/renderer/rhi/OceanValidation.cpp', 'src/rhi/shaders/ocean-surface-common.glsl',
        'src/rhi/shaders/water-surface-model.glsl', 'src/rhi/shaders/water-model-validate.comp')}
    (images/'ocean-reflection-validation.json').write_text(json.dumps(result, indent=2)+'\n')
    for name, v in reports.items():
        for suffix, case in v['cases'].items():
            print(name, suffix, {region: round(value['relative_l1']*100, 2) for region, value in case['pt'].items()})


if __name__ == '__main__':
    main()
