#!/usr/bin/env python3
"""Capture joint cloud/grass/lake frames and native timelines in a desktop GPU session."""
import argparse
import collections
import hashlib
import json
import os
from pathlib import Path
import subprocess
from analyze_runtime_trace import summarize


def validate_cloud_dispatches(labels, frames):
    counts = {name: labels.get(label, 0) for name, label in {
        "density": "Cloud XYZ density bake/compute",
        "distance": "Cloud conservative signed distance bake/compute",
        "light": "Cloud voxel light cache/compute"}.items()}
    if counts != {"density": 1, "distance": 33, "light": 1}:
        raise RuntimeError(f"Unexpected cloud cache dispatch counts: {counts}")
    if labels.get("Cloud GPU tile classification/compute", 0) != frames:
        raise RuntimeError("Cloud classification missing from submitted frames")
    return counts

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--executable', type=Path, default=Path('build/Scene-Renderer'))
    parser.add_argument('--backend', choices=['Metal', 'Vulkan'], default='Metal')
    parser.add_argument('--output', type=Path, default=Path('build/environment/joint'))
    parser.add_argument('--frames', type=int, default=90)
    parser.add_argument('--warmup', type=int, default=30)
    parser.add_argument('--repeats', type=int, default=2)
    args = parser.parse_args()
    if args.frames <= args.warmup or args.warmup < 0 or args.repeats < 1:
        parser.error('Require frames > warmup >= 0 and repeats >= 1')
    root = args.output.resolve()
    root.mkdir(parents=True, exist_ok=True)
    executable = args.executable.resolve()
    digest = hashlib.sha256(executable.read_bytes()).hexdigest()
    environment = dict(os.environ, MTL_DEBUG_LAYER='0', MTL_SHADER_VALIDATION='0', SCENERENDERER_GPU_PROFILE='1')
    cases = [('fixed-midpoint', 'mountain-lake', ['--environment-ticks', '180'])]
    cases += [(f'lake-live-{i+1}', 'mountain-lake', []) for i in range(args.repeats)]
    cases += [('ground-live', 'mountain-lake-ground', [])]
    runs = []
    for name, scene, extra in cases:
        trace, checkpoint, screenshot = [root / f'{name}.{ext}' for ext in ('trace.json', 'checkpoint.json', 'ppm')]
        for path in (trace, checkpoint, screenshot):
            path.unlink(missing_ok=True)
        command = [str(executable), '--backend', args.backend, '--classic', scene, '--hidden',
                   '--size', '1280x720', '--frames', str(args.frames), '--environment-clouds',
                   '--environment-cloud-coverage', '1', '--environment-wind', '-20', '8',
                   '--environment-sun', '45', '20', '--environment-save', str(checkpoint),
                   '--screenshot', str(screenshot), *extra]
        result = subprocess.run(command, env=dict(environment, SCENERENDERER_RUNTIME_TRACE_OUTPUT=str(trace)),
                                capture_output=True, text=True, timeout=180)
        (root / f'{name}.log').write_text(result.stdout + result.stderr)
        if result.returncode:
            raise RuntimeError(f'{name}: exit {result.returncode}; see log')
        state = json.loads(checkpoint.read_text())
        assert state['schema_version'] == 5 and state['applied_sequence'] == 3 and not state['events']
        assert state['coverage_target'] == 1 and 0 <= state['cloud_coverage'] <= 1 and screenshot.exists()
        raw = json.loads(trace.read_text())
        labels = collections.Counter(e['name'] for e in raw['traceEvents'] if e['gpu'])
        if not labels and args.backend == "Metal":
            raise RuntimeError("Expected Metal GPU pass profiling data")
        cache_dispatches = validate_cloud_dispatches(labels, args.frames) if labels else None
        run = dict(name=name, scene=scene, argv=command[1:], checkpoint=state,
                   cloud_cache_dispatches=cache_dispatches,
                   summary=summarize(trace, args.warmup), gpu_labels=dict(labels),
                   screenshot=str(screenshot), trace=str(trace))
        runs.append(run)
        print(f'{name}: tick {state["tick"]}, coverage {state["cloud_coverage"]:.4f}, '
              f'GPU surface completion {run["summary"]["gpu_surface_completion_period"]}', flush=True)
    assert hashlib.sha256(executable.read_bytes()).hexdigest() == digest
    report = dict(backend=args.backend, executable_sha256=digest, passed=True, runs=runs,
                  gpu_pass_profiling_available=all(bool(r["gpu_labels"]) for r in runs),
                  scope='Joint main-view cloud/grass/lake workload; distant grass can be culled. '
                        'Surface completion intervals measure native throughput, not display latency. '
                        'Warmup discards startup; no global steady-state or budget claim. '
                        'Clouds are absent from water reflections and PT sky capture.')
    (root / 'validation.json').write_text(json.dumps(report, indent=2) + '\n')


if __name__ == '__main__':
    main()
