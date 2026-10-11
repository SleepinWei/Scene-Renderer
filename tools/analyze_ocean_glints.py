#!/usr/bin/env python3
"""Matched glint-filter ablations and linear raster supersampling diagnostics.

The one-sample PT invocation supplies the shared capture/camera; its film is
never used as ground truth. SSAA is a raster reference, not converged transport.
"""
import argparse
import array
import hashlib
import json
import math
import os
from pathlib import Path
import shutil
import statistics
import struct
import subprocess
import zlib


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def film(path):
    with Path(path).open('rb') as f:
        assert f.readline().strip() == b'PF'
        w, h = map(int, f.readline().split())
        assert float(f.readline()) < 0
        v = array.array('f'); v.frombytes(f.read())
    assert len(v) == w*h*3 and all(math.isfinite(x) for x in v)
    return w, h, v  # PFM bottom-up; comparisons and masks use the same order.


def reduce(f, factor):
    w, h, v = f; rw, rh = w//factor, h//factor
    assert w == rw*factor and h == rh*factor
    out = array.array('f', [0])*(rw*rh*3)
    for y in range(rh):
        for x in range(rw):
            for c in range(3):
                out[(y*rw+x)*3+c] = sum(v[((y*factor+dy)*w+x*factor+dx)*3+c]
                                      for dy in range(factor) for dx in range(factor))/(factor*factor)
    return rw, rh, out


def png(path, f, exposure):
    w, h, v = f
    raw = bytearray()
    for y in range(h-1, -1, -1):
        raw.append(0)
        for i in range(y*w*3, (y+1)*w*3):
            raw.append(round((1-math.exp(-max(v[i], 0)*exposure))**(1/2.2)*255))
    def chunk(tag, data):
        return struct.pack('>I', len(data))+tag+data+struct.pack('>I', zlib.crc32(tag+data))
    Path(path).write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0))+
                          chunk(b'IDAT', zlib.compress(raw))+chunk(b'IEND', b''))


def metrics(f, reference, mask):
    w, h, v = f; assert f[:2] == reference[:2] == mask[:2]
    r, m = reference[2], mask[2]
    def luminance(data, i):
        return data[i]*.2126+data[i+1]*.7152+data[i+2]*.0722
    regions = {name: [] for name in ('water', 'near_water', 'far_water', 'glint', 'side_water')}
    for y in range(h):
        screen_y = (h-1-y)/h
        for x in range(w):
            i = (y*w+x)*3
            if m[i] <= .5: continue
            regions['water'].append(i)
            if screen_y >= .7: regions['near_water'].append(i)
            if .36 <= screen_y < .5: regions['far_water'].append(i)
            if .35 <= x/w <= .65 and screen_y >= .36: regions['glint'].append(i)
            # Separate the broad solar corridor from ordinary water appearance.
            # This is a fixed screen ROI, not a path classification / AOV.
            if x/w < .35 or x/w > .65: regions['side_water'].append(i)
    result = {}
    for name, indices in regions.items():
        values = [luminance(v, i) for i in indices]; refs = [luminance(r, i) for i in indices]
        error = sum(abs(v[i+c]-r[i+c]) for i in indices for c in range(3))
        denom = sum(abs(r[i+c]) for i in indices for c in range(3))
        selected = set(indices); gradient = ref_gradient = edges = 0
        for i in indices:
            for j in (i+3, i+w*3):
                if j not in selected or (j == i+3 and (i//3)%w == w-1): continue
                gradient += abs(luminance(v, i)-luminance(v, j))
                ref_gradient += abs(luminance(r, i)-luminance(r, j)); edges += 1
        result[name] = {'pixels': len(indices), 'relative_l1': error/max(denom, 1e-30),
                        'mean_Y': statistics.mean(values), 'reference_mean_Y': statistics.mean(refs),
                        'median_Y': statistics.median(values), 'fraction_Y_above_1': sum(x > 1 for x in values)/len(values),
                        'mean_absolute_neighbour_gradient_Y': gradient/max(edges, 1),
                        'reference_mean_absolute_neighbour_gradient_Y': ref_gradient/max(edges, 1)}
    return result


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--render', action='store_true')
    p.add_argument('--executable', type=Path, default=Path('build/pt-native-rt/runtime/Scene-Renderer'))
    p.add_argument('--directory', type=Path, default=Path('build/ocean-aa'))
    p.add_argument('--images', type=Path, default=Path('img/path-tracing'))
    args = p.parse_args(); root = args.directory; root.mkdir(parents=True, exist_ok=True)
    args.images.mkdir(parents=True, exist_ok=True)
    binary = digest(args.executable)
    shaders = args.executable.parent/'rhi/shaders'
    shader_hashes = {str(v): digest(v) for v in sorted(shaders.iterdir()) if v.suffix in ('.metallib', '.spv')}
    cases = [('unfiltered', 1, {'SCENERENDERER_WATER_UNFILTERED': '1'}),
             ('spatial-only', 1, {'SCENERENDERER_WATER_TAA_LEGACY': '1'}), ('filtered', 1, {}),
             ('ssaa4-unfiltered', 4, {'SCENERENDERER_WATER_UNFILTERED': '1'}), ('ssaa4-filtered', 4, {})]
    commands_path = root/'aa-commands.json'
    commands = json.loads(commands_path.read_text()) if commands_path.exists() else []
    commands = [c for c in commands if c['case'] in {name for name, _, _ in cases}]
    if args.render:
        for name, scale, overrides in cases:
            prefix = root/name
            cmd = [str(args.executable), '--path-trace-gpu', 'ocean-sea-state', '--backend', 'Metal',
                   '--pt-traversal', 'native', '--pt-size', f'{640*scale}x{360*scale}', '--pt-samples', '1',
                   '--pt-bounces', '2', '--pt-time', '8', '--pt-ocean-grid', '513', '--pt-fixed',
                   '--pt-compare-ocean', '--pt-water-surface-roughness', '0', '--pt-output', str(prefix)]
            env = dict(os.environ, SCENERENDERER_GPU_PROFILE='1')
            for key in ('SCENERENDERER_WATER_UNFILTERED', 'SCENERENDERER_WATER_TAA_LEGACY', 'SCENERENDERER_OCEAN_UNCACHED'):
                env.pop(key, None)
            env.update(overrides)
            with prefix.with_suffix('.log').open('w') as f: subprocess.run(cmd, env=env, stdout=f, stderr=subprocess.STDOUT, check=True)
            commands = [v for v in commands if v['case'] != name]
            commands.append({'case': name, 'args': cmd, 'environment': overrides, 'binary_sha256': binary, 'shader_sha256': shader_hashes})
            commands_path.write_text(json.dumps(commands, indent=2)+'\n')
            print('Rendered', name, flush=True)
    assert len(commands) == len(cases) and all(c['binary_sha256'] == binary and c.get('shader_sha256') == shader_hashes for c in commands), 'Rerender all cases after binary change'
    assert digest(args.executable) == binary
    assert all(digest(k) == v for k, v in shader_hashes.items()), 'Cooked shaders changed during rendering'
    refs = {name: reduce(film(str(root/name)+'-raster.pfm'), 4) for name in ('ssaa4-unfiltered', 'ssaa4-filtered')}
    mask = film(str(root/'filtered')+'-water-mask.pfm')
    report = {'scope': 'Frozen sea, 640x360, same snapshot and exporter. Linear 4x raster area averages, not converged PT GT. Gradient alone is not an aliasing score.',
              'binary_sha256': binary, 'commands': commands, 'cases': {}, 'images': [], 'raw_sha256': {}}
    for name, scale, _ in cases:
        metadata = json.loads(Path(str(root/name)+'-comparison.json').read_text())
        pt = json.loads((root/name).with_suffix('.json').read_text())
        assert pt['non_finite_samples'] == 0
        report['cases'][name] = {'raster_report': metadata, 'pt_invocation_role': 'Capture only; 1 spp PT film excluded from comparison'}
        for suffix in ('-raster', '-raster-taa'):
            path = str(root/name)+suffix+'.pfm'; report['raw_sha256'][path] = digest(path)
            f = film(path)
            if scale == 1:
                report['cases'][name][suffix] = {key: metrics(f, ref, mask) for key, ref in refs.items()}
                dst = args.images/f'ocean-aa-{name}{suffix}.png'; shutil.copyfile(str(root/name)+suffix+'.png', dst)
                report['images'].append({'path': str(dst), 'sha256': digest(dst)})
    for name, ref in refs.items():
        dst = args.images/f'ocean-aa-{name}.png'; png(dst, ref, 1.4)
        report['images'].append({'path': str(dst), 'sha256': digest(dst)})
    report['reference_agreement'] = metrics(refs['ssaa4-filtered'], refs['ssaa4-unfiltered'], mask)
    matched = root/'pt-matched'
    provenance_path = root/'pt-matched-provenance.json'
    provenance = json.loads(provenance_path.read_text()) if provenance_path.exists() else {}
    if matched.with_suffix('.json').exists() and provenance.get('binary_sha256') == binary and provenance.get('shader_sha256') == shader_hashes:
        pt = json.loads(matched.with_suffix('.json').read_text())
        assert pt['samples'] == 512 and pt['average_spp'] == 512 and pt['non_finite_samples'] == 0
        previous = Path('build/ocean-surface-physical/combined.pfm')
        report['matched_pt'] = {'pt_report': pt, 'raster_report': json.loads(Path(str(matched)+'-comparison.json').read_text()),
                                'raw_sha256': digest(matched.with_suffix('.pfm')),
                                'previous_phase_raw_identical': previous.exists() and digest(previous) == digest(matched.with_suffix('.pfm')),
                                'args': provenance['args']}
        pt_film = film(matched.with_suffix('.pfm'))
        pt_mask = film(str(matched)+'-water-mask.pfm')
        report['matched_pt']['comparison_scope'] = ('Linear RGB vs raw 512 spp PT, before OIDN / display transform. '
                                                   'Finite-sample reference; solar glint L1 includes sampling / normal-field differences. '
                                                   'side_water excludes the central 35%-65% screen corridor; it is not an AOV.')
        report['matched_pt']['raster_agreement'] = {
            name: {suffix: metrics(film(str(root/name)+suffix+'.pfm'), pt_film, pt_mask)
                   for suffix in ('-raster', '-raster-taa')}
            for name in ('unfiltered', 'spatial-only', 'filtered')}
        report['matched_pt']['ssaa_agreement'] = {name: metrics(ref, pt_film, pt_mask)
                                                 for name, ref in refs.items()}
        for suffix in ('', '-denoised'):
            dst = args.images/f'ocean-aa-pt{suffix}.png';shutil.copyfile(str(matched)+suffix+'.png', dst)
            report['images'].append({'path': str(dst), 'sha256': digest(dst)})

    sources = ['tools/analyze_ocean_glints.py', 'include/renderer/rhi/GpuOcean.h', 'src/renderer/rhi/GpuOcean.cpp',
               'src/renderer/rhi/OceanSurface.cpp', 'src/renderer/rhi/GpuTemporal.cpp', 'include/renderer/rhi/GpuTemporal.h', 'include/renderer/rhi/OceanSurface.h', 'src/renderer/rhi/ForwardPbrRenderer.cpp', 'src/renderer/rhi/TemporalValidation.cpp',
               'src/renderer/rhi/OceanValidation.cpp', 'src/rhi/shaders/ocean-slope-init.comp', 'src/rhi/shaders/ocean-slope-reduce.comp',
               'src/rhi/shaders/water-slope-filter.glsl', 'src/rhi/shaders/water-surface-model.glsl',
               'src/rhi/shaders/ocean-surface.frag', 'src/rhi/shaders/ocean-surface-common.glsl', 'src/rhi/shaders/ocean-surface-legacy.frag', 'src/rhi/shaders/temporal.comp']
    report['source_sha256'] = {v: digest(v) for v in sources}
    shaders = args.executable.parent/'rhi/shaders'
    report['cooked_shader_sha256'] = shader_hashes
    report['validation_logs'] = {name: Path(f'/tmp/scene-ocean-aa-{name}.log').read_text()
                                 for name in ('metal', 'vulkan', 'temporal-metal', 'temporal-vulkan', 'water-metal', 'water-vulkan', 'cpu', 'native-pt')}
    (args.images/'ocean-aa-validation.json').write_text(json.dumps(report, indent=2, ensure_ascii=False)+'\n')
    for name in ('unfiltered', 'spatial-only', 'filtered'):
        data = report['cases'][name]['-raster-taa']['ssaa4-unfiltered']['water']
        print(name, data)


if __name__ == '__main__':
    main()
