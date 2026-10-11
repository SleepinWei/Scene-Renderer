#!/usr/bin/env python3
"""Display native DDGI GPU captures; no scene pixels are synthesized."""
import argparse
import json
from pathlib import Path
import numpy as np
from PIL import Image, ImageDraw, ImageFont


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('directory', type=Path)
    args = parser.parse_args()
    root = args.directory
    manifest = json.loads((root / 'validation.json').read_text())
    if not manifest['passed']:
        raise ValueError('DDGI validation must pass before publishing captures')
    for name in ['room-off', 'room-on']:
        Image.open(root / (name + '.ppm')).save(root / (name + '.png'))
    images = [Image.open(root / ('room-' + state + '.png')).convert('RGB') for state in ['off', 'on']]
    sheet = Image.new('RGB', (660, 288), '#181d24')
    draw = ImageDraw.Draw(sheet)
    try:
        font = ImageFont.truetype('/System/Library/Fonts/Supplemental/Arial.ttf', 17)
    except OSError:
        font = ImageFont.load_default()
    for index, (label, im) in enumerate(zip(['DDGI off: unoccluded ambient', 'DDGI on: visibility + diffuse bounce'], images)):
        sheet.paste(im, (10 + index * 330, 38))
        draw.text((10 + index * 330, 10), label, fill='white', font=font)
    sheet.save(root / 'room-comparison.png')
    for scene in ['cornell', 'sponza']:
        if not all((root / f'{scene}-{state}.ppm').exists() for state in ['off', 'on']):
            continue
        captures = [Image.open(root / f'{scene}-{state}.ppm').convert('RGB') for state in ['off', 'on']]
        width, height = captures[0].size
        if captures[1].size != (width, height):
            raise ValueError(f'{scene} comparison captures must have identical dimensions')
        comparison = Image.new('RGB', (width * 2, height + 40), '#181d24')
        caption = ImageDraw.Draw(comparison)
        for index, (state, im) in enumerate(zip(['off', 'on'], captures)):
            im.save(root / f'{scene}-{state}.png')
            comparison.paste(im, (index * width, 40))
            caption.text((index * width + 14, 10), f'{scene.title()} / DDGI {state.upper()}', fill='white', font=font)
        comparison.save(root / f'{scene}-comparison.png')
    layout = manifest['cache_layout']
    count = int(np.prod(layout['probe_grid']))
    field = np.fromfile(root / 'thin-wall-field.bin', dtype='<f4').reshape(count, layout['stride_vec4'], 4)
    if not np.isfinite(field).all():
        raise ValueError('Nonfinite GPU field')
    np.savez_compressed(root / 'thin-wall-field.npz', field=field)
    # Each tile is one actual octahedral irradiance cache; ordered x, y, z.
    irradiance = field[:, 1:65, :3].reshape(count, 8, 8, 3)
    rgb = ((1 - np.exp(-np.maximum(irradiance, 0))) ** (1 / 2.2) * 255).astype(np.uint8)
    atlas = Image.new('RGB', (8 * 57, ((count + 7) // 8) * 57), '#282f39')
    for index, tile in enumerate(rgb):
        atlas.paste(Image.fromarray(tile).resize((56, 56), Image.Resampling.NEAREST), ((index % 8) * 57, (index // 8) * 57))
    atlas.save(root / 'thin-wall-irradiance.png')
    off, on = manifest['thin_wall']['visibility_off'][0], manifest['thin_wall']['visibility_on'][0]
    print(json.dumps({'backend': manifest['backend'], 'thin_wall_leak_reduction_percent': (1 - on / off) * 100,
                      'cache_bytes': manifest['default_cache_bytes'], 'artifacts': ['room-comparison.png', 'thin-wall-irradiance.png', 'thin-wall-field.npz']}, indent=2))


if __name__ == '__main__':
    main()
