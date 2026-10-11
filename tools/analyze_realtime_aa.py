#!/usr/bin/env python3
"""Sponza AA upgrade: unchanged raw PT plus separate same-field TAA ablation."""
import argparse
import json
from pathlib import Path
import numpy as np
from PIL import Image, ImageDraw, ImageFont
from compare_ddgi_gt import pfm, metrics, erode


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('reference', type=Path)
    parser.add_argument('previous', type=Path)
    parser.add_argument('current', type=Path)
    args = parser.parse_args()
    ref, old, new = map(str, [args.reference, args.previous, args.current])
    gt = pfm(ref+'.pfm')
    setup = json.loads(Path(new+'-ddgi-reference.json').read_text())
    metadata = json.loads(Path(ref+'.json').read_text())
    core = np.load(ref+'-errors.npz')['core_mask']
    h, w = core.shape
    variants = {
        'previous_no_mips_taa': pfm(old+'-raster-ddgi-taa.pfm'),
        'mips_no_taa': pfm(new+'-raster-ddgi.pfm'),
        'mips_legacy_taa': pfm(new+'-raster-ddgi-taa-legacy.pfm'),
        'mips_quality_taa': pfm(new+'-raster-ddgi-taa.pfm'),
    }
    report = {
        'size': [w, h], 'reference_spp': metadata['samples'], 'exposure': setup['exposure'],
        'diagnostics': setup['surface_diagnostics'],
        'scope': 'Old vs new includes material color/roughness/normal filtering and the effect of mips on DDGI capture. Only the two new TAA variants share exactly the same frozen GI field and material evaluation. No exposure fitting or image denoising.',
        'mask': 'Reuse the original geometry/material AOV-aligned eroded mask unchanged. This is an end-to-end metric, not a pure AA or DDGI error.',
        'aligned_core': {key: metrics(data, gt, core) for key, data in variants.items()},
        'regions': {},
    }
    regions = {'red_curtain': (.02, .35, .19, .82), 'left_arch': (.02, .02, .25, .32),
               'back_wall': (.40, .25, .61, .66), 'shadow_floor': (.53, .73, .86, .97)}
    def laplace(data):
        return data[:-2, 1:-1]+data[2:, 1:-1]+data[1:-1, :-2]+data[1:-1, 2:]-4*data[1:-1, 1:-1]
    for name, (x0, y0, x1, y1) in regions.items():
        roi = np.zeros_like(core)
        roi[int(y0*h):int(y1*h), int(x0*w):int(x1*w)] = True
        selected = core & roi
        interior = erode(selected)[1:-1, 1:-1]
        report['regions'][name] = {
            key: {**metrics(data, gt, selected),
                  'high_frequency_error_mae': float(np.abs(laplace(data-gt))[interior].mean())}
            for key, data in variants.items()}
    Path(new+'-aa-analysis.json').write_text(json.dumps(report, indent=2)+'\n')
    font = ImageFont.truetype('/System/Library/Fonts/Supplemental/Arial.ttf', 16)
    def mapped(data):
        return Image.fromarray((np.clip((1-np.exp(-np.maximum(data, 0)*setup['exposure']))**(1/2.2), 0, 1)*255+.5).astype(np.uint8))
    def sheet(cases, path):
        result = Image.new('RGB', (w*len(cases), h+40), '#181d24');draw = ImageDraw.Draw(result)
        for i, (label, data) in enumerate(cases):
            result.paste(mapped(data), (i*w, 40));draw.text((i*w+8, 10), label, fill='white', font=font)
        result.save(path)
    sheet([('Previous: no mips + TAA', variants['previous_no_mips_taa']),
           ('New: mips + quality TAA', variants['mips_quality_taa']),
           ('Unchanged raw PT / 2048 spp', gt)], new+'-upgrade.png')
    sheet([('Mips / no TAA', variants['mips_no_taa']), ('Mips / legacy TAA', variants['mips_legacy_taa']),
           ('Mips / quality TAA', variants['mips_quality_taa'])], new+'-same-field.png')
    sheet([('Camera move: no TAA', pfm(new+'-motion-no-taa.pfm')),
           ('Camera move: legacy TAA', pfm(new+'-motion-taa-legacy.pfm')),
           ('Camera move: quality TAA', pfm(new+'-motion-taa.pfm'))], new+'-motion.png')
    # Albedo alone shows that prefiltering removes aliasing before temporal lighting.
    sheet([('Previous single-sample albedo', pfm(old+'-raster-ddgi-albedo.pfm')),
           ('New single-sample filtered albedo', pfm(new+'-raster-ddgi-albedo.pfm'))], new+'-albedo-filtering.png')
    print(json.dumps({'aligned_core': report['aligned_core'], 'curtain': report['regions']['red_curtain']}, indent=2))


if __name__ == '__main__':
    main()
