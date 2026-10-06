#!/usr/bin/env python3
"""Freeze a shared PT package into explicitly matched closure profiles.

Run with ordinary Python, no Blender needed. Assets are copied, not re-baked.
Original exported packages and original Blender GT stay unchanged.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil


def prepare(source, output, profile, normals=False, sun=True):
    if profile not in ('lambert','lambert-leaves','ggx-water'):raise ValueError('Unsupported comparison profile')
    source=Path(source).resolve();output=Path(output).resolve()
    if output==source.parent:raise ValueError('Comparison must have a separate directory')
    j=json.loads(source.read_text())
    if j['format']!='SceneRenderer.PT.v1':raise ValueError('Unsupported PT package')
    assets={j['geometry']}
    if 'tangents' in j:assets.add(j['tangents'])
    if 'environment' in j:assets.add(j['environment']['file'])
    for m in j['materials']:
        for key in ('base_texture','orm_texture','normal_texture','transmission_texture'):
            if key in m:assets.add(m[key])
        if not normals:m.pop('normal_texture',None)
        m['bsdf_model']='thin_diffuse' if profile!='lambert' and m.get('bsdf_model')=='thin_diffuse' else 'lambert' if profile.startswith('lambert') else 'ggx_add'
        if profile.startswith('lambert'):
            # All boundaries become diffuse for the lighting/geometry baseline.
            m.update(ior=0,dielectric_roughness=0,thin_dielectric=False,bounded_volume=False,absorption=[0,0,0],scattering=[0,0,0])
        elif m.get('ior',0)>0 and not m.get('thin_dielectric',False):
            # Stock Cycles Glass tints both lobes; engine solid glass tints only
            # transmission. White interface isolates matching absorption/IOR.
            # Removing the base atlas also avoids tint from unused black UV padding.
            m['base_color']=[1,1,1];m.pop('base_texture',None)
    if not sun:j.pop('sun',None)
    output.mkdir(parents=True,exist_ok=True);(output/'scene.json').unlink(missing_ok=True)
    hashes={}
    for name in sorted(assets):
        relative=Path(name)
        if relative.is_absolute() or '..' in relative.parts:raise ValueError('Unsafe package asset')
        src=(source.parent/relative).resolve()
        if not src.is_relative_to(source.parent):raise ValueError('Asset escapes package')
        dest=output/relative;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(src,dest)
        hashes[name]=hashlib.sha256(dest.read_bytes()).hexdigest()
    j['comparison']={'profile':profile,'normal_maps':normals,'finite_sun':sun,'solid_interface_tint':'white' if profile=='ggx-water' else 'not applicable','source_manifest_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),'assets_sha256':hashes,'contract':'same frozen object-space geometry, transforms, camera, UVs, byte textures, environment and optics; corner MikkTSpace frames when exported; white solid dielectric interfaces; ggx_add uses .96*(1-metallic)*albedo Lambert + mix(.04,albedo,metallic) single-scatter correlated Smith GGX; thin_diffuse uses separately baked Lambert reflection + transmission; source Cycles layered closures not preserved'}
    j.setdefault('export',{})['comparison_profile']=profile
    (output/'scene.json').write_text(json.dumps(j,indent=2)+'\n')
    print(output/'scene.json')


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('source');p.add_argument('--output',required=True);p.add_argument('--profile',choices=['lambert','lambert-leaves','ggx-water'],required=True);p.add_argument('--normals',action='store_true');p.add_argument('--no-sun',action='store_true');args=p.parse_args()
    prepare(args.source,args.output,args.profile,args.normals,not args.no_sun)


if __name__=='__main__':main()
