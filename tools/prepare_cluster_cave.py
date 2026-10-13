#!/usr/bin/env python3
"""Build a deterministic, dense limestone grotto without downloading assets.

Requires numpy and Pillow. Binary PLY retains shared vertices, smooth geometric
normals and continuous projected UVs. Instances share the same source mesh.
The package (including existing files) must remain below 10 decimal GB.
"""
import argparse
import ctypes
import resource
import sys
import hashlib
import json
from pathlib import Path

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'samples/assets/benchmarks/procedural-cave'
LIMIT = 10_000_000_000
VERSION = 1


def memory_guard(budget_mib, additional):
    """Match renderer defaults; refuse heavy construction at warning pressure."""
    physical, pressure = 0, 0
    if sys.platform == 'darwin':
        libc = ctypes.CDLL(None)
        def query(name, value):
            size = ctypes.c_size_t(ctypes.sizeof(value))
            if libc.sysctlbyname(name.encode(), ctypes.byref(value), ctypes.byref(size), None, 0):
                raise RuntimeError(f'Cannot sample memory safety value {name}')
            return value.value
        physical = query('hw.memsize', ctypes.c_uint64())
        flags = query('kern.memorystatus_vm_pressure_level', ctypes.c_uint32())
        pressure = 2 if flags & 4 else 1 if flags & 2 else 0
    budget = budget_mib*1024**2 if budget_mib else min(3*1024**3, physical//5) if physical else 1024**3
    if budget < 64*1024**2 or (physical and (budget > physical//2 or (physical >= 4*1024**3 and physical-budget < 2*1024**3))):
        raise ValueError('Memory budget must preserve the renderer system reserve')
    peak = resource.getrusage(resource.RUSAGE_SELF).ru_maxrss
    if sys.platform != 'darwin':
        peak *= 1024
    if pressure or peak+additional+min(128*1024**2,budget//16) > budget:
        raise RuntimeError(f'Cave generation refused: pressure={pressure}, sampled peak={peak//1024**2} MiB, '
                           f'next-step estimate={additional//1024**2} MiB, budget={budget//1024**2} MiB')
    return budget


def noise(p, seed):
    cell = np.floor(p).astype(np.int32)
    t = p - cell
    t = t*t*t*(t*(t*6-15)+10)
    result = np.zeros(p.shape[:-1], dtype=np.float64)
    for z in range(2):
        for y in range(2):
            for x in range(2):
                q = (cell + (x, y, z)).astype(np.uint32)
                with np.errstate(over='ignore'):
                    h = q[..., 0]*np.uint32(73856093) ^ q[..., 1]*np.uint32(19349663) ^ q[..., 2]*np.uint32(83492791) ^ np.uint32(seed)
                    h ^= h >> 16
                    h *= np.uint32(0x7feb352d)
                    h ^= h >> 15
                    h *= np.uint32(0x846ca68b)
                    h ^= h >> 16
                value = (h >> 8).astype(np.float64)/8388608-1
                weight = (t[..., 0] if x else 1-t[..., 0])*(t[..., 1] if y else 1-t[..., 1])*(t[..., 2] if z else 1-t[..., 2])
                result += value*weight
    return result


def icosphere(level):
    phi = (1+np.sqrt(5))/2
    points = np.array([[-1,phi,0],[1,phi,0],[-1,-phi,0],[1,-phi,0],
                       [0,-1,phi],[0,1,phi],[0,-1,-phi],[0,1,-phi],
                       [phi,0,-1],[phi,0,1],[-phi,0,-1],[-phi,0,1]], dtype=np.float64)
    points /= np.linalg.norm(points, axis=1)[:, None]
    faces = np.array([[0,11,5],[0,5,1],[0,1,7],[0,7,10],[0,10,11],
                      [1,5,9],[5,11,4],[11,10,2],[10,7,6],[7,1,8],
                      [3,9,4],[3,4,2],[3,2,6],[3,6,8],[3,8,9],
                      [4,9,5],[2,4,11],[6,2,10],[8,6,7],[9,8,1]], dtype=np.uint32)
    for _ in range(level):
        edges = np.concatenate((faces[:, [0,1]], faces[:, [1,2]], faces[:, [2,0]]))
        edges.sort(axis=1)
        unique, mapping = np.unique(edges, axis=0, return_inverse=True)
        middle = points[unique[:, 0]]+points[unique[:, 1]]
        middle /= np.linalg.norm(middle, axis=1)[:, None]
        a, b, c = faces.T
        ab, bc, ca = (mapping+len(points)).reshape(3, -1).astype(np.uint32)
        faces = np.concatenate((np.stack((a, ab, ca), axis=1), np.stack((b, bc, ab), axis=1),
                                np.stack((c, ca, bc), axis=1), np.stack((ab, bc, ca), axis=1)))
        points = np.concatenate((points, middle))
    return points, faces


def rock(directions, variant, seed):
    dimensions = [(5.2,4.5,4.1),(4.8,4.0,4.5),(4.7,2.3,3.8),(1.7,5.2,1.7),
                  (1.6,4.6,1.5),(2.4,5.8,2.3),(3.1,2.6,2.9),(5.0,1.65,3.9)]
    power = 2.65 if variant in (0,1,2,7) else 2.15
    p = directions/np.sum(np.abs(directions)**power, axis=1)[:, None]**(1/power)
    p *= dimensions[variant]
    if variant in (3,4):
        # Broad attachment, narrowing toward a bent mineral-growth tip.
        height = np.clip((directions[:, 1]+1)/2, 0, 1)
        taper = .10+.90*((1-height) if variant == 3 else height)**.80
        p[:, [0,2]] *= taper[:, None]
        p[:, 0] += .36*np.sin(height*3.2+variant)*(height if variant == 3 else 1-height)
    elif variant == 5:
        p[:, [0,2]] *= (.8+.2*np.cos(directions[:, 1]*4))[:, None]
    # Centimeter-scale geometric relief sits on large fractures and strata.
    # Every octave moves vertices; there is no normal-map substitute for it.
    radius = np.zeros(len(p))
    for octave, (frequency, amplitude) in enumerate(((.28,.65),(.72,.28),(1.8,.13),(5.2,.055),(16,.018),(43,.006))):
        n = noise(p*frequency+(variant*13.17, 0, 0), seed+octave*7919)
        radius += amplitude*(n if octave > 1 else (1-2*np.abs(n)))
    radius += .12*np.sin(p[:, 1]*3.8+.45*noise(p*.6, seed+107))
    normal = np.sign(directions)*np.abs(directions)**(power-1)/np.array(dimensions[variant])
    normal /= np.linalg.norm(normal, axis=1)[:, None]
    p += normal*radius[:, None]
    return p.astype(np.float32)


def write_ply(path, points, faces, closed):
    a, b, c = (points[faces[:, i]].astype(np.float64) for i in range(3))
    face_normal = np.cross(b-a, c-a)
    areas = np.linalg.norm(face_normal, axis=1)*.5
    if not np.isfinite(points).all() or not np.isfinite(areas).all() or areas.min() <= 1e-10:
        raise ValueError(f'Invalid or degenerate source geometry: {path.name}')
    normals = np.zeros(points.shape, dtype=np.float64)
    for k in range(3):
        np.add.at(normals, faces[:, k], face_normal)
    normals /= np.linalg.norm(normals, axis=1)[:, None]
    uv = np.column_stack((points[:, 0]*.17+points[:, 2]*.11, points[:, 1]*.16+points[:, 2]*.07))
    vertices = np.column_stack((points, normals, uv)).astype('<f4')
    edges = np.concatenate((faces[:, [0,1]], faces[:, [1,2]], faces[:, [2,0]]))
    edges.sort(axis=1)
    unique_edges, counts = np.unique(edges, axis=0, return_counts=True)
    if np.any(counts > 2) or (closed and (np.any(counts != 2) or len(points)-len(unique_edges)+len(faces) != 2)):
        raise ValueError(f'Non-manifold geometry: {path.name}')
    header = ('ply\nformat binary_little_endian 1.0\ncomment SceneRenderer procedural grotto v1\n'
              f'element vertex {len(vertices)}\n'
              'property float x\nproperty float y\nproperty float z\n'
              'property float nx\nproperty float ny\nproperty float nz\nproperty float s\nproperty float t\n'
              f'element face {len(faces)}\nproperty list uchar uint vertex_indices\nend_header\n').encode()
    records = np.empty(len(faces), dtype=[('count','u1'), ('vertices','<u4',(3,))])
    records['count'] = 3
    records['vertices'] = faces
    with path.open('wb') as stream:
        stream.write(header)
        vertices.tofile(stream)
        records.tofile(stream)
    return {'model': str(path.relative_to(ROOT)), 'vertices': len(points), 'triangles': len(faces),
            'bytes': path.stat().st_size, 'sha256': hashlib.sha256(path.read_bytes()).hexdigest(),
            'closed_manifold': closed, 'boundary_edges': int(np.count_nonzero(counts == 1)),
            'min_triangle_area_m2': float(areas.min()), 'median_triangle_area_m2': float(np.median(areas)),
            'bounds_min': points.min(axis=0).tolist(), 'bounds_max': points.max(axis=0).tolist()}


def floor_mesh():
    nx, nz = 256, 512
    x, z = np.meshgrid(np.linspace(-22,22,nx+1), np.linspace(-69,27,nz+1))
    p = np.stack((x, np.zeros_like(x), z), axis=-1)
    # Meandering lower channel with true pebbly relief; all camera routes stay above it.
    p[..., 1] = -.6+.18*np.sin(z*.12)+.20*np.sin(x*.38+z*.08)+.18*noise(p*.9, 32019)+.025*noise(p*8, 193)
    points = p.reshape(-1,3).astype(np.float32)
    i = np.arange((nz+1)*(nx+1), dtype=np.uint32).reshape(nz+1,nx+1)[:-1,:-1].ravel()
    # +Y winding in the XZ plane.
    faces = np.concatenate((np.stack((i,i+nx+1,i+1), axis=1), np.stack((i+1,i+nx+1,i+nx+2), axis=1)))
    return points, faces


def texture(folder, size):
    u, v = np.meshgrid(np.arange(size)/size, np.arange(size)/size)
    p = np.stack((u*9,v*9,np.zeros_like(u)), axis=-1)
    # Integer-frequency sinusoids are periodic across the projected-UV tile.
    broad = .5*np.sin(2*np.pi*(u*3+v*2))+.3*np.sin(2*np.pi*(u*7-v*5))
    grain = np.sin(2*np.pi*(u*113+v*67))*np.sin(2*np.pi*(u*89-v*97))
    strata = np.sin(2*np.pi*(v*12+.14*np.sin(2*np.pi*u*3)))
    brightness = .86+.075*broad+.025*grain+.055*strata
    color = np.array((.68,.61,.48))*brightness[..., None]
    Image.fromarray(np.clip(color*255,0,255).astype('uint8')).save(folder/'limestone-albedo.png')
    roughness = np.clip(.86+.035*strata+.025*grain, 0, 1)
    Image.fromarray((roughness*255).astype('uint8')).save(folder/'limestone-roughness.png')


def layout():
    instances = []
    def add(mesh, name, position, scale=(1,1,1), rotation=(0,0,0)):
        instances.append({'mesh':mesh, 'name':name, 'position':position, 'scale':scale, 'rotation':rotation})
    # Staggered wall blocks: no regular grid or repeated identical silhouette.
    for row, z in enumerate((7,-4,-15,-26,-37,-48,-59)):
        for side in (-1,1):
            x = side*(10.3+1.0*np.sin(row*1.9))
            add(row%2, 'fractured lower wall', (x,3.4,z), (1.0,.94,1.45), (0,side*(17+row*23),side*6))
            add((row+1)%2, 'fractured upper wall', (x+side*.8,10.0,z-1.2), (1.10,.84,1.50), (0,side*(49+row*19),-side*11))
    # Four stone vaults leave daylight gaps between chambers.
    for row, z in enumerate((1,-17,-35,-53)):
        for side in (-1,1):
            add(7, 'stone arch shoulder', (side*5.6,12.1,z), (1.02,1.25,1.2), (0,row*11,side*25))
        add(2, 'stone arch crown', (0,14.0,z+.4), (1.15,.63,1.35), (0,17+row*13,4*np.sin(row)))
    for i, (x,z) in enumerate(((-5.1,-4),(5.4,-19),(-4.7,-34),(5.1,-47))):
        add(5, 'fluted mineral pillar', (x,5.0,z), (.82,.96,.88), (0,i*61,(-1)**i*5))
    for i, (x,z) in enumerate(((-4.4,2),(5.2,-8),(-6.4,-20),(4.2,-30),(-5.9,-43),(6.5,-55))):
        add(3, 'stalagmite', (x,3.1,z), (.76,.70,.82), (0,i*83,(-1)**i*7))
    for i, (x,z) in enumerate(((-4.8,0),(3.7,-2),(-2.8,-17),(4.8,-18),(-4,-34),(2.2,-36),(-4.3,-52),(4.2,-54))):
        add(4, 'hanging stalactite', (x,10.4,z), (.60,.77,.65), (0,i*47,(-1)**i*6))
    for i in range(17):
        z = 13-i*4.2
        x = (-1 if i%2 else 1)*(3.2+(i*1.79)%4.8)
        size = .22+(i*.173)%.37
        add(6, 'fallen irregular rock', (x,.1+size*1.5,z), (size,size*.8,size*1.14), (i*19,i*67,i*31))
    return instances


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--level', type=int, choices=(5,6,7), default=7,
                        help='Icosphere subdivisions; default 327,680 triangles per unique rock')
    parser.add_argument('--texture-size', type=int, choices=(512,1024,2048), default=1024)
    parser.add_argument('--output', type=Path, help='Separate output folder; defaults to dense or small package by level')
    parser.add_argument('--memory-budget-mib', type=int, default=0, help='0 = automatic renderer-compatible process budget')
    args = parser.parse_args()
    global OUT
    OUT = args.output.resolve() if args.output else OUT if args.level == 7 else OUT.with_name('procedural-cave-small')
    OUT.relative_to(ROOT)  # Manifest paths must remain inside this checkout.
    scratch_estimate = max(96*1024**2+max(20*4**args.level,262144)*384,
                           args.texture_size**2*160)
    memory_budget = memory_guard(args.memory_budget_mib,scratch_estimate)
    OUT.mkdir(parents=True, exist_ok=True)
    existing = sum(p.stat().st_size for p in OUT.rglob('*') if p.is_file())
    # Conservative upper bound includes a temporary replacement alongside old files.
    estimate = 8*(10*4**args.level+2)*32+8*(20*4**args.level)*13+32_000_000
    if existing+estimate >= LIMIT:
        raise ValueError('Cave package would exceed the 10 GB limit; no files changed')
    directions, faces = icosphere(args.level)
    meshes = []
    for variant in range(8):
        memory_guard(args.memory_budget_mib,scratch_estimate)
        points = rock(directions, variant, 47291+variant*827)
        meshes.append(write_ply(OUT/f'rock-{variant}.ply', points, faces, True))
        print(f'Rock {variant}: {len(faces):,} triangles / {meshes[-1]["bytes"]:,} bytes', flush=True)
    memory_guard(args.memory_budget_mib,scratch_estimate)
    points, ground_faces = floor_mesh()
    meshes.append(write_ply(OUT/'ground.ply', points, ground_faces, False))
    memory_guard(args.memory_budget_mib,scratch_estimate)
    texture(OUT,args.texture_size)
    instances = layout()
    instances.append({'mesh':8,'name':'continuous grotto floor','position':[0,0,0],'scale':[1,1,1],'rotation':[0,0,0]})
    unique_triangles = sum(m['triangles'] for m in meshes)
    logical_triangles = sum(meshes[i['mesh']]['triangles'] for i in instances)
    manifest = {'version':VERSION,'scene':'cluster-cave' if args.level == 7 else 'cluster-cave-small','generator':'tools/prepare_cluster_cave.py',
                'geometry_origin':'Fully procedural; no downloaded scan geometry or pretrained assets',
                'level':args.level,'texture_size':args.texture_size,'limit_bytes':LIMIT,
                'generation_memory_budget_bytes':memory_budget,
                'meshes':meshes,'instances':instances,'unique_triangles':unique_triangles,
                'logical_triangles':logical_triangles,'textures':str(OUT.relative_to(ROOT)),
                'camera':{'position':[0,3.2,18],'target':[1,5,-24],'fov':62,'exposure':1.1},
                'views':[
                    {'label':'0 px / entrance','eye':[0,3.2,18],'target':[1,5,-24],'pixel_error':0},
                    {'label':'4 px / same entrance','eye':[0,3.2,18],'target':[1,5,-24],'pixel_error':4},
                    {'label':'Near / wall relief','eye':[-2.5,3.2,13],'target':[-7,4,4],'pixel_error':4},
                    {'label':'Middle / first chamber','eye':[0,3.2,18],'target':[1,5,-24],'pixel_error':4},
                    {'label':'Far / entrance retreat','eye':[0,5,38],'target':[0,6,-24],'pixel_error':4},
                    {'label':'Farther / stone vaults','eye':[0,8,65],'target':[0,6,-24],'pixel_error':4},
                    {'label':'Overview / four chambers','eye':[-35,29,39],'target':[0,6,-24],'pixel_error':4},
                    {'label':'2 px / same entrance','eye':[0,3.2,18],'target':[1,5,-24],'pixel_error':2},
                    {'label':'8 px / same entrance','eye':[0,3.2,18],'target':[1,5,-24],'pixel_error':8},
                    {'label':'Inside / second chamber','eye':[0,3.2,-9],'target':[1,6,-34],'pixel_error':4},
                    {'label':'Inside / third chamber','eye':[0,3.2,-29],'target':[-1,6,-57],'pixel_error':4}]}
    output = OUT/'scene.json'
    output.write_text(json.dumps(manifest,indent=2)+'\n')
    total = sum(p.stat().st_size for p in OUT.rglob('*') if p.is_file())
    if total >= LIMIT:
        raise ValueError('Cave package exceeds 10 GB')
    for _ in range(3):
        total = sum(p.stat().st_size for p in OUT.rglob('*') if p.is_file())
        if manifest.get('package_bytes') == total:
            break
        manifest['package_bytes'] = total
        output.write_text(json.dumps(manifest,indent=2)+'\n')
    total = sum(p.stat().st_size for p in OUT.rglob('*') if p.is_file())
    print(json.dumps({'unique_triangles':unique_triangles,'logical_triangles':logical_triangles,
                      'instances':len(instances),'package_bytes':total,'limit_bytes':LIMIT},indent=2))


if __name__ == '__main__':
    main()
