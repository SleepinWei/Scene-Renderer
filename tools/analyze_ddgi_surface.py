#!/usr/bin/env python3
"""Measure native AA / lighting ablations against the unchanged raw PT reference."""
import argparse
import json
from pathlib import Path
import numpy as np
from PIL import Image, ImageDraw, ImageFont
from compare_ddgi_gt import pfm, metrics


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('reference', type=Path)
    parser.add_argument('capture', type=Path)
    args = parser.parse_args()
    ref, cap = str(args.reference), str(args.capture)
    gt = pfm(ref + '.pfm')
    original = pfm(ref + '-raster-ddgi.pfm')
    setup = json.loads(Path(cap + '-ddgi-reference.json').read_text())
    reference_setup = json.loads(Path(ref + '.json').read_text())
    masks = np.load(ref + '-errors.npz')
    core = masks['core_mask']
    variants = {
        'raw': pfm(cap + '-raster-ddgi.pfm'),
        'taa': pfm(cap + '-raster-ddgi-taa.pfm'),
        'taa_no_sky_specular': pfm(cap + '-raster-ddgi-taa-no-sky-specular.pfm'),
        'raw_no_sky_specular': pfm(cap + '-raster-ddgi-no-sky-specular.pfm'),
        'raw_no_shadow': pfm(cap + '-raster-ddgi-no-shadow.pfm'),
    }
    h, w = core.shape
    regions = {'left_arch': (.02, .02, .25, .32), 'back_wall': (.40, .25, .61, .66),
               'shadow_floor': (.53, .73, .86, .97), 'sun_floor': (.26, .72, .42, .97),
               'red_curtain': (.02, .35, .19, .82)}
    report = {
        'reference': str(args.reference), 'capture': str(args.capture),
        'reference_spp': reference_setup['samples'], 'exposure': setup['exposure'],
        'size': [w, h], 'surface_diagnostics': setup['surface_diagnostics'],
        'original_raw_max_absolute_difference': float(np.abs(original-variants['raw']).max()),
        'original_raw_core_max_absolute_difference': float(np.abs(original-variants['raw'])[core].max()),
        'scope': 'Same frozen probe field. TAA uses native jitter/reprojection, 64 frames. No exposure fit or image denoising. No-sky-specular and no-shadow are diagnostic ablations, not proposed final shading.',
        'core_mask': 'Original raw AOV-aligned eroded mask, reused unchanged. TAA has subpixel filtering so these end-to-end metrics also include reconstruction differences.',
        'aligned_core': {name: metrics(data, gt, core) for name, data in variants.items()},
        'regions': {},
        'arch_irradiance_points': [
            {**p, 'rgb_energy_ratio': sum(p['ddgi'])/max(sum(p['pt_E_over_pi']), 1e-10)}
            for p in setup['points'] if p['pixel'] in ([30, 22], [90, 67])
        ],
    }
    # Independent incident irradiance, before albedo, direct shadows, sky
    # specular and temporal resolve. These queries isolate the probe estimate.
    points = setup['points']
    estimate = np.array([p['ddgi'] for p in points])
    reference = np.array([p['pt_E_over_pi'] for p in points])
    standard_error = np.array([p['pt_standard_error'] for p in points])
    denominator = max(float(np.abs(reference).sum()), 1e-10)
    ratios = estimate.sum(axis=1) / np.maximum(reference.sum(axis=1), 1e-10)
    report['ddgi_surface_irradiance'] = {
        'quantity': 'Incident E/pi; excludes current-surface albedo, analytic primary sun, final sky specular, shadows and TAA.',
        'queries': len(points), 'cosine_samples_per_query': setup['point_samples'],
        'normalized_l1': float(np.abs(estimate-reference).sum()) / denominator,
        'relative_mean_bias': float((estimate-reference).sum()) / denominator,
        'mean_reference_rgb': reference.mean(axis=0).tolist(),
        'mean_ddgi_rgb': estimate.mean(axis=0).tolist(),
        'mean_pt_standard_error_rgb': standard_error.mean(axis=0).tolist(),
        'rgb_energy_ratio_percentiles': dict(zip(['min', 'p25', 'median', 'p75', 'p90', 'max'],
                                                np.percentile(ratios, [0, 25, 50, 75, 90, 100]).tolist())),
        'coverage_range': [min(p['coverage'] for p in points), max(p['coverage'] for p in points)],
        'method': setup['reference_method'],
        'scope': '47 fixed grid queries are not a scene-wide maximum or convergence proof. Ratios are unstable where PT energy is small.',
    }
    for name, rectangle in regions.items():
        x0, y0, x1, y1 = rectangle
        roi = np.zeros_like(core)
        roi[int(y0*h):int(y1*h), int(x0*w):int(x1*w)] = True
        report['regions'][name] = {'rectangle': rectangle,
                                  **{key: metrics(data, gt, core & roi) for key, data in variants.items()}}
    # High-frequency RGB error on the eroded curtain core. The same stencil is
    # applied to all variants and GT; this measures aliasing residual, not MTF.
    x0, y0, x1, y1 = regions['red_curtain']
    selected = np.zeros_like(core)
    selected[int(y0*h):int(y1*h), int(x0*w):int(x1*w)] = True
    from compare_ddgi_gt import erode
    selected = erode(core & selected)[1:-1, 1:-1]
    def laplace(data):
        return (data[:-2, 1:-1]+data[2:, 1:-1]+data[1:-1, :-2]+data[1:-1, 2:]-4*data[1:-1, 1:-1])
    report['curtain_high_frequency_error'] = {
        'metric': 'Mean absolute four-neighbour Laplacian of (actual minus raw PT) in the eroded curtain mask. Includes texture/shading aliasing and residual PT noise.',
        'pixels': int(selected.sum()),
        **{key: float(np.abs(laplace(data-gt))[selected].mean()) for key, data in variants.items()},
    }
    Path(cap + '-surface-analysis.json').write_text(json.dumps(report, indent=2)+'\n')
    font = ImageFont.truetype('/System/Library/Fonts/Supplemental/Arial.ttf', 17)
    def mapped(data):
        return Image.fromarray((np.clip((1-np.exp(-np.maximum(data, 0)*setup['exposure']))**(1/2.2), 0, 1)*255+.5).astype(np.uint8))
    cases = [('DDGI / TAA OFF', variants['raw']), ('DDGI / TAA ON (64 frames)', variants['taa']),
             (f'Raw PT / {reference_setup["samples"]} spp', gt)]
    sheet = Image.new('RGB', (w*3, h+40), '#181d24')
    draw = ImageDraw.Draw(sheet)
    for i, (label, data) in enumerate(cases):
        sheet.paste(mapped(data), (i*w, 40));draw.text((i*w+10, 10), label, fill='white', font=font)
    sheet.save(cap + '-aa-comparison.png')
    cases = [('DDGI + TAA', variants['taa']), ('Diagnostic: sky specular OFF + TAA', variants['taa_no_sky_specular']),
             ('Diagnostic: all shadows OFF / no TAA', variants['raw_no_shadow']), ('Raw PT / 2048 spp', gt)]
    sheet = Image.new('RGB', (w*2, (h+40)*2), '#181d24')
    draw = ImageDraw.Draw(sheet)
    for i, (label, data) in enumerate(cases):
        x, y = (i%2)*w, (i//2)*(h+40)
        sheet.paste(mapped(data), (x, y+40));draw.text((x+10, y+10), label, fill='white', font=font)
    sheet.save(cap + '-lighting-ablation.png')
    # Same exposure for every crop; nearest enlargement introduces no additional
    # reconstruction and makes the source pixel footprint explicit.
    crop_cases = [('DDGI / quality TAA', variants['taa']),
                  ('Diagnostic: sky specular OFF', variants['taa_no_sky_specular']),
                  (f'Raw PT / {reference_setup["samples"]} spp', gt)]
    tile_w, tile_h = 360, 260
    crops = Image.new('RGB', (tile_w*3, (tile_h+50)*3), '#181d24')
    annotations = ImageDraw.Draw(crops)
    for row, name in enumerate(['left_arch', 'shadow_floor', 'red_curtain']):
        x0, y0, x1, y1 = regions[name]
        rectangle = (int(x0*w), int(y0*h), int(x1*w), int(y1*h))
        for column, (label, data) in enumerate(crop_cases):
            x, y = column*tile_w, row*(tile_h+50)
            patch = mapped(data).crop(rectangle)
            scale = min(tile_w/patch.width, tile_h/patch.height)
            patch = patch.resize((round(patch.width*scale), round(patch.height*scale)), Image.Resampling.NEAREST)
            crops.paste(patch, (x+(tile_w-patch.width)//2, y+50))
            annotations.text((x+8, y+5), label, fill='white', font=font)
            detail = name
            if column < 2:
                key = ['taa', 'taa_no_sky_specular'][column]
                detail += f' / nL1 {report["regions"][name][key]["normalized_l1"]*100:.2f}%'
            annotations.text((x+8, y+25), detail, fill='white', font=font)
    crops.save(cap + '-gi-crops.png')
    palette = np.array([[.04,.04,.17],[.13,.37,.66],[.27,.73,.66],[.96,.87,.33],[.94,.25,.17]])
    def error_colour(t):
        return np.stack([np.interp(t, np.linspace(0, 1, len(palette)), palette[:,c]) for c in range(3)], axis=-1)
    heat = Image.new('RGB', (w*2, h+90), '#181d24')
    annotations = ImageDraw.Draw(heat)
    for column, (label, data) in enumerate(crop_cases[:2]):
        colour = error_colour(np.clip(np.abs(data-gt).mean(axis=2)/.05, 0, 1))
        colour[~core] = 0
        heat.paste(Image.fromarray((colour*255+.5).astype(np.uint8)), (column*w, 40))
        annotations.text((column*w+8, 10), label+' / error vs PT', fill='white', font=font)
    bar = np.tile(error_colour(np.linspace(0, 1, 400))[None, :, :], (12, 1, 1))
    heat.paste(Image.fromarray((bar*255+.5).astype(np.uint8)), (10, h+48))
    annotations.text((10, h+64), '0               linear RGB MAE               0.05+', fill='white', font=font)
    annotations.text((w+10, h+48), 'Black: excluded sky / edges / AOV mismatch', fill='white', font=font)
    heat.save(cap + '-gi-error.png')
    print(json.dumps({key: report[key] for key in ['original_raw_max_absolute_difference', 'aligned_core', 'curtain_high_frequency_error']}, indent=2))


if __name__ == '__main__':
    main()
