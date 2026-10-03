#!/usr/bin/env python3
"""Convert ill_drakon's downloaded Mountain Lake to terrain VT (NumPy, Pillow).

Download Original FBX and converted glTF ZIPs from the official Sketchfab page.
No login tokens are stored. This converter targets that asset's Z-up regular grid,
not arbitrary glTF models; it validates positions, seam duplicates and UVs.
"""
import argparse
import hashlib
import io
import json
import zipfile
from pathlib import Path

import numpy as np
from PIL import Image
from bake_terrain_vt import extent_for, height_levels, material_levels, write_pack

from prepare_beach_material import shoreline_mask

ROOT = Path(__file__).resolve().parents[1]


def grid_from_vertices(positions, uv):
    """Return source heights in increasing X/Y order; check a complete heightfield."""
    if positions.ndim != 2 or positions.shape[1] != 3 or uv.shape != (len(positions), 2):
        raise ValueError('Invalid terrain vertex or UV shape')
    if not np.isfinite(positions).all() or not np.isfinite(uv).all():
        raise ValueError('Nonfinite terrain data')
    # FBX/glTF conversion introduces sub-millimetre noise in source coordinates.
    xy = np.round(positions[:, :2].astype(np.float64), 3)
    xs, ix = np.unique(xy[:, 0], return_inverse=True)
    ys, iy = np.unique(xy[:, 1], return_inverse=True)
    if min(len(xs), len(ys)) < 2 or max(len(xs), len(ys)) > 16384:
        raise ValueError('Invalid grid extent')
    for coordinates in (xs, ys):
        if not np.allclose(np.diff(coordinates), np.diff(coordinates).mean(), atol=.003, rtol=1e-5):
            raise ValueError('Terrain is not a regular grid')
    expected_uv = (xy - [xs[0], ys[0]]) / [xs[-1]-xs[0], ys[-1]-ys[0]]
    if not np.allclose(uv, expected_uv, atol=1e-5, rtol=0):
        raise ValueError('Source UVs do not match the expected planar mapping')
    flat = iy * len(xs) + ix
    low = np.full(len(xs)*len(ys), np.inf, dtype=np.float32)
    high = np.full_like(low, -np.inf)
    np.minimum.at(low, flat, positions[:, 2])
    np.maximum.at(high, flat, positions[:, 2])
    if not np.isfinite(low).all():
        raise ValueError('Height grid has missing samples')
    if np.max(high-low) > .003:
        raise ValueError('Overlapping vertices disagree on height')
    return low.reshape(len(ys), len(xs)), xs, ys


def read_accessor(document, buffers, index):
    accessor = document['accessors'][index]
    if 'sparse' in accessor or accessor.get('normalized', False):
        raise ValueError('Unsupported accessor encoding')
    view = document['bufferViews'][accessor['bufferView']]
    data = buffers[view['buffer']]
    dtype = {5126: '<f4', 5123: '<u2', 5125: '<u4'}[accessor['componentType']]
    channels = {'VEC3': 3, 'VEC2': 2, 'SCALAR': 1}[accessor['type']]
    item = np.dtype(dtype).itemsize
    offset = view.get('byteOffset', 0) + accessor.get('byteOffset', 0)
    return np.ndarray((accessor['count'], channels), dtype, buffer=data,
                      offset=offset, strides=(view.get('byteStride', channels*item), item)).copy()


def verify_archive(path, expected):
    with path.open('rb') as stream:
        digest = hashlib.file_digest(stream, 'sha256').hexdigest()
    if digest != expected:
        raise ValueError(f'Archive differs from the verified source: {path}')


def prepare(original, gltf, output, verify=True):
    manifest = json.loads((ROOT/'samples/mountain-lake.json').read_text())
    if verify:
        verify_archive(original, manifest['archives']['original']['sha256'])
        verify_archive(gltf, manifest['archives']['gltf']['sha256'])
    with zipfile.ZipFile(gltf) as archive:
        document = json.loads(archive.read('scene.gltf'))
        buffers = [archive.read(source['uri']) for source in document['buffers']]
        # This asset has no transforms below its FBX root. Preserve source units;
        # ignore the viewer's whole-scene centimetre conversion and presentation.
        if any(any(k in n for k in ('matrix', 'translation', 'rotation', 'scale'))
               for n in document['nodes'][2:]):
            raise ValueError('Unexpected transforms below the source root')
        positions, texcoords, water = [], [], []
        for mesh in document['meshes']:
            for primitive in mesh['primitives']:
                if primitive.get('mode', 4) != 4:
                    raise ValueError('Expected triangle primitives')
                material = document['materials'][primitive['material']]
                p = read_accessor(document, buffers, primitive['attributes']['POSITION'])
                if material['name'] == 'wm_meters0':
                    positions.append(p)
                    texcoords.append(read_accessor(document, buffers, primitive['attributes']['TEXCOORD_0']))
                elif material['name'] == 'PM3D_Cube3D10':
                    water.append(p)
                else:
                    raise ValueError('Unknown source material')
        field, xs, ys = grid_from_vertices(np.concatenate(positions), np.concatenate(texcoords))
        if field.shape != (1025, 1025) or not np.allclose([xs[0], xs[-1], ys[0], ys[-1]], [-4000,4000,-4000,4000]):
            raise ValueError('Unexpected Mountain Lake dimensions')
        # The source water object is a closed box; its upper face is the waterline.
        waterline = float(np.concatenate(water)[:, 2].max())
    with zipfile.ZipFile(original) as archive:
        color = Image.open(io.BytesIO(archive.read('textures/wm_meters_TXTR.png'))).convert('RGB')
        if color.size != (1025, 1025):
            raise ValueError('Unexpected source color resolution')
        # The converted glTF UVs use image-row V. Its color map flips the original
        # FBX image vertically. Preserve the original resolution with that mapping.
        color = color.transpose(Image.Transpose.FLIP_TOP_BOTTOM)
    output.mkdir(parents=True, exist_ok=True)
    span = float(xs[-1]-xs[0])
    # Right-handed Z-up -> Y-up: engine (X,Y,Z) = source (X,Z,-Y).
    # Store normalized elevations; the scene restores the original source scale.
    raw = output/'height.f32'
    (field[::-1]/span).astype('<f4').tofile(raw)
    albedo = output/'albedo.png'
    color.save(albedo)
    beach = shoreline_mask(field,waterline,span)
    Image.fromarray(np.rint(beach*255).astype(np.uint8)).save(output/'beach-mask.png')
    write_pack(output/'vt', 'height', height_levels(raw, 1025, 1025), True)
    write_pack(output/'vt', 'material', material_levels([albedo, None, None, None, None], extent_for(1025)), False)
    info = dict(heightVT=(output/'vt/height.json').as_posix(),
                materialVT=(output/'vt/material.json').as_posix(), maxLeaves=8192,
                sourceResolution=[1025,1025], sourceHorizontalSpan=span,
                sourceElevation=[float(field.min()),float(field.max())],
                sceneScale=1., modelScale=[span*.5,span,span*.5],
                shoreline=dict(seaLevel=waterline,heightRange=24, textures=[
                    "samples/assets/materials/aerial-beach-01/albedo-mips.png",
                    "samples/assets/materials/aerial-beach-01/normal-mips.png",
                    "samples/assets/materials/aerial-beach-01/arm-mips.png",
                    (output/"beach-mask.png").as_posix()]),
                seaLevel=waterline, source=manifest['source'], author=manifest['author'],
                license=manifest['license'],
                changes='Reconstructed source height grid; converted Z-up to Y-up; flipped source color rows; baked VT; replaced source water with an FFT lake preset.')
    (output/'scene.json').write_text(json.dumps(info, indent=2)+'\n')
    print(f'Native height/color: 1025 x 1025; VT extent: 2048 (resampled, not extra source detail)')
    print(f'Lake level: {info["seaLevel"]:.6f}; scene: {output/"scene.json"}')
    return info


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--original', type=Path, default=ROOT/'samples/downloads/mountain-lake/original.zip')
    parser.add_argument('--gltf', type=Path, default=ROOT/'samples/downloads/mountain-lake/gltf.zip')
    parser.add_argument('--output', type=Path, default=Path('samples/assets/terrain/mountain-lake'))
    args = parser.parse_args()
    prepare(args.original, args.gltf, args.output)


if __name__ == '__main__':
    main()
