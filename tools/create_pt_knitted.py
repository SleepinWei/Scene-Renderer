#!/usr/bin/env python3
"""Import YarnSim relaxed B-splines and build static sunset knit PT packages.

Dataset remains local: https://graphics.stanford.edu/projects/yarnsim/
No garment simulation or automatic arbitrary garment retopology is performed.
"""
import argparse
import hashlib
import itertools
import json
import math
from pathlib import Path
import random
import struct
import zipfile

from pt_groom import Package, add, sub, mul, dot, length, frame, cross, unit, tubes, ribbons

MEMBER = 'dataset/knitpatterns/datafiles/slip_stitch_honeycomb_1.225.txt'
RADIUS = 1.225
PERIOD = (15., 20.)  # Opposite boundary control-point translations in this pattern.


def read_yarns(text):
    lines = iter(line.strip() for line in text.splitlines() if line.strip())
    header = next(lines, '').split(':')
    if len(header) != 2 or header[0] != 'Num Yarns':
        raise ValueError('Expected YarnSim Num Yarns header')
    count = int(header[1])
    if not 1 <= count <= 4096:
        raise ValueError('Invalid yarn count')
    curves = []
    for _ in range(count):
        n = int(next(lines, '0'))
        if not 4 <= n <= 100000:
            raise ValueError('Invalid cubic B-spline control count')
        points = [list(map(float, next(lines, '').split())) for _ in range(n)]
        if any(len(p) != 3 or not all(math.isfinite(x) for x in p) for p in points):
            raise ValueError('Invalid yarn controls')
        curves.append(points)
    if next(lines, None) is not None:
        raise ValueError('Trailing yarn data')
    return curves


def bspline(controls, steps=6):
    """Uniform, non-clamped cubic B-spline (dataset includes boundary controls)."""
    if steps < 1 or len(controls) < 4:
        raise ValueError('Cubic B-spline needs four controls and positive steps')
    result = []
    for i in range(len(controls)-3):
        for j in range(steps + (i == len(controls)-4)):
            t = j/steps
            weights = [(1-t)**3/6, (3*t**3-6*t*t+4)/6,
                       (-3*t**3+3*t*t+3*t+1)/6, t**3/6]
            result.append([sum(weights[k]*controls[i+k][a] for k in range(4)) for a in range(3)])
    return result


def clip_segment(a, b, period=PERIOD, bounds=None):
    """Clip to a centered periodic cell, preserving interpolated depth."""
    lo, hi = 0., 1.
    bounds = bounds or [(-extent/2,extent/2) for extent in period]
    for axis, (minimum,maximum) in enumerate(bounds):
        delta = b[axis]-a[axis]
        if abs(delta) < 1e-12:
            if a[axis] < minimum or a[axis] > maximum:
                return None
        else:
            t0, t1 = (minimum-a[axis])/delta, (maximum-a[axis])/delta
            lo, hi = max(lo, min(t0, t1)), min(hi, max(t0, t1))
    if hi-lo < 1e-8:
        return None
    return add(a, mul(sub(b, a), lo)), add(a, mul(sub(b, a), hi))


def plies(points, radius, count=3, pitch=None):
    pitch = pitch or radius*12
    previous, arc = None, 0.
    result = [[] for _ in range(count)]
    for i, p in enumerate(points):
        axis = unit(sub(points[min(i+1, len(points)-1)], points[max(i-1, 0)]))
        t = frame(axis)[0] if previous is None else sub(previous, mul(axis, dot(previous, axis)))
        t = unit(t) if length(t) > 1e-8 else frame(axis)[0]
        previous = t; b = cross(axis, t)
        if i: arc += length(sub(p, points[i-1]))
        for k in range(count):
            angle = 2*math.pi*(arc/pitch+k/count)
            result[k].append(add(p, add(mul(t, radius*.48*math.cos(angle)), mul(b, radius*.48*math.sin(angle)))))
    return result


def join_yarns(curves, tolerance=.0025, max_overlap=1):
    """Weld periodic boundary ends before twisting plies (pattern units).

    Independent cell fragments would reset twist phase at every boundary.
    Only endpoints within the rounding precision of the dataset are joined.
    """
    import collections
    buckets = collections.defaultdict(list)
    def key(p): return tuple(math.floor(x/tolerance) for x in p)
    for i,c in enumerate(curves):
        for overlap in range(1,min(max_overlap,len(c)-1)+1):
            buckets[key(c[overlap-1])].append((i,0,overlap))
            buckets[key(c[-overlap])].append((i,-1,overlap))
    unused, result = set(range(len(curves))), []
    while unused:
        index = min(unused); unused.remove(index); chain = list(curves[index])
        for _ in range(2):
            while True:
                p = chain[-1]; candidates = []; at = key(p)
                for delta in itertools.product((-1,0,1),repeat=3):
                    for i,end,overlap in buckets.get(tuple(a+b for a,b in zip(at,delta)),[]):
                        if i not in unused or len(chain)<overlap: continue
                        extension = curves[i] if end==0 else list(reversed(curves[i]))
                        distances = [length(sub(a,b)) for a,b in zip(chain[-overlap:],extension[:overlap])]
                        if max(distances)<=tolerance:
                            candidates.append((-overlap,max(distances),i,end))
                if not candidates: break
                minus_overlap,_,i,end = min(candidates); unused.remove(i)
                extension = curves[i] if end==0 else list(reversed(curves[i]))
                chain.extend(extension[-minus_overlap:])
            chain.reverse()
        result.append(chain)
    return result


def tiled_yarns(controls, columns, rows, steps=8):
    """Connect real/virtual controls before B-spline evaluation, then crop.

    Evaluating each exported fragment separately discards spline spans at its
    endpoints. Endpoint and two-control overlaps encode the periodic links.
    One neighboring cell provides the cubic support at the cropped boundary.
    """
    unique = {}
    for c in controls:
        k = tuple(tuple(p) for p in c); unique[min(k,k[::-1])] = c
    copies = [[[x+tx*15,y+ty*20,z] for x,y,z in c]
              for tx,ty in itertools.product(range(-1,columns+1),range(-1,rows+1)) for c in unique.values()]
    joined = join_yarns(copies,max_overlap=2)
    bounds = [(-7.5,(columns-.5)*15),(-10,(rows-.5)*20)]
    seen, result = set(), []
    for controls in joined:
        sampled = bspline(controls,steps); strip = []
        for a,b in zip(sampled,sampled[1:]):
            pair = clip_segment(a,b,bounds=bounds)
            if pair:
                a,b = pair; key = tuple(sorted(tuple(round(x,4) for x in p) for p in pair))
                if key not in seen:
                    seen.add(key)
                    if strip and length(sub(strip[-1],a))>1e-4:
                        result.append(strip); strip=[]
                    if not strip: strip.append(a)
                    strip.append(b); continue
            if len(strip)>1: result.append(strip)
            strip=[]
        if len(strip)>1: result.append(strip)
    connected = join_yarns(result)
    for c in connected:
        if length(sub(c[0],c[-1]))<=.005: continue
        for p in (c[0],c[-1]):
            if not any(abs(p[a]-edge)<=.005 for a,axis in enumerate(bounds) for edge in axis):
                raise ValueError('Disconnected yarn endpoint inside the periodic knit')
    return connected


def nap(curves, radius, count, seed=47):
    # Area along constant-radius yarn is proportional to arc length.
    import bisect
    segments, cdf, total = [], [], 0.
    for curve in curves:
        for a, b in zip(curve, curve[1:]):
            total += length(sub(b, a)); cdf.append(total); segments.append((a, b))
    rng, strands = random.Random(seed), []
    for _ in range(count):
        a, b = segments[min(bisect.bisect_right(cdf, rng.random()*total), len(segments)-1)]
        axis = unit(sub(b, a)); t, bt = frame(axis); angle = rng.random()*2*math.pi
        radial = add(mul(t, math.cos(angle)), mul(bt, math.sin(angle)))
        p = add(add(a, mul(sub(b, a), rng.random())), mul(radial, radius*1.01))
        extent = radius*(.15+.35*rng.random())
        strands.append(dict(points=[p, add(p, add(mul(radial, extent*.5), mul(axis, extent*.3))),
                                   add(p, add(mul(radial, extent), mul(axis, extent*.8)))], radius=radius*.018))
    return strands


def yarn_material(color=(.78, .67, .52)):
    return dict(bsdf_model='yarn', base_color=list(color), roughness=.65, sheen_weight=.18,
                sheen_roughness=.8, sheen_color=[.95, .88, .75], cloth_transmission=.03,
                yarn_specular_weight=.25, two_sided=True)


def relight(path, lighting):
    scene = json.loads(path.read_text()); source = json.loads((lighting/'scene.json').read_text())
    for key in ['environment', 'sun', 'exposure']: scene[key] = source[key]
    scene['environment']['file'] = 'environment.bin'
    (path.parent/'environment.bin').write_bytes((lighting/source['environment']['file']).read_bytes())
    scene['source']['lighting'] = source['source'].get('solar_relight', {})
    path.write_text(json.dumps(scene, indent=2)+'\n')


def swatch(root, controls, source, lighting):
    p = Package(root, [.95, 1.2, 2.9], [0, 1.05, 0], 30)
    material = p.material(**yarn_material()); scale = .018
    joined = tiled_yarns(controls,5,3)
    centers = [[[(x-30)*scale,1.05+(y-20)*scale,z*scale] for x,y,z in c] for c in joined]
    # A real three-dimensional knit swatch: no opaque backing plane.
    ply_radius = RADIUS*scale*.52
    p.store((triangle for points in centers for ply in plies(points, RADIUS*scale)
             for triangle in tubes(ply, ply_radius, 16)), material)
    curves = nap(centers, RADIUS*scale, 16000)
    fuzz = p.material(bsdf_model='hair', base_color=[1,1,1], absorption=[.16,.28,.48],
                      hair_beta_m=.4, hair_beta_n=.45, two_sided=True)
    p.store(ribbons(curves, p.eye), fuzz)
    floor = p.material(base_color=[.32,.3,.25], roughness=.85)
    p.quad([[-100,0,100],[100,0,100],[100,0,-100],[-100,0,-100]], [0,1,0], floor)
    path = p.save(dict(**source, name='YarnSim honeycomb knitted cloth at sunset',
                       cells=15, connected_yarns=len(centers), geometric_plies=3, fuzz_strands=len(curves)), curves)
    relight(path, lighting); return path


def sweater(root, controls, source, lighting):
    # Retain the prototype silhouette/cuffs but replace its stylized front loops
    # and nap with relaxed dataset yarns mapped over the front torso.
    old = json.loads((lighting/'scene.json').read_text()); root.mkdir(parents=True, exist_ok=True)
    yarn_slots = [i for i,m in enumerate(old['materials']) if m.get('bsdf_model')=='cloth' and m.get('knit_weight')==0]
    nap_slots = [i for i,m in enumerate(old['materials']) if m.get('bsdf_model')=='hair']
    if len(yarn_slots)!=1 or len(nap_slots)!=1 or old['source'].get('yarn_loops')!=1280:
        raise ValueError('Sweater mapping requires the refined procedural sweater source')
    replaced = set(yarn_slots+nap_slots)
    p = Package(root, old['camera']['world'][12:15], [0,1,0])
    p.materials = old['materials']
    for m in p.materials:
        if m.get('bsdf_model') in ('cloth', 'knit'):
            m['yarn_specular_weight'] = .25; m['roughness'] = .65
    data, tangent = (lighting/old['geometry']).read_bytes(), (lighting/old['tangents']).read_bytes()
    for d in old['draws']:
        if d['material'] in replaced: continue
        n = d['vertices']; draw = dict(d, offset=len(p.geometry), tangents_offset=len(p.tangents))
        p.geometry.extend(data[d['offset']:d['offset']+32*n])
        p.tangents.extend(tangent[d['tangents_offset']:d['tangents_offset']+16*n]); p.draws.append(draw)
    def torso(u, v):
        angle = 2*math.pi*u; width = .63+.1*math.sin(math.pi*v)
        if v>.72: width = width*(1-(v-.72)/.28)+.245*(v-.72)/.28
        depth = .3+.025*math.sin(math.pi*v)
        if v>.82: depth = depth*(1-(v-.82)/.18)+.19*(v-.82)/.18
        pleat = .012*math.sin(angle*6+v*3)*math.sin(math.pi*v)
        return [(width+pleat)*math.sin(angle), .38+1.7*v, (depth+pleat)*math.cos(angle)]
    joined = tiled_yarns(controls,20,14)
    def map_point(x, y, z):
        u, v = ((x+7.5)/15-10)/(20/.4375), .06+((y+10)/20)/(14/.7142857)
        n = unit(cross(sub(torso(u+.0001,v),torso(u-.0001,v)), sub(torso(u,v+.0001),torso(u,v-.0001))))
        return add(torso(u,v), mul(n, .0279+z*.004286))
    centers = [[map_point(x,y,z) for x,y,z in c] for c in joined]
    yarn = p.material(**yarn_material()); radius = .00525
    p.store((tri for points in centers for tri in tubes(points, radius, 10)), yarn)
    body_model = old['draws'][0]['model']; p.draws[-1]['model'] = body_model
    curves = nap(centers, radius, 32000)
    local_eye = sub(p.eye, body_model[12:15])
    p.store(ribbons(curves, local_eye), nap_slots[0]); p.draws[-1]['model'] = body_model
    path = p.save(dict(**source, name='Prototype sweater with YarnSim honeycomb front',
                       cells=280, connected_yarns=len(centers), geometric_plies=1, fuzz_strands=len(curves),
                       limitation='Relaxed planar yarns mapped to front torso; garment silhouette, sleeves and seams are procedural, not mechanically simulated.'), curves)
    scene = json.loads(path.read_text()); scene['camera'] = old['camera']; path.write_text(json.dumps(scene,indent=2)+'\n')
    relight(path, lighting); return path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--dataset', type=Path, default=Path('samples/assets/pt/knit-patterns/yarnsim-dataset.zip'))
    parser.add_argument('--lighting', type=Path, default=Path('build/pt-grooms-sunset/sweater'))
    parser.add_argument('--output', type=Path, default=Path('build/pt-knit'))
    args = parser.parse_args()
    with zipfile.ZipFile(args.dataset) as archive: raw = archive.read(MEMBER)
    controls = read_yarns(raw.decode())
    source = dict(dataset='Interactive Design of Periodic Yarn-Level Cloth Patterns (2018)',
                  url='https://graphics.stanford.edu/projects/yarnsim/', member=MEMBER,
                  data_sha256=hashlib.sha256(raw).hexdigest(), control_yarns=len(controls),
                  interpolation='periodic control-chain joins followed by uniform cubic B-spline, 8 subdivisions', cell_period=list(PERIOD),
                  closure='Oren-Nayar, bounded sheen, anisotropic dielectric yarn reflection, thin diffuse transmission; hair nap',
                  dataset_redistributed=False)
    for name, create in [('swatch',swatch), ('sweater',sweater)]:
        path = create(args.output/name, controls, source, args.lighting)
        scene = json.loads(path.read_text()); print(path, sum(d['vertices']//3 for d in scene['draws']), 'triangles', flush=True)


if __name__ == '__main__': main()
