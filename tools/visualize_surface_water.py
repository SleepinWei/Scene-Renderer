#!/usr/bin/env python3
"""Render a schema-5 CPU surface-water checkpoint (requires Pillow)."""
import argparse
import json
from pathlib import Path
from PIL import Image, ImageDraw


def summarize(state):
    if state['schema_version'] != 5 or state['scope'] != 'cpu-environment-spatial-water':
        raise ValueError('Expected a schema-5 spatial-water checkpoint')
    grid, water, config = state['land_grid'], state['land'], state['land_config']
    if not grid or grid['width'] != 16:
        raise ValueError('Checkpoint contains a uniform land account, not a grid')
    areas, storage = grid['area_m2'], state['land_storage_m']
    if len(areas) != 256 or len(storage) != 256:
        raise ValueError('Invalid grid extent')
    depths = [s for a, s in zip(areas, storage) if a > 0]
    area = config['area_m2']
    balance = (config['initial_storage_m'] + water['rain_m'] - water['storage_m']
               - water['infiltration_m'] - water['evaporation_m'] - water['outflow_m']) * area
    return dict(tick=state['tick'], seconds=state['initial_seconds'] + state['tick'] * state['step_seconds'],
                rain_mm_per_hour=state['rain_m_per_s'] * 3600000, land_area_m2=area,
                storage_min_mm=min(depths) * 1000, storage_max_mm=max(depths) * 1000,
                storage_mean_mm=water['storage_m'] * 1000,
                stored_m3=sum(a * s for a, s in zip(areas, storage)), balance_residual_m3=balance,
                internal_transfer_m3=state['land_transferred_m3'],
                external_outflow_m3=water['outflow_m'] * area,
                unconsumed_direct_lake_rain_m3=state['lake_rain_m3'],
                scope='16x16 horizontal land cells; artistic thin-film runoff; fixed lake level')


def render(state, destination):
    report = summarize(state)
    grid, depths = state['land_grid'], state['land_storage_m']
    areas, heights = grid['area_m2'], grid['elevation_m']
    lo, hi = min(heights), max(heights)
    wet = Image.new('RGBA', (16, 16))
    elevation = Image.new('RGB', (16, 16))
    for z in range(16):
        for x in range(16):
            i, y = z * 16 + x, 15 - z  # Material V / image top is +Z.
            value = round(min(1, max(0, depths[i] / .001)) * 255)
            wet.putpixel((x, y), (value, 0, 0, 255))
            level = round(45 + 190 * (heights[i] - lo) / max(hi - lo, 1e-9))
            elevation.putpixel((x, y), (level, level, level))
    destination.mkdir(parents=True, exist_ok=True)
    wet.save(destination / 'wetness-16.png')
    canvas = Image.new('RGB', (900, 600), '#101b26')
    draw = ImageDraw.Draw(canvas)
    draw.text((24, 20), 'CPU SURFACE WATER | actual saved state | 16 x 16 cells', fill='#ecf5fa')
    draw.text((24, 47), f"tick {report['tick']} | {report['seconds']:.2f} s | {report['rain_mm_per_hour']:.1f} mm/h", fill='#bad0df')
    draw.text((24, 80), 'Coarse terrain elevation', fill='white')
    draw.text((464, 80), 'Stored water (mm); black = zero-land cell', fill='white')
    maximum = state['land_config']['capacity_m']
    for z in range(16):
        for x in range(16):
            i, y = z * 16 + x, 15 - z
            grey = elevation.getpixel((x, y))
            t = min(1, max(0, depths[i] / maximum))
            colour = (round(225 * (1 - t) + 15 * t), round(193 * (1 - t) + 137 * t), round(124 * (1 - t) + 232 * t))
            if areas[i] == 0:
                colour = (5, 10, 17)
            for left, cell in ((24, grey), (464, colour)):
                draw.rectangle((left + x * 24, 110 + y * 24, left + x * 24 + 22, 110 + y * 24 + 22), fill=cell)
    lines = [f"Storage: {report['storage_min_mm']:.4f} - {report['storage_max_mm']:.4f} mm | mean {report['storage_mean_mm']:.4f} mm",
             f"Internal transfer: {report['internal_transfer_m3']:.5f} m3 | external outflow: {report['external_outflow_m3']:.5f} m3",
             f"Land budget residual: {report['balance_residual_m3']:.3g} m3 | +Z at top",
             'Visual wetness = clamp(storage / 1 mm); no lake-level or groundwater simulation.']
    for n, line in enumerate(lines):
        draw.text((24, 512 + n * 19), line, fill='#d4e3ee')
    canvas.save(destination / 'water-grid.png')
    (destination / 'summary.json').write_text(json.dumps(report, indent=2) + '\n')
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('checkpoint', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    print(json.dumps(render(json.loads(args.checkpoint.read_text()), args.output), indent=2))


if __name__ == '__main__':
    main()
