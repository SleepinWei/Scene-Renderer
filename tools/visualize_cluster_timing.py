#!/usr/bin/env python3
"""Plot measured GPU scope intervals, including gaps; no invented timings."""
import argparse
from collections import defaultdict
import json
from pathlib import Path
from PIL import Image, ImageDraw
from visualize_render_diagnostics import BG, font

COLORS = {'cluster': (48, 197, 165), 'geometry': (73, 151, 230),
          'prepass': (240, 115, 102), 'pyramid': (238, 194, 78), 'object test': (178, 136, 235)}


def phase(label):
    if label.startswith('clusters/'):
        return 'cluster'
    if label.startswith('geometry'):
        return 'geometry'
    if label == 'hiz/occluder-depth':
        return 'prepass'
    if label.startswith(('hiz/pyramid-', 'hiz/current-')):
        return 'pyramid'
    return 'object test'


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('capture', type=Path)
    args = parser.parse_args()
    data = json.loads((args.capture/'benchmark.json').read_text())
    rows = []
    reference = 'two_stage_separate' if 'two_stage_separate' in data else 'batch_hiz_4px' if 'batch_hiz_4px' in data else 'two_stage_original'
    for name in ('batch_4px', reference, 'two_stage_4px'):
        record = data[name]
        frames = defaultdict(list)
        for s in record['raw']:
            frames[s['round'], s['sample']].append(s)
        def interval(spans):
            return max(s['end_ms'] for s in spans)-min(s['start_ms'] for s in spans)
        key, spans = min(frames.items(), key=lambda item: abs(interval(item[1])-record['gpu_window_median_ms']))
        rows.append((name, record, key, spans))
    canvas = Image.new('RGB', (1400, 610), BG)
    draw = ImageDraw.Draw(canvas)
    draw.text((28, 17), 'Sponza: measured GPU critical scopes', font=font(29), fill='white')
    draw.text((28, 62), '960 x 540 / 4 px LOD / Metal M4 / native counter profiler / no UI', font=font(18), fill=(182, 198, 217))
    left, right, scale = 325, 1360, 280
    for tick in range(8):
        x = left+tick*.5*scale
        draw.line((x, 118, x, 475), fill=(52, 65, 81), width=1)
        draw.text((x-16, 92), f'{tick*.5:.1f}', font=font(16), fill=(180, 196, 210))
    draw.text((right-25, 92), 'ms', font=font(16), fill=(180, 196, 210))
    titles = {'batch_4px': 'Batch: no occlusion', 'batch_hiz_4px': 'Previous: depth prepass', 'two_stage_original': 'Previous: two-stage Hi-Z', 'two_stage_separate': 'Original: 16 compute encoders', 'two_stage_4px': 'Optimized: coarse Hi-Z / late'}
    selected = []
    for row, (name, record, key, spans) in enumerate(rows):
        y = 138+row*113
        draw.text((28, y-5), titles[name], font=font(20), fill='white')
        draw.text((28, y+29), f"Median window: {record['gpu_window_median_ms']:.3f} ms", font=font(16), fill=(182, 198, 217))
        draw.text((28, y+53), f"CPU render call: {record['cpu_render_median_ms']:.3f} ms", font=font(16), fill=(182, 198, 217))
        start = min(s['start_ms'] for s in spans)
        draw.rectangle((left, y, left+interval(spans)*scale, y+45), fill=(65, 75, 90))
        for span in spans:
            x0 = left+(span['start_ms']-start)*scale
            x1 = left+(span['end_ms']-start)*scale
            draw.rectangle((x0, y, max(x0+1, x1), y+45), fill=COLORS[phase(span['stage'])])
        draw.text((left, y+58), f'Representative raw frame: round {key[0]+1}, sample {key[1]+1}; window {interval(spans):.3f} ms', font=font(15), fill=(175, 193, 209))
        selected.append({'mode': name, 'round': key[0], 'sample': key[1], 'window_ms': interval(spans), 'spans': spans})
    x = 28
    for label, color in {**COLORS, 'scope gaps': (65, 75, 90)}.items():
        draw.rectangle((x, 492, x+20, 512), fill=color)
        draw.text((x+28, 489), 'Hi-Z / late' if label == 'pyramid' else label, font=font(17), fill=(210, 218, 230))
        x += 198
    draw.text((28, 539), 'Each row uses an actual sample nearest its mode median; bars preserve native start/end intervals.', font=font(17), fill=(182, 198, 217))
    draw.text((28, 571), 'Scope gaps can include unprofiled work; this chart does not prove whole-device idle time or frame FPS.', font=font(17), fill=(182, 198, 217))
    canvas.save(args.capture/'timing.png')
    (args.capture/'timing-frames.json').write_text(json.dumps(selected, indent=2)+'\n')


if __name__ == '__main__':
    main()
