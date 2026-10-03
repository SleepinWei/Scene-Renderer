#!/usr/bin/env python3
"""Bake bounded-runtime terrain VT packs (requires numpy and Pillow).
Height input: raw little-endian float32, row zero at local Z=-1.
Material input: top-row-first images, albedo RGB encoded, other maps linear.
"""
import argparse
import json
import hashlib
import os
import tempfile
from pathlib import Path
import numpy as np
from PIL import Image

TILE, BORDER = 64, 2
PITCH = TILE + BORDER * 2


def extent_for(n):
    if not 1 <= n <= 16384:
        raise ValueError('dimensions must be in 1..16384')
    return max(TILE, 1 << (n - 1).bit_length())


def resize_plane(plane, n):
    return np.asarray(Image.fromarray(plane.astype(np.float32), 'F').resize((n, n), Image.Resampling.BOX), dtype=np.float32).copy()


def height_levels(raw, width, height):
    if width < 2 or height < 2 or raw.stat().st_size != width * height * 4:
        raise ValueError('height file must contain exactly width * height float32 samples')
    field = np.memmap(raw, mode='r', dtype='<f4', shape=(height, width))
    if not np.isfinite(field).all():
        raise ValueError('height input contains nonfinite values')
    minimum, maximum = float(field.min()), float(field.max())
    extent = extent_for(max(width, height))
    # Resample endpoints, preserving the local XZ [-1,1] coordinate convention.
    x = np.linspace(0, width-1, extent)
    y = np.linspace(0, height-1, extent)
    x0, y0 = x.astype(int), y.astype(int)
    x1, y1 = np.minimum(x0+1, width-1), np.minimum(y0+1, height-1)
    resampled = np.empty((extent, extent), dtype=np.float32)
    for start in range(0, extent, 64):
        end = min(start+64, extent)
        lo, hi = y0[start:end], y1[start:end]
        weight = (y[start:end]-lo)[:, None]
        top = (1-(x-x0)[None, :])*field[lo[:, None], x0] + (x-x0)[None, :]*field[lo[:, None], x1]
        bottom = (1-(x-x0)[None, :])*field[hi[:, None], x0] + (x-x0)[None, :]*field[hi[:, None], x1]
        resampled[start:end] = (1-weight)*top + weight*bottom
    field = resampled

    while field.shape[0] >= TILE:
        yield [field[..., None]], minimum, maximum
        if field.shape[0] == TILE:
            break
        # Symmetric low-pass before endpoint resampling; affine fields stay affine.
        padded = np.pad(field, ((0,0),(1,1)), mode='edge')
        filtered = .25*padded[:, :-2]+.5*padded[:, 1:-1]+.25*padded[:, 2:]
        padded = np.pad(filtered, ((1,1),(0,0)), mode='edge')
        filtered = .25*padded[:-2]+.5*padded[1:-1]+.25*padded[2:]
        n = field.shape[0]//2
        position = np.linspace(0, field.shape[0]-1, n)
        lo, hi = position.astype(int), np.minimum(position.astype(int)+1, field.shape[0]-1)
        weight = position-lo
        lower = ((1-weight[:, None])*((1-weight[None, :])*filtered[lo[:, None], lo]+weight[None, :]*filtered[lo[:, None], hi])+
                 weight[:, None]*((1-weight[None, :])*filtered[hi[:, None], lo]+weight[None, :]*filtered[hi[:, None], hi])).astype(np.float32)
        for row in (0, -1):
            lower[row] = (1-weight)*field[row, lo]+weight*field[row, hi]
            lower[:, row] = (1-weight)*field[lo, row]+weight*field[hi, row]
        field = lower



def material_levels(paths, extent):
    defaults = [(255,255,255,255), (128,128,255,255), (0,0,0,255), (255,255,255,255), (255,255,255,255)]
    planes = []
    for layer, path in enumerate(paths):
        if path:
            pixels = np.asarray(Image.open(path).convert('RGBA'), dtype=np.float32)/255
        else:
            pixels = np.array(defaults[layer], dtype=np.float32).reshape(1,1,4)/255
        if layer == 0:
            pixels[..., :3] **= 2.2
        planes.append(np.stack([resize_plane(pixels[..., c], extent) for c in range(4)], axis=-1))
    while extent >= TILE:
        encoded = []
        for layer, plane in enumerate(planes):
            value = plane.copy()
            if layer == 0:
                value[..., :3] = np.maximum(value[..., :3], 0)**(1/2.2)
            if layer == 1:
                normal = value[..., :3]*2-1
                normal /= np.maximum(np.linalg.norm(normal, axis=-1, keepdims=True), 1e-8)
                value[..., :3] = normal*.5+.5
            encoded.append(np.rint(np.clip(value, 0, 1)*255).astype(np.uint8))
        yield encoded, 0., 0.
        if extent == TILE:
            break
        extent //= 2
        planes = [np.stack([resize_plane(plane[..., c], extent) for c in range(4)], axis=-1) for plane in planes]


def write_pack(output, name, levels, height_field):
    output.mkdir(parents=True, exist_ok=True)
    extent = None
    temporary = []
    try:
        digest = hashlib.sha256()
        with tempfile.NamedTemporaryFile(dir=output, prefix=name+'-', suffix='.tmp', delete=False) as stream:
            temporary.append(Path(stream.name))
            for planes, minimum, maximum in levels:
                n = planes[0].shape[0]
                if extent is None:
                    extent, bounds = n, (minimum, maximum)
                for y in range(n//TILE):
                    rows = np.clip(np.arange(y*TILE-BORDER, y*TILE+TILE+BORDER), 0, n-1)
                    for x in range(n//TILE):
                        columns = np.clip(np.arange(x*TILE-BORDER, x*TILE+TILE+BORDER), 0, n-1)
                        for plane in planes:
                            tile = plane[rows[:, None], columns]
                            if height_field:
                                rgba = np.zeros((PITCH, PITCH, 4), dtype='<f4')
                                rgba[..., 0] = tile[..., 0]
                                tile = rgba
                            payload = tile.tobytes(order='C')
                            stream.write(payload)
                            digest.update(payload)
        if extent is None:
            raise ValueError('VT source contains no mip levels')
        # Immutable generations keep a running renderer and old manifest valid.
        tile_path = output/(name+'-'+digest.hexdigest()[:16]+'.tiles')
        if tile_path.exists():
            temporary[0].unlink()
        else:
            os.replace(temporary[0], tile_path)
        manifest = dict(version=1, extent=extent, tile=TILE, border=BORDER, heightField=height_field,
                        minimum=bounds[0], maximum=bounds[1], formats=['rgba32f'] if height_field else ['rgba8']*5, data=tile_path.name)
        with tempfile.NamedTemporaryFile(mode='w', dir=output, prefix=name+'-', suffix='.tmp', delete=False) as stream:
            temporary.append(Path(stream.name))
            stream.write(json.dumps(manifest, indent=2)+'\n')
        os.replace(temporary[1], output/(name+'.json'))
        print(f'{name}: {extent} x {extent}, {tile_path.stat().st_size} disk bytes')
    finally:
        for path in temporary:
            path.unlink(missing_ok=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--height', type=Path, required=True)
    parser.add_argument('--width', type=int, required=True)
    parser.add_argument('--height-size', type=int, required=True)
    parser.add_argument('--albedo', type=Path)
    parser.add_argument('--normal', type=Path)
    parser.add_argument('--metallic', type=Path)
    parser.add_argument('--roughness', type=Path)
    parser.add_argument('--ao', type=Path)
    parser.add_argument('--material-size', type=int, default=0)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    write_pack(args.output, 'height', height_levels(args.height, args.width, args.height_size), True)
    paths = [args.albedo, args.normal, args.metallic, args.roughness, args.ao]
    size = args.material_size or max([max(Image.open(p).size) for p in paths if p] or [64])
    write_pack(args.output, 'material', material_levels(paths, extent_for(size)), False)


if __name__ == '__main__':
    main()
