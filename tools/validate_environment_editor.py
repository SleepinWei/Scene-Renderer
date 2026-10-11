#!/usr/bin/env python3
"""Exercise the native editor checkpoint boundary; requires a desktop GPU session."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--executable', type=Path, default=Path('build/Scene-Renderer'))
    parser.add_argument('--backend', choices=['Metal', 'Vulkan'], default='Metal')
    parser.add_argument('--spatial-terrain', action='store_true', help='Also replay Mountain Lake spatial water (requires prepared assets)')
    parser.add_argument('--output', type=Path, default=Path('build/environment/editor-validation'))
    args = parser.parse_args()
    root = args.output.resolve()
    root.mkdir(parents=True, exist_ok=True)
    executable = args.executable.resolve()
    runs = []

    def run(name, flags, expected=0, scene='sky'):
        command = [str(executable), '--backend', args.backend, '--classic', scene,
                   '--hidden', '--size', '800x450', '--frames', '12', *map(str, flags)]
        result = subprocess.run(command, capture_output=True, text=True, timeout=90)
        (root / f'{name}.log').write_text(result.stdout + result.stderr)
        if result.returncode != expected:
            raise RuntimeError(f'{name}: exit {result.returncode}, expected {expected}; see log')
        runs.append({'name': name, 'argv': command[1:], 'exit_code': result.returncode})

    first, second = root / 'first.json', root / 'resumed.json'
    # Remove old artifacts so a success cannot accidentally reuse a stale checkpoint.
    for path in (first, second):
        path.unlink(missing_ok=True)
    run('capture', ['--environment-ticks', '120', '--environment-sun', '30', '45',
                    '--environment-wind', '-20', '8', '--environment-cloud-coverage', '1', '--environment-save', first, '--screenshot', root / 'sky.ppm'])
    initial = json.loads(first.read_text())
    assert initial['scope'] == 'cpu-environment-spatial-water'
    assert initial['tick'] == 120 and initial['initial_seconds'] == 0
    assert initial['solar'] == {'elevation_degrees': 30, 'azimuth_degrees': 45}
    assert initial['schema_version'] == 5 and initial['wind_target'] == {'x': -20, 'z': 8}
    assert 0 < initial['wave_elapsed'] < 8
    assert initial['events'] == [] and initial['applied_sequence'] == 3
    run('resume', ['--environment-load', first, '--environment-ticks', '1',
                   '--environment-save', second])
    resumed = json.loads(second.read_text())
    assert resumed['tick'] == 121 and resumed['solar'] == initial['solar']
    assert resumed['events'] == [] and resumed['applied_sequence'] == 3
    # Restoring halfway through a wave blend must reproduce a direct CPU timeline.
    continuous, recovered = root / 'continuous.json', root / 'recovered.json'
    for path in (continuous, recovered):
        path.unlink(missing_ok=True)
    run('continuous-wind', ['--environment-ticks', '240', '--environment-sun', '30', '45',
                            '--environment-wind', '-20', '8', '--environment-cloud-coverage', '1', '--environment-save', continuous])
    run('recovered-wind', ['--environment-load', first, '--environment-ticks', '120',
                           '--environment-save', recovered])
    assert json.loads(continuous.read_text()) == json.loads(recovered.read_text())
    assert initial['coverage_target'] == 1 and 0 < initial['coverage_elapsed'] < 6
    assert initial['cloud_coverage'] < resumed['cloud_coverage'] < 1
    run('clear-coverage', ['--environment-cloud-coverage', '0', '--environment-ticks', '360', '--environment-save', root / 'clear.json'])
    assert json.loads((root / 'clear.json').read_text())['cloud_coverage'] == 0
    run('invalid-coverage', ['--environment-cloud-coverage', 'nan'], 1)
    run('invalid-wind', ['--environment-wind', 'nan', '8'], 1)
    corrupt = dict(initial, scene_key='classic:another-scene')
    bad = root / 'wrong-scene.json'
    bad.write_text(json.dumps(corrupt))
    rejected = root / 'rejected.json'
    rejected.unlink(missing_ok=True)
    run('wrong-scene', ['--environment-load', bad, '--environment-save', rejected], 1)
    assert not rejected.exists()
    assert 'another scene' in (root / 'wrong-scene.log').read_text()
    run('conflicting-time', ['--environment', '--time', '8'], 1)
    run('invalid-sun', ['--environment-sun', '30junk', '45'], 1)
    rainFirst,rainDirect,rainReplay=root/'rain-first.json',root/'rain-direct.json',root/'rain-replay.json'
    for path in (rainFirst,rainDirect,rainReplay):path.unlink(missing_ok=True)
    run('rain-capture',['--environment-rain','36','--environment-ticks','120','--environment-save',rainFirst])
    run('rain-direct',['--environment-rain','36','--environment-ticks','240','--environment-save',rainDirect])
    run('rain-replay',['--environment-load',rainFirst,'--environment-ticks','120','--environment-save',rainReplay])
    assert json.loads(rainDirect.read_text())==json.loads(rainReplay.read_text())
    assert json.loads(rainFirst.read_text())['rain_m_per_s']==36/3600000
    run('invalid-rain',['--environment-rain','nan'],1)
    spatial_replay = None
    if args.spatial_terrain:
        spatial_first, spatial_direct, spatial_replay_path = [root / (name + '.json') for name in ('spatial-first', 'spatial-direct', 'spatial-replay')]
        for path in (spatial_first, spatial_direct, spatial_replay_path):
            path.unlink(missing_ok=True)
        run('spatial-capture', ['--environment-rain', '50', '--environment-ticks', '1800', '--environment-save', spatial_first], scene='mountain-lake')
        run('spatial-direct', ['--environment-rain', '50', '--environment-ticks', '3600', '--environment-save', spatial_direct, '--screenshot', root / 'spatial-lake.ppm'], scene='mountain-lake')
        run('spatial-replay', ['--environment-load', spatial_first, '--environment-ticks', '1800', '--environment-save', spatial_replay_path], scene='mountain-lake')
        direct = json.loads(spatial_direct.read_text())
        assert direct == json.loads(spatial_replay_path.read_text())
        assert direct['land_grid']['width'] == 16 and len(direct['land_storage_m']) == 256
        area = direct['land_config']['area_m2']
        depths = [s for s, a in zip(direct['land_storage_m'], direct['land_grid']['area_m2']) if a > 0]
        assert min(depths) >= 0 and max(depths) <= direct['land_config']['capacity_m'] + 1e-15
        assert max(depths) - min(depths) > .0001 and direct['land_transferred_m3'] > 0
        w, c = direct['land'], direct['land_config']
        residual = (c['initial_storage_m'] + w['rain_m'] - w['storage_m'] - w['infiltration_m'] - w['evaporation_m'] - w['outflow_m']) * area
        assert abs(residual) < 1e-4
        run('uniform-terrain', ['--environment-uniform-ground', '--environment-rain', '50', '--environment-ticks', '3600', '--environment-save', root / 'uniform.json'], scene='mountain-lake')
        assert json.loads((root / 'uniform.json').read_text())['land_grid'] is None
        spatial_replay = dict(exact=True, minimum_mm=min(depths)*1000, maximum_mm=max(depths)*1000, balance_residual_m3=residual)
    report = {'backend': args.backend, 'scope': 'native editor CPU environment integration',
              'executable_sha256': hashlib.sha256(executable.read_bytes()).hexdigest(),
              'checkpoint': initial, 'resumed_checkpoint': resumed, 'runs': runs,
              'spatial_replay': spatial_replay, 'wind_replay_exact': True, 'coverage_replay_exact': True, 'rain_replay_exact': True, 'passed': True}
    (root / 'validation.json').write_text(json.dumps(report, indent=2) + '\n')
    print(f'{args.backend}: capture, wind/coverage replay, clear endpoint, resume, wrong-scene rejection and CLI validation passed')


if __name__ == '__main__':
    main()
