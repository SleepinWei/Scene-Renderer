#!/usr/bin/env python3
"""Visualize actual Hi-Z GPU mip/argument captures (NumPy + Pillow).

Validates max reduction and matched renderer outputs before drawing diagnostic
bounds. No generated imagery; false colour only remaps depth for display.
"""
import argparse
import json
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw
from visualize_render_diagnostics import BG, font, panel

CULLED = (245, 91, 111)
KEPT = (100, 215, 144)
OCCLUDER = (69, 207, 240)
CLEAR = (239, 244, 252)


def load_mips(directory, metadata):
    levels = []
    archive = np.load(directory/metadata['depth_archive']) if metadata.get('depth_archive') else None
    for mip in metadata['mips']:
        path = directory/mip['file']
        data = np.fromfile(path, dtype='<f4') if path.exists() else archive[f'mip_{mip["level"]}'].reshape(-1)
        if data.size != mip['width']*mip['height']:
            raise ValueError('GPU mip dimensions do not match the manifest')
        data = data.reshape(mip['height'], mip['width'])
        if not np.isfinite(data).all() or (data < 0).any() or (data > 1).any():
            raise ValueError('GPU depth is outside the native 0..1 domain')
        if levels:
            source = levels[-1]
            expected = np.empty_like(data)
            for y in range(data.shape[0]):
                end_y = source.shape[0] if y == data.shape[0]-1 else min(y*2+2, source.shape[0])
                for x in range(data.shape[1]):
                    end_x = source.shape[1] if x == data.shape[1]-1 else min(x*2+2, source.shape[1])
                    expected[y, x] = source[y*2:end_y, x*2:end_x].max()
            if not np.array_equal(expected, data):
                raise ValueError(f'GPU mip {mip["level"]} differs from conservative CPU max reduction')
        levels.append(data)
    if archive is not None:
        archive.close()
    return levels


def colour_depth(depth, low, high):
    scaled = np.clip((depth-low)/max(high-low, 1e-8), 0, 1)
    stops = np.array([(58, 111, 225), (40, 190, 174), (250, 204, 85)], dtype=np.uint8)
    colour = np.stack([np.interp(scaled, [0, .5, 1], stops[:, c]) for c in range(3)], axis=-1).astype(np.uint8)
    colour[depth == 1] = CLEAR
    return Image.fromarray(colour)


def overlay(image, objects):
    canvas = image.convert('RGBA')
    layer = Image.new('RGBA', canvas.size)
    draw = ImageDraw.Draw(layer)
    # Draw red rectangles first; retained/occluder borders remain legible.
    ordered = sorted(objects, key=lambda obj: obj['occluder'] or obj['output_triangles'] > 0)
    for obj in ordered:
        if not obj['testable'] and not obj['occluder']:
            continue  # Fail-open bounds have no safe projected rectangle.
        hidden = obj['input_triangles'] > 0 and obj['output_triangles'] == 0
        colour = OCCLUDER if obj['occluder'] else CULLED if hidden else KEPT
        rect = tuple(obj['rectangle'])
        if hidden:
            draw.rectangle(rect, fill=(*colour, 36), outline=(*colour, 255), width=2)
        else:
            draw.rectangle(rect, outline=(*colour, 255), width=3 if obj['occluder'] else 2)
    return Image.alpha_composite(canvas, layer).convert('RGB')


def compose(panels, columns, title, subtitle, footer, path):
    width = panels[0].width
    height = panels[0].height
    rows = (len(panels)+columns-1)//columns
    result = Image.new('RGB', (columns*width, rows*height+140), BG)
    draw = ImageDraw.Draw(result)
    draw.text((18, 14), title, font=font(29), fill='white')
    draw.text((18, 55), subtitle, font=font(18), fill=(182, 198, 217))
    for i, item in enumerate(panels):
        result.paste(item, ((i % columns)*width, 92+(i//columns)*height))
    draw.text((18, result.height-34), footer, font=font(16), fill=(182, 198, 217))
    result.save(path)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    parser.add_argument('--name', default='occlusion-window')
    parser.add_argument('--output', type=Path)
    parser.add_argument('--archive-depth', action='store_true', help='Save lossless float32 NPZ alongside capture metadata')
    args = parser.parse_args()
    directory = args.directory
    output = args.output or directory
    output.mkdir(parents=True, exist_ok=True)
    metadata = json.loads((directory/f'{args.name}-capture.json').read_text())
    off = Image.open(directory/f'{args.name}-off.png').convert('RGB')
    on = Image.open(directory/f'{args.name}-on.png').convert('RGB')
    revealed = Image.open(directory/f'{args.name}-occluders-removed.png').convert('RGB')
    if off.size != (metadata['width'], metadata['height']) or not np.array_equal(np.asarray(off), np.asarray(on)):
        raise ValueError('Renderer off/on images differ')
    levels = load_mips(directory, metadata)
    if args.archive_depth:
        archive_name = f'{args.name}-depth.npz'
        np.savez_compressed(directory/archive_name, **{f'mip_{i}': data for i, data in enumerate(levels)})
        with np.load(directory/archive_name) as archive:
            if any(not np.array_equal(data, archive[f'mip_{i}']) for i, data in enumerate(levels)):
                raise ValueError('Lossless depth archive differs from GPU data')
        metadata['depth_archive'] = archive_name
        (directory/f'{args.name}-capture.json').write_text(json.dumps(metadata, indent=2)+'\n')
    valid = levels[0][levels[0] < 1]
    low, high = float(valid.min()), float(valid.max())
    objects = metadata['objects']
    culled = sum(obj['input_triangles'] > 0 and obj['output_triangles'] == 0 for obj in objects)
    if culled != metadata['culled_objects']:
        raise ValueError('Visibility mask differs from GPU indirect counts')
    retained = len(objects)-culled-sum(obj['occluder'] for obj in objects)
    mip = min(4, len(levels)-1)
    panels = [
        panel(off, 'Hi-Z OFF', f'{metadata["nonzero_draws_before"]} nonzero main draws', width=540, height=434),
        panel(on, 'Hi-Z ON', f'{metadata["nonzero_draws_after"]} nonzero main draws | identical pixels', width=540, height=434),
        panel(overlay(on, objects), 'GPU visibility / projected bounds', f'Red: {culled} culled | green: {retained} kept | cyan: occluder', width=540, height=434),
        panel(revealed, 'Occluders removed / reference only', 'Re-rendered to expose the objects behind the wall', width=540, height=434),
        panel(colour_depth(levels[0], low, high), 'Actual GPU Hi-Z mip 0', f'{levels[0].shape[1]} x {levels[0].shape[0]} | white: clear depth 1', width=540, height=434, nearest=True),
        panel(colour_depth(levels[mip], low, high), f'Actual GPU Hi-Z mip {mip}', f'{levels[mip].shape[1]} x {levels[mip].shape[0]} | max reduction preserves holes', width=540, height=434, nearest=True),
    ]
    title = f'Hi-Z occlusion culling / {metadata["backend"]} GPU capture'
    subtitle = f'Main triangles: {metadata["main_triangles_before"]:,} -> {metadata["main_triangles_after"]:,} | extra occluder prepass: {metadata["prepass_triangles"]:,} | HDR max error: {metadata["hdr_max_error"]:g}'
    compose(panels, 3, title, subtitle, 'Controlled static scene with a window. Bounds colours follow GPU draw counts; removing occluders is a separate reference render.', output/'hiz-overview.png')
    mip_panels = [panel(colour_depth(data, low, high), f'Mip {i}', f'{data.shape[1]} x {data.shape[0]} | white = depth 1', width=350, height=310, nearest=True) for i, data in enumerate(levels)]
    compose(mip_panels, 4, 'Actual GPU farthest-depth pyramid', f'CPU max-reduction check: {len(levels)-1}/{len(levels)-1} transitions exact | shared display range {low:.6f} .. {high:.6f}', 'False colour stretches valid 0..1 depths for display. White holes expand conservatively; native float32 readbacks are saved alongside.', output/'hiz-pyramid.png')
    result = {'scene': metadata['scene'], 'backend': metadata['backend'], 'display_equal': True,
              'hdr_max_error': metadata['hdr_max_error'], 'validated_mip_transitions': len(levels)-1,
              'culled_objects': culled, 'depth_colour_range': [low, high],
              'overview': 'hiz-overview.png', 'pyramid': 'hiz-pyramid.png'}
    (output/'visualization-checks.json').write_text(json.dumps(result, indent=2)+'\n')
    print(json.dumps(result))


if __name__ == '__main__':
    main()
