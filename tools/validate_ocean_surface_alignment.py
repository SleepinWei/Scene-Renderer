#!/usr/bin/env python3
"""Matched native FFT normals and box/tent solar reconstruction diagnostics."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
from analyze_ocean_glints import digest, film, reduce, png, metrics


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--render', action='store_true')
    parser.add_argument('--executable', type=Path, default=Path('build/pt-native-rt/runtime/Scene-Renderer'))
    parser.add_argument('--directory', type=Path, default=Path('build/ocean-surface-alignment'))
    args = parser.parse_args()
    root = args.directory; root.mkdir(parents=True, exist_ok=True)
    images = Path('img/path-tracing'); images.mkdir(parents=True, exist_ok=True)
    shaders = args.executable.parent/'rhi/shaders'
    signature = {'binary_sha256': digest(args.executable),
                 'shader_sha256': {str(p): digest(p) for p in sorted(shaders.iterdir()) if p.suffix in ('.spv', '.metallib')}}
    commands_path = root/'commands.json'
    commands = []
    cases = [('box', 1), ('tent', 1), ('box-ssaa4', 4), ('tent-ssaa4', 4)]
    if args.render:
        for name, scale in cases:
            prefix = root/name
            cmd = [str(args.executable), '--path-trace-gpu', 'ocean-sea-state', '--backend', 'Metal',
                   '--pt-traversal', 'native', '--pt-size', f'{640*scale}x{360*scale}',
                   '--pt-samples', '512' if scale == 1 else '1', '--pt-bounces', '24' if scale == 1 else '2',
                   '--pt-time', '8', '--pt-ocean-grid', '513', '--pt-fixed', '--pt-compare-ocean',
                   '--pt-water-surface-roughness', '0', '--pt-output', str(prefix)]
            if scale == 1: cmd += ['--pt-checkpoint-samples', '512', '--pt-denoise']
            env = dict(os.environ, SCENERENDERER_GPU_PROFILE='1')
            for key in ('SCENERENDERER_WATER_UNFILTERED', 'SCENERENDERER_WATER_TAA_LEGACY',
                        'SCENERENDERER_OCEAN_UNCACHED', 'SCENERENDERER_WATER_GLINT_BOX', 'SCENERENDERER_WATER_TAA_RESAMPLE'):
                env.pop(key, None)
            overrides = {'SCENERENDERER_WATER_GLINT_BOX': '1'} if name.startswith('box') else {}
            env.update(overrides)
            print('Rendering', name, flush=True)
            with (root/(name+'.log')).open('w') as log:
                subprocess.run(cmd, env=env, stdout=log, stderr=subprocess.STDOUT, check=True)
            assert signature['binary_sha256'] == digest(args.executable), 'Binary changed during capture'
            assert all(digest(p) == h for p, h in signature['shader_sha256'].items()), 'Shaders changed during capture'
            commands.append({'case': name, 'args': cmd, 'environment_overrides': overrides, **signature})
            commands_path.write_text(json.dumps(commands, indent=2)+'\n')
    else:
        commands = json.loads(commands_path.read_text())
    assert len(commands) == 4
    assert all({k: c[k] for k in signature} == signature for c in commands), 'Stale captures: rerun with --render'
    pt = film(root/'tent.pfm'); mask = film(root/'tent-water-mask.pfm')
    assert digest(root/'box.pfm') == digest(root/'tent.pfm'), 'Realtime reconstruction changed matched PT film'
    pt_meta = json.loads((root/'tent.json').read_text())
    assert pt_meta['average_spp'] == 512 and pt_meta['non_finite_samples'] == 0
    references = {name: reduce(film(str(root/name)+'-raster.pfm'), 4) for name in ('box-ssaa4', 'tent-ssaa4')}
    report = {**signature, 'commands': commands,
              'scope': 'Matched time=8, 640x360, 513 grid, native floating-point FFT normals. Raw 512 spp PT before OIDN; finite-sample reference. 4x raster is a separate reconstruction diagnostic, not transport GT.',
              'pt': pt_meta, 'cases': {}, 'raw_sha256': {}, 'images': [],
              'realtime_filter_changes_pt': False}
    for name, scale in cases:
        report['cases'][name] = {'raster_report': json.loads(Path(str(root/name)+'-comparison.json').read_text())}
        for suffix in ('-raster', '-raster-taa'):
            path = str(root/name)+suffix+'.pfm'; report['raw_sha256'][path] = digest(path)
            if scale != 1: continue
            f = film(path)
            report['cases'][name][suffix] = {'pt': metrics(f, pt, mask),
                                           **{ref: metrics(f, v, mask) for ref, v in references.items()}}
            dst = images/f'ocean-alignment-{name}{suffix}.png'
            shutil.copyfile(str(root/name)+suffix+'.png', dst)
            report['images'].append({'path': str(dst), 'sha256': digest(dst)})
    for name, f in references.items():
        dst = images/f'ocean-alignment-{name}.png'; png(dst, f, pt_meta['exposure'])
        report['images'].append({'path': str(dst), 'sha256': digest(dst)})
        report['cases'][name]['area_average_vs_pt'] = metrics(f, pt, mask)
    for suffix in ('', '-denoised'):
        dst = images/f'ocean-alignment-pt{suffix}.png'; shutil.copyfile(str(root/'tent')+suffix+'.png', dst)
        report['images'].append({'path': str(dst), 'sha256': digest(dst)})
        report['raw_sha256'][str(root/'tent')+suffix+'.pfm'] = digest(str(root/'tent')+suffix+'.pfm')
    sources = ['tools/validate_ocean_surface_alignment.py', 'tools/analyze_ocean_glints.py',
               'include/renderer/rhi/SceneSnapshot.h', 'include/PT/SceneData.h', 'src/PT/CpuPathTracer.cpp',
               'src/PT/ProceduralGeometry.cpp', 'src/PT/CpuPathTracingEntry.cpp', 'src/PT/OceanReference.cpp', 'tests/PT/ProceduralTests.cpp', 'tests/PT/CpuPathTracerTests.cpp', 'src/renderer/rhi/OceanSurface.cpp',
               'src/rhi/shaders/path-trace.comp', 'src/rhi/shaders/ocean-surface-common.glsl',
               'src/rhi/shaders/water-surface-model.glsl', 'src/rhi/shaders/temporal.comp', 'src/renderer/rhi/GpuTemporal.cpp', 'src/renderer/rhi/TemporalValidation.cpp']
    report['source_sha256'] = {p: digest(p) for p in sources}
    report['validation_logs'] = {name: Path(f'/tmp/scene-ocean-refine-{name}.log').read_text()
                                 for name in ('cpu', 'metal', 'vulkan', 'native-pt', 'temporal-metal', 'temporal-vulkan', 'vulkan-pt', 'vulkan-cpu')
                                 if Path(f'/tmp/scene-ocean-refine-{name}.log').exists()}
    if all((root/f'vulkan-{kind}.json').exists() for kind in ('cpu', 'gpu')):
        a, b = (film(root/f'vulkan-{kind}.pfm') for kind in ('gpu', 'cpu'))
        assert a[:2] == b[:2] == (32, 24)
        error = sum(abs(x-y) for x, y in zip(a[2], b[2]))/max(sum(abs(x) for x in b[2]), 1e-30)
        assert error < .05, 'Vulkan float-normal path differs from CPU'
        reports = {kind: json.loads((root/f'vulkan-{kind}.json').read_text()) for kind in ('gpu', 'cpu')}
        for v in reports.values():
            assert v['average_spp'] == 128 and v['non_finite_samples'] == 0
            assert v['frozen_oceans'][0]['normal_encoding'] == 'native-rgba32f-separate-periods'
        report['vulkan_cpu_probe'] = {'scope': '32x24, 128 spp, depth 6, time=8, grid=33; Vulkan compute traversal vs CPU on Vulkan FFT captures. Separate small correctness probe, not performance comparison.',
                                     'relative_rgb_l1': error, 'reports': reports,
                                     'raw_sha256': {kind: digest(root/f'vulkan-{kind}.pfm') for kind in ('gpu', 'cpu')}}
    (images/'ocean-alignment-validation.json').write_text(json.dumps(report, indent=2, ensure_ascii=False)+'\n')
    for name in ('box', 'tent'):
        for suffix in ('-raster', '-raster-taa'):
            d = report['cases'][name][suffix]
            print(name, suffix, 'near/4x-box', d['box-ssaa4']['near_water']['relative_l1'],
                  'water/PT', d['pt']['water']['relative_l1'], 'side/PT', d['pt']['side_water']['relative_l1'])


if __name__ == '__main__':
    main()
