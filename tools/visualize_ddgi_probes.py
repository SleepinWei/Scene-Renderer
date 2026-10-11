#!/usr/bin/env python3
"""Native probe screenshots and layer maps from the actual Sponza GPU cache."""
import argparse
import hashlib
import json
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('prefix', type=Path)
    args = parser.parse_args()
    prefix = str(args.prefix)
    setup = json.loads(Path(prefix+'-ddgi-reference.json').read_text())
    volume = setup['probe_volume']
    nx, ny, nz = volume['counts']
    count = nx*ny*nz
    binary = Path(prefix+'-probe-field.bin')
    raw = binary.read_bytes() if binary.exists() else np.load(prefix+'-probe-field.npz')['field'].astype('<f4').tobytes()
    field = np.frombuffer(raw, dtype='<f4').reshape(count, volume['stride_vec4'], 4)
    if not np.isfinite(field).all():
        raise ValueError('Nonfinite native probe field')
    state = field[:, 0]
    initialized = state[:, 1] > 0
    active = initialized & (state[:, 0] > .5)
    inactive = initialized & ~active
    if int(active.sum()) != setup['active_probes'] or int(initialized.sum()) != setup['initialized_probes']:
        raise ValueError('Readback classification differs from capture metadata')
    indices = np.arange(count)
    grid = np.column_stack((indices % nx, (indices//nx) % ny, indices//(nx*ny)))
    positions = np.array(volume['origin'])+grid*volume['spacing']
    np.savez_compressed(prefix+'-probe-field.npz', field=field, positions=positions, grid=grid)
    font = ImageFont.truetype('/System/Library/Fonts/Supplemental/Arial.ttf', 17)
    small = ImageFont.truetype('/System/Library/Fonts/Supplemental/Arial.ttf', 14)
    cases = [('state', 'Classification / building occlusion'), ('xray', 'Classification / X-ray'),
             ('layer', f'X-ray / layer {volume["layer_capture_index"]}, Y={volume["layer_capture_y"]:g} m'),
             ('irradiance', 'Directional E/pi / building occlusion')]
    width, height = setup['width'], setup['height']
    sheet = Image.new('RGB', (width*2, (height+40)*2+50), '#181d24')
    draw = ImageDraw.Draw(sheet)
    for index, (name, label) in enumerate(cases):
        source = Path(prefix+'-probes-'+name+'.ppm')
        if not source.exists():
            source = Path(prefix+'-probes-'+name+'.png')
        image = Image.open(source).convert('RGB')
        if image.size != (width, height):
            raise ValueError('Probe screenshot dimensions differ')
        image.save(prefix+'-probes-'+name+'.png')
        x, y = (index%2)*width, (index//2)*(height+40)
        sheet.paste(image, (x, y+40))
        draw.text((x+12, y+10), label, fill='white', font=font)
    draw.text((12, sheet.height-35), f'Green: active ({active.sum()}) / Red: rejected ({inactive.sum()}) / Yellow: unvisited ({(~initialized).sum()})', fill='white', font=font)
    sheet.save(prefix+'-probes-comparison.png')
    sheet.crop((0, 0, width*2, height+40)).save(prefix+'-probes-placement.png')
    # X/Z slices of exact grid positions; colours are the GPU state, not a
    # reconstructed geometric containment test. No scene pixels are synthesized.
    columns = 4
    tile_w, tile_h = 360, 280
    layout = Image.new('RGB', (columns*tile_w, ((ny+columns-1)//columns)*tile_h+80), '#181d24')
    draw = ImageDraw.Draw(layout)
    colours = {'active': '#35d389', 'inactive': '#ef6262', 'unvisited': '#ffcf55'}
    layers = []
    for layer in range(ny):
        x, y = (layer%columns)*tile_w, (layer//columns)*tile_h+70
        selected = grid[:, 1] == layer
        layers.append({'index': layer, 'y': volume['origin'][1]+layer*volume['spacing'],
                       'active': int((active & selected).sum()), 'inactive': int((inactive & selected).sum()),
                       'unvisited': int((~initialized & selected).sum())})
        info = layers[-1]
        draw.text((x+12, y+5), f'Layer {layer} / Y={info["y"]:g} m / active {info["active"]} / rejected {info["inactive"]}', fill='white', font=small)
        left, top = x+30, y+42
        cell_x, cell_z = 20, 20
        for gx in range(nx):
            draw.line((left+gx*cell_x, top, left+gx*cell_x, top+(nz-1)*cell_z), fill='#34414d')
        for gz in range(nz):
            draw.line((left, top+gz*cell_z, left+(nx-1)*cell_x, top+gz*cell_z), fill='#34414d')
        for i in np.flatnonzero(selected):
            px, py = left+grid[i, 0]*cell_x, top+(nz-1-grid[i, 2])*cell_z
            colour = colours['active' if active[i] else 'inactive' if inactive[i] else 'unvisited']
            draw.ellipse((px-5, py-5, px+5, py+5), fill=colour)
        draw.text((left, top+(nz-1)*cell_z+13), f'X {volume["origin"][0]:g} -> {positions[:,0].max():g} m', fill='#bdc7d4', font=small)
        draw.text((left, top+(nz-1)*cell_z+33), f'Z {positions[:,2].max():g} (top) -> {volume["origin"][2]:g} m', fill='#bdc7d4', font=small)
    draw.text((12, 10), f'{setup["scene"].title()} / actual GPU classification / {nx} x {ny} x {nz} / spacing {volume["spacing"]:g} m', fill='white', font=font)
    draw.text((12, 35), f'Active {active.sum()} / rejected {inactive.sum()} / unvisited {(~initialized).sum()} / all {count}', fill='white', font=font)
    layout.save(prefix+'-probe-layers.png')
    report = {'scene': setup['scene'], 'volume': volume, 'probe_count': count,
              'active': int(active.sum()), 'inactive': int(inactive.sum()), 'unvisited': int((~initialized).sum()),
              'layers': layers, 'readback_sha256': hashlib.sha256(raw).hexdigest(),
              'frozen_field_unchanged': setup['surface_diagnostics']['frozen_probe_field_unchanged'],
              'classification': 'GPU backface solid-angle fraction >= threshold rejects a probe; this is a heuristic, not an exact point-in-solid test.',
              'visualization': 'Native GPU billboard markers display directional E/pi or classification. Radius is display-only; X-ray reveals occluded markers at reduced alpha.',
              'reference': 'This run uses 1 spp PT only as CLI smoke, never as a quality reference.'}
    Path(prefix+'-probe-summary.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
