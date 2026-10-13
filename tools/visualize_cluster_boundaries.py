#!/usr/bin/env python3
"""Visualize exact cluster-ID readbacks and actual selected triangle geometry.

White boundaries come from the captured R32Float raster keys, never from RGB
color thresholds. Black triangle lines are a native depth-tested raster pass.
"""
import argparse
import gzip
import json
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw
from visualize_render_diagnostics import panel, grid, font, BG


def read_keys(root, name, width, height):
    path = root / f'{name}-clusters-ids.f32'
    values = path.read_bytes() if path.exists() else gzip.decompress(Path(str(path)+'.gz').read_bytes())
    keys = np.frombuffer(values, dtype='<f4')
    if keys.size != width*height or not np.isfinite(keys).all() or not np.equal(keys, np.floor(keys)).all():
        raise ValueError('Invalid GPU raster-key texture')
    return keys.astype(np.uint32).reshape(height, width)


def boundaries(keys):
    mask = np.zeros_like(keys, dtype=bool)
    horizontal = (keys[:, 1:] != keys[:, :-1]) & (keys[:, 1:] != 0) & (keys[:, :-1] != 0)
    vertical = (keys[1:] != keys[:-1]) & (keys[1:] != 0) & (keys[:-1] != 0)
    mask[:, 1:] |= horizontal
    mask[1:] |= vertical
    return mask


def outline(image, mask):
    values = np.array(image.convert('RGB'))
    halo = mask.copy()
    halo[:, 1:] |= mask[:, :-1]
    halo[:, :-1] |= mask[:, 1:]
    halo[1:] |= mask[:-1]
    halo[:-1] |= mask[1:]
    values[halo] = (17, 23, 32)
    values[mask] = (240, 244, 247)
    return Image.fromarray(values)


def geometry_projection(info):
    triangles = np.array(info['world_triangles'], dtype=float)
    if triangles.shape != (info['triangles'], 3, 3):
        raise ValueError('Selected triangle count differs from geometry export')
    # Check the native exact-position edge-component analysis independently.
    parent = list(range(len(triangles)))
    def root(i):
        while parent[i] != i:
            i = parent[i]
        return i
    edges = {}
    for i, triangle in enumerate(triangles):
        for a, b in zip(triangle, np.roll(triangle, -1, axis=0)):
            edge = tuple(sorted((tuple(a), tuple(b))))
            if edge in edges:
                parent[root(i)] = root(edges[edge])
            else:
                edges[edge] = i
    roots = [root(i) for i in range(len(triangles))]
    sizes = sorted((roots.count(i) for i in set(roots)), reverse=True)
    if sizes != info['component_triangle_counts']:
        raise ValueError('Native/Python edge-component analysis disagrees')
    points = triangles.reshape(-1, 3)
    points -= points.mean(axis=0)
    _, _, axes = np.linalg.svd(points, full_matrices=False)
    projected = (points @ axes[:2].T).reshape(-1, 3, 2)
    low, high = projected.min(axis=(0, 1)), projected.max(axis=(0, 1))
    scale = min(480/max(high[0]-low[0], 1e-12), 340/max(high[1]-low[1], 1e-12))
    projected = (projected-(low+high)*.5)*scale
    projected[..., 0] += 260
    projected[..., 1] = 190-projected[..., 1]
    image = Image.new('RGB', (520, 380), BG)
    draw = ImageDraw.Draw(image)
    for triangle in projected:
        polygon = [tuple(p) for p in triangle]
        draw.polygon(polygon, fill=(28, 111, 152))
        draw.line(polygon+[polygon[0]], fill=(194, 231, 244), width=1)
    return image


def create_boundary_views(root, data, scene):
    root = Path(root)
    if 'raster_key_bases' not in data:
        return None
    width, height = data['viewport']
    cluster_packets = {m['packet'] for m in data['meshes']}
    context = {key for packet, key in enumerate(data['raster_key_bases']) if packet not in cluster_packets}
    stats = {'scope': 'Visible ID transitions, including occlusion boundaries; geometry and LOD cuts unchanged', 'frames': []}
    for frame in data['frames']:
        name = frame['name']
        keys = read_keys(root, name, width, height)
        membership = {m['raster_key']: m for m in frame['membership']}
        visible = set(np.unique(keys).tolist())-{0}
        if not visible <= set(membership) | context:
            raise ValueError('Raster contains a key outside actual GPU-selected membership')
        if visible-context != set(frame['visible_cluster_raster_keys']):
            raise ValueError('Visible raster keys disagree with native readback analysis')
        mask = boundaries(keys)
        outline(Image.open(root/f'{name}-clusters.png'), mask).save(root/f'{name}-clusters-boundaries.png')
        outline(Image.open(root/f'{name}-levels.png'), mask).save(root/f'{name}-levels-boundaries.png')
        wire = root/f'{name}-clusters-wire.png'
        if wire.exists():
            outline(Image.open(wire), mask).save(root/f'{name}-clusters-boundaries-wire.png')
        legacy = {m['packet']*997+m['cluster'] for m in frame['membership']}
        legacy_visible = {membership[key]['packet']*997+membership[key]['cluster'] for key in visible-context}
        stats['frames'].append({'name': name, 'visible_cluster_count': len(visible-context),
                                'visible_id_boundary_pixels': int(mask.sum()),
                                'legacy_key_aliases': len(membership)-len(legacy),
                                'legacy_visible_key_aliases': len(visible-context)-len(legacy_visible), 'unique_keys_verified': True})
    frame = next(f for f in data['frames'] if f['name'] == '1')
    crop = (round(50*width/1280), round(130*height/720), round(640*width/1280), round(700*height/720))
    panels = []
    for suffix, title, detail in [('clusters', 'Cluster ID colors', 'One color per meshlet; source planes may be large'),
                                  ('clusters-boundaries', 'White: visible cluster boundaries', 'Exact GPU ID transitions, including occlusion edges'),
                                  ('clusters-boundaries-wire', 'Black: actual triangle edges', 'Native wireframe, depth tested against the filled cut')]:
        path = root/f'1-{suffix}.png'
        if path.exists():
            panels.append(panel(Image.open(path).crop(crop), title, detail, width=600, height=650, nearest=True))
    if len(panels) == 3:
        grid(panels, 3, f'{scene}: visible cluster boundaries vs actual triangle edges',
             'World-space meshlets have a triangle budget, not a fixed spatial size. Large source planes remain large.', root/'boundary-detail.png')
    probes = data.get('surface_inspection', [])
    for i, info in enumerate(probes):
        # Pixel-coordinate IDs connect the visible patch to its real full range.
        x, y = info['pixel']
        keys = read_keys(root, '1', width, height)
        if keys[y, x] != info['raster_key']:
            raise ValueError('Picked cluster differs from captured pixel ID')
        panels.append(panel(geometry_projection(info), f"Picked cluster {info['raster_key']} / level {info['level']}",
                            f"{info['triangles']} triangles / {info['position_edge_components']} edge-connected pieces", width=600, height=480, nearest=True))
    if len(panels) == 6:
        grid(panels, 3, f'{scene}: cluster boundaries vs actual triangles',
             'Bottom: principal-plane projection of ALL triangles in three picked clusters, including hidden parts.', root/'surface-blocks.png')
    stats['picked_clusters'] = [{k: v for k, v in info.items() if k != 'world_triangles'} for info in probes]
    (root/'boundary-summary.json').write_text(json.dumps(stats, indent=2)+'\n')
    return stats


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('capture', type=Path)
    parser.add_argument('--scene', default='Bistro exterior')
    args = parser.parse_args()
    root = args.capture/'flow'
    path = root/'flow.json'
    data = json.loads(path.read_text() if path.exists() else gzip.decompress((root/'flow.json.gz').read_bytes()))
    result = create_boundary_views(root, data, args.scene)
    if result is None:
        raise ValueError('Capture has no unique raster-key readbacks; regenerate --cluster-scene-flow')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
