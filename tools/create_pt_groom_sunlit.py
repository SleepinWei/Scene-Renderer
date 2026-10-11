#!/usr/bin/env python3
"""Generate finer Bunny fur and warm knitwear lit by the captured realtime sky.

NumPy is required. Sky radiance and finite-sun irradiance share yaw and scale.
Original studio fixtures remain unchanged; outputs contain no area emitters.
"""
import argparse
import json
import math
from pathlib import Path

import numpy as np

from create_pt_groom_fixtures import bunny, sweater, yarn_swatch
from create_pt_hair_sky import decode_baked_hdr, sha


def relight(path, environment, yaw, scale, exposure):
    scene = json.loads(path.read_text())
    root = path.parent
    # Studio fixtures end with one floor followed by three area lights.
    if len(scene['draws']) < 4 or not all(
            any(scene['materials'][d['material']].get('emission', [0, 0, 0]))
            for d in scene['draws'][-3:]):
        raise ValueError('Expected studio fixture layout')
    scene['draws'] = scene['draws'][:-3]
    floor = scene['draws'][-1]
    scene['materials'][floor['material']].update(base_color=[.32, .30, .25])
    geometry = bytearray((root/'geometry.bin').read_bytes())
    vertices = np.frombuffer(geometry, dtype='<f4', count=floor['vertices']*8,
                             offset=floor['offset']).reshape(-1, 8)
    vertices[:, 0] *= 200/12
    vertices[:, 2] *= 200/12
    if root.name == 'sweater':
        minimum = min(float(np.frombuffer(geometry, dtype='<f4', count=d['vertices']*8,
                                          offset=d['offset']).reshape(-1, 8)[:, 1].min())
                      for d in scene['draws'][:-1])
        translation = .03-minimum
        for draw in scene['draws'][:-1]:
            draw['model'][13] += translation
        scene['camera']['world'][13] += translation
        scene['source']['ground_translation_y'] = translation
    if root.name == 'yarn':
        # A dedicated near view reveals individual opaque cloth yarn tubes.
        scene['materials'][0].update(base_color=[.78,.67,.52],sheen_color=[.9,.82,.7],sheen_weight=.22)
    (root/'geometry.bin').write_bytes(geometry)
    extent = decode_baked_hdr(environment, root/'environment.bin')
    pixels = np.fromfile(root/'environment.bin', dtype='<f4').reshape(extent['height'], extent['width'], 3)
    shift = yaw/360*extent['width']; integer = math.floor(shift); fraction = shift-integer
    rotated = (np.roll(pixels, integer, axis=1)*(1-fraction)+
               np.roll(pixels, integer+1, axis=1)*fraction)*scale
    rotated.astype('<f4').tofile(root/'environment.bin')
    solar = json.loads(Path(str(environment)+'.json').read_text())
    x,y,z = solar['sun_direction']; angle = math.radians(yaw)
    scene['sun'] = dict(direction=[math.cos(angle)*x-math.sin(angle)*z,y,
                                   math.sin(angle)*x+math.cos(angle)*z],
                        irradiance=[v*scale for v in solar['sun_irradiance']],radius=solar['sun_radius'])
    scene['environment'] = dict(file='environment.bin',width=extent['width'],height=extent['height'])
    scene['exposure'] = exposure
    scene['source']['name'] += ' / refined clear-sky sunlight'
    scene['source']['solar_relight'] = dict(yaw_degrees=yaw,light_scale=scale,
                                           sky_sha256=sha(environment),solar_sha256=sha(str(environment)+'.json'))
    # Remove unused emissive materials as well as their draws.
    used = sorted({d['material'] for d in scene['draws']})
    for draw in scene['draws']:
        draw['material'] = used.index(draw['material'])
    scene['materials'] = [scene['materials'][i] for i in used]
    path.write_text(json.dumps(scene,indent=2)+'\n')
    return path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,default=Path('build/pt-grooms-sunlit'))
    parser.add_argument('--environment',type=Path,default=Path('build/pt-hair-sky/daylight-environment.hdr'))
    parser.add_argument('--bunny',type=Path,default=Path('samples/assets/bunny/bun_zipper.ply'))
    parser.add_argument('--fur-strands',type=int,default=72000)
    parser.add_argument('--sky-yaw',type=float,default=70)
    parser.add_argument('--light-scale',type=float,default=3)
    parser.add_argument('--exposure',type=float,default=.85)
    args = parser.parse_args()
    if not 1 <= args.fur_strands <= 100000:
        parser.error('Fur count must be in [1,100000]')
    if not all(math.isfinite(v) for v in (args.sky_yaw,args.light_scale,args.exposure)) or not 0 < args.light_scale <= 1000 or args.exposure <= 0:
        parser.error('Invalid illumination settings')
    for name,create in [('bunny',lambda p:bunny(p,args.bunny,args.fur_strands,refined=True)),
                        ('sweater',lambda p:sweater(p,refined=True)),('yarn',lambda p:yarn_swatch(p,refined=True))]:
        print(relight(create(args.output/name),args.environment,args.sky_yaw,args.light_scale,args.exposure),flush=True)


if __name__ == '__main__':
    main()
