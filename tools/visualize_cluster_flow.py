#!/usr/bin/env python3
"""Arrange real GPU membership captures and show measured LOD changes."""
import argparse
import json
from pathlib import Path
from PIL import Image, ImageDraw
from visualize_render_diagnostics import panel, grid, font, BG


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('capture', type=Path)
    parser.add_argument('--scene', default='Sponza')
    args = parser.parse_args()
    root = args.capture / 'flow'
    data = json.loads((root / 'flow.json').read_text())
    frames = {f['name']: f for f in data['frames']}
    for frame in frames.values():
        membership = frame['membership']
        if len({(v['packet'], v['cluster']) for v in membership}) != len(membership):
            raise ValueError('duplicate GPU cluster in flow capture')
        if sum(v['triangles'] for v in membership) != frame['triangles']:
            raise ValueError('GPU membership differs from indirect draw count')
        if sum(frame['triangles_by_level'].values()) != frame['triangles']:
            raise ValueError('LOD level partition lost triangles')
    panels = []
    for name, title in [('0', 'Zero-error cut'), ('1', '4 px cut / same camera')]:
        frame = frames[name]
        for kind, label in [('beauty', 'PBR'), ('clusters', 'Cluster IDs'), ('levels', 'Group LOD level')]:
            panels.append(panel(Image.open(root / f'{name}-{kind}.png'), f'{title}: {label}',
                                f"{frame['triangles']:,} triangles / {frame['clusters']:,} selected clusters", width=550, height=370))
    grid(panels, 3, f'{args.scene}: original surface -> GPU cluster partition -> selected LOD',
         'Cluster colors use actual GPU membership; debug raster omits alpha masks and non-cluster meshes.', root / 'partition.png')
    panels, animation = [], []
    for name, title in [('2', 'Near / 0.45x distance'), ('3', 'Middle / 1x distance'), ('4', 'Far / 2x distance'), ('5', 'Far / 4x distance')]:
        frame = frames[name]
        row = []
        for kind, label in [('beauty', 'PBR'), ('clusters', 'Cluster IDs'), ('levels', 'Group LOD level')]:
            tile = panel(Image.open(root / f'{name}-{kind}.png'), f'{title}: {label}',
                         f"{frame['triangles']:,} tris / same-view LOD reduction {frame['lod_reduction_percent']:.1f}%", width=550, height=370)
            panels.append(tile)
            row.append(tile)
        canvas = Image.new('RGB', (1650, 440), BG)
        draw = ImageDraw.Draw(canvas)
        draw.text((18, 12), f'{args.scene}: camera dolly / fixed 4 px error / GPU-selected LOD', font=font(26), fill='white')
        for i, tile in enumerate(row):
            canvas.paste(tile, (i*550, 52))
        animation.append(canvas)
    grid(panels, 3, f'{args.scene}: dynamic LOD from near to far / fixed 4 px threshold',
         'LOD reduction compares 0 px and 4 px in the SAME view, excluding camera-frustum savings.', root / 'dynamic-lod.png')
    animation += animation[-2:0:-1]
    animation[0].save(root / 'dynamic-lod.gif', save_all=True, append_images=animation[1:], duration=1100, loop=0)
    legend = Image.new('RGB', (900, 130), BG)
    draw = ImageDraw.Draw(legend)
    draw.text((18, 10), 'Group hierarchy level / leaf = 0', font=font(25), fill='white')
    for level, color in enumerate(data['level_colors']):
        x = 18+level*73
        draw.rectangle((x, 52, x+54, 81), fill=tuple(round(c*255) for c in color))
        draw.text((x+15, 88), str(level), font=font(20), fill='white')
    legend.save(root / 'level-legend.png')
    summary = {'scene': args.scene, 'mesh_count': len(data['meshes']),
               'source_triangles': sum(m['source_triangles'] for m in data['meshes']),
               'protected_leaf_triangles': sum(m['source_triangles'] for m in data['meshes'] if m['protected_leaf']),
               'resident_bytes': sum(m['resident_bytes'] for m in data['meshes']),
               'budget_limited_meshes': sum(m['budget_limited'] for m in data['meshes']),
               'frames': [{k: v for k, v in f.items() if k != 'membership'} for f in frames.values()]}
    (root / 'summary.json').write_text(json.dumps(summary, indent=2)+'\n')
    print(json.dumps(summary, indent=2))


if __name__ == '__main__':
    main()
