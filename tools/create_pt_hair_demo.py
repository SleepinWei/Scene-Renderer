#!/usr/bin/env python3
"""Standalone dense static-hair path tracing demo (stdlib; no downloaded assets)."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import random
import subprocess

from pt_groom import Package, ribbons


VIEWS = {
    'front': ([0, 2.45, 6.9], [0, 1.95, 0]),
    'three-quarter': ([2.65, 2.65, 6.5], [0, 1.95, 0]),
    'back': ([-2.4, 2.6, -6.5], [0, 1.95, 0]),
}


def dense_groom(count=40000, seed=19, radius=.0015):
    """Shared guide waves within locks, with strand-level length/shape variation."""
    if not 1 <= count <= 100000 or not math.isfinite(radius) or not .0003 <= radius <= .004:
        raise ValueError('Expected 1..100000 strands and radius 0.0003..0.004')
    rng = random.Random(seed)
    locks = [dict(phase=rng.uniform(0, 2*math.pi), fall=rng.uniform(1.35, 1.6),
                  wave=rng.uniform(.035, .075)) for _ in range(48)]
    curves = []
    for _ in range(count):
        theta = math.acos(1-rng.random()*(1-math.cos(1.16)))
        phi = rng.uniform(0, 2*math.pi)
        lock_id = min(47, int(phi/(2*math.pi)*48))
        lock = locks[lock_id]
        sign = 1 if math.cos(phi) >= 0 else -1
        target_phi = 0 if sign > 0 else math.pi
        delta = math.atan2(math.sin(target_phi-phi), math.cos(target_phi-phi))
        front = max(0, math.sin(phi))
        phase = lock['phase']+rng.uniform(-.2, .2)
        fall = lock['fall']+rng.uniform(-.09, .09)
        layer = rng.uniform(.006, .025)
        drift = rng.uniform(-.015, .015)
        points = []
        for j in range(35):
            u = j/34
            cap = min(u/.43, 1)
            turn = cap*cap*(3-2*cap)
            angle = theta+(1.52-theta)*cap
            azimuth = phi+delta*front*.85*turn
            clearance = .014+layer*turn+.018*math.sin(math.pi*cap*.5)
            p = [(.56+clearance)*math.sin(angle)*math.cos(azimuth),
                 2.25+(.8+clearance)*math.cos(angle),
                 (.48+clearance)*math.sin(angle)*math.sin(azimuth)]
            tail = max(0, (u-.43)/.57)
            p[1] -= fall*tail
            p[0] += sign*.17*tail*tail+lock['wave']*math.sin(phase+tail*8)*tail+drift*tail
            p[2] += .065*math.sin(phase*.7+tail*9)*tail
            points.append(p)
        curves.append(dict(points=points, radius=radius*rng.uniform(.8, 1.2),
                           scalp_theta=theta, scalp_phi=phi, lock=lock_id))
    return curves


def create_demo(output, count=40000, seed=19, radius=.0015, view='three-quarter'):
    if view not in VIEWS:
        raise ValueError('Unknown reference view')
    eye, target = VIEWS[view]
    package = Package(output, eye, target, 29)
    skin = package.material(base_color=[.73, .53, .4], roughness=.7)
    cloth = package.material(bsdf_model='cloth', base_color=[.10, .12, .15],
                             roughness=.85, sheen_weight=.2)
    white = package.material(base_color=[.7, .72, .66], roughness=.55)
    iris = package.material(base_color=[.07, .11, .13], roughness=.3)
    lip = package.material(base_color=[.5, .23, .17], roughness=.65)
    hair = package.material(bsdf_model='hair', base_color=[.88, .72, .46],
                            hair_beta_m=.24, hair_beta_n=.3, hair_tilt=.035, two_sided=True)
    package.ellipsoid([0, 2.25, 0], [.56, .8, .48], skin)
    package.ellipsoid([0, 1.35, -.015], [.22, .48, .22], skin)
    package.ellipsoid([0, .75, -.08], [.88, .62, .4], cloth)
    package.ellipsoid([0, 2.18, .47], [.095, .14, .11], skin)
    package.ellipsoid([0, 1.98, .45], [.15, .022, .024], lip)
    for sign in (-1, 1):
        package.ellipsoid([sign*.20, 2.43, .427], [.095, .038, .047], white)
        package.ellipsoid([sign*.20, 2.43, .471], [.029, .027, .017], iris)
        package.ellipsoid([sign*.55, 2.22, 0], [.095, .16, .06], skin)
    curves = dense_groom(count, seed, radius)
    package.store(ribbons(curves, package.eye), hair)
    package.studio()
    return package.save(dict(name='Dense wavy hair standalone demo', strands=count,
                             points_per_strand=35, hair_triangles=count*68,
                             guide_locks=48, seed=seed, root_radius=radius, view=view,
                             description='Original mannequin; static layered scalp groom and shared wavy lock guides. Fixed reference-camera ribbons, no dynamics or native curves.'), curves)


def render(scene, renderer, backend, size, samples, bounces, denoiser=None):
    """Render with the current app; an optional separate binary only filters PFM."""
    output = scene.parent/'render'
    command = [str(renderer.resolve()), '--path-trace' if backend == 'CPU' else '--path-trace-gpu']
    if backend != 'CPU':
        command += ['--backend', backend]
    command += ['--pt-scene-file', str(scene.resolve()), '--pt-size', f'{size}x{size}',
                '--pt-samples', str(samples), '--pt-bounces', str(bounces), '--pt-fixed',
                '--pt-output', str(output.resolve())]
    with (scene.parent/'render.txt').open('w') as log:
        subprocess.run(command, check=True, stdout=log, stderr=subprocess.STDOUT)
    if denoiser:
        with (scene.parent/'denoise.txt').open('w') as log:
            subprocess.run([str(denoiser.resolve()), '--path-trace', '--pt-denoise-input',
                            str(output.with_suffix('.pfm').resolve()), '--pt-denoise-device', 'cpu',
                            '--pt-output', str((scene.parent/'filtered').resolve())],
                           check=True, stdout=log, stderr=subprocess.STDOUT)
    paths = [scene.parent/p for p in ('scene.json', 'groom.json', 'geometry.bin', 'tangents.bin', 'environment.bin')]
    paths += [renderer]
    paths += [Path(__file__), Path(__file__).parent/'pt_groom.py',
              Path(__file__).parent/'create_pt_material_fixture.py']
    if denoiser:
        paths.append(denoiser)
    paths += list(scene.parent.glob('render*'))+list(scene.parent.glob('filtered*'))
    if denoiser:
        paths.append(scene.parent/'denoise.txt')
    report = json.loads(output.with_suffix('.json').read_text())
    if report['non_finite_samples'] or [report[k] for k in ('width', 'height', 'samples', 'max_depth')] != [size, size, samples, bounces]:
        raise RuntimeError('Render report has nonfinite samples or unexpected settings')
    (scene.parent/'demo-validation.json').write_text(json.dumps(dict(
        generator='tools/create_pt_hair_demo.py', backend=backend,
        width=size, height=size, samples=samples, max_depth=bounces, seed=1,
        sha256={str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in paths if p.is_file()},
        report=report,
        filter_report=json.loads((scene.parent/'filtered.json').read_text()) if denoiser else None), indent=2)+'\n')
    return scene.parent/('filtered-denoised.png' if denoiser else 'render.png')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=Path('build/pt-hair-demo'))
    parser.add_argument('--strands', type=int, default=40000)
    parser.add_argument('--seed', type=int, default=19)
    parser.add_argument('--radius', type=float, default=.0015)
    parser.add_argument('--view', choices=VIEWS, default='three-quarter')
    parser.add_argument('--render', choices=['CPU', 'Metal', 'Vulkan'])
    parser.add_argument('--renderer', type=Path, default=Path('build/Scene-Renderer'))
    parser.add_argument('--denoiser', type=Path, help='Optional OIDN-enabled executable for offline CPU filtering')
    parser.add_argument('--size', type=int, default=768)
    parser.add_argument('--samples', type=int, default=256)
    parser.add_argument('--bounces', type=int, default=12)
    args = parser.parse_args()
    if not 1 <= args.strands <= 100000 or not math.isfinite(args.radius) or not .0003 <= args.radius <= .004:
        parser.error('Use 1..100000 strands and radius 0.0003..0.004')
    if not 16 <= args.size <= 4096 or not 1 <= args.samples <= 65536 or not 1 <= args.bounces <= 64:
        parser.error('Render size, samples or bounces outside demo budget')
    if args.render and (not args.renderer.is_file() or (args.denoiser and not args.denoiser.is_file())):
        parser.error('Renderer or requested denoiser executable is missing')
    scene = create_demo(args.output, args.strands, args.seed, args.radius, args.view)
    print(scene, flush=True)
    if args.render:
        print(render(scene, args.renderer, args.render, args.size, args.samples, args.bounces, args.denoiser), flush=True)


if __name__ == '__main__':
    main()
