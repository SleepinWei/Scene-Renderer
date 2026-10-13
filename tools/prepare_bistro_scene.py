#!/usr/bin/env python3
"""Prepare the free, complete ORCA Bistro scene for the native renderer.

Download the official archive to samples/downloads/bistro-v5.2.zip first:
https://developer.nvidia.com/bistro
The original FBX geometry stays intact. Only texture representation changes.
"""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import shutil
import struct
import zipfile

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
SHA256 = '0d50e3c724c6c5da19f8eb99ad3f53e36fec37ffa2df9621f9ccf0603f3934e1'


def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


def convert_texture(source, destination, maximum):
    with source.open('rb') as stream:
        header = stream.read(148)
    fourcc = header[84:88] if header[:4] == b'DDS ' else b''
    dxgi = struct.unpack('<I', header[128:132])[0] if fourcc == b'DX10' else None
    bc5 = fourcc in (b'ATI2', b'BC5U') or dxgi == 83
    with Image.open(source) as image:
        if source.stem.endswith('_Normal'):
            values = np.array(image.convert('RGB'), dtype=np.float32) / 127.5 - 1
            if bc5:
                values[..., 2] = np.sqrt(np.maximum(0, 1 - np.sum(values[..., :2] ** 2, axis=-1)))
            values[..., 1] *= -1  # DirectX normal convention -> OpenGL.
            image = Image.fromarray(np.rint(np.clip(values * .5 + .5, 0, 1) * 255).astype(np.uint8))
            image.thumbnail((maximum, maximum), Image.Resampling.LANCZOS)
            values = np.array(image, dtype=np.float32) / 127.5 - 1
            lengths = np.linalg.norm(values, axis=-1, keepdims=True)
            values /= np.maximum(lengths, 1e-8)
            image = Image.fromarray(np.rint(np.clip(values * .5 + .5, 0, 1) * 255).astype(np.uint8))
        else:
            image = image.convert('RGBA')
            image.thumbnail((maximum, maximum), Image.Resampling.LANCZOS)
        image.save(destination, compress_level=4)
        return {'source': source.name, 'output': destination.name, 'size': list(image.size), 'bc5_z_reconstructed': bc5 and source.stem.endswith('_Normal')}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive', type=Path, default=ROOT / 'samples/downloads/bistro-v5.2.zip')
    parser.add_argument('--texture-size', type=int, choices=(512, 1024, 2048), default=512)
    args = parser.parse_args()
    if not args.archive.is_file():
        parser.error('Download the free ORCA Bistro archive from https://developer.nvidia.com/bistro first')
    if digest(args.archive) != SHA256:
        raise RuntimeError('Bistro v5.2 archive differs from the verified source')
    base = ROOT / 'samples/assets/benchmarks/bistro'
    source = base / 'source'
    with zipfile.ZipFile(args.archive) as bundle:
        if sum(i.file_size for i in bundle.infolist()) > 8 * 1024**3:
            raise ValueError('Unexpected expanded Bistro package size')
        for item in bundle.infolist():
            name = PurePosixPath(item.filename.replace('\\', '/'))
            if name.is_absolute() or '..' in name.parts or ':' in str(name) or ((item.external_attr >> 16) & 0o170000) == 0o120000:
                raise ValueError('Unsafe archive member')
            if item.is_dir():
                continue
            target = source / Path(*name.parts)
            target.parent.mkdir(parents=True, exist_ok=True)
            if target.is_file() and target.stat().st_size == item.file_size:
                continue
            with bundle.open(item) as incoming, target.open('wb') as outgoing:
                shutil.copyfileobj(incoming, outgoing, 1024 * 1024)
    destination = base / 'runtime' / f'textures-{args.texture_size}'
    destination.mkdir(parents=True, exist_ok=True)
    source_root = source / 'Bistro_v5_2'
    textures = sorted([*source_root.rglob('*.dds'), *source_root.rglob('*.tga')])
    names = set()
    converted = []
    for index, texture in enumerate(textures):
        name = texture.name + '.png'
        if name.casefold() in names:
            raise RuntimeError('Ambiguous texture basename: ' + name)
        names.add(name.casefold())
        converted.append(convert_texture(texture, destination / name, args.texture_size))
        if index % 64 == 0:
            print(f'Converted {index + 1}/{len(textures)} textures', flush=True)
    report = {
        'source': 'https://developer.nvidia.com/orca/amazon-lumberyard-bistro',
        'author': 'Amazon Lumberyard', 'license': 'CC-BY-4.0',
        'archive_sha256': SHA256, 'archive_bytes': args.archive.stat().st_size,
        'texture_max_dimension': args.texture_size,
        'geometry': 'Original source FBX and node transforms, no subdivision or simplification',
        'normal_conversion': 'BC5 positive Z reconstruction, DirectX green flip, filtered normals renormalized',
        'packed_specular_channels': {'R': 'unused occlusion placeholder, ignored as in Falcor', 'G': 'roughness', 'B': 'metallic'},
        'material_reference': 'https://github.com/NVIDIAGameWorks/Falcor/blob/master/docs/usage/scene-formats.md',
        'textures': converted,
    }
    (base / 'runtime/provenance.json').write_text(json.dumps(report, indent=2) + '\n')
    layout = {
        'model': str((source_root / 'BistroExterior.fbx').relative_to(ROOT)),
        'textures': str(destination.relative_to(ROOT)), 'height': 24,
        'source_triangles': 2832120, 'texture_max_dimension': args.texture_size,
        'camera': {'source_position': [14.6121, 5.1, -41.8093], 'source_target': [7, 5, -18], 'fov': 58, 'exposure': 1.15},
    }
    (base / 'runtime/scene.json').write_text(json.dumps(layout, indent=2) + '\n')
    print(f'Ready: {base / "runtime/scene.json"}', flush=True)


if __name__ == '__main__':
    main()
