#!/usr/bin/env python3
"""Show captured full/LOD beauty and actual CPU primary-ray wireframe diagnostics."""
import argparse
import json
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

FONT = '/System/Library/Fonts/Supplemental/Arial.ttf'
BG = '#111923'
FG = '#e8eef5'
CYAN = '#40e0ef'


def font(size):
    return ImageFont.truetype(FONT, size)


def sheet(size, title, subtitle):
    im = Image.new('RGB', size, BG)
    d = ImageDraw.Draw(im)
    d.text((24, 16), title, font=font(28), fill=FG)
    d.text((24, 54), subtitle, font=font(17), fill='#aebbc9')
    return im, d


def main():
    p = argparse.ArgumentParser()
    p.add_argument('capture', type=Path)
    p.add_argument('wireframe', type=Path)
    a = p.parse_args()
    root = a.capture
    report = json.loads((root / 'sponza-lod-world-lod.json').read_text())
    before = report['source_unique_triangles']
    after = report['selected_unique_triangles']
    full = Image.open(root / 'sponza-full.png').convert('RGB')
    low = Image.open(root / 'sponza-lod.png').convert('RGB')
    wire = [Image.open(str(a.wireframe) + f'-{v}-wireframe.png').convert('RGB') for v in ('full', 'lod')]
    assert full.size == low.size and wire[0].size == wire[1].size
    # A compact overview retains the captured image's complete field of view.
    im, d = sheet((1488, 970), 'Sponza / PT world LOD',
                  'Same camera | 0.10 m vertex displacement budget | 0.05 UV budget | target ratio 0.50')
    for i, (title, beauty, geom) in enumerate(zip((f'FULL / {before:,} triangles', f'LOD / {after:,} triangles'), (full, low), wire)):
        x = 24 + i * 732
        d.text((x, 88), title, font=font(22), fill=FG)
        im.paste(beauty.resize((708, 398), Image.Resampling.NEAREST), (x, 124))
        d.text((x, 538), 'Actual PT mesh / primary-ray wireframe', font=font(18), fill=FG)
        im.paste(geom.resize((708, 398), Image.Resampling.LANCZOS), (x, 568))
    # Footer belongs in a dedicated margin, not on top of the geometry.
    final = Image.new('RGB', (1488, 1068), BG)
    final.paste(im, (0, 0))
    fd = ImageDraw.Draw(final)
    fd.text((24, 982), 'CYAN = meshes that were simplified (both panels). GRAY = meshes retained.', font=font(18), fill=CYAN)
    fd.text((24, 1009), 'Wireframe uses CPU PT visibility with 2 x 2 primary samples; no lighting transport or material shading.', font=font(16), fill='#aebbc9')
    fd.text((24, 1038), 'Beauty: Metal native PT / 64 spp / 16 bounces / same exposure / no denoising', font=font(17), fill='#ccd5df')
    final.save(root / 'lod-overview.png')
    # Fixed source-pixel ROIs; enlarge geometry rather than the noisy beauty.
    rois = [('Left arch / column', (.0, .0, .375, 1/3)),
            ('Right arch / cornice', (.625, .0, 1., 1/3)),
            ('Courtyard / distant arches', (.32, .34, .68, .66))]
    detail, dd = sheet((1488, 1320), 'LOD geometry / detail',
                       'Full geometry on the left; simplified geometry on the right. Same crop and scale.')
    for row, (title, roi) in enumerate(rois):
        y = 92 + row * 400
        dd.text((24, y), title, font=font(21), fill=FG)
        for i, src in enumerate(wire):
            w, h = src.size
            box = tuple(round(v * (w if j % 2 == 0 else h)) for j, v in enumerate(roi))
            detail.paste(src.crop(box).resize((708, 354), Image.Resampling.NEAREST), (24 + i * 732, y + 34))
    dd.text((24, 1295), 'No Monte Carlo noise. Visible edges come from the actual full and selected LOD triangles.', font=font(17), fill=CYAN)
    detail.save(root / 'lod-wireframe-detail.png')
    meshes = sorted(report['meshes'], key=lambda m: m['levels'][0]['triangles'], reverse=True)
    chart, cd = sheet((1360, 1090), 'Sponza / triangle reduction by mesh',
                      f'{before:,} -> {after:,} triangles (-{(1-after/before)*100:.1f}%) | mesh IDs from the captured report')
    max_count = max(m['levels'][0]['triangles'] for m in meshes)
    changed = 0
    for row, mesh in enumerate(meshes):
        old = mesh['levels'][0]['triangles']
        new = mesh['levels'][mesh['selected_level']]['triangles']
        changed += new < old
        y = 104 + row * 36
        cd.text((24, y), f"Mesh {mesh['source_id']}", font=font(16), fill=FG)
        x = 142
        cd.rectangle((x, y + 3, x + 650 * old / max_count, y + 22), fill='#4d5b6c')
        cd.rectangle((x, y + 3, x + 650 * new / max_count, y + 22), fill=CYAN if new < old else '#8795a6')
        cd.text((816, y), f'{old:>7,} -> {new:>7,}   (-{(1-new/old)*100:4.1f}%)', font=font(16), fill=FG)
    cd.text((24, 1036), f'{changed}/{len(meshes)} meshes simplified. Gray remainder = removed triangles; cyan = retained LOD triangles.', font=font(17), fill=CYAN)
    cd.text((24, 1064), 'Vertex displacement bound is not a Hausdorff or light-transport error guarantee.', font=font(16), fill='#aebbc9')
    chart.save(root / 'lod-mesh-reduction.png')
    (root / 'lod-visualization.json').write_text(json.dumps({
        'beauty_sources': ['sponza-full.png', 'sponza-lod.png'],
        'wireframe_source_prefix': str(a.wireframe), 'wireframe_size': wire[0].size,
        'scope': 'CPU PT primary-ray visibility; 2x2 samples/pixel; projected actual triangle edges; cyan marks simplified mesh instances in both worlds',
        'source_triangles': before, 'selected_triangles': after,
        'simplified_meshes': changed, 'meshes': len(meshes),
        'beauty_difference_caveat': '64-spp differences include changed Monte Carlo paths; not pure geometry error',
    }, indent=2) + '\n')
    print('Saved lod-overview.png, lod-wireframe-detail.png, lod-mesh-reduction.png')


if __name__ == '__main__':
    main()
