#!/usr/bin/env python3
"""Check coherent sun/sky relighting and publish the stronger hair previews.

NumPy/Pillow required. Raw HDR checks exclude denoising and exposure fitting.
The sun-off control isolates finite-sun illumination, not just the R lobe.
"""
import argparse
import json
import math
from pathlib import Path
import shutil

import numpy as np
from PIL import Image

from compare_pt_references import read
from create_pt_hair_sky import sha, decode_baked_hdr


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input', type=Path, default=Path('build/pt-hair-sunlit'))
    parser.add_argument('--output', type=Path, default=Path('img/path-tracing'))
    parser.add_argument('--evidence', type=Path, default=Path('img/diagnostics/materials/hair-sunlit'))
    args = parser.parse_args()
    frozen = json.loads((args.input/'source-freeze.json').read_text())
    for path, digest in frozen.items():
        if sha(path) != digest:
            raise ValueError('Rendering input changed after freezing: '+path)
    hdr = args.input/'low-sun-environment.hdr'
    decoded = args.input/'validation-environment.bin'
    extent = decode_baked_hdr(hdr, decoded)
    environment = np.fromfile(decoded, dtype='<f4').reshape(extent['height'], extent['width'], 3)
    args.output.mkdir(parents=True, exist_ok=True)
    args.evidence.mkdir(parents=True, exist_ok=True)
    checks, outputs = {}, {}
    for name in ('straight', 'wavy'):
        root = args.input/name
        scene = json.loads((root/'scene.json').read_text())
        imported = json.loads((root/'sunlit-import.json').read_text())
        original = json.loads(Path(next(p for p in imported['input_sha256'] if p.endswith('scene.json'))).read_text())
        for key in ('geometry', 'tangents'):
            path = next(p for p in imported['input_sha256'] if p.endswith('/'+key+'.bin'))
            assert sha(root/scene[key]) == imported['input_sha256'][path]
        assert original['camera'] == scene['camera'] and original['draws'] == scene['draws']
        assert scene['source']['strands'] == 50000
        assert len(scene['draws']) == len(scene['materials']) == 2 and not scene.get('lights')
        assert all(not any(m.get('emission', [0, 0, 0])) for m in scene['materials'])
        shift = imported['hdr_texel_shift']; base, fraction = math.floor(shift), shift-math.floor(shift)
        expected = (np.roll(environment, base, axis=1)*(1-fraction)+
                    np.roll(environment, base+1, axis=1)*fraction)*imported['light_scale']
        actual = np.fromfile(root/'environment.bin', dtype='<f4').reshape(expected.shape)
        assert np.array_equal(actual, expected.astype('<f4'))
        # Independently check longitude: initial sun points -Z, positive yaw
        # places it at sin(yaw), -cos(yaw); rotation preserves its elevation.
        direction = np.array(scene['sun']['direction'])
        assert np.isclose(np.linalg.norm(direction), 1, atol=1e-6)
        assert np.isclose(direction[1], imported['source_sun']['sun_direction'][1])
        longitude = math.atan2(direction[0], -direction[2])
        assert np.isclose(longitude, math.radians(imported['yaw_degrees']), atol=1e-6)
        assert np.allclose(scene['sun']['irradiance'], np.array(imported['source_sun']['sun_irradiance'])*imported['light_scale'])
        report = json.loads((root/'metal.json').read_text())
        filtered = json.loads((root/'filtered.json').read_text())
        assert [report[k] for k in ('width', 'height', 'samples', 'max_depth', 'seed')] == [512, 512, 1024, 12, 1]
        assert report['non_finite_samples'] == 0 and not report['adaptive']
        assert report['exposure'] == filtered['exposure']
        assert np.allclose(report['sun_irradiance'], scene['sun']['irradiance'], rtol=1e-6)
        assert filtered['denoiser'] == 'Open Image Denoise 2.5.1' and filtered['denoise_device'] == 'CPU'
        raw = read(root/'metal.pfm'); albedo = read(root/'metal-albedo.pfm')
        assert raw.shape == (512, 512, 3) and np.isfinite(raw).all() and np.min(raw) >= 0
        assert min(float(raw[y, x].sum()) for y in (0, 511) for x in (0, 511)) > 0
        mask = np.min(albedo, axis=2) > .99
        assert mask.sum() > 1000
        luminance = np.sum(raw*np.array([.2126, .7152, .0722]), axis=2)
        checks[name] = dict(imported=imported, report=report, filter=filtered,
                            raw_hair_luminance_percentiles=dict(zip(('p50', 'p90', 'p99'),
                                                                   np.percentile(luminance[mask], [50, 90, 99]).tolist())))
        for source, suffix in [('metal.png', '-raw.png'), ('filtered-denoised.png', '.png')]:
            assert Image.open(root/source).size == (512, 512)
            target = args.output/('yuksel-'+name+'-sunlit'+suffix)
            shutil.copyfile(root/source, target); outputs[str(target)] = sha(target)
        for filename in ('metal.json', 'metal.txt', 'filtered.json', 'denoise.txt', 'sunlit-import.json'):
            shutil.copyfile(root/filename, args.evidence/(name+'-'+filename))
        for filename in ('metal.pfm', 'metal-albedo.pfm', 'filtered-denoised.pfm'):
            outputs[str(root/filename)] = sha(root/filename)
    root = args.input/'wavy'
    metal, vulkan, off = [read(root/(p+'.pfm')) for p in ('check-metal', 'check-vulkan', 'check-no-sun')]
    assert all(v.shape == (128, 128, 3) and np.isfinite(v).all() for v in (metal, vulkan, off))
    assert np.array_equal(metal, vulkan)
    mask = np.min(read(root/'check-metal-albedo.pfm'), axis=2) > .99
    luma = np.array([.2126, .7152, .0722])
    on_mean = float(np.mean(metal[mask]@luma)); off_mean = float(np.mean(off[mask]@luma))
    assert on_mean > off_mean*2, 'Finite sunlight must visibly contribute to the hair'
    for prefix in ('check-metal', 'check-vulkan', 'check-no-sun'):
        report = json.loads((root/(prefix+'.json')).read_text())
        assert [report[k] for k in ('width', 'height', 'samples', 'max_depth', 'seed')] == [128, 128, 256, 12, 1]
        assert report['non_finite_samples'] == 0 and not report['adaptive']
        for suffix in ('.json', '.txt'):
            shutil.copyfile(root/(prefix+suffix), args.evidence/(prefix+suffix))
        outputs[str(root/(prefix+'.pfm'))] = sha(root/(prefix+'.pfm'))
    for filename in ('low-sun-environment.hdr', 'low-sun-environment.hdr.json', 'low-sun.json', 'source-freeze.json',
                     'runtime-source-audit.json', 'runtime-configure.txt', 'runtime-build.txt'):
        shutil.copyfile(args.input/filename, args.evidence/filename)
    shutil.copyfile(root/'sun-off-scene.json', args.evidence/'sun-off-scene.json')
    shutil.copyfile('build/pt-hair-sunlit-low-sun.txt', args.evidence/'bake.txt')
    data = dict(date='2026-10-09', device='Apple M4', scenes=checks,
                credit='Hair: Cem Yuksel; matching head courtesy of Murat Afshar',
                source_page='https://www.cemyuksel.com/research/hairmodels/',
                runtime=dict(executable=str(args.input/'runtime/pt-package-render'),
                             source_audit=json.loads((args.input/'runtime-source-audit.json').read_text()),
                             provenance='Exact private executable, compiled shaders and scene bytes frozen; live source snapshot not guaranteed to match compilation'),
                metal_vulkan_raw_identical=True,
                sun_off_control=dict(hair_mean_luminance_sun_on=on_mean, hair_mean_luminance_sun_off=off_mean,
                                     ratio=on_mean/off_mean, mask='First-hit albedo minimum > 0.99',
                                     scope='Finite sun total illumination, includes R/TT/TRT, not isolated R'),
                input_sha256=frozen, output_sha256=outputs, validator_sha256=sha(__file__),
                evidence_sha256={str(p): sha(p) for p in args.evidence.iterdir() if p.is_file()},
                limits=['Camera and all 50000-strand ribbon geometry retained; native curves not implemented',
                        'Sun and sky rotated/scaled together; hair roughness changed explicitly',
                        'Neutral diffuse head, not skin appearance verification',
                        'OIDN can smooth fine strands; checks use raw HDR',
                        'Vulkan via MoltenVK; no Khronos validation layer or new PBRT comparison'])
    (args.output/'yuksel-hair-sunlit-validation.json').write_text(json.dumps(data, indent=2)+'\n')
    print(json.dumps(dict(metal_vulkan_identical=True, sunlight_hair_luminance_ratio=on_mean/off_mean), indent=2))


if __name__ == '__main__':
    main()
