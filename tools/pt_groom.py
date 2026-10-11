"""Deterministic static groom geometry and frozen PT package helpers.

Curves are retained as control points. Rendering currently tessellates one
reference-camera ribbon per strand, not closed tubes or crossed alpha cards:
the hair BSDF already integrates transport through a fiber's cross section.
"""
import bisect
import json
import math
from pathlib import Path
import random
import struct

from create_pt_material_fixture import cross, matrix, unit


def add(a, b): return [x+y for x, y in zip(a, b)]
def sub(a, b): return [x-y for x, y in zip(a, b)]
def mul(a, t): return [x*t for x in a]
def dot(a, b): return sum(x*y for x, y in zip(a, b))
def length(a): return math.sqrt(dot(a, a))
def frame(axis):
    t = unit(cross(axis, [0,1,0] if abs(axis[1]) < .95 else [1,0,0]))
    return t, cross(axis, t)


def read_ascii_ply(path):
    """Triangulated Stanford-style ASCII PLY; rejects invalid indices/positions."""
    with Path(path).open() as f:
        if f.readline().strip() != 'ply' or f.readline().strip() != 'format ascii 1.0':
            raise ValueError('Expected ASCII PLY 1.0')
        nv = nf = 0
        for line in f:
            if line.startswith('element vertex '): nv = int(line.split()[2])
            if line.startswith('element face '): nf = int(line.split()[2])
            if line.strip() == 'end_header': break
        if not 0 < nv <= 2000000 or not 0 < nf <= 4000000:
            raise ValueError('Invalid PLY counts')
        vertices = [list(map(float, f.readline().split()[:3])) for _ in range(nv)]
        if any(len(p) != 3 or not all(math.isfinite(x) for x in p) for p in vertices):
            raise ValueError('Invalid PLY positions')
        faces = []
        for _ in range(nf):
            indices = list(map(int, f.readline().split()))
            if len(indices) != 4 or indices[0] != 3 or any(i < 0 or i >= nv for i in indices[1:]):
                raise ValueError('Expected valid triangular PLY faces')
            faces.append(indices[1:])
    return vertices, faces


def vertex_normals(vertices, faces):
    normals = [[0,0,0] for _ in vertices]
    for a, b, c in faces:
        n = cross(sub(vertices[b],vertices[a]), sub(vertices[c],vertices[a]))
        for index in (a,b,c): normals[index] = add(normals[index],n)
    return [unit(n) if length(n) > 1e-12 else [0,1,0] for n in normals]


def surface_roots(vertices, faces, count, seed=1, accept=None):
    """Area-weighted triangles and sqrt-barycentrics; no vertex-density bias."""
    if count < 1: raise ValueError('Positive strand count required')
    normals = vertex_normals(vertices, faces)
    cdf, total = [], 0
    for a,b,c in faces:
        total += .5*length(cross(sub(vertices[b],vertices[a]),sub(vertices[c],vertices[a])))
        cdf.append(total)
    if total <= 0: raise ValueError('Cannot grow hair on zero-area geometry')
    rng, roots = random.Random(seed), []
    for _ in range(max(count*100, 100)):
        at = min(bisect.bisect_right(cdf,rng.random()*total), len(faces)-1)
        indices = faces[at]; r = math.sqrt(rng.random()); v = rng.random()
        weights = [1-r,r*(1-v),r*v]
        p = [sum(vertices[index][k]*w for index,w in zip(indices,weights)) for k in range(3)]
        n = [sum(normals[index][k]*w for index,w in zip(indices,weights)) for k in range(3)]
        if length(n) < 1e-10: continue
        n = unit(n)
        if accept and not accept(p,n): continue
        roots.append(dict(position=p,normal=n,triangle=at,barycentric=weights))
        if len(roots) == count: return roots
    raise ValueError('Groom root mask accepted too little surface area')


def ribbons(curves, eye):
    """Continuous width frames, tapered radius, root-to-tip U, cross-section V."""
    for curve in curves:
        points, radius = curve['points'], curve['radius']; previous = None; pairs = []
        if len(points)<2 or not math.isfinite(radius) or radius<=0 or any(len(p)!=3 or not all(math.isfinite(x) for x in p) for p in points):
            raise ValueError('Invalid strand points/radius')
        arc = [0]
        for a,b in zip(points,points[1:]): arc.append(arc[-1]+length(sub(b,a)))
        if len(points) < 2 or arc[-1] < 1e-9: raise ValueError('Degenerate groom strand')
        for i,p in enumerate(points):
            direction=sub(points[min(i+1,len(points)-1)],points[max(0,i-1)])
            if length(direction)<1e-9:direction=sub(points[i],points[max(0,i-1)])
            if length(direction)<1e-9:raise ValueError('Coincident strand points')
            axis=unit(direction)
            width = cross(sub(eye,p),axis)
            width = unit(width) if length(width) > 1e-8 else frame(axis)[0]
            if previous is not None and dot(width,previous) < 0: width = mul(width,-1)
            previous = width; n = unit(cross(axis,width)); u = arc[i]/arc[-1]
            r = radius*max(.04, (1-u)**.7)
            pairs.append([(add(p,mul(width,sign*r)),n,[u,v],[*axis,1]) for sign,v in [(-1,0),(1,1)]])
        for a,b in zip(pairs,pairs[1:]):
            yield a[0],b[0],b[1]
            yield a[0],b[1],a[1]


def tubes(points, radius, sides=8):
    """Opaque yarn geometry with transported radial frames, distinct from hair."""
    rings = []; previous = None
    for i,p in enumerate(points):
        axis = unit(sub(points[min(i+1,len(points)-1)],points[max(0,i-1)]))
        t = frame(axis)[0] if previous is None else sub(previous,mul(axis,dot(previous,axis)))
        if length(t) < 1e-8: t = frame(axis)[0]
        t = unit(t); previous=t; b=cross(axis,t); ring=[]
        for j in range(sides):
            a=2*math.pi*j/sides;n=add(mul(t,math.cos(a)),mul(b,math.sin(a)))
            ring.append((add(p,mul(n,radius)),n,[i/(len(points)-1),j/sides],[*axis,1]))
        rings.append(ring)
    for a,b in zip(rings,rings[1:]):
        for j in range(sides):
            k=(j+1)%sides
            yield a[j],b[j],b[k]
            yield a[j],b[k],a[k]


def ellipsoid(center, radii, rows=32, columns=64):
    vertices, uvs, faces = [], [], []
    for i in range(rows+1):
        t=math.pi*i/rows
        for j in range(columns+1):
            p=2*math.pi*j/columns
            vertices.append(add(center,[radii[0]*math.sin(t)*math.cos(p),radii[1]*math.cos(t),radii[2]*math.sin(t)*math.sin(p)]))
            uvs.append([j/columns,i/rows])
    for i in range(rows):
        for j in range(columns):
            a=i*(columns+1)+j;b=a+columns+1
            if i: faces.append([a,b+1,b])
            if i<rows-1: faces.append([a,a+1,b+1])
    return vertices,faces,uvs


class Package:
    def __init__(self, root, eye, target, fov=35, aspect=1):
        self.root=Path(root);self.root.mkdir(parents=True,exist_ok=True)
        self.eye,self.target,self.fov,self.aspect=eye,target,fov,aspect
        self.geometry=bytearray(b'PTMESH01');self.tangents=bytearray(b'PTTANG01')
        self.materials=[];self.draws=[]

    def material(self, **parameters):
        material=dict(metallic=0);material.update(parameters)
        self.materials.append(material);return len(self.materials)-1

    def store(self, triangles, material):
        offset,toffset,count=len(self.geometry),len(self.tangents),0
        for triangle in triangles:
            for p,n,uv,t in triangle:
                if not all(math.isfinite(x) for x in [*p,*n,*uv,*t]):raise ValueError('Nonfinite geometry')
                self.geometry.extend(struct.pack('<8f',*p,*n,*uv));self.tangents.extend(struct.pack('<4f',*t));count+=1
        if count:
            self.draws.append(dict(offset=offset,vertices=count,tangents_offset=toffset,material=material,model=matrix()))

    def mesh(self, vertices, faces, material, uvs=None):
        normals=vertex_normals(vertices,faces)
        tangents=[[0,0,0] for _ in vertices];bitangents=[[0,0,0] for _ in vertices]
        if uvs:
            for a,b,c in faces:
                e1,e2=sub(vertices[b],vertices[a]),sub(vertices[c],vertices[a]);d1,d2=sub(uvs[b],uvs[a]),sub(uvs[c],uvs[a]);det=d1[0]*d2[1]-d1[1]*d2[0]
                if abs(det)<1e-10:continue
                t=mul(sub(mul(e1,d2[1]),mul(e2,d1[1])),1/det);bt=mul(sub(mul(e2,d1[0]),mul(e1,d2[0])),1/det)
                for i in (a,b,c):tangents[i]=add(tangents[i],t);bitangents[i]=add(bitangents[i],bt)
        for i,n in enumerate(normals):
            t=sub(tangents[i],mul(n,dot(n,tangents[i])))
            t=unit(t) if length(t)>1e-10 else frame(n)[0]
            tangents[i]=[*t,-1 if dot(cross(n,t),bitangents[i])<0 else 1]
        def generate():
            for indices in faces:
                yield [(vertices[i],normals[i],uvs[i] if uvs else [0,0],tangents[i]) for i in indices]
        self.store(generate(),material)

    def ellipsoid(self, center, radii, material):
        p,f,uv=ellipsoid(center,radii);self.mesh(p,f,material,uv)

    def quad(self, points, normal, material):
        t=unit(sub(points[1],points[0]));v=[(p,normal,uv,[*t,1]) for p,uv in zip(points,[(0,0),(1,0),(1,1),(0,1)])]
        self.store([(v[0],v[1],v[2]),(v[0],v[2],v[3])],material)

    def studio(self):
        floor=self.material(base_color=[.3,.32,.35],roughness=.85)
        self.quad([[-12,0,12],[12,0,12],[12,0,-12],[-12,0,-12]],[0,1,0],floor)
        key=self.material(base_color=[0,0,0],emission=[14,12.5,11])
        self.quad([[-3.5,4.5,4],[-.5,4.5,4],[-.5,2.5,4],[-3.5,2.5,4]],[0,0,-1],key)
        fill=self.material(base_color=[0,0,0],emission=[4,5,7])
        self.quad([[3.5,1,2],[3.5,4,2],[3.5,4,-2],[3.5,1,-2]],[-1,0,0],fill)
        rim=self.material(base_color=[0,0,0],emission=[32,26,18])
        self.quad([[-2,5.8,-2],[2,5.8,-2],[2,5.8,-.8],[-2,5.8,-.8]],[0,-1,0],rim)

    def save(self, source, curves=None):
        forward=unit(sub(self.target,self.eye));right=unit(cross(forward,[0,1,0]));up=cross(right,forward)
        camera=[*right,0,*up,0,*mul(forward,-1),0,*self.eye,1]
        f=1/math.tan(math.radians(self.fov)/2);near,far=.03,100
        projection=[f/self.aspect,0,0,0,0,f,0,0,0,0,(far+near)/(near-far),-1,0,0,2*far*near/(near-far),0]
        scene=dict(format='SceneRenderer.PT.v1',geometry='geometry.bin',tangents='tangents.bin',materials=self.materials,draws=self.draws,
                   camera=dict(world=camera,projection=projection,aspect=self.aspect),environment=dict(file='environment.bin',width=16,height=8),source=source)
        (self.root/'geometry.bin').write_bytes(self.geometry);(self.root/'tangents.bin').write_bytes(self.tangents)
        (self.root/'environment.bin').write_bytes(struct.pack('<384f',*([.035,.04,.05]*128)))
        if curves is not None:
            (self.root/'groom.json').write_text(json.dumps(dict(format='SceneRenderer.StaticGroom.v1',representation='reference-camera ribbons',reference_camera=self.eye,strands=curves),separators=(',',':'))+'\n')
            scene['source']['groom']='groom.json'
        (self.root/'scene.json').write_text(json.dumps(scene,indent=2)+'\n')
        return self.root/'scene.json'
