#!/usr/bin/env python3
"""Render a source .blend or controlled PT package with Cycles; run inside Blender.

Writes a finite-sample reference, scene-linear EXR/PFM, source-view PNG and a PNG
using SceneRenderer's display mapping. Source mode preserves original shaders;
--pt-scene-file reconstructs explicitly controlled closures and frozen assets.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys
import time
import zlib
import bpy
import numpy as np


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--output',required=True)
    p.add_argument('--scene')
    p.add_argument('--pt-scene-file',help='reconstruct an explicitly matched PT comparison package')
    p.add_argument('--size',default='640x360')
    p.add_argument('--samples',type=int,default=1024)
    p.add_argument('--max-depth',type=int,default=24)
    p.add_argument('--threads',type=int,default=8)
    p.add_argument('--seed',type=int,default=1)
    p.add_argument('--source-integrator-settings',action='store_true',help='keep source clamp and glossy filtering; default GT disables both')
    args=p.parse_args(sys.argv[sys.argv.index('--')+1:])
    width,height=map(int,args.size.lower().split('x'))
    if not 0<width<=8192 or not 0<height<=8192 or not 0<args.samples<=1048576 or not 0<args.max_depth<=128 or not 0<args.threads<=256:
        raise ValueError('Invalid reference render parameters')
    if not 0<=args.seed<=2147483647:raise ValueError('Invalid Cycles seed')
    package=None
    if args.pt_scene_file:
        sys.path.insert(0,str(Path(__file__).parent))
        from cycles_pt_package import build
        package=build(args.pt_scene_file,width,height)
    scene=bpy.data.scenes[args.scene] if args.scene else bpy.context.scene
    bpy.context.window.scene=scene
    if not scene.camera or scene.camera.data.type!='PERSP':raise ValueError('Perspective source camera required')
    prefix=Path(args.output).resolve();prefix.parent.mkdir(parents=True,exist_ok=True)
    def file(suffix):return prefix.with_name(prefix.name+suffix)
    # Invalidate stale completion metadata if a rebuild fails.
    file('.json').unlink(missing_ok=True)
    report=dict(kind='Cycles source-scene finite-sample reference',blender_version=bpy.app.version_string,
                source_blend=Path(bpy.data.filepath).name if bpy.data.filepath else None,source_blend_sha256=digest(Path(bpy.data.filepath)) if bpy.data.filepath else None,
                scene=scene.name,camera=scene.camera.name,frame=scene.frame_current,
                width=width,height=height,samples=args.samples,max_depth=args.max_depth,seed=args.seed,threads=args.threads,
                material_conversion=False,particle_budget=False,denoise=False,compositor=False,
                source_view_transform=scene.view_settings.view_transform,source_look=scene.view_settings.look,
                source_view_exposure=scene.view_settings.exposure,source_view_gamma=scene.view_settings.gamma,
                project_display_mapping='(1-exp(-max(scene_linear_RGB,0)))^(1/2.2), exposure=1',
                limits=['finite Monte Carlo sample count; not noise-free mathematical ground truth',
                        'source shaders, full render visibility and full particles differ from exported PT preview',
                        'source water IOR/open sheet retained; no explicit pool profile applied'])
    if package:
        report.update(kind='Cycles matched PT package reference',scene_package=package,material_conversion=True,particle_budget=package.get('particle_budget'),limits=package['limits']+['finite Monte Carlo sample count and maximum depth'])
    report['source_integrator_settings']={name:getattr(scene.cycles,name) for name in ('sample_clamp_direct','sample_clamp_indirect','blur_glossy','caustics_reflective','caustics_refractive','use_light_tree')}
    report['preserve_source_integrator_settings']=args.source_integrator_settings
    if not args.source_integrator_settings:
        scene.cycles.sample_clamp_direct=0;scene.cycles.sample_clamp_indirect=0;scene.cycles.blur_glossy=0
    scene.render.engine='CYCLES';scene.cycles.device='CPU';scene.cycles.samples=args.samples
    scene.cycles.use_adaptive_sampling=False;scene.cycles.use_denoising=False;scene.cycles.seed=args.seed;scene.cycles.use_animated_seed=False
    if package:scene.cycles.pixel_filter_type='BOX';scene.cycles.filter_width=1
    report['pixel_filter']=dict(type=scene.cycles.pixel_filter_type,width=scene.cycles.filter_width)
    scene.cycles.max_bounces=args.max_depth
    for setting in ('diffuse_bounces','glossy_bounces','transmission_bounces','volume_bounces','transparent_max_bounces'):
        setattr(scene.cycles,setting,args.max_depth)
    scene.render.resolution_x=width;scene.render.resolution_y=height;scene.render.resolution_percentage=100
    scene.render.threads_mode='FIXED';scene.render.threads=args.threads;scene.render.use_compositing=False
    report['cycles_settings']={name:getattr(scene.cycles,name) for name in ('sample_clamp_direct','sample_clamp_indirect','blur_glossy','caustics_reflective','caustics_refractive','use_light_tree')}
    print('REFERENCE_START',json.dumps(report),flush=True)
    start=time.monotonic();bpy.ops.render.render();report['render_seconds']=time.monotonic()-start
    scene.render.image_settings.file_format='OPEN_EXR';scene.render.image_settings.color_depth='32';scene.render.image_settings.color_mode='RGBA';scene.render.image_settings.exr_codec='ZIP'
    bpy.data.images['Render Result'].save_render(str(file('.exr')),scene=scene)
    scene.render.image_settings.file_format='PNG';scene.render.image_settings.color_depth='8'
    bpy.data.images['Render Result'].save_render(str(file('-source-view.png')),scene=scene)
    image=bpy.data.images.load(str(file('.exr')),check_existing=False)
    if tuple(image.size)!=(width,height):raise RuntimeError('Reference render dimensions changed')
    pixels=np.empty(width*height*4,dtype=np.float32);image.pixels.foreach_get(pixels);pixels=pixels.reshape(height,width,4)[:,:,:3]
    if not np.isfinite(pixels).all():raise RuntimeError('Reference contains non-finite pixels')
    report['negative_rgb_components']=int((pixels<0).sum())
    # Blender image buffers and PFM both start at the bottom scanline.
    file('.pfm').write_bytes(f'PF\n{width} {height}\n-1.0\n'.encode()+pixels.astype('<f4').tobytes())
    display=np.clip((1-np.exp(-np.maximum(pixels[::-1],0)))**(1/2.2)*255+.5,0,255).astype(np.uint8)
    def chunk(kind,data):return struct.pack('>I',len(data))+kind+data+struct.pack('>I',zlib.crc32(kind+data)&0xffffffff)
    png=b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',width,height,8,2,0,0,0))+chunk(b'IDAT',zlib.compress(b''.join(b'\0'+row.tobytes() for row in display)))+chunk(b'IEND',b'')
    file('.png').write_bytes(png)
    report['outputs_sha256']={str(file(suffix)):digest(file(suffix)) for suffix in ('.exr','.pfm','.png','-source-view.png')}
    file('.json').write_text(json.dumps(report,indent=2)+'\n')
    print('REFERENCE_FINISHED',json.dumps(report),flush=True)


if __name__=='__main__':main()
