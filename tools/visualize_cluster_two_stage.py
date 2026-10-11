#!/usr/bin/env python3
"""Validate and display native two-stage cluster readbacks; never synthesize renders."""
import argparse
import json
from pathlib import Path
import numpy as np
from PIL import Image
from visualize_render_diagnostics import panel, grid
from visualize_hiz import colour_depth, load_mips


def projected_rectangle(cluster, vp, model, width, height):
    # Independent double-precision proof over every covered base-level pixel.
    # No recreation of the GPU SSE cut, scan, mip selection or prefix algorithm.
    lo, hi = np.asarray(cluster['low']), np.asarray(cluster['high'])
    corners = np.array([[*(hi[j] if i & (1 << j) else lo[j] for j in range(3)), 1.0] for i in range(8)])
    clip = corners @ (vp @ model).T
    if not np.isfinite(clip).all() or (clip[:, 3] <= 1e-6).any() or (clip[:, 2] <= 1e-6).any():
        return None
    ndc = clip[:, :3] / clip[:, 3, None]
    low, high = ndc[:, :2].min(0), ndc[:, :2].max(0)
    if high[0] < -1 or low[0] > 1 or high[1] < -1 or low[1] > 1:
        return None
    rect = np.array([(low[0]*.5+.5)*width-2, (.5-high[1]*.5)*height-2,
                     (high[0]*.5+.5)*width+2, (.5-low[1]*.5)*height+2])
    rect = np.floor(np.clip(rect, 0, [width-1, height-1, width-1, height-1])).astype(int)
    return rect, ndc[:, 2].min()


def capture_case(root, directory):
    metadata = json.loads((directory/'metadata.json').read_text())
    w, h = metadata['width'], metadata['height']
    archive_path = directory/'readbacks.npz'
    archive = np.load(archive_path) if archive_path.exists() else None
    def floats(name):
        file = directory/f'{name}.f32'
        return np.fromfile(file, '<f4') if file.exists() else archive[name]
    pyramids = {}
    saved = {}
    for which, manifest in metadata['pyramids'].items():
        if not manifest:
            continue
        validation = {'mips': manifest}
        if archive is not None:
            # load_mips uses a simple level-named archive. Validate here after
            # temporarily retaining source data in memory through a separate view.
            levels = [floats(f'{which}-{m["level"]}').reshape(m['height'], m['width']) for m in manifest]
            for i, depth in enumerate(levels):
                if not np.isfinite(depth).all() or (depth < 0).any() or (depth > 1).any():
                    raise ValueError('invalid native depth')
                if i:
                    src = levels[i-1]
                    for y in range(depth.shape[0]):
                        ye = src.shape[0] if y == depth.shape[0]-1 else min(y*2+2, src.shape[0])
                        for x in range(depth.shape[1]):
                            xe = src.shape[1] if x == depth.shape[1]-1 else min(x*2+2, src.shape[1])
                            if depth[y, x] != src[y*2:ye, x*2:xe].max():
                                raise ValueError('native max-depth reduction mismatch')
        else:
            levels = load_mips(directory, validation)
        pyramids[which] = levels
        for i, depth in enumerate(levels):
            saved[f'{which}-{i}'] = depth.reshape(-1)
    final = floats('final-depth').reshape(h, w)
    reference = floats('reference-depth').reshape(h, w)
    if not np.array_equal(final, reference):
        raise ValueError('final depth differs from uncullled reference')
    saved['final-depth'], saved['reference-depth'] = final.reshape(-1), reference.reshape(-1)
    albedo_panels = []
    for stage in ('early', 'final'):
        name = stage+'-albedo'
        if (directory/f'{name}.f32').exists() or (archive is not None and name in archive):
            values = floats(name).reshape(h, w, 4)
            saved[name] = values.reshape(-1)
            image = np.round(np.clip(values[:, :, :3], 0, 1)**(1/2.2)*255).astype(np.uint8)
            Image.fromarray(image).save(directory/f'{name}.png')
            albedo_panels.append(panel(Image.fromarray(image), f'Actual {stage} G-buffer albedo',
                                       'Native GPU attachment copy; linear RGB shown as sRGB'))
    clusters = metadata['clusters']
    cut = sum(c['triangles'] for c in clusters)
    early = sum(c['triangles'] for c in clusters if c['early'])
    late = sum(c['triangles'] for c in clusters if c['late'])
    rejected = [c for c in clusters if not c['early'] and not c['late']]
    if any(c['early'] and c['late'] for c in clusters) or cut != early+late+sum(c['triangles'] for c in rejected):
        raise ValueError('GPU visibility partition duplicates or loses a cluster')
    vp = np.array(metadata['view_projection']).reshape((4, 4), order='F')
    models = [np.array(m).reshape((4, 4), order='F') for m in metadata['models']]
    margins = []
    depth = floats('early-depth').reshape(h, w) if ((directory/'early-depth.f32').exists() or (archive is not None and 'early-depth' in archive)) else pyramids['current'][0]
    if metadata.get('current_base_mip', 0):
        coarse = depth
        for _ in range(metadata['current_base_mip']):
            hh, ww = max(coarse.shape[0]//2, 1), max(coarse.shape[1]//2, 1)
            reduced = np.empty((hh, ww), dtype=np.float32)
            for y in range(hh):
                ye = coarse.shape[0] if y == hh-1 else y*2+2
                for x in range(ww):
                    xe = coarse.shape[1] if x == ww-1 else x*2+2
                    reduced[y, x] = coarse[y*2:ye, x*2:xe].max()
            coarse = reduced
        if not np.array_equal(coarse, pyramids['current'][0]):
            raise ValueError('coarse Hi-Z base does not include every early depth texel')
    saved['early-depth'] = depth.reshape(-1)
    for c in rejected:
        projection = projected_rectangle(c, vp, models[c['packet']], w, h)
        if projection is None:
            raise ValueError('rejected a near/eye/outside uncertain cluster')
        (x0, y0, x1, y1), nearest = projection
        farthest = depth[y0:y1+1, x0:x1+1].max()
        margin = nearest-farthest
        if margin <= 1e-4:
            raise ValueError(f'current base-depth cannot prove rejection: {margin}')
        margins.append(float(margin))
    recovered_pixels = int((depth > final+1e-6).sum())
    if late and not recovered_pixels:
        raise ValueError('late geometry recovered no depth pixels')
    beauty = Image.open(root/f'{directory.name}-final.png').convert('RGB')
    reference_image = Image.open(root/f'{directory.name}-reference.png').convert('RGB')
    if not np.array_equal(np.array(beauty), np.array(reference_image)):
        raise ValueError('native displayed images differ')
    panels = [panel(reference_image, 'Reference: Hi-Z off', 'Same camera / materials / LOD / exposure'),
              panel(beauty, 'Final: early + late', f'{early+late:,} triangles; exact reference pixels')]
    counts = {'early': early, 'pending': cut-early, 'late': late, 'rejected': cut-early-late}
    labels = {'early': 'Early G-buffer membership', 'pending': 'History rejected: retest all',
              'late': 'Late recovered membership', 'rejected': 'Final current-depth rejects'}
    for stage in counts:
        panels.append(panel(Image.open(directory/f'{stage}.png'), labels[stage],
                            f'{counts[stage]:,} triangles; draw actual GPU flags'))
    if albedo_panels:
        grid(albedo_panels, 2, f'Native intermediate attachments: {directory.name}',
             'Early albedo copied immediately after early draw; diagnostic copy disabled during benchmarks.', directory/'attachments.png')
    grid(panels, 2, f'Sponza two-stage occlusion: {directory.name}',
         'Native captures; isolated stage membership uses original triangles and materials; UI hidden.', directory/'stages.png')
    occupied = depth[depth < 1]
    low, high = np.quantile(occupied, [.01, .99]) if occupied.size else (0., 1.)
    panels = []
    for which, levels in pyramids.items():
        for mip in (0, 2, 4, 7):
            if mip < len(levels):
                z = levels[mip]
                panels.append(panel(colour_depth(z, low, high), f'{which.title()} Hi-Z mip {mip+metadata.get(which+"_base_mip", 0)}',
                                    f'{z.shape[1]} x {z.shape[0]}; native standard-Z max', nearest=True))
    panels.append(panel(colour_depth(final, low, high), 'Final depth: early + late',
                        f'{recovered_pixels:,} pixels restored after early draw'))
    difference = np.abs(np.array(beauty).astype(int)-np.array(reference_image).astype(int)).astype(np.uint8)
    panels.append(panel(Image.fromarray(difference), 'Image difference (exact)', 'Black = zero difference in every display pixel'))
    grid(panels, 2, f'Actual GPU depth pyramids: {directory.name}',
         'False colour remaps native depths. White is clear depth 1. Every max-reduction mip checked on CPU.', directory/'pyramids.png')
    proof = {'case': directory.name, 'mip_levels_checked': {k: len(v) for k, v in pyramids.items()},
             'cut_triangles': cut, 'early_triangles': early, 'late_triangles': late,
             'final_rejected_clusters_proven_from_current_base_depth': len(rejected),
             'minimum_occlusion_margin': min(margins) if margins else None,
             'pixels_restored_late': recovered_pixels, 'display_exact': True, 'final_depth_exact': True}
    (directory/'proof.json').write_text(json.dumps(proof, indent=2)+'\n')
    if archive is not None:
        archive.close()
    np.savez_compressed(archive_path, **saved)
    metadata['readback_archive'] = 'readbacks.npz'
    (directory/'metadata.json').write_text(json.dumps(metadata, indent=2)+'\n')
    for file in directory.glob('*.f32'):
        file.unlink()
    return proof


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('capture', type=Path)
    args = parser.parse_args()
    root = args.capture/'two-stage'
    results = [capture_case(root, directory) for directory in sorted(root.iterdir())
               if directory.is_dir() and (directory/'metadata.json').exists()]
    (root/'proof.json').write_text(json.dumps(results, indent=2)+'\n')
    print(json.dumps(results, indent=2))


if __name__ == '__main__':
    main()
