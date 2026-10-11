#!/usr/bin/env python3
"""Compare unchanged linear DDGI/Raster captures against raw Monte Carlo PT."""
import argparse
import gzip
import json
from pathlib import Path
import numpy as np
from PIL import Image, ImageDraw, ImageFont


def pfm(path):
    path = Path(path)
    if not path.exists():
        path = Path(str(path) + '.gz')
    opener = gzip.open if path.suffix == '.gz' else open
    with opener(path, 'rb') as f:
        if f.readline().strip() != b'PF':
            raise ValueError(f'Expected RGB PFM: {path}')
        width, height = map(int, f.readline().split())
        scale = float(f.readline())
        data = np.frombuffer(f.read(), dtype='<f4' if scale < 0 else '>f4')
    data = data.reshape(height, width, 3)[::-1].copy() * abs(scale)
    if not np.isfinite(data).all():
        raise ValueError(f'Nonfinite HDR: {path}')
    return data.astype(np.float64)


def metrics(actual, reference, mask):
    if not mask.any():
        return {'pixels': 0}
    a, r = actual[mask], reference[mask]
    difference = a - r
    mae = float(np.abs(difference).mean())
    reference_mean = float(np.abs(r).mean())
    return {'pixels': int(mask.sum()), 'linear_rgb_mae': mae,
            'linear_rgb_rmse': float(np.sqrt((difference ** 2).mean())),
            'normalized_l1': mae / max(reference_mean, 1e-10),
            'relative_mean_rgb_bias': float(difference.mean()) / max(reference_mean, 1e-10),
            'reference_mean_rgb': reference_mean}


def erode(mask):
    pad = np.pad(mask, 1, constant_values=False)
    return np.logical_and.reduce([pad[y:y+mask.shape[0], x:x+mask.shape[1]] for y in range(3) for x in range(3)])


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('prefix', type=Path)
    parser.add_argument('--second-reference', type=Path)
    args = parser.parse_args()
    prefix = str(args.prefix)
    report = json.loads(Path(prefix + '.json').read_text())
    setup = json.loads(Path(prefix + '-ddgi-reference.json').read_text())
    raw = pfm(prefix + '.pfm')
    off = pfm(prefix + '-raster-off.pfm')
    on = pfm(prefix + '-raster-ddgi.pfm')
    mask = pfm(prefix + '-surface-mask.pfm')[:, :, 0] > .5
    raster_normal = pfm(prefix + '-raster-ddgi-normal.pfm')
    pt_normal = pfm(prefix + '-normal.pfm')
    raster_albedo = pfm(prefix + '-raster-ddgi-albedo.pfm')
    pt_albedo = pfm(prefix + '-albedo.pfm')
    if raw.shape != off.shape or raw.shape != on.shape:
        raise ValueError('Reference and Raster dimensions differ')
    rn = raster_normal / np.maximum(np.linalg.norm(raster_normal, axis=2, keepdims=True), 1e-8)
    pn = pt_normal / np.maximum(np.linalg.norm(pt_normal, axis=2, keepdims=True), 1e-8)
    # The mask uses geometry/material AOVs, never the beauty error itself.
    core = erode(mask & ((rn * pn).sum(axis=2) > .98) & (np.abs(raster_albedo - pt_albedo).mean(axis=2) < .05))
    result = {'scene': setup['scene'], 'reference_spp': report['samples'], 'reference_depth': report['max_depth'],
              'reference_execution': report['execution'], 'reference_non_finite_samples': report['non_finite_samples'],
              'exposure': report['exposure'], 'size': [report['width'], report['height']],
              'reference': 'Raw finite-spp PT; no denoising, guiding, radiance cache or fitted exposure.',
              'core_mask': 'Surface mask AND normalized normal dot > .98 AND mean absolute albedo difference < .05, then 3x3 erosion. Geometry/material AOVs only; sky/edges/material mismatch excluded.',
              'core_fraction_of_surface': float(core.sum() / max(mask.sum(), 1)),
              'beauty_scope': 'End-to-end comparison includes Raster direct shadow/BSDF/specular environment approximations. This is not a pure DDGI error.',
              'beauty': {scope: {state: metrics(actual, raw, selected) for state, actual in [('off', off), ('ddgi', on)]}
                         for scope, selected in [('all_surfaces', mask), ('aligned_core', core)]},
              'regions': {}, 'ddgi_surface_irradiance': {}}
    h, w = mask.shape
    regions = {'left_arch': (.02, .02, .25, .32), 'back_wall': (.40, .25, .61, .66),
               'shadow_floor': (.53, .73, .86, .97), 'sun_floor': (.26, .72, .42, .97),
               'red_curtain': (.02, .35, .19, .82)}
    for name, (x0, y0, x1, y1) in regions.items():
        roi = np.zeros_like(core)
        roi[int(y0*h):int(y1*h), int(x0*w):int(x1*w)] = True
        selected = core & roi
        result['regions'][name] = {'normalized_rectangle': [x0, y0, x1, y1],
                                  'off': metrics(off, raw, selected), 'ddgi': metrics(on, raw, selected)}
    points = setup['points']
    gt = np.array([p['pt_E_over_pi'] for p in points])
    estimate = np.array([p['ddgi'] for p in points])
    se = np.array([p['pt_standard_error'] for p in points])
    result['ddgi_surface_irradiance'] = {'queries': len(points), 'cosine_samples_per_query': setup['point_samples'],
        'quantity': 'E/pi, incident sky + secondary surface radiance; excludes analytic primary direct sun and all Raster shadow/beauty composition.',
        'normalized_l1': float(np.abs(estimate-gt).sum()/max(np.abs(gt).sum(), 1e-10)),
        'mean_reference': gt.mean(axis=0).tolist(), 'mean_ddgi': estimate.mean(axis=0).tolist(),
        'relative_mean_bias': float((estimate-gt).sum()/max(np.abs(gt).sum(), 1e-10)),
        'mean_pt_standard_error': se.mean(axis=0).tolist(),
        'coverage_range': [min(p['coverage'] for p in points), max(p['coverage'] for p in points)],
        'method': setup['reference_method']}
    if args.second_reference:
        second = pfm(str(args.second_reference) + '.pfm')
        second_report = json.loads(Path(str(args.second_reference) + '.json').read_text())
        if second.shape != raw.shape:
            raise ValueError('Independent reference dimensions differ')
        result['pt_sampling_check'] = {'primary_seed': report['seed'], 'primary_spp': report['samples'],
                                      'secondary_seed': second_report['seed'], 'secondary_spp': second_report['samples'],
                                      'raw_reference_difference_core': metrics(second, raw, core),
                                      'note': 'Independent Monte Carlo image difference estimates residual sampling noise; it is not an error bar or proof of convergence.'}
    output = Path(prefix + '-comparison.json')
    output.write_text(json.dumps(result, indent=2) + '\n')
    try:
        font = ImageFont.truetype('/System/Library/Fonts/Supplemental/Arial.ttf', 17)
    except OSError:
        font = ImageFont.load_default()
    sheet = Image.new('RGB', (w*3, h+40), '#181d24')
    draw = ImageDraw.Draw(sheet)
    for i, (label, data) in enumerate([('Raster / DDGI OFF', off), ('Raster / DDGI ON', on), (f'Raw PT / {report["samples"]} spp', raw)]):
        # Identical exponential mapper, exposure and gamma; no spatial filtering.
        mapped = np.clip((1-np.exp(-np.maximum(data, 0)*report['exposure'])) ** (1/2.2), 0, 1)
        sheet.paste(Image.fromarray((mapped*255+.5).astype(np.uint8)), (i*w, 40))
        draw.text((i*w+10, 10), label, fill='white', font=font)
    sheet.save(prefix + '-comparison.png')
    # Direct error-image encoding: black masks excluded pixels; both panels
    # share a linear 0..0.05 HDR error scale. No scene pixels are modified.
    palette = np.array([[.04,.04,.17],[.13,.37,.66],[.27,.73,.66],[.96,.87,.33],[.94,.25,.17]])
    def error_colour(t):
        return np.stack([np.interp(t, np.linspace(0, 1, len(palette)), palette[:,c]) for c in range(3)], axis=-1)
    heat = Image.new('RGB', (w*2, h+90), '#181d24')
    captions = ImageDraw.Draw(heat)
    for i, (actual, label) in enumerate([(off, 'DDGI OFF / error vs raw PT'), (on, 'DDGI ON / error vs raw PT')]):
        error = np.abs(actual-raw).mean(axis=2)
        t = np.clip(error/.05, 0, 1)
        encoded = error_colour(t)
        encoded[~core] = 0
        heat.paste(Image.fromarray((encoded*255+.5).astype(np.uint8)), (i*w, 40))
        captions.text((i*w+10, 10), label, fill='white', font=font)
    # Numeric legend for the same linear RGB MAE scale used in both panels.
    bar_width = min(w, 400)
    values = np.linspace(0, 1, bar_width)
    bar = error_colour(values)
    bar = np.tile(bar[None, :, :], (12, 1, 1))
    heat.paste(Image.fromarray((bar*255+.5).astype(np.uint8)), (10, h+48))
    captions.text((10, h+64), '0                 linear RGB MAE                 0.05+', fill='white', font=font)
    captions.text((w+10, h+48), 'Black: sky, edges or AOV mismatch excluded', fill='white', font=font)
    heat.save(prefix + '-error.png')
    np.savez_compressed(prefix + '-errors.npz', core_mask=core, surface_mask=mask,
                        ddgi_off_error=off-raw, ddgi_on_error=on-raw)
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
