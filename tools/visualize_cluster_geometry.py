#!/usr/bin/env python3
"""Arrange actual native cluster captures; never generate geometry or shading."""
import argparse
import json
from pathlib import Path
import numpy as np
from PIL import Image, ImageDraw, ImageFont


def main():
    p = argparse.ArgumentParser()
    p.add_argument('capture', type=Path)
    a = p.parse_args()
    r = json.loads((a.capture / 'validation.json').read_text())['sponza']
    cuts = {int(x['pixel_error']): x for x in r['cuts']}
    names = ['sponza-full.png', 'sponza-clusters-4px.png', 'sponza-wireframe-0px.png', 'sponza-wireframe-4px.png']
    images = [Image.open(a.capture / x).convert('RGB') for x in names]
    assert len({im.size for im in images}) == 1
    w, h = images[0].size
    font = lambda n: ImageFont.truetype('/System/Library/Fonts/Supplemental/Arial.ttf', n)
    im = Image.new('RGB', (w*2+72, h*2+276), '#101720')
    d = ImageDraw.Draw(im)
    d.text((24, 16), 'Nanite-inspired hierarchical cluster geometry / Sponza', font=font(30), fill='white')
    d.text((24, 57), 'Actual Metal PBR + indexed indirect output / same camera and exposure / TAA, AO and DDGI off', font=font(19), fill='#aebbc9')
    titles = ['Full source geometry', 'GPU cluster LOD / 4 px budget',
              f"GPU wireframe / 0 px: {cuts[0]['visible_triangles']:,} cluster triangles",
              f"GPU wireframe / 4 px: {cuts[4]['visible_triangles']:,} cluster triangles"]
    for i, (src, title) in enumerate(zip(images, titles)):
        x = 24+(i%2)*(w+24)
        y = 103+(i//2)*(h+51)
        d.text((x, y), title, font=font(24), fill='#ebf1f7')
        im.paste(src, (x, y+35))
    y = h*2+209
    d.text((24, y), 'Wireframe is a separate native diagnostic; 0 px disables simplification while keeping cluster frustum culling.', font=font(19), fill='#56dfe9')
    d.text((24, y+27), 'Group borders are locked during simplification. Pixel error is an estimate; shadows and auxiliary views use source geometry.', font=font(18), fill='#aebbc9')
    im.save(a.capture / 'overview.png')
    full = np.asarray(images[0], dtype=np.int16)
    metrics = {}
    for budget in (0, 1, 4, 16):
        test = np.asarray(Image.open(a.capture / f'sponza-clusters-{budget}px.png').convert('RGB'), dtype=np.int16)
        diff = abs(full-test)
        metrics[str(budget)] = {'changed_display_pixels': int(np.any(diff != 0, axis=2).sum()),
                                'max_display_code_difference': int(diff.max()),
                                'mean_absolute_display_code_difference': float(diff.mean())}
    (a.capture / 'display-comparison.json').write_text(json.dumps({'scope': '8-bit displayed images; not linear-HDR, Hausdorff or transport error', 'budgets_px': metrics}, indent=2)+'\n')
    print(json.dumps(metrics, indent=2))


if __name__ == '__main__':
    main()
