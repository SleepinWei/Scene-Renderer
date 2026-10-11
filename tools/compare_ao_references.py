#!/usr/bin/env python3
"""Compare float32 AO benchmark captures on a shared raster/CPU match mask."""
import argparse
import array
import json
import math
from pathlib import Path
import sys


def read_pfm(path):
    with Path(path).open('rb') as stream:
        if stream.readline().strip() != b'Pf':
            raise ValueError(f'{path}: expected grayscale PFM')
        width, height = map(int, stream.readline().split())
        scale = float(stream.readline())
        if width <= 0 or height <= 0 or abs(scale) != 1:
            raise ValueError(f'{path}: unsupported dimensions or scale')
        payload = stream.read()
    if len(payload) != width * height * 4:
        raise ValueError(f'{path}: truncated PFM')
    values = array.array('f')
    values.frombytes(payload)
    if (scale < 0) != (sys.byteorder == 'little'):
        values.byteswap()
    values = array.array('f', (values[y * width + x] for y in range(height - 1, -1, -1) for x in range(width)))
    if not all(math.isfinite(v) for v in values):
        raise ValueError(f'{path}: nonfinite PFM')
    return width, height, values


def statistics(values, reference, valid):
    errors = [a - b for a, b, flag in zip(values, reference, valid) if flag]
    if not errors:
        raise ValueError('No common matched pixels')
    return {'pixels': len(errors), 'rmse': math.sqrt(sum(e * e for e in errors) / len(errors)),
            'mae': sum(abs(e) for e in errors) / len(errors),
            'mean_signed_error': sum(errors) / len(errors), 'max_absolute_error': max(abs(e) for e in errors)}


def compare(baseline, refined):
    first = json.loads((baseline / 'summary.json').read_text())
    second = json.loads((refined / 'summary.json').read_text())
    for key in ('width', 'height', 'radius', 'bias', 'power', 'slices', 'steps_per_side', 'definition'):
        if first[key] != second[key]:
            raise ValueError(f'Incompatible benchmark setting: {key}')
    report = {'baseline_spp': first['samples_per_pixel'], 'refined_spp': second['samples_per_pixel'],
              'baseline_seed': first['seed'], 'refined_seed': second['seed'],
              'note': 'Reference difference is a sampling convergence diagnostic, not a confidence interval; score frame 0 on the common match mask.',
              'cases': {}}
    for name in sorted(first['cases'].keys() & second['cases'].keys()):
        def load(directory, suffix):
            width, height, values = read_pfm(directory / (name + suffix + '.pfm'))
            if (width, height) != (first['width'], first['height']):
                raise ValueError('PFM/report dimensions differ')
            return values
        ref_a, ref_b = load(baseline, '-reference'), load(refined, '-reference')
        mask_a, mask_b = load(baseline, '-match-mask'), load(refined, '-match-mask')
        valid = [a > .5 and b > .5 for a, b in zip(mask_a, mask_b)]
        case = {'reference_difference': statistics(ref_a, ref_b, valid), 'modes': {}}
        for mode in first['cases'][name]['frames'][0]['modes']:
            ao = load(baseline, '-' + mode)
            case['modes'][mode] = {'against_baseline': statistics(ao, ref_a, valid),
                                   'against_refined': statistics(ao, ref_b, valid)}
        report['cases'][name] = case
    if not report['cases']:
        raise ValueError('No common benchmark cases')
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('baseline', type=Path)
    parser.add_argument('refined', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    report = compare(args.baseline, args.refined)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + '\n')
    for name, case in report['cases'].items():
        print(name, 'reference difference RMSE', case['reference_difference']['rmse'])
        for mode, values in case['modes'].items():
            print(' ', mode, 'refined-reference RMSE', values['against_refined']['rmse'])


if __name__ == '__main__':
    main()
