#!/usr/bin/env python3
"""Import Yuksel HAIR benchmarks without thinning, tapering or curve resampling."""
import argparse
import hashlib
import json
import math
from pathlib import Path

from pt_groom import Package, add, sub, mul, dot, length, unit, cross, frame
from pt_hair_file import read_hair

ROOT = Path(__file__).resolve().parents[1]
SIGMA = [.2, .45, .8]
FIBER = dict(hair_beta_m=.3, hair_beta_n=.3, hair_ior=1.55, hair_tilt=math.radians(2))


def read_obj(path):
    vertices, faces = [], []
    for line in Path(path).read_text().splitlines():
        fields = line.split()
        if not fields or fields[0].startswith('#'):
            continue
        if fields[0] == 'v':
            p = list(map(float, fields[1:4]))
            if len(p) != 3 or not all(math.isfinite(v) for v in p):
                raise ValueError('Invalid OBJ position')
            vertices.append(p)
        elif fields[0] == 'f':
            indices = [int(v.split('/')[0]) for v in fields[1:]]
            indices = [v-1 if v > 0 else len(vertices)+v for v in indices]
            if len(indices) < 3 or min(indices) < 0 or max(indices) >= len(vertices):
                raise ValueError('Invalid OBJ face')
            faces.extend([indices[0], indices[i], indices[i+1]] for i in range(1, len(indices)-1))
    if not vertices or not faces:
        raise ValueError('Empty OBJ geometry')
    return vertices, faces


def source_transform(vertices):
    # Author's model is Z-up, face toward +X. Rotate by an orientation-preserving
    # axis permutation to our Y-up / +Z-front, then apply uniform scale/translation.
    low = [min(p[k] for p in vertices) for k in range(3)]
    high = [max(p[k] for p in vertices) for k in range(3)]
    scale = 2.6/(high[2]-low[2])
    center = [(a+b)*.5 for a,b in zip(low, high)]
    def transform(p):
        return [(p[1]-center[1])*scale, (p[2]-low[2])*scale+.035, (p[0]-center[0])*scale]
    return transform, scale, dict(axis_permutation=[1, 2, 0], scale=scale,
                                  original_center=center, original_low=low, floor_offset=.035)


def strands(hair, transform, scale):
    for i in range(hair.strands):
        start, end = hair.offsets[i:i+2]
        points = [transform(p) for p in hair.strand(i)]
        radii = [.5*scale*(hair.thickness[j] if hair.thickness else hair.default_thickness) for j in range(start, end)]
        yield points, radii


def imported_ribbons(curves, eye):
    """Same fixed-eye representation as local grooms, but retain source widths."""
    for points, radii in curves:
        if len(points)<2 or len(points)!=len(radii) or any(not math.isfinite(r) or r<0 for r in radii):
            raise ValueError('Invalid imported curve widths')
        arc = [0]
        for a,b in zip(points, points[1:]):
            distance = length(sub(b,a))
            if distance < 1e-12:
                raise ValueError('Coincident points in imported strand')
            arc.append(arc[-1]+distance)
        pairs, previous = [], None
        for i,p in enumerate(points):
            direction = sub(points[min(i+1,len(points)-1)], points[max(0,i-1)])
            if length(direction)<1e-12:
                direction = sub(points[min(i+1,len(points)-1)],points[i])
            axis = unit(direction)
            width = cross(sub(eye,p), axis)
            width = unit(width) if length(width) > 1e-9 else frame(axis)[0]
            if previous is not None and dot(width,previous) < 0:
                width = mul(width,-1)
            previous = width
            normal = unit(cross(axis,width))
            pairs.append([(add(p,mul(width,sign*radii[i])), normal, [arc[i]/arc[-1],v], [*axis,1])
                          for sign,v in [(-1,0), (1,1)]])
        for a,b in zip(pairs, pairs[1:]):
            yield a[0], b[0], b[1]
            yield a[0], b[1], a[1]


def export_pbrt(package, curves, size=256, samples=1024, depth=12):
    """Native ray-facing flat curves; independent appearance reference, not mesh parity."""
    root = package.root
    # Head, floor and lamps use the exact non-hair triangle positions from the PT package.
    import struct
    for number, draw in enumerate(package.draws):
        material = package.materials[draw['material']]
        if material.get('bsdf_model') == 'hair':
            continue
        count = draw['vertices']
        with (root/f'mesh-{number}.ply').open('wb') as file:
            file.write((f'ply\nformat binary_little_endian 1.0\nelement vertex {count}\nproperty float x\nproperty float y\nproperty float z\nproperty float nx\nproperty float ny\nproperty float nz\nproperty float u\nproperty float v\nelement face {count//3}\nproperty list uchar int vertex_indices\nend_header\n').encode())
            file.write(package.geometry[draw['offset']:draw['offset']+count*32])
            for i in range(0,count,3):
                file.write(struct.pack('<B3i',3,i,i+1,i+2))
    def numbers(values): return ' '.join(format(v,'.9g') for v in values)
    with (root/'curves.pbrt').open('w') as file:
        for points, radii in curves:
            for i,(a,b) in enumerate(zip(points,points[1:])):
                # Collinear cubic exactly represents the original line segment.
                controls = [a, add(mul(a,2/3),mul(b,1/3)), add(mul(a,1/3),mul(b,2/3)), b]
                file.write('Shape "curve" "string type" ["flat"] "float width0" ['+numbers([2*radii[i]])+'] "float width1" ['+numbers([2*radii[i+1]])+'] "point P" ['+numbers(v for p in controls for v in p)+']\n')
    with (root/'reference.pbrt').open('w') as file:
        # PBRT camera basis is mirrored relative to GL's camera x for this LookAt.
        file.write('Scale -1 1 1\nLookAt '+numbers([*package.eye,*package.target,0,1,0])+'\n')
        file.write('Camera "perspective" "float fov" ['+numbers([package.fov])+']\n')
        file.write(f'Sampler "sobol" "integer pixelsamples" [{samples}]\nIntegrator "path" "integer maxdepth" [{depth}]\nPixelFilter "box" "float xwidth" [.5] "float ywidth" [.5]\nFilm "image" "integer xresolution" [{size}] "integer yresolution" [{size}] "string filename" ["reference.pfm"]\nWorldBegin\nLightSource "infinite" "rgb L" [.035 .04 .05]\n')
        for number, draw in enumerate(package.draws):
            material = package.materials[draw['material']]
            if material.get('bsdf_model') == 'hair':
                continue
            # SceneRenderer package base_color is decoded by pow(c,2.2) at hit.
            file.write('AttributeBegin\nMaterial "matte" "rgb Kd" ['+numbers(v**2.2 for v in material['base_color'])+']\n')
            if 'emission' in material:
                side = 'true' if material.get('two_sided',False) else 'false'
                file.write('AreaLightSource "diffuse" "rgb L" ['+numbers(material['emission'])+'] "bool twosided" ["'+side+'"]\n')
            file.write(f'Shape "plymesh" "string filename" ["mesh-{number}.ply"]\nAttributeEnd\n')
        file.write('AttributeBegin\nMaterial "hair" "rgb sigma_a" ['+numbers(SIGMA)+'] "float beta_m" [.3] "float beta_n" [.3] "float eta" [1.55] "float alpha" [2]\nInclude "curves.pbrt"\nAttributeEnd\nWorldEnd\n')


def create(asset, output, directory=ROOT/'samples/assets/pt/hair-benchmarks', reference=False, size=256, samples=1024):
    name = 'wStraight.hair' if asset == 'straight' else 'wWavy.hair'
    path = directory/name
    hair = read_hair(path)
    manifest = json.loads((ROOT/'samples/hair-benchmarks.json').read_text())
    expected = manifest['assets'][asset]
    for file, digest in [(name,expected['member_sha256'][name]),('woman.obj',manifest['assets']['head']['member_sha256']['woman.obj'])]:
        if hashlib.sha256((directory/file).read_bytes()).hexdigest()!=digest:
            raise ValueError('Benchmark member differs from pinned source: '+file)
    if hair.strands != expected['strands'] or len(hair.points)//3 != expected['points']:
        raise ValueError('Official hair counts differ from pinned benchmark')
    vertices, faces = read_obj(directory/'woman.obj')
    transform, scale, normalization = source_transform(vertices)
    package = Package(output,[2.4,2.0,6.6],[0,1.42,0],29)
    head = package.material(bsdf_model='lambert',base_color=[.62,.5,.41])
    material = package.material(bsdf_model='hair',base_color=[1,1,1],absorption=SIGMA,
                                two_sided=True,**FIBER)
    package.mesh([transform(p) for p in vertices], faces, head)
    package.store(imported_ribbons(strands(hair,transform,scale),package.eye),material)
    package.studio()
    for m in package.materials:
        if m.get('bsdf_model') != 'hair':
            m['bsdf_model'] = 'lambert'
    source = dict(name=f'Yuksel {asset} benchmark',credit=manifest['credit'],source_url=manifest['source_page'],
                  original_hair=str(path),original_hair_sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
                  head_sha256=hashlib.sha256((directory/'woman.obj').read_bytes()).hexdigest(),
                  strands=hair.strands,points=len(hair.points)//3,normalization=normalization,
                  hair_triangles=2*(len(hair.points)//3-hair.strands),
                  attributes=dict(default_thickness=hair.default_thickness,default_transparency=hair.default_transparency,
                                  default_color=hair.default_color,flags=hair.flags,width='source thickness interpreted as diameter; uniform scale only; no synthetic taper',
                                  optical_material='explicit sigma_a [0.2,0.45,0.8], eta 1.55, beta_m/beta_n 0.3, tilt 2 degrees',
                                  unused='Source transparency/color retained in original HAIR, not mapped to opacity or reflectance. Head textures not applied; neutral diffuse head.'),
                  representation='fixed reference-camera ribbons; source HAIR remains authoritative control data')
    scene = package.save(source)
    (package.root/'import.json').write_text(json.dumps(source,indent=2)+'\n')
    if reference:
        export_pbrt(package,strands(hair,transform,scale),size,samples)
    return scene


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--scene',choices=['all','straight','wavy'],default='all')
    parser.add_argument('--output',type=Path,default=Path('build/pt-hair-benchmarks'))
    parser.add_argument('--pbrt',action='store_true',help='Also export native flat-curve PBRT v3 reference')
    parser.add_argument('--reference-size',type=int,default=256)
    parser.add_argument('--reference-samples',type=int,default=1024)
    args = parser.parse_args()
    if not 16 <= args.reference_size <= 4096 or not 1 <= args.reference_samples <= 65536:
        parser.error('Reference size/samples outside budget')
    for name in ['straight','wavy'] if args.scene == 'all' else [args.scene]:
        print(create(name,args.output/name,reference=args.pbrt,size=args.reference_size,samples=args.reference_samples),flush=True)


if __name__ == '__main__':
    main()
