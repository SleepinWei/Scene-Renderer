#!/usr/bin/env python3
"""Create static hair ribbons and draped cloth, with no external assets or DCC."""
import argparse
import json
import math
from pathlib import Path
import struct
from create_pt_material_fixture import unit, cross, matrix


def create(root, strands=3500):
    root.mkdir(parents=True, exist_ok=True)
    geometry, tangents = bytearray(b'PTMESH01'), bytearray(b'PTTANG01')
    def store(triangles):
        offset, tangent_offset, count = len(geometry), len(tangents), 0
        for triangle in triangles:
            for p, n, uv, tangent in triangle:
                geometry.extend(struct.pack('<8f', *p, *n, *uv))
                tangents.extend(struct.pack('<4f', *tangent)); count += 1
        return dict(offset=offset, vertices=count, tangents_offset=tangent_offset)
    def quad(points, n):
        t = unit([points[1][k]-points[0][k] for k in range(3)])
        v = [(p, n, uv, [*t, 1]) for p, uv in zip(points, [(0,0),(1,0),(1,1),(0,1)])]
        return store([(v[0],v[1],v[2]),(v[0],v[2],v[3])])
    eye, target = [0,1.5,6.1], [0,1.05,0]
    center = [-1.25,.93,0]
    def sphere(i,j):
        theta,phi = math.pi*i/24,2*math.pi*j/48
        n=[math.sin(theta)*math.cos(phi), math.cos(theta), math.sin(theta)*math.sin(phi)]
        return ([center[k]+n[k]*.68 for k in range(3)],n,[j/48,i/24],[-math.sin(phi),0,math.cos(phi),1])
    triangles=[]
    for i in range(24):
        for j in range(48):
            a,b,c,d=sphere(i,j),sphere(i+1,j),sphere(i+1,j+1),sphere(i,j+1)
            if i:triangles.append((a,c,b))
            if i<23:triangles.append((a,d,c))
    core=store(triangles);triangles=[]
    for i in range(strands):
        y=1-2*(i+.5)/strands;phi=i*math.pi*(3-math.sqrt(5));r=math.sqrt(1-y*y)
        normal=[r*math.cos(phi),y,r*math.sin(phi)]
        comb=unit(cross(normal,[0,1,0]) if abs(y)<.98 else [1,0,0])
        length=.18+.07*(.5+.5*math.sin(i*37.1))
        pairs=[]
        for segment in range(6):
            u=segment/5
            p=[center[k]+normal[k]*(.68+length*u)+comb[k]*.08*u*u for k in range(3)]
            axis=unit([normal[k]*length+comb[k]*.16*u for k in range(3)])
            view=unit([eye[k]-p[k] for k in range(3)]);width=unit(cross(view,axis));n=unit(cross(axis,width))
            radius=.006*(1-.95*u)
            pairs.append([( [p[k]+sign*radius*width[k] for k in range(3)],n,[u,v],[*axis,1]) for sign,v in [(-1,0),(1,1)]])
        for a,b in zip(pairs,pairs[1:]):triangles.extend([(a[0],b[0],b[1]),(a[0],b[1],a[1])])
    hair=store(triangles)
    def cloth_vertex(i,j):
        u,v=i/48,j/64;x=.35+1.8*u;y=.12+1.9*(1-v)-.06*math.sin(math.pi*u)
        z=.26*math.sin(u*6*math.pi)*(.2+.8*v)
        du=[1.8,-.06*math.pi*math.cos(math.pi*u),.26*6*math.pi*math.cos(u*6*math.pi)*(.2+.8*v)]
        dv=[0,-1.9,.26*.8*math.sin(u*6*math.pi)]
        n=unit(cross(dv,du));return ([x,y,z],n,[u,v],[*unit(du),-1])
    triangles=[]
    for i in range(48):
        for j in range(64):
            a,b,c,d=cloth_vertex(i,j),cloth_vertex(i+1,j),cloth_vertex(i+1,j+1),cloth_vertex(i,j+1)
            triangles.extend([(a,c,b),(a,d,c)])
    cloth=store(triangles)
    floor=quad([[-8,0,8],[8,0,8],[8,0,-8],[-8,0,-8]],[0,1,0])
    key=quad([[-3,3.8,1],[3,3.8,1],[3,3.8,2],[-3,3.8,2]],[0,-1,0])
    back=quad([[-3,.4,-1.5],[3,.4,-1.5],[3,3,-1.5],[-3,3,-1.5]],[0,0,1])
    materials=[dict(base_color=[.24,.12,.06],roughness=.8,metallic=0),
               dict(bsdf_model='hair',base_color=[.75,.4,.16],metallic=0,hair_beta_m=.3,hair_beta_n=.3,hair_ior=1.55,hair_tilt=.034906585,two_sided=True),
               dict(bsdf_model='cloth',base_color=[.7,.2,.12],metallic=0,roughness=.65,sheen_weight=.45,sheen_roughness=.65,sheen_color=[.9,.35,.2],cloth_transmission=.2,two_sided=True),
               dict(base_color=[.4,.4,.4],roughness=.9,metallic=0),
               dict(base_color=[0,0,0],metallic=0,emission=[12,12,12]),dict(base_color=[0,0,0],metallic=0,emission=[3,3,3])]
    draws=[{**mesh,'material':i,'model':matrix()} for i,mesh in enumerate([core,hair,cloth,floor,key,back])]
    forward=unit([b-a for a,b in zip(eye,target)]);right=unit(cross(forward,[0,1,0]));up=cross(right,forward)
    camera=[*right,0,*up,0,*[-x for x in forward],0,*eye,1]
    aspect,near,far=16/9,.05,100;f=1/math.tan(math.radians(40)/2)
    projection=[f/aspect,0,0,0,0,f,0,0,0,0,(far+near)/(near-far),-1,0,0,2*far*near/(near-far),0]
    scene=dict(format='SceneRenderer.PT.v1',geometry='geometry.bin',tangents='tangents.bin',camera=dict(world=camera,projection=projection,aspect=aspect),materials=materials,draws=draws,
               environment=dict(file='environment.bin',width=16,height=8),source=dict(name='Static fur and draped cloth',strands=strands,description='Fixed camera-oriented tapered triangle ribbons, six control samples per strand; cloth is a static folded mesh. No native curves or dynamics.'))
    (root/'geometry.bin').write_bytes(geometry);(root/'tangents.bin').write_bytes(tangents)
    (root/'environment.bin').write_bytes(struct.pack('<384f',*([.07,.07,.07]*128)))
    (root/'scene.json').write_text(json.dumps(scene,indent=2)+'\n');print(root/'scene.json')


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--output',type=Path,required=True);parser.add_argument('--strands',type=int,default=3500)
    args=parser.parse_args()
    if not 1<=args.strands<=100000:parser.error('strands must be in [1,100000]')
    create(args.output,args.strands)
