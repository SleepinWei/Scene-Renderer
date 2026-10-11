#!/usr/bin/env python3
"""Collect matched ocean raster/PT films and optionally benchmark spectrum caching.

Uses only the Python standard library. PFM data are compared before tone mapping
and denoising. Raster is a different transport approximation, not a PT truth test.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import statistics
import subprocess

from benchmark_pt_native import comparison, pfm


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def command(executable, prefix, samples=512, clear=False, surface_model='legacy', roughness=0):
    result = [str(executable), '--path-trace-gpu', 'ocean-sea-state', '--backend', 'Metal',
              '--pt-traversal', 'native', '--pt-size', '640x360', '--pt-samples', str(samples),
              '--pt-bounces', '24', '--pt-time', '8', '--pt-ocean-grid', '513',
              '--pt-fixed', '--pt-checkpoint-samples', str(samples), '--pt-compare-ocean',
              '--pt-output', str(prefix)]
    if samples > 1:
        result.append('--pt-denoise')
    if clear:
        result += ['--pt-water-scattering-scale', '0']
    result += ['--pt-water-surface-legacy'] if surface_model == 'legacy' else ['--pt-water-surface-roughness', str(roughness)]
    return result


def run(executable, prefix, samples, uncached=False, clear=False, surface_model='legacy', roughness=0):
    args = command(executable, prefix, samples, clear, surface_model, roughness)
    environment = dict(os.environ, SCENERENDERER_GPU_PROFILE='1')
    environment.pop('SCENERENDERER_OCEAN_UNCACHED', None)
    if uncached:
        environment['SCENERENDERER_OCEAN_UNCACHED'] = '1'
    with prefix.with_suffix('.log').open('w') as log:
        subprocess.run(args, env=environment, stdout=log, stderr=subprocess.STDOUT, check=True)
    print('Rendered', prefix, flush=True)
    return {'args': args, 'uncached': uncached, 'binary_sha256': digest(executable)}


def film(path):
    width, height, bottom_up = pfm(path)
    rows = [bottom_up[y*width*3:(y+1)*width*3] for y in range(height-1, -1, -1)]
    return width, height, tuple(value for row in rows for value in row)


def collect_case(prefix):
    report = json.loads(prefix.with_suffix('.json').read_text())
    raster = json.loads(Path(str(prefix)+'-comparison.json').read_text())
    if report['non_finite_samples'] or report['average_spp'] != report['samples']:
        raise ValueError('Invalid or adaptive PT samples')
    if report['width'] != raster['width'] or report['height'] != raster['height'] or report['exposure'] != raster['exposure']:
        raise ValueError('Dimensions or exposure do not match')
    if report['frozen_time_seconds'] != raster['time_seconds']:
        raise ValueError('Time does not match')
    width, height, pt = film(prefix.with_suffix('.pfm'))
    mw, mh, mask = film(Path(str(prefix)+'-water-mask.pfm'))
    if (mw, mh) != (width, height):
        raise ValueError('Mask dimensions differ')
    comparisons = {}
    for suffix in ('-raster', '-raster-taa'):
        rw, rh, rt = film(Path(str(prefix)+suffix+'.pfm'))
        if (rw, rh) != (width, height):
            raise ValueError('Raster film dimensions differ')
        regions = {'water': [], 'near_water': [], 'far_water': [], 'side_water': [], 'sky': []}
        for y in range(height):
            for x in range(width):
                i = (y*width+x)*3
                if mask[i] > .5:
                    regions['water'].append(i)
                    if y/height >= .7:
                        regions['near_water'].append(i)
                    if .36 <= y/height < .5:
                        regions['far_water'].append(i)
                    if y/height >= .5 and (x/width < .35 or x/width > .65):
                        regions['side_water'].append(i)
                elif y/height < .3:
                    regions['sky'].append(i)
        metrics = {}
        for name, indices in regions.items():
            if not indices:
                continue
            a = [rt[i+c] for i in indices for c in range(3)]
            b = [pt[i+c] for i in indices for c in range(3)]
            data = comparison(a, b)
            data['pixels'] = len(indices)
            data['raster_mean_rgb'] = [sum(rt[i+c] for i in indices)/len(indices) for c in range(3)]
            data['pt_mean_rgb'] = [sum(pt[i+c] for i in indices)/len(indices) for c in range(3)]
            weights = (.2126, .7152, .0722)
            data['raster_mean_luminance'] = sum(w*v for w, v in zip(weights, data['raster_mean_rgb']))
            data['pt_mean_luminance'] = sum(w*v for w, v in zip(weights, data['pt_mean_rgb']))
            for label, values in (('raster', rt), ('pt', pt)):
                luminance = sorted(sum(weights[c]*values[i+c] for c in range(3)) for i in indices)
                data[label+'_luminance_percentiles'] = {
                    str(q): luminance[round((len(luminance)-1)*q/100)] for q in (50, 90, 99)}
                data[label+'_fraction_luminance_above_1'] = sum(v > 1 for v in luminance)/len(luminance)
            metrics[name] = data
        comparisons[suffix] = metrics
    return {'pt_report': report, 'raster_report': raster, 'linear_regions': comparisons,
            'raw_sha256': {suffix: digest(Path(str(prefix)+suffix+'.pfm'))
                           for suffix in ('', '-raster', '-raster-taa', '-water-mask')}}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--executable', type=Path, default=Path('build/pt-native-rt/runtime/Scene-Renderer'))
    parser.add_argument('--directory', type=Path, default=Path('build/ocean-comparison'))
    parser.add_argument('--output', type=Path, default=Path('img/path-tracing/ocean-comparison-validation.json'))
    parser.add_argument('--render', action='store_true', help='Render both matched transport cases')
    parser.add_argument('--case', choices=('combined', 'clear'), action='append', help='Restrict --render to these cases')
    parser.add_argument('--surface-model', choices=('legacy', 'physical'), default='legacy')
    parser.add_argument('--test-log-prefix', default='/tmp/scene-ocean-cache')
    parser.add_argument('--image-prefix', default='ocean-comparison')
    parser.add_argument('--extra-case', choices=('legacy', 'rough'), action='append', default=[], help='Add legacy/no-scattering and roughness 0.12/no-scattering cases')
    parser.add_argument('--benchmark-cache', action='store_true', help='Run three alternating pairs; PT 1 spp only triggers the shared capture')
    args = parser.parse_args()
    root = args.directory;root.mkdir(parents=True, exist_ok=True)
    executable = args.executable
    before = digest(executable)
    history_path = root/'commands.json'
    history = json.loads(history_path.read_text()) if history_path.exists() else []
    def record(entry):
        output = entry['args'][entry['args'].index('--pt-output')+1]
        history[:] = [old for old in history if old['args'][old['args'].index('--pt-output')+1] != output]
        history.append(entry)
        history_path.write_text(json.dumps(history, indent=2)+'\n')
    if args.render:
        for name, clear in (('combined', False), ('clear', True)):
            if args.case and name not in args.case:
                continue
            record(run(executable, root/name, 512, clear=clear, surface_model=args.surface_model))
        for name in args.extra_case:
            record(run(executable, root/name, 512, clear=True,
                       surface_model='legacy' if name == 'legacy' else 'physical', roughness=.12 if name == 'rough' else 0))
    if args.benchmark_cache:
        for repeat in range(3):
            for uncached in ([False, True] if repeat % 2 == 0 else [True, False]):
                prefix = root/f'cache-{repeat}-{"off" if uncached else "on"}'
                record(run(executable, prefix, 1, uncached, surface_model=args.surface_model))
    if digest(executable) != before:
        raise ValueError('Executable changed while rendering')
    if any(entry['binary_sha256'] != before for entry in history):
        raise ValueError('Recorded binary differs: rerender both cases and cache pairs, or preserve the original validation JSON')
    cases = {name: collect_case(root/name) for name in ['combined', 'clear']+args.extra_case}
    a = cases['combined']['pt_report']['frozen_oceans'][0]
    b = cases['clear']['pt_report']['frozen_oceans'][0]
    if any(a[k] != b[k] for k in a if k != 'scattering') or any(b['scattering']):
        raise ValueError('Clear-water ablation changed more than scattering')
    model_comparisons = {}
    if 'legacy' in cases:
        model_comparisons['smooth_pt_legacy_vs_physical'] = comparison(film(root/'legacy.pfm')[2], film(root/'clear.pfm')[2])
        if not model_comparisons['smooth_pt_legacy_vs_physical']['exact']:
            raise ValueError('Enabling the smooth raster model changed the matched PT reference')
    if 'rough' in cases:
        if cases['rough']['pt_report']['media'][0]['dielectric_roughness'] != cases['rough']['raster_report']['water_surface_models'][0]['roughness']:
            raise ValueError('Rough water material parameter differs between raster and PT')
    images = []
    args.output.parent.mkdir(parents=True, exist_ok=True)
    for name in cases:
        for suffix, label in (('-raster-taa', '-realtime'), ('-raster', '-realtime-single'), ('', '-pt-raw'), ('-denoised', '-pt')):
            src = Path(str(root/name)+suffix+'.png')
            dst = args.output.parent/f'{args.image_prefix}-{name}{label}.png'
            if not src.exists():
                continue  # Auxiliary low-spp captures need not have a denoised film.
            shutil.copyfile(src, dst)
            images.append({'path': str(dst), 'sha256': digest(dst), 'denoised': suffix == '-denoised'})
    benchmark = []
    for repeat in range(3):
        on, off = root/f'cache-{repeat}-on', root/f'cache-{repeat}-off'
        if not Path(str(on)+'-comparison.json').exists():
            continue
        cached = json.loads(Path(str(on)+'-comparison.json').read_text())
        uncached = json.loads(Path(str(off)+'-comparison.json').read_text())
        item = {'repeat': repeat, 'cached': cached, 'uncached': uncached, 'linear_agreement': {}}
        for mode, suffix in (('raster', '-raster'), ('raster_taa', '-raster-taa')):
            a, b = film(Path(str(on)+suffix+'.pfm')), film(Path(str(off)+suffix+'.pfm'))
            if a[:2] != b[:2]:
                raise ValueError('Cache benchmark dimensions differ')
            metrics = comparison(a[2], b[2]);item['linear_agreement'][mode] = metrics
            if metrics['relative_l1'] > 1e-6:
                raise ValueError('Initial spectrum cache changed rendered radiance')
            if cached[mode].get('warm_initial_spectrum_dispatches') != 0 or uncached[mode].get('warm_initial_spectrum_dispatches') != 32:
                raise ValueError('Missing cache dispatch evidence (requires Metal GPU profiler)')
        benchmark.append(item)
    timing = {}
    if benchmark:
        for mode in ('raster', 'raster_taa'):
            on = statistics.median(b['cached'][mode]['median_wall_ms'] for b in benchmark)
            off = statistics.median(b['uncached'][mode]['median_wall_ms'] for b in benchmark)
            timing[mode] = {'cached_median_of_medians_ms': on, 'uncached_median_of_medians_ms': off, 'speedup': off/on}
    sources = ['include/renderer/rhi/GpuOcean.h', 'src/renderer/rhi/GpuOcean.cpp',
               'src/rhi/shaders/ocean-initial.comp', 'src/rhi/shaders/ocean-height.comp',
               'src/renderer/rhi/OceanValidation.cpp', 'src/PT/OceanReference.cpp',
               'src/PT/CpuPathTracingEntry.cpp', 'tools/compare_ocean_rendering.py',
               'include/PT/OceanReference.h', 'src/PT/ProceduralCapture.cpp', 'src/PT/ProceduralGeometry.cpp',
               'src/PT/CpuPathTracer.cpp', 'src/PT/GpuPathTracer.cpp', 'src/renderer/rhi/OceanSurface.cpp',
               'src/renderer/rhi/SceneSnapshot.cpp', 'src/renderer/rhi/ClassicScenes.cpp',
               'src/renderer/rhi/AtmosphereBake.cpp', 'src/rhi/shaders/ocean-surface.frag',
               'src/rhi/shaders/ocean-surface.vert', 'src/rhi/shaders/water-waves.glsl',
               'src/rhi/shaders/ocean-parameters.glsl', 'src/rhi/shaders/ocean-normalfoam.comp',
               'src/rhi/shaders/path-trace.comp', 'include/component/WaterSurfaceModel.h', 'src/rhi/shaders/dielectric-fresnel.glsl', 'src/rhi/shaders/water-surface-model.glsl', 'src/rhi/shaders/water-model-validate.comp']
    payload = {'date': '2026-10-10', 'device': 'Apple M4 / macOS / Metal native RT',
               'binary_sha256': before, 'commands': history, 'cases': cases, 'cache_benchmark': benchmark,
               'cache_timing_summary': timing, 'images': images, 'model_comparisons': model_comparisons,
               'source_sha256': {p: digest(Path(p)) for p in sources},
               'interpretation': 'Different transport models and finite sampling. Linear differences are diagnostics, not an accuracy score against ground truth. PT sampling time and raster frame wall latency have different scopes.'}
    payload['numerical_test_logs'] = {}
    for backend in ('metal', 'vulkan'):
        path = Path(f'{args.test_log_prefix}-{backend}.log')
        if path.exists():
            payload['numerical_test_logs'][backend] = path.read_text()
    native_log = Path(args.test_log_prefix+'-pt-regression.log')
    if native_log.exists():
        payload['numerical_test_logs']['native_pt'] = native_log.read_text()
    shaders = executable.parent/'rhi/shaders'
    payload['cooked_shader_sha256'] = {str(p): digest(p) for p in sorted(shaders.iterdir())
                                      if p.suffix in ('.metallib', '.spv')}
    args.output.write_text(json.dumps(payload, indent=2, ensure_ascii=False)+'\n')
    print(json.dumps(timing, indent=2))


if __name__ == '__main__':
    main()
