#!/usr/bin/env python3
"""Relight refined groom packages with the renderer's frozen sunset environment."""
import argparse
import json
import math
from pathlib import Path
import shutil

import numpy as np

from create_pt_hair_sky import decode_baked_hdr, sha


def create(source,output,environment,yaw,scale,exposure):
    if source.resolve() == output.resolve():
        raise ValueError('Sunset output must differ from its source')
    scene=json.loads((source/'scene.json').read_text())
    output.mkdir(parents=True,exist_ok=True)
    for key in ['geometry','tangents']:
        shutil.copyfile(source/scene[key],output/(key+'.bin'))
        scene[key]=key+'.bin'
    if (source/'groom.json').exists():
        shutil.copyfile(source/'groom.json',output/'groom.json')
    extent=decode_baked_hdr(environment,output/'environment.bin')
    pixels=np.fromfile(output/'environment.bin',dtype='<f4').reshape(extent['height'],extent['width'],3)
    shift=yaw/360*extent['width'];integer=math.floor(shift);fraction=shift-integer
    rotated=(np.roll(pixels,integer,axis=1)*(1-fraction)+np.roll(pixels,integer+1,axis=1)*fraction)*scale
    rotated.astype('<f4').tofile(output/'environment.bin')
    solar=json.loads(Path(str(environment)+'.json').read_text())
    x,y,z=solar['sun_direction'];angle=math.radians(yaw)
    scene['sun']=dict(direction=[math.cos(angle)*x-math.sin(angle)*z,y,math.sin(angle)*x+math.cos(angle)*z],
                      irradiance=[v*scale for v in solar['sun_irradiance']],radius=solar['sun_radius'])
    scene['environment']=dict(file='environment.bin',width=extent['width'],height=extent['height'])
    scene['exposure']=exposure
    scene['source']['name']=scene['source']['name'].replace('refined clear-sky sunlight','refined sunset')
    scene['source']['solar_relight']=dict(yaw_degrees=yaw,light_scale=scale,sky_sha256=sha(environment),solar_sha256=sha(str(environment)+'.json'),
                                        source='Realtime clouds-sunset: 5 degree sun, frozen HDR clouds; projected cloud-shadow field not applied')
    (output/'scene.json').write_text(json.dumps(scene,indent=2)+'\n')
    (output/'sunset-import.json').write_text(json.dumps(dict(source=str(source),source_scene_sha256=sha(source/'scene.json'),
        geometry_unchanged=sha(source/scene['geometry'])==sha(output/'geometry.bin'),
        tangents_unchanged=sha(source/scene['tangents'])==sha(output/'tangents.bin'),camera_unchanged=True,materials_unchanged=True,
        sun_elevation_degrees=math.degrees(math.asin(y)),sun=scene['sun'],sky=extent),indent=2)+'\n')
    return output/'scene.json'


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input',type=Path,default=Path('build/pt-grooms-sunlit'))
    parser.add_argument('--output',type=Path,default=Path('build/pt-grooms-sunset'))
    parser.add_argument('--environment',type=Path,default=Path('build/pt-grooms-sunset/sunset-environment.hdr'))
    parser.add_argument('--sky-yaw',type=float,default=80)
    parser.add_argument('--light-scale',type=float,default=1.2)
    parser.add_argument('--exposure',type=float,default=.7)
    args=parser.parse_args()
    if not all(math.isfinite(v) for v in [args.sky_yaw,args.light_scale,args.exposure]) or not 0 < args.light_scale <= 1000 or args.exposure <= 0:
        parser.error('Invalid illumination settings')
    for name in ['bunny','sweater','yarn']:
        print(create(args.input/name,args.output/name,args.environment,args.sky_yaw,args.light_scale,args.exposure),flush=True)


if __name__=='__main__':
    main()
