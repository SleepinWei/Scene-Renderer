#!/usr/bin/env python3
"""Relight the sky hair portraits with a coherent stronger, rotated sun/sky.

Yaw rotates the entire HDR and the analytic sun together about world up.
Light scale multiplies both radiance and solar irradiance. Optional roughness
overrides narrow the physical fiber lobes; no extra area light is introduced.
"""
import argparse
import json
import math
from pathlib import Path
import shutil

import numpy as np

from create_pt_hair_sky import decode_baked_hdr, sha


def create(source, output, environment, yaw=135, light_scale=10, exposure=.6,
           beta_m=None, beta_n=None):
    source, output, environment = Path(source), Path(output), Path(environment)
    if source.resolve() == output.resolve():
        raise ValueError('Relighting output must differ from its source')
    if (not all(math.isfinite(v) for v in [yaw, light_scale, exposure]) or
            not 0 < light_scale <= 1000 or exposure <= 0):
        raise ValueError('Invalid yaw, illumination scale or exposure')
    if any(v is not None and (not math.isfinite(v) or not .05 <= v <= 1) for v in (beta_m, beta_n)):
        raise ValueError('Hair roughness must be in [0.05, 1]')
    scene = json.loads((source/'scene.json').read_text())
    original_materials = json.loads(json.dumps(scene['materials']))
    geometry_path, tangent_path = source/scene['geometry'], source/scene['tangents']
    if len(scene['draws']) != 2 or len(scene['materials']) != 2 or scene['materials'][1]['bsdf_model'] != 'hair':
        raise ValueError('Expected the head + hair sky portrait package')
    output.mkdir(parents=True, exist_ok=True)
    for key in ('geometry', 'tangents'):
        shutil.copyfile(source/scene[key], output/(key+'.bin'))
        scene[key] = key+'.bin'
    sky = decode_baked_hdr(environment, output/'environment.bin')
    pixels = np.fromfile(output/'environment.bin', dtype='<f4').reshape(sky['height'], sky['width'], 3)
    shift = (yaw % 360)/360*sky['width']
    integer, fraction = math.floor(shift), shift-math.floor(shift)
    rotated = (np.roll(pixels, integer, axis=1)*(1-fraction)+
               np.roll(pixels, integer+1, axis=1)*fraction)*light_scale
    if not np.isfinite(rotated).all():
        raise ValueError('Nonfinite scaled sky')
    rotated.astype('<f4').tofile(output/'environment.bin')
    solar = json.loads(Path(str(environment)+'.json').read_text())
    direction, irradiance = solar['sun_direction'], solar['sun_irradiance']
    angle = math.radians(yaw); c, s = math.cos(angle), math.sin(angle)
    scene['sun'] = dict(direction=[c*direction[0]-s*direction[2], direction[1],
                                    s*direction[0]+c*direction[2]],
                        irradiance=[v*light_scale for v in irradiance], radius=solar['sun_radius'])
    if (not all(math.isfinite(v) for v in scene['sun']['direction']) or
            abs(sum(v*v for v in scene['sun']['direction'])-1) > .002 or
            not all(math.isfinite(v) and v >= 0 for v in scene['sun']['irradiance']) or
            not 0 < scene['sun']['radius'] <= .1):
        raise ValueError('Invalid solar sidecar')
    scene['environment'] = dict(file='environment.bin', width=sky['width'], height=sky['height'])
    for key, value in [('hair_beta_m', beta_m), ('hair_beta_n', beta_n)]:
        if value is not None:
            scene['materials'][1][key] = value
    scene['exposure'] = exposure
    scene['source']['name'] += ' / stronger solar reflections'
    scene['source']['lighting'] = 'Yaw-rotated clear atmosphere + finite sun; both scaled equally'
    scene['source']['solar_relight'] = dict(yaw_degrees=yaw, light_scale=light_scale,
                                           original_materials=original_materials,
                                           hair_beta_m=scene['materials'][1]['hair_beta_m'],
                                           hair_beta_n=scene['materials'][1]['hair_beta_n'])
    (output/'scene.json').write_text(json.dumps(scene, indent=2)+'\n')
    inputs = [source/'scene.json', geometry_path, tangent_path,
              environment, Path(str(environment)+'.json'), Path(__file__),
              Path(__file__).parent/'create_pt_hair_sky.py']
    (output/'sunlit-import.json').write_text(json.dumps(dict(
        input_sha256={str(p): sha(p) for p in inputs},
        geometry_unchanged=True, tangents_unchanged=True, camera_unchanged=True,
        yaw_degrees=yaw, hdr_texel_shift=shift, light_scale=light_scale,
        source_sun=solar, sun=scene['sun'], source_sky=sky,
        original_materials=original_materials, materials=scene['materials'], exposure=exposure,
        limits='Static ribbons; neutral diffuse head; no additional area lights or native curves'), indent=2)+'\n')
    return output/'scene.json'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input', type=Path, default=Path('build/pt-hair-sky'))
    parser.add_argument('--output', type=Path, default=Path('build/pt-hair-sunlit'))
    parser.add_argument('--environment', type=Path, default=Path('build/pt-hair-sunlit/low-sun-environment.hdr'))
    parser.add_argument('--sky-yaw', type=float, default=135)
    parser.add_argument('--light-scale', type=float, default=10)
    parser.add_argument('--exposure', type=float, default=.6)
    parser.add_argument('--hair-beta-m', type=float)
    parser.add_argument('--hair-beta-n', type=float)
    args = parser.parse_args()
    for name in ('straight', 'wavy'):
        print(create(args.input/name, args.output/name, args.environment,
                     args.sky_yaw, args.light_scale, args.exposure, args.hair_beta_m, args.hair_beta_n))


if __name__ == '__main__':
    main()
