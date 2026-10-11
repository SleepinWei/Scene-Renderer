#!/usr/bin/env python3
"""Collect existing sea-state render outputs; preserve raw films and measured differences.

Render commands are in docs/ocean-sea-state-and-pt.md. This collector checks
fixed sampling, sea-state ablations, finite films and GPU backend consistency.
CPU/GPU differences are recorded, not certified as exact or independent GT.
"""
import argparse
import hashlib
import json
import shutil
from pathlib import Path

from benchmark_pt_native import comparison, pfm


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def collect(root, destination):
    command_record = json.loads((root / 'commands.json').read_text())
    executable = Path(command_record['commands'][0][0])
    if digest(executable) != command_record['binary_sha256']:
        raise ValueError('Binary changed after the render suite')
    records = {}
    for name in ('combined-final', 'wind-only', 'swell-only', 'cpu-depth8', 'metal-depth8', 'vulkan-depth8',
                 'cpu-depth8-1024', 'metal-depth8-1024'):
        report = json.loads((root / (name + '.json')).read_text())
        width, height, film = pfm(root / (name + '.pfm'))
        if (width, height) != (report['width'], report['height']):
            raise ValueError('Film/report dimensions differ: ' + name)
        if report['non_finite_samples'] != 0 or report['average_spp'] != report['samples']:
            raise ValueError('Nonfinite or nonuniform samples: ' + name)
        records[name] = {'report': report, 'raw_pfm': str(root / (name + '.pfm')),
                         'raw_pfm_sha256': digest(root / (name + '.pfm'))}
    main = records['combined-final']['report']['frozen_oceans'][0]
    changing = {'wind_height_metres', 'swell_height_metres', 'captured_vertex_height_range_metres',
                'captured_vertex_height_rms_metres'}
    for name, zero in (('wind-only', 'swell_height_metres'), ('swell-only', 'wind_height_metres')):
        sea = records[name]['report']['frozen_oceans'][0]
        if sea[zero] != 0 or any(sea[k] != v for k, v in main.items() if k not in changing):
            raise ValueError('Ablation changed other ocean parameters: ' + name)
    def compare(a, b):
        fa, fb = pfm(root / (a + '.pfm')), pfm(root / (b + '.pfm'))
        if fa[:2] != fb[:2]:
            raise ValueError('Comparison dimensions differ')
        return comparison(fa[2], fb[2])
    pairs = {'metal_vs_cpu_64spp': compare('metal-depth8', 'cpu-depth8'),
             'vulkan_vs_cpu_64spp': compare('vulkan-depth8', 'cpu-depth8'),
             'metal_vs_vulkan_64spp': compare('metal-depth8', 'vulkan-depth8'),
             'metal_vs_cpu_1024spp': compare('metal-depth8-1024', 'cpu-depth8-1024')}
    if pairs['metal_vs_vulkan_64spp']['relative_l1'] > 1e-4:
        raise ValueError('Metal/Vulkan raw-film agreement exceeded relative L1 tolerance')
    images = []
    destination.parent.mkdir(parents=True, exist_ok=True)
    for name, label in (('combined-final', 'ocean-sea-state'), ('wind-only', 'ocean-sea-state-wind'),
                        ('swell-only', 'ocean-sea-state-swell')):
        for suffix, output_suffix in (('-denoised.png', '.png'), ('.png', '-raw.png')):
            src, dst = root / (name + suffix), destination.parent / (label + output_suffix)
            shutil.copyfile(src, dst)
            images.append({'path': str(dst), 'sha256': digest(dst), 'denoised': suffix.startswith('-denoised')})
    realtime = destination.parent / 'ocean-sea-state-realtime.png'
    shutil.copyfile(root / 'realtime/ocean-sea-state.png', realtime)
    images.append({'path': str(realtime), 'sha256': digest(realtime), 'renderer': 'realtime Metal'})
    sources = ['include/component/OceanSeaState.h', 'include/renderer/rhi/OceanSpectrum.h',
               'include/renderer/rhi/GpuOcean.h', 'src/renderer/rhi/GpuOcean.cpp',
               'src/rhi/shaders/ocean-height.comp', 'src/rhi/shaders/ocean-gaussian.comp',
               'src/rhi/shaders/ocean-parameters.glsl', 'src/PT/ProceduralCapture.cpp',
               'src/PT/ProceduralGeometry.cpp', 'src/PT/CpuPathTracingEntry.cpp',
               'src/renderer/rhi/ClassicScenes.cpp', 'src/renderer/rhi/OceanValidation.cpp']
    payload = {'date': '2026-10-10', 'device': 'Apple M4 / macOS', 'commands': command_record,
               'renders': records, 'raw_comparisons': pairs, 'images': images,
               'additional_commands': [[str(executable), '--path-trace' if mode == 'cpu' else '--path-trace-gpu', 'ocean-sea-state', '--backend', 'Metal', '--pt-size', '64x40', '--pt-samples', '1024', '--pt-bounces', '12', '--pt-fixed', '--pt-checkpoint-samples', '1024', '--pt-ocean-depth', '8', '--pt-ocean-grid', '513', '--pt-output', str(root / (mode + '-depth8-1024'))] + ([] if mode == 'cpu' else ['--pt-traversal', 'native']) for mode in ('cpu', 'metal')] + [[str(executable), '--pt-self-test', '--backend', 'Metal']],
               'source_sha256': {p: digest(Path(p)) for p in sources},
               'interpretation': 'Finite-sample implementation comparisons, not independent ocean or transport ground truth; timings are single-run CPU wall times.',
               'numerical_test_logs': {},
               'realtime_metrics': json.loads((root / 'realtime/ocean-sea-state-water-metrics.json').read_text())}
    for key, path in [('metal_ocean', '/tmp/scene-ocean-sea-metal-final.log'),
                      ('vulkan_ocean', '/tmp/scene-ocean-sea-vulkan-final.log'),
                      ('cpu_procedural', '/tmp/scene-ocean-sea-focused-cpu.log'),
                      ('native_pt', '/tmp/scene-ocean-sea-pt-self-test.log')]:
        log = Path(path).read_text()
        if key.endswith('ocean') and 'sea state:' not in log:
            raise ValueError('Missing sea-state numerical evidence')
        if key == 'native_pt' and 'CPU PT render-thread HDR sky bake and importance sampling passed' not in log:
            raise ValueError('Full native PT regression did not complete')
        payload['numerical_test_logs'][key] = log
    destination.write_text(json.dumps(payload, indent=2, ensure_ascii=False) + '\n')
    print(json.dumps(pairs, indent=2))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--render-directory', type=Path, default=Path('build/ocean-sea-state'))
    parser.add_argument('--output', type=Path, default=Path('img/path-tracing/ocean-sea-state-validation.json'))
    args = parser.parse_args()
    collect(args.render_directory, args.output)
