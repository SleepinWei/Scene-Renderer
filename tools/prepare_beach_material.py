#!/usr/bin/env python3
"""Pack Poly Haven beach maps into manually filtered mip atlases (NumPy/Pillow)."""
import argparse
import hashlib
import json
from pathlib import Path
import urllib.request
import numpy as np
from PIL import Image
ROOT = Path(__file__).resolve().parents[1]


def plane(path):
    image = Image.open(path)
    data = np.asarray(image)
    if data.dtype == np.uint16:
        data = data.astype(np.float32)/65535
    else:
        data = np.asarray(image.convert('RGB'),dtype=np.float32)/255
    if data.ndim == 2:
        data = np.repeat(data[...,None],3,axis=-1)
    return data[...,:3]


def mip_atlas(data, kind):
    n = data.shape[0]
    if data.shape != (n,n,3) or n&(n-1):
        raise ValueError('Square power-of-two RGB image required')
    atlas = np.zeros((n*2,n,4),dtype=np.uint8);atlas[...,3]=255
    if kind == 'albedo':
        data = data**2.2
    row = 0
    while n:
        encoded = data.copy()
        if kind == 'albedo':
            encoded = np.maximum(encoded,0)**(1/2.2)
        if kind == 'normal':
            normal = encoded*2-1
            normal /= np.maximum(np.linalg.norm(normal,axis=-1,keepdims=True),1e-8)
            encoded = normal*.5+.5
        atlas[row:row+n,:n,:3] = np.rint(np.clip(encoded,0,1)*255).astype(np.uint8)
        row += n
        if n == 1:
            break
        data = (data[::2,::2]+data[1::2,::2]+data[::2,1::2]+data[1::2,1::2])*.25
        n //= 2
    return atlas


def shoreline_mask(field, waterline, span, full_width=40, outer_width=100):
    if not 0 <= full_width < outer_width or span <= 0:
        raise ValueError('Invalid shore width')
    # Bounded octile distance from actual water samples; derived data, not a
    # modification of the imagegen water mask. One source texel is ~7.81 metres.
    step = span/(field.shape[0]-1)
    distance = np.where(field <= waterline,0,1e9).astype(np.float32)
    for _ in range(int(np.ceil(outer_width/step))+1):
        p = np.pad(distance,1,constant_values=1e9)
        for dx,dy in ((-1,0),(1,0),(0,-1),(0,1),(-1,-1),(-1,1),(1,-1),(1,1)):
            distance = np.minimum(distance,p[1+dy:1+dy+field.shape[0],1+dx:1+dx+field.shape[1]]+step*np.hypot(dx,dy))
    t = np.clip((distance-full_width)/(outer_width-full_width),0,1)
    return 1-t*t*(3-2*t)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--download',action='store_true')
    parser.add_argument('--source',type=Path,default=ROOT/'samples/downloads/beach')
    parser.add_argument('--output',type=Path,default=ROOT/'samples/assets/materials/aerial-beach-01')
    args = parser.parse_args()
    manifest = json.loads((ROOT/'samples/beach-material.json').read_text())
    args.source.mkdir(parents=True,exist_ok=True);args.output.mkdir(parents=True,exist_ok=True)
    for entry in manifest['files'].values():
        path = args.source/entry['file']
        if not path.exists() and args.download:
            request = urllib.request.Request(entry['url'],headers={'User-Agent':'SceneRenderer asset preparation'})
            with urllib.request.urlopen(request,timeout=60) as stream:
                path.write_bytes(stream.read())
        if hashlib.sha256(path.read_bytes()).hexdigest() != entry['sha256']:
            raise ValueError(f'Beach source checksum mismatch: {path}')
    albedo = plane(args.source/'diffuse.jpg');normal = plane(args.source/'normal.png')
    arm = np.stack([plane(args.source/'ao.png')[...,0],plane(args.source/'roughness.png')[...,0],np.zeros(albedo.shape[:2])],axis=-1)
    for data,kind in ((albedo,'albedo'),(normal,'normal'),(arm,'arm')):
        Image.fromarray(mip_atlas(data,kind)).save(args.output/f'{kind}-mips.png')
    print(f'Packed 1024 x 2048 mip atlases: {args.output}')

if __name__ == '__main__':
    main()
