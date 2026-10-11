#!/usr/bin/env python3
"""Create a self-contained three-sphere GGX appearance fixture (no DCC needed)."""
import argparse
import json
import math
from pathlib import Path
import struct


def unit(v):
    length = math.sqrt(sum(x * x for x in v))
    return [x / length for x in v]


def cross(a, b):
    return [a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0]]


def matrix(position=(0, 0, 0)):
    return [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, *position, 1]


def create(destination):
    destination.mkdir(parents=True, exist_ok=True)
    geometry = bytearray(b'PTMESH01')
    tangents = bytearray(b'PTTANG01')

    def store(triangles):
        offset, tangent_offset = len(geometry), len(tangents)
        count = 0
        for triangle in triangles:
            for p, n, uv, tangent in triangle:
                geometry.extend(struct.pack('<8f', *p, *n, *uv))
                tangents.extend(struct.pack('<4f', *tangent))
                count += 1
        return {'offset': offset, 'vertices': count, 'tangents_offset': tangent_offset}

    def sphere_vertex(i, j):
        theta, phi = math.pi*i/32, 2*math.pi*j/64
        n = [math.sin(theta)*math.cos(phi), math.cos(theta), math.sin(theta)*math.sin(phi)]
        # V grows from the north pole toward the south pole.
        tangent = [-math.sin(phi), 0, math.cos(phi), 1]
        return ([x*.65 for x in n], n, [j/64, i/32], tangent)

    triangles = []
    for i in range(32):
        for j in range(64):
            a, b, c, d = sphere_vertex(i, j), sphere_vertex(i+1, j), sphere_vertex(i+1, j+1), sphere_vertex(i, j+1)
            # Reverse the theta/phi winding to obtain outward geometry normals.
            if i > 0:
                triangles.append((a, c, b))
            if i < 31:
                triangles.append((a, d, c))
    sphere = store(triangles)

    def quad(points, normal):
        uv = [(0, 0), (1, 0), (1, 1), (0, 1)]
        t = unit([points[1][k]-points[0][k] for k in range(3)])
        b = [points[3][k]-points[0][k] for k in range(3)]
        handedness = -1 if sum(x*y for x, y in zip(cross(normal, t), b)) < 0 else 1
        v = [(p, normal, tex, [*t, handedness]) for p, tex in zip(points, uv)]
        return store([(v[0], v[1], v[2]), (v[0], v[2], v[3])])

    floor = quad([[-8,0,8], [8,0,8], [8,0,-8], [-8,0,-8]], [0,1,0])
    light = quad([[-4,3,0], [4,3,0], [4,3,0.3], [-4,3,0.3]], [0,-1,0])
    eye, target = [0,1.6,6.5], [0,.7,0]
    forward = unit([b-a for a,b in zip(eye,target)])
    right = unit(cross(forward,[0,1,0])); up = cross(right,forward)
    camera = [*right,0,*up,0,*[-x for x in forward],0,*eye,1]
    aspect, near, far = 3, .05, 100
    f = 1/math.tan(math.radians(35)/2)
    projection = [f/aspect,0,0,0,0,f,0,0,0,0,(far+near)/(near-far),-1,0,0,2*far*near/(near-far),0]
    material = {'base_color': [.98,.82,.55], 'metallic':1, 'roughness':.4}
    materials = [{**material,'specular_anisotropy':a,'specular_rotation':r} for a,r in [(0,0),(.8,0),(.8,math.pi/2)]]
    materials += [{'base_color':[.45,.45,.45],'roughness':.8,'metallic':0}, {'base_color':[0,0,0],'roughness':1,'metallic':0,'emission':[18,18,18]}]
    draws = [{**sphere,'material':i,'model':matrix((x,.65,0))} for i,x in enumerate([-1.65,0,1.65])]
    draws += [{**floor,'material':3,'model':matrix()}, {**light,'material':4,'model':matrix()}]
    environment = struct.pack('<384f', *([.18,.18,.18]*128))
    scene = {'format':'SceneRenderer.PT.v1','geometry':'geometry.bin','tangents':'tangents.bin','camera':{'world':camera,'projection':projection,'aspect':aspect},'materials':materials,'draws':draws,'environment':{'file':'environment.bin','width':16,'height':8},'source':{'name':'GGX material axes','description':'Left: isotropic; center: anisotropy .8 / rotation 0; right: anisotropy .8 / rotation pi/2. Same metal, roughness and lighting.'}}
    for name, data in [('geometry.bin',geometry),('tangents.bin',tangents),('environment.bin',environment)]:
        (destination/name).write_bytes(data)
    (destination/'scene.json').write_text(json.dumps(scene,indent=2)+'\n')
    print(destination/'scene.json')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,required=True)
    create(parser.parse_args().output)
