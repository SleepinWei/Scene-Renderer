#!/usr/bin/env python3
"""Sequential CPU PT/SPPM convergence comparison against independent raw PT.

Times include camera tracing, photon emission/indexing and merging. SPPM
iterations are not equivalent to PT spp. No denoiser or exposure in errors.
"""
import argparse
import hashlib
import json
from pathlib import Path
import platform
import statistics
import subprocess
import time
from benchmark_pt_native import pfm, comparison


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--executable', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--reference', type=Path, help='Existing same-size .pfm prefix, independent PT seed')
    p.add_argument('--density-reference', type=Path, help='Optional independent dense photon estimate; biased diagnostic, not GT')
    p.add_argument('--radius', type=float, default=.08)
    p.add_argument('--fixture', default='pool-sunlit-underwater')
    p.add_argument('--size', default='64x48')
    p.add_argument('--threads', type=int, default=6)
    p.add_argument('--reference-samples', type=int, default=32768)
    p.add_argument('--runs', type=int, default=3)
    args = p.parse_args()
    if args.runs < 1:
        p.error('Runs must be positive')
    args.output.mkdir(parents=True, exist_ok=True)
    exe = args.executable.resolve()
    base = [str(exe), '--backend', 'CPU', '--fixture', args.fixture,
            '--size', args.size, '--bounces', '8', '--threads', str(args.threads)]

    def render(prefix, samples, extra, seed=1):
        command = base+['--samples', str(samples), '--seed', str(seed), '--output', str(prefix)]+extra
        start = time.monotonic()
        result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, check=True)
        wall = time.monotonic()-start
        Path(str(prefix)+'.log').write_text(result.stdout)
        data = json.loads(Path(str(prefix)+'.json').read_text())
        return dict(command=command, process_seconds=wall,
                    algorithm_seconds=data['render_seconds']+data['photon_seconds']+data['training_seconds'],
                    camera_and_merge_seconds=data['render_seconds'], photon_build_seconds=data['photon_seconds'],
                    camera_rays=data['rays'], photon_rays=data['photon_rays'], sppm=data.get('sppm_statistics'))

    ref = args.reference or args.output/'reference'
    if not args.reference:
        render(ref, args.reference_samples, [], seed=73)
    rw, rh, target = pfm(Path(str(ref)+'.pfm'))
    reference_report = json.loads(Path(str(ref)+'.json').read_text())
    if reference_report.get('sppm') or reference_report['photon_mapping'] or reference_report['seed'] == 1:
        raise ValueError('Reference must be independent ordinary PT')
    if reference_report['scene_package']['source']['name'] != args.fixture:
        raise ValueError('Reference fixture differs')
    density_target = None
    if args.density_reference:
        dw, dh, density_target = pfm(Path(str(args.density_reference)+'.pfm'))
        density_report = json.loads(Path(str(args.density_reference)+'.json').read_text())
        if (dw, dh) != (rw, rh) or density_report['seed'] == 1 or density_report['scene_package']['source']['name'] != args.fixture:
            raise ValueError('Density reference must use independent seed and matching scene/dimensions')
    report = dict(executable_sha256=hashlib.sha256(exe.read_bytes()).hexdigest(),
                  machine=platform.machine(), platform=platform.platform(), fixture=args.fixture,
                  reference=str(ref.resolve()), reference_samples=reference_report['sample_budget'],
                  reference_seed=reference_report['seed'], size=[rw, rh], threads=args.threads,
                  scope='CPU wall time, sequential processes; finite-spp PT reference, raw linear RGB; SPPM has finite-budget density bias; not a GPU speed comparison',
                  results=[])
    if density_target is not None:
        report['density_reference'] = dict(prefix=str(args.density_reference.resolve()),
                                           scope='Independent finite-density biased estimate; not ground truth',
                                           seed=density_report['seed'], photon_paths=density_report['photon_paths'], radius=density_report['photon_radius'])
    cases = [('pt-128', 128, []), ('pt-512', 512, []), ('pt-2048', 2048, []),
             ('sppm-16', 16, ['--sppm', '--photon-paths', '20000', '--photon-radius', str(args.radius)]),
             ('sppm-64', 64, ['--sppm', '--photon-paths', '20000', '--photon-radius', str(args.radius)]),
             ('sppm-256', 256, ['--sppm', '--photon-paths', '20000', '--photon-radius', str(args.radius)]),
             ('fixed-photons', 64, ['--photons', '--photon-paths', '1280000', '--photon-radius', str(args.radius)])]
    for name, samples, extra in cases:
        runs = []
        for run in range(args.runs):
            prefix = args.output/f'{name}-{run}'
            result = render(prefix, samples, extra)
            w, h, values = pfm(Path(str(prefix)+'.pfm'))
            if (w, h) != (rw, rh):
                raise ValueError('Reference dimensions differ')
            error = comparison(values, target)
            result['error'] = error
            if density_target is not None:
                result['density_reference_error'] = comparison(values, density_target)
            result['film_sha256'] = hashlib.sha256(Path(str(prefix)+'.pfm').read_bytes()).hexdigest()
            runs.append(result)
        report['results'].append(dict(name=name, samples_or_iterations=samples, runs=runs,
                                      median_algorithm_seconds=statistics.median(r['algorithm_seconds'] for r in runs),
                                      raw_nrmse=runs[0]['error']['nrmse'], raw_relative_l1=runs[0]['error']['relative_l1'],
                                      deterministic_across_runs=all(r['film_sha256']==runs[0]['film_sha256'] for r in runs)))
        print(name, report['results'][-1]['median_algorithm_seconds'], report['results'][-1]['raw_nrmse'], flush=True)
        (args.output/'summary.json').write_text(json.dumps(report, indent=2)+'\n')
    if hashlib.sha256(exe.read_bytes()).hexdigest() != report['executable_sha256']:
        raise RuntimeError('Executable changed during benchmark; rebuild first and rerun')


if __name__ == '__main__':
    main()
