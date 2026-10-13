#!/usr/bin/env python3
"""Arrange real GPU membership captures and show measured LOD changes."""
import argparse
import gzip
import json
from pathlib import Path
import numpy as np
from PIL import Image, ImageDraw
from visualize_render_diagnostics import panel, grid, font, BG
from visualize_cluster_boundaries import create_boundary_views


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('capture', type=Path)
    parser.add_argument('--scene', default='Sponza')
    args = parser.parse_args()
    root = args.capture / 'flow'
    raw = root / 'flow.json'
    data = json.loads(raw.read_text() if raw.exists() else gzip.decompress((root / 'flow.json.gz').read_bytes()))
    frames = {f['name']: f for f in data['frames']}
    if any(f.get('membership_file') for f in frames.values()) and sum(f['clusters'] for f in frames.values()) > 200000:
        parser.error('Dense streamed capture: use tools/visualize_cluster_scene_compact.py CAPTURE OUTPUT --scene NAME to avoid retaining every membership list')
    for frame in frames.values():
        if frame.get('membership_file'):
            with (root/frame['membership_file']).open() as source:
                frame['membership'] = [json.loads(line) for line in source]
        membership = frame['membership']
        if len({(v['packet'], v['cluster']) for v in membership}) != len(membership):
            raise ValueError('duplicate GPU cluster in flow capture')
        if sum(v['triangles'] for v in membership) != frame['triangles']:
            raise ValueError('GPU membership differs from indirect draw count')
        if sum(frame['triangles_by_level'].values()) != frame['triangles']:
            raise ValueError('LOD level partition lost triangles')
    boundary_stats = create_boundary_views(root, data, args.scene)
    def capture_image(name, kind):
        if boundary_stats and kind in ('clusters', 'levels'):
            kind += '-boundaries'
        return Image.open(root/f'{name}-{kind}.png')
    panels = []
    for name, title in [('0', 'Zero-error cut'), ('1', '4 px cut / same camera')]:
        frame = frames[name]
        for kind, label in [('beauty', 'PBR'), ('clusters', 'Cluster IDs'), ('levels', 'Group LOD level')]:
            panels.append(panel(capture_image(name, kind), f'{title}: {label}',
                                f"{frame['triangles']:,} triangles / {frame['clusters']:,} selected clusters", width=550, height=370))
    diagnostic = ('Actual GPU membership / white = unique raster-ID boundaries / gray = non-cluster geometry / no UI.'
                  if boundary_stats else
                  'Actual GPU membership / original alpha masks / gray = non-cluster geometry / no UI.'
                  if 'diagnostic_raster' in data else
                  'Cluster colors use actual GPU membership; debug raster omits alpha masks and non-cluster meshes.')
    grid(panels, 3, f'{args.scene}: full-detail cut -> GPU cluster partition -> selected LOD',
         diagnostic, root / 'partition.png')
    panels, animation = [], []
    for name, title in [('2', 'Near / 0.45x distance'), ('3', 'Middle / 1x distance'), ('4', 'Far / 2x distance'), ('5', 'Far / 4x distance')]:
        frame = frames[name]
        title = frame.get('label') or title
        row = []
        for kind, label in [('beauty', 'PBR'), ('clusters', 'Cluster IDs'), ('levels', 'Group LOD level')]:
            tile = panel(capture_image(name, kind), f'{title}: {label}',
                         f"{frame['triangles']:,} tris / same-view LOD reduction {frame['lod_reduction_percent']:.1f}%", width=550, height=370)
            panels.append(tile)
            row.append(tile)
        canvas = Image.new('RGB', (1650, 510), BG)
        draw = ImageDraw.Draw(canvas)
        draw.text((18, 12), f'{args.scene}: camera route / fixed 4 px error / GPU-selected LOD', font=font(26), fill='white')
        for i, tile in enumerate(row):
            canvas.paste(tile, (i*550, 52))
        x, y, length = 18, 434, 900
        total = frame['triangles']
        for level, triangles in sorted(frame['triangles_by_level'].items(), key=lambda v: int(v[0])):
            end = x + length * triangles / total
            color = tuple(round(c*255) for c in data['level_colors'][int(level)])
            draw.rectangle((round(x), y, round(end), y+18), fill=color)
            x = end
        levels = ', '.join(f"L{level}: {100*count/total:.1f}%" for level, count in sorted(frame['triangles_by_level'].items(), key=lambda v: int(v[0])))
        draw.text((18, 465), 'Submitted triangle distribution / ' + levels, font=font(16), fill='white')
        draw.text((950, 432), f"Same-view 0 px: {frame['same_view_zero_error_triangles']:,} tris", font=font(19), fill='white')
        draw.text((950, 464), f"LOD saves {frame['lod_reduction_percent']:.2f}% / {frame['clusters']:,} clusters", font=font(19), fill='white')
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
    if '6' in frames:
        frame = frames['6']
        overview = [panel(capture_image('6', kind), label,
                          f"{frame['triangles']:,} tris / {frame['clusters']:,} selected clusters", width=550, height=370)
                    for kind, label in [('beauty', 'Complete block / PBR'), ('clusters', 'Actual GPU cluster IDs'), ('levels', 'Group LOD level / 4 px')]]
        grid(overview, 3, f'{args.scene}: whole-scene overview', diagnostic, root / 'overview.png')
        crops = []
        for name, title in [('0', '0 px / full-detail cut'), ('1', '4 px / same camera')]:
            for kind, label in [('clusters', 'Cluster IDs'), ('levels', 'LOD levels')]:
                image = capture_image(name, kind)
                box = tuple(round(v*image.width/1280) for v in (70, 130, 620, 650))
                crops.append(panel(image.crop(box), f'{title}: {label}', 'Identical crop of native GPU readbacks', width=650, height=680, nearest=True))
        grid(crops, 2, f'{args.scene}: facade cluster detail / same view',
             'Cluster IDs identify raster groups; cyan = leaf level 0, green = parent level 1, lime = level 2.', root / 'detail.png')
        reference = np.array(Image.open(root / '1-reference.png').convert('RGB'), dtype=np.int16)
        selected = np.array(Image.open(root / '1-beauty.png').convert('RGB'), dtype=np.int16)
        difference = Image.fromarray(np.clip(np.abs(reference-selected)*8, 0, 255).astype(np.uint8))
        frame = frames['1']
        quality = [panel(Image.fromarray(reference.astype(np.uint8)), '0 px / same-view reference', 'Full-detail cluster cut, original materials', width=550, height=370),
                   panel(Image.fromarray(selected.astype(np.uint8)), '4 px / selected LOD', f"HDR relative RMSE {frame['zero_error_hdr_relative_rmse_percent']:.3f}%", width=550, height=370),
                   panel(difference, 'Display difference / 8x', 'Absolute RGB readback difference, amplified', width=550, height=370)]
        grid(quality, 3, f'{args.scene}: LOD appearance comparison / fixed street camera',
             'Pixel error constrains projected geometric displacement; it does not bound shading or texture differences.', root / 'quality.png')
    summary = {'scene': args.scene, 'mesh_count': len(data['meshes']),
               'source_triangles': sum(m['source_triangles'] for m in data['meshes']),
               'protected_leaf_triangles': sum(m['source_triangles'] for m in data['meshes'] if m['protected_leaf']),
               'resident_bytes': sum(m['resident_bytes'] for m in data['meshes']),
               'budget_limited_meshes': sum(m['budget_limited'] for m in data['meshes']),
               'frames': [{k: v for k, v in f.items() if k != 'membership'} for f in frames.values()]}
    for key in ('viewport', 'object_culling', 'occlusion', 'diagnostic_raster', 'non_cluster_triangles', 'total_source_triangles', 'degenerate_cluster_source_triangles', 'source_meshes', 'hierarchies_ready', 'publication_wait_seconds', 'vertical_fov_degrees', 'near_plane', 'far_plane'):
        if key in data:
            summary[key] = data[key]
    summary['leaf_clusters'] = sum(m['leaf_clusters'] for m in data['meshes'])
    summary['leaf_triangle_utilization_percent'] = 100*summary['source_triangles']/max(128*summary['leaf_clusters'], 1)
    summary['parent_groups_accepted'] = sum(m.get('parent_accepted', 0) for m in data['meshes'])
    summary['parent_groups_attempted'] = sum(m.get('parent_attempts', 0) for m in data['meshes'])
    summary['parent_groups_reduction_rejected'] = sum(m.get('reduction_rejected', 0) for m in data['meshes'])
    if boundary_stats:
        summary['cluster_boundaries'] = boundary_stats
    (root / 'summary.json').write_text(json.dumps(summary, indent=2)+'\n')
    print(json.dumps(summary, indent=2))


if __name__ == '__main__':
    main()
