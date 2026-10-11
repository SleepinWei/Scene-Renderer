#!/usr/bin/env python3
"""Create knitwear, Stanford Bunny fur, and a parted wavy hairstyle for PT."""
import argparse
import hashlib
import math
from pathlib import Path
import random

from pt_groom import Package, add, sub, mul, dot, length, frame, unit, cross
from pt_groom import read_ascii_ply, surface_roots, ribbons, tubes


def bunny(root, source, count=24000, seed=7, refined=False):
    vertices,faces=read_ascii_ply(source)
    low=[min(p[k] for p in vertices) for k in range(3)];high=[max(p[k] for p in vertices) for k in range(3)]
    scale=2.6/(high[1]-low[1]);center=[(a+b)*.5 for a,b in zip(low,high)]
    vertices=[[(p[0]-center[0])*scale,(p[1]-low[1])*scale+.035,(p[2]-center[2])*scale] for p in vertices]
    package=Package(root,[3.4,1.45,6.2] if refined else [3.4,2.35,6.2],[0,1.35,0],32)
    core=package.material(base_color=[.72,.69,.62],roughness=.9)
    fur=package.material(bsdf_model='hair',base_color=[.97,.95,.9],hair_beta_m=.48,hair_beta_n=.42,hair_tilt=.035,two_sided=True)
    if refined:
        package.materials[fur].update(base_color=[1,1,1],absorption=[.08,.11,.18],hair_beta_m=.32,hair_beta_n=.38)
    package.mesh(vertices,faces,core)
    roots=surface_roots(vertices,faces,count,seed,lambda p,n: p[1]>.07 or n[1]>-.5)
    rng=random.Random(seed+1);curves=[]
    for root in roots:
        p,n=root['position'],root['normal'];flow=[.15,-.9,-.4]
        flow=sub(flow,mul(n,dot(flow,n)));flow=unit(flow) if length(flow)>1e-8 else frame(n)[0]
        strand_length=((.042+.023*rng.random()) if refined else (.052+.032*rng.random()))*(.5 if p[1]>2.0 else 1)
        side=cross(n,flow);phase=rng.random()*2*math.pi;points=[]
        for j in range(6):
            u=j/5
            points.append(add(add(add(p,mul(n,.002+strand_length*(.55*u+.25*u*u))),mul(flow,strand_length*.65*u*u)),mul(side,.004*math.sin(phase+u*5)*u*u)))
        curves.append(dict(points=points,radius=(.0018 if refined else .0033)*(.8+.4*rng.random()),root_triangle=root['triangle'],barycentric=root['barycentric']))
    package.store(ribbons(curves,package.eye),fur);package.studio()
    return package.save(dict(name='Stanford Bunny short fur',strands=count,seed=seed,input=str(source),input_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),
                             source_url='https://graphics.stanford.edu/data/3Dscanrep/',root_sampling='triangle area CDF + sqrt barycentrics; reject underside of feet',
                             length_range_world=[.021,.065] if refined else [.026,.084],refined=refined,description='Static surface groom, shorter ear fur, tangential comb flow and tapered reference-camera ribbons.'),curves)


def hairstyle(root, count=4500, seed=11):
    package=Package(root,[3.1,2.7,6.8],[0,1.9,0],30)
    skin=package.material(base_color=[.73,.53,.4],roughness=.7)
    cloth=package.material(bsdf_model='cloth',base_color=[.14,.16,.19],roughness=.85,sheen_weight=.2)
    white=package.material(base_color=[.7,.72,.66],roughness=.55)
    iris=package.material(base_color=[.07,.11,.13],roughness=.3)
    lip=package.material(base_color=[.5,.23,.17],roughness=.65)
    hair=package.material(bsdf_model='hair',base_color=[.88,.72,.46],hair_beta_m=.24,hair_beta_n=.3,hair_tilt=.035,two_sided=True)
    # An original stylized mannequin, not a downloaded human likeness.
    package.ellipsoid([0,2.25,0],[.56,.8,.48],skin)
    package.ellipsoid([0,1.35,-.015],[.22,.48,.22],skin)
    package.ellipsoid([0,.75,-.08],[.88,.62,.4],cloth)
    package.ellipsoid([0,2.18,.47],[.095,.14,.11],skin)
    package.ellipsoid([0,1.98,.45],[.15,.022,.024],lip)
    for sign in (-1,1):
        package.ellipsoid([sign*.20,2.43,.427],[.095,.038,.047],white)
        package.ellipsoid([sign*.20,2.43,.471],[.029,.027,.017],iris)
        package.ellipsoid([sign*.55,2.22,0],[.095,.16,.06],skin)
    rng=random.Random(seed);curves=[]
    for _ in range(count):
        theta=math.acos(1-rng.random()*(1-math.cos(1.16)));phi=rng.random()*2*math.pi
        sign=1 if math.cos(phi)>=0 else -1;target_phi=0 if sign>0 else math.pi
        delta=math.atan2(math.sin(target_phi-phi),math.cos(target_phi-phi))
        front=max(0,math.sin(phi));phase=rng.random()*2*math.pi;fall=1.20+.27*rng.random();points=[]
        for j in range(29):
            u=j/28;cap=min(u/.44,1);turn=cap*cap*(3-2*cap)
            angle=theta+(1.52-theta)*cap;azimuth=phi+delta*front*.85*turn
            clearance=.014+.018*math.sin(math.pi*cap*.5)
            p=[(.56+clearance)*math.sin(angle)*math.cos(azimuth),2.25+(.8+clearance)*math.cos(angle),(.48+clearance)*math.sin(angle)*math.sin(azimuth)]
            tail=max(0,(u-.44)/.56)
            p[1]-=fall*tail
            p[0]+=sign*.13*tail*tail+.045*math.sin(phase+tail*7)*tail
            p[2]+=.055*math.sin(phase*.4+tail*8)*tail
            points.append(p)
        curves.append(dict(points=points,radius=.0028*(.8+.4*rng.random()),scalp_theta=theta,scalp_phi=phi))
    package.store(ribbons(curves,package.eye),hair);package.studio()
    return package.save(dict(name='Parted wavy mid-length hair',strands=count,seed=seed,description='Original mannequin; ellipsoidal scalp guides, center part, head-following cap and gravity-directed wavy tails. Static grooming, not dynamics.'),curves)


def param_mesh(package, evaluate, rows, columns, material):
    vertices=[];uvs=[];faces=[]
    for i in range(rows+1):
        for j in range(columns+1):
            u,v=j/columns,i/rows;vertices.append(evaluate(u,v));uvs.append([u,v])
    for i in range(rows):
        for j in range(columns):
            a=i*(columns+1)+j;b=a+columns+1
            faces.extend([(a,b+1,b),(a,a+1,b+1)])
    # Ensure geometric winding agrees with intended outward garment normals.
    package.mesh(vertices,faces,material,uvs)


def sweater(root, refined=False):
    package=Package(root,[2.5,1.25,5.5] if refined else [2.5,2.0,5.5],[0,1.25,0],30)
    body=package.material(bsdf_model='knit',base_color=[.18,.42,.58],roughness=.8,sheen_weight=.35,sheen_roughness=.85,sheen_color=[.35,.65,.8],cloth_transmission=.03,
                          knit_repeats_u=36,knit_repeats_v=34,knit_yarn_radius=.13,two_sided=True)
    sleeve=package.material(**{**package.materials[body],'knit_repeats_u':12,'knit_repeats_v':26})
    rib=package.material(bsdf_model='cloth',base_color=[.15,.36,.5],roughness=.8,sheen_weight=.3,sheen_roughness=.85,two_sided=True)
    if refined:
        for material in (body,sleeve,rib):
            package.materials[material].update(base_color=[.78,.67,.52],sheen_color=[.9,.82,.7],sheen_weight=.22)
        package.materials[body]['knit_weight']=.22
        package.materials[body].update(knit_repeats_u=64,knit_repeats_v=52)
        package.materials[sleeve].update(knit_weight=.45,knit_repeats_u=24,knit_repeats_v=42)
    def torso(u,v):
        angle=2*math.pi*u;width=.63+.1*math.sin(math.pi*v)
        if v>.72:width=width*(1-(v-.72)/.28)+.245*((v-.72)/.28)
        depth=.3+.025*math.sin(math.pi*v)
        if v>.82:depth=depth*(1-(v-.82)/.18)+.19*((v-.82)/.18)
        pleat=.012*math.sin(angle*6+v*3)*math.sin(math.pi*v)
        return [(width+pleat)*math.sin(angle),.38+1.7*v,(depth+pleat)*math.cos(angle)]
    param_mesh(package,torso,72,96,body)
    yarn=package.material(**{**package.materials[body],'bsdf_model':'cloth','knit_weight':0})
    guide=[[-.5,-.25,-.12],[-.27,-.4,-.1],[-.12,-.18,0],[-.3,.22,.1],[-.32,.6,.07],[0,.78,.02],[.32,.6,.07],[.3,.22,.1],[.12,-.18,0],[.27,-.4,-.1],[.5,-.25,-.12]]
    cell=catmull(guide,3)
    yarn_nap=[];nap_rng=random.Random(23)
    rows,columns=(range(2,42),range(-16,16)) if refined else (range(2,28),range(-9,9))
    repeats_u,repeats_v=(64,52) if refined else (36,34)
    for row in rows:
        for column in columns:
            points=[]
            for x,y,z in cell:
                u,v=(column+.5+x)/repeats_u,(row+.5+y)/repeats_v;p=torso(u,v)
                n=unit(cross(sub(torso(u+.0001,v),torso(u-.0001,v)),sub(torso(u,v+.0001),torso(u,v-.0001))))
                points.append(add(p,mul(n,(.005+.015*z) if refined else (.009+.06*z))))
            package.store(tubes(points,.0085 if refined else .0075,12 if refined else 8),yarn)
            if refined:
                for _ in range(12):
                    index=nap_rng.randrange(1,len(points)-1);axis=unit(sub(points[index+1],points[index-1]))
                    radial,other=frame(axis);angle=nap_rng.random()*2*math.pi
                    radial=add(mul(radial,math.cos(angle)),mul(other,math.sin(angle)))
                    p=add(points[index],mul(radial,.0088));extent=.004+.006*nap_rng.random()
                    yarn_nap.append(dict(points=[p,add(p,add(mul(radial,extent*.5),mul(axis,.002))),
                                                 add(p,add(mul(radial,extent),mul(axis,.005)))],radius=.00045))
    fuzz=package.material(bsdf_model='hair',base_color=[.55,.74,.84],hair_beta_m=.55,hair_beta_n=.5,two_sided=True)
    if refined:
        package.materials[fuzz].update(base_color=[1,1,1],absorption=[.16,.28,.48],hair_beta_m=.4,hair_beta_n=.45)
    rng=random.Random(17);curves=yarn_nap
    for _ in range(8000 if refined else 2000):
        u,v=rng.random(),.05+.65*rng.random();p=torso(u,v)
        n=unit(cross(sub(torso(u+.0001,v),torso(u-.0001,v)),sub(torso(u,v+.0001),torso(u,v-.0001))))
        tangent=frame(n)[0];extent=.012+.014*rng.random()
        curves.append(dict(points=[add(p,mul(n,.006)),add(p,add(mul(n,.006+extent*.5),mul(tangent,.004))),add(p,add(mul(n,.006+extent),mul(tangent,.01)))],radius=.00065 if refined else .0012))
    package.store(ribbons(curves,package.eye),fuzz)
    for sign in (-1,1):
        start=[sign*.53,1.60,0];axis=unit([sign*.62,-1.06,0]);t,b=frame(axis)
        package.ellipsoid(start,[.28,.28,.265],sleeve)
        def arm(u,v):
            c=add(start,mul(axis,1.23*v));r=.265-.10*v;phi=2*math.pi*u
            return add(c,add(mul(t,r*math.cos(phi)),mul(b,r*math.sin(phi))))
        param_mesh(package,arm,48,64,sleeve)
        c=add(start,mul(axis,1.23))
        points=[add(c,add(mul(t,.17*math.cos(2*math.pi*j/96)),mul(b,.17*math.sin(2*math.pi*j/96)))) for j in range(97)]
        package.store(tubes(points,.035,10),rib)
    for y,rx,rz,radius in [(2.08,.245,.19,.045),(.38,.63,.3,.04)]:
        points=[[rx*math.sin(2*math.pi*j/144),y,rz*math.cos(2*math.pi*j/144)] for j in range(145)]
        package.store(tubes(points,radius,10),rib)
    package.studio()
    return package.save(dict(name='Procedural knitted sweater',yarn_loops=len(rows)*len(columns),fuzz_strands=len(curves),refined=refined,description='Static pullover with collar/cuffs/hem, knit UV appearance, explicit front yarn loops and fiber nap. Garment joins and unrelaxed yarns are a geometric prototype, not simulated stitch topology.'),curves)


def catmull(points, steps=4):
    result=[]
    for i in range(len(points)-1):
        a,b,c,d=points[max(0,i-1)],points[i],points[i+1],points[min(len(points)-1,i+2)]
        for j in range(steps):
            t=j/steps
            result.append([.5*((2*b[k])+(-a[k]+c[k])*t+(2*a[k]-5*b[k]+4*c[k]-d[k])*t*t+(-a[k]+3*b[k]-3*c[k]+d[k])*t*t*t) for k in range(3)])
    return result+[points[-1]]


def yarn_swatch(root, refined=False):
    package=Package(root,[1.3,1.3,3.9],[0,1.1,0],28)
    yarn=package.material(bsdf_model='cloth',base_color=[.22,.47,.61],roughness=.85,sheen_weight=.45,sheen_roughness=.9,sheen_color=[.45,.75,.9],two_sided=True)
    # Stylized crossing loop rows, without a mechanical relaxation/topology claim.
    guide=[[-.5,-.25,-.12],[-.27,-.4,-.1],[-.12,-.18,0],[-.3,.22,.1],[-.32,.6,.07],[0,.78,.02],[.32,.6,.07],[.3,.22,.1],[.12,-.18,0],[.27,-.4,-.1],[.5,-.25,-.12]]
    curves=[];rng=random.Random(31)
    for row in range(9):
        points=[]
        for column in range(11):
            cell=catmull(guide,8 if refined else 4)
            points.extend([[.13*(p[0]+column-5),.18*(p[1]+row)+.28,.045*p[2]+.02*math.sin((row+p[1])*.7)] for p in cell[:-1]])
        points.append([.13*(5.5),.18*(row-.25)+.28,-.0054+.02*math.sin((row-.25)*.7)])
        if refined:
            # Three geometric plies wind around a transported yarn centerline.
            frames=[];previous=None;arc=0
            for i,p in enumerate(points):
                axis=unit(sub(points[min(i+1,len(points)-1)],points[max(0,i-1)]))
                radial=frame(axis)[0] if previous is None else sub(previous,mul(axis,dot(previous,axis)))
                radial=unit(radial) if length(radial)>1e-8 else frame(axis)[0];previous=radial
                if i:arc+=length(sub(p,points[i-1]))
                frames.append((axis,radial,cross(axis,radial),arc))
            for ply in range(3):
                helix=[]
                for p,(axis,t,b,arc) in zip(points,frames):
                    angle=arc/.095*2*math.pi+ply*2*math.pi/3
                    helix.append(add(p,add(mul(t,.009*math.cos(angle)),mul(b,.009*math.sin(angle)))))
                package.store(tubes(helix,.009,16),yarn)
            for _ in range(2000):
                i=rng.randrange(1,len(points)-1);axis,t,b,_=frames[i];angle=rng.random()*2*math.pi
                radial=add(mul(t,math.cos(angle)),mul(b,math.sin(angle)));p=add(points[i],mul(radial,.0183));extent=.004+.006*rng.random()
                curves.append(dict(points=[p,add(p,add(mul(radial,extent*.5),mul(axis,.002))),
                                           add(p,add(mul(radial,extent),mul(axis,.005)))],radius=.0006))
        else:
            package.store(tubes(points,.018,10),yarn)
    if refined:
        fuzz=package.material(bsdf_model='hair',base_color=[1,1,1],absorption=[.12,.2,.34],hair_beta_m=.4,hair_beta_n=.45,two_sided=True)
        package.store(ribbons(curves,package.eye),fuzz)
    package.studio()
    return package.save(dict(name='Knit yarn closeup',refined=refined,yarn_plies=3 if refined else 1,fuzz_strands=len(curves),
                             description='Explicit continuous yarn rows with curved stitch loops; opaque cloth yarn tubes. Stylized unrelaxed geometry, not a topology-certified knit simulation.'),curves if refined else None)


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True)
    p.add_argument('--scene',choices=['all','sweater','bunny','hair','yarn'],default='all')
    p.add_argument('--bunny',type=Path,default=Path('samples/assets/bunny/bun_zipper.ply'))
    p.add_argument('--fur-strands',type=int,default=24000);p.add_argument('--hair-strands',type=int,default=4500)
    args=p.parse_args()
    if not 1<=args.fur_strands<=100000 or not 1<=args.hair_strands<=30000:p.error('strand count outside bounded fixture budget')
    for name in ['sweater','bunny','hair','yarn'] if args.scene=='all' else [args.scene]:
        root=args.output/name
        if name=='bunny':
            if not args.bunny.is_file():p.error('Bunny PLY missing; run tools/fetch_classic_assets.py or pass --bunny')
            scene=bunny(root,args.bunny,args.fur_strands)
        elif name=='hair':scene=hairstyle(root,args.hair_strands)
        elif name=='sweater':scene=sweater(root)
        else:scene=yarn_swatch(root)
        print(scene,flush=True)


if __name__=='__main__': main()
