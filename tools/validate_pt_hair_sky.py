#!/usr/bin/env python3
"""Verify the atmosphere-lit hair previews and preserve their provenance.

Requires NumPy and Pillow. Compares package RGB32F with direct stb HDR loading
and Metal with Vulkan using raw PFM; OIDN is used only for display images.
"""
import argparse
import json
from pathlib import Path
import shutil

import numpy as np
from PIL import Image

from compare_pt_references import read
from create_pt_hair_sky import sha


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input', type=Path, default=Path('build/pt-hair-sky'))
    parser.add_argument('--output', type=Path, default=Path('img/path-tracing'))
    parser.add_argument('--evidence', type=Path, default=Path('img/diagnostics/materials/hair-sky'))
    args = parser.parse_args()
    frozen = json.loads((args.input/'source-freeze.json').read_text())
    for path, digest in frozen.items():
        if sha(path) != digest:
            raise ValueError('Input changed during rendering: '+path)
    package = read(args.input/'straight/check-package.pfm')
    direct = read(args.input/'straight/check-hdr.pfm')
    vulkan = read(args.input/'straight/check-vulkan.pfm')
    for pixels in (package, direct, vulkan):
        assert pixels.shape == (64, 64, 3) and np.isfinite(pixels).all()
    assert np.array_equal(package, direct), 'RGB32F vs direct stb HDR mismatch'
    assert np.array_equal(package, vulkan), 'Metal vs Vulkan sky mismatch'
    args.output.mkdir(parents=True, exist_ok=True)
    args.evidence.mkdir(parents=True, exist_ok=True)
    checks, outputs = {}, {}
    sun = json.loads((args.input/'daylight-environment.hdr.json').read_text())
    for name in ('straight', 'wavy'):
        root = args.input/name
        scene = json.loads((root/'scene.json').read_text())
        imported = json.loads((root/'sky-import.json').read_text())
        beauty = json.loads((root/'metal.json').read_text())
        filtered = json.loads((root/'filtered.json').read_text())
        assert scene['source']['strands'] == 50000
        assert len(scene['draws']) == len(scene['materials']) == 2 and not scene.get('lights')
        assert all(not any(m.get('emission', [0, 0, 0])) for m in scene['materials'])
        assert scene['sun']['irradiance'] == sun['sun_irradiance']
        assert [beauty[k] for k in ('width', 'height', 'samples', 'max_depth', 'seed')] == [512, 512, 512, 12, 1]
        assert beauty['non_finite_samples'] == 0 and not beauty['adaptive']
        assert beauty['environment']['width'] == 512 and beauty['environment']['height'] == 256
        assert beauty['sun_irradiance'] == sun['sun_irradiance']
        assert filtered['denoiser'] == 'Open Image Denoise 2.5.1' and filtered['denoise_device'] == 'CPU'
        raw = read(root/'metal.pfm')
        assert raw.shape == (512, 512, 3) and np.isfinite(raw).all() and np.min(raw) >= 0
        # Corners should see the clear sky, with no black lower hemisphere.
        assert min(float(raw[y, x].sum()) for y in (0, 511) for x in (0, 511)) > 0
        for original, suffix in [('metal.png', '-raw.png'), ('filtered-denoised.png', '.png')]:
            assert Image.open(root/original).size == (512, 512)
            target = args.output/('yuksel-'+name+'-sky'+suffix)
            shutil.copyfile(root/original, target)
            outputs[str(target)] = sha(target)
        for filename in ('metal.json', 'metal.txt', 'filtered.json', 'denoise.txt', 'sky-import.json'):
            shutil.copyfile(root/filename, args.evidence/(name+'-'+filename))
        for filename in ('metal.pfm', 'metal.png', 'filtered-denoised.pfm', 'filtered-denoised.png'):
            outputs[str(root/filename)] = sha(root/filename)
        checks[name] = dict(imported=imported, render=beauty, filter=filtered)
    for prefix in ('check-package', 'check-hdr', 'check-vulkan'):
        report = json.loads((args.input/'straight'/(prefix+'.json')).read_text())
        assert [report[k] for k in ('width', 'height', 'samples', 'max_depth', 'seed')] == [64, 64, 8, 12, 1]
        assert report['non_finite_samples'] == 0 and not report['adaptive']
        for suffix in ('.json', '.txt'):
            shutil.copyfile(args.input/'straight'/(prefix+suffix), args.evidence/(prefix+suffix))
        outputs[str(args.input/'straight'/(prefix+'.pfm'))] = sha(args.input/'straight'/(prefix+'.pfm'))
    for filename in ('daylight-environment.hdr', 'daylight-environment.hdr.json', 'daylight.json', 'source-freeze.json', 'create.txt'):
        shutil.copyfile(args.input/filename, args.evidence/filename)
    shutil.copyfile('build/pt-hair-sky-daylight.txt', args.evidence/'bake.txt')
    data = dict(date='2026-10-09', device='Apple M4', scenes=checks,
                source_page='https://www.cemyuksel.com/research/hairmodels/',
                credit='Hair: Cem Yuksel; matching head: courtesy of Murat Afshar',
                package_vs_stb_hdr_raw_identical=True, metal_vulkan_raw_identical=True,
                check_settings=dict(width=64, height=64, samples=8, max_depth=12, seed=1),
                input_sha256=frozen, output_sha256=outputs,
                validator_sha256=sha(__file__),
                evidence_sha256={str(p): sha(p) for p in args.evidence.iterdir() if p.is_file()},
                limits=['Static camera-facing ribbons regenerated for the new camera, not native curves',
                        'All 50000 source strands, points and widths retained; neutral untextured Lambert head',
                        'Clear-sky atmosphere bake at 10 m; no clouds, ground geometry or studio area lights',
                        'Sky excludes the disk; sun irradiance includes atmospheric transmittance',
                        'OIDN display can smooth fine strands; checks use raw HDR without exposure fitting',
                        'Vulkan via MoltenVK; no Khronos validation layer; no new PBRT sky comparison'])
    (args.output/'yuksel-hair-sky-validation.json').write_text(json.dumps(data, indent=2)+'\n')
    print('Two 50000-strand sky previews verified; HDR decoding and Metal/Vulkan raw checks are identical.')


if __name__ == '__main__':
    main()
