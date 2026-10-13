#!/usr/bin/env python3
"""Arrange native cluster readbacks without loading the large membership JSON.

The capture producer validates CPU/GPU indices, raster IDs and diagnostic depth.
This viewer streams past detailed membership arrays; it does not revalidate them.
"""
import argparse
import io
import json
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw
from visualize_render_diagnostics import panel, grid, font, BG


def metadata(path):
    output = io.StringIO()
    skip = None
    key = None
    with path.open() as source:
        for line in source:
            indent = len(line)-len(line.lstrip())
            if skip is not None:
                if indent == skip and line.lstrip().startswith(']'):
                    output.write(' '*skip+'"'+key+'": []'+(',' if line.rstrip().endswith(',') else '')+'\n')
                    skip = None
                continue
            key = next((k for k in ('membership', 'visible_cluster_raster_keys')
                        if line.lstrip().startswith('"'+k+'": [')), None)
            if key and not line.rstrip().endswith(('[]', '[],')):
                skip = indent
                continue
            output.write(line)
    return json.loads(output.getvalue())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('capture', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--scene', default='Procedural limestone grotto')
    parser.add_argument('--capture-note', help='Capture provenance printed directly on figures')
    args = parser.parse_args()
    source = args.capture/'flow'
    data = metadata(source/'flow.json')
    args.output.mkdir(parents=True, exist_ok=True)
    frames = {f['name']:f for f in data['frames']}
    if not all(f['cpu_cut_indices_exact'] and f['diagnostic_raster_keys_exact']
               and f['debug_depth_max_error'] == 0 and f['debug_coverage_mismatched_pixels'] == 0
               for f in frames.values()):
        raise ValueError('Capture producer did not validate exact geometry/depth')
    width, height = data['viewport']
    # Streaming read verifies sidecar totals without materializing each cut as
    # Python dictionaries. Exact CPU/GPU index parity remains producer evidence.
    for f in frames.values():
        if f.get('membership_file'):
            count, triangles = 0, 0
            with (source/f['membership_file']).open() as members:
                for line in members:
                    member = json.loads(line)
                    count += 1
                    triangles += member['triangles']
            if count != f['clusters'] or triangles != f['triangles']:
                raise ValueError('Streamed GPU membership differs from captured draw totals')
        if sum(f['triangles_by_level'].values()) != f['triangles']:
            raise ValueError('LOD level totals differ from captured draw totals')
    safety = data.get('memory')
    note = args.capture_note or (
        f"Process/GPU budgets {safety['process_budget_bytes']/1024**2:.0f}/{safety['gpu_budget_bytes']/1024**2:.0f} MiB; sampled peaks, see metadata."
        if safety else 'Recorded before memory-admission defaults; protected capture pending.')

    def picture(name, kind):
        image = Image.open(source/f'{name}-{kind}.png').convert('RGB')
        if kind in ('clusters', 'levels'):
            keys = np.fromfile(source/f'{name}-clusters-ids.f32', dtype='<f4').reshape(height,width)
            edges = np.zeros((height,width), dtype=bool)
            edges[:,1:] |= keys[:,1:] != keys[:,:-1]
            edges[1:,:] |= keys[1:,:] != keys[:-1,:]
            pixels = np.array(image)
            pixels[edges] = (240,245,250)
            image = Image.fromarray(pixels)
        return image

    def row(name):
        f = frames[name]
        return [panel(picture(name, kind), f['label']+' / '+title,
                      f"{f['triangles']:,} triangles / LOD reduction {f['lod_reduction_percent']:.2f}%", width=550,height=370)
                for kind, title in (('beauty','PBR'),('clusters','actual GPU cluster IDs'),('levels','LOD level'))]

    grid(row('0')+row('1'),3,args.scene+': dense irregular geometry and cluster LOD',
         'GPU readbacks / white = cluster boundary / '+note,args.output/'partition.png')
    grid(sum((row(name) for name in ('2','3','4','5')),[]),3,args.scene+': near-to-far LOD / fixed 4 px budget',
         'Same-camera zero-error references / no GPU timing / '+note,args.output/'dynamic-lod.png')
    # Each animation frame is a real recorded camera pose. No interpolation
    # invents intermediate GPU selections or suggests measured frame rate.
    animation = []
    route = [name for name in ('2','3','4','5','9','10') if name in frames]
    for name in route:
        f = frames[name]
        columns = [panel(picture(name,kind), title,
                         f"{f['label']} / {f['pixel_error']:g} px",width=420,height=310)
                   for kind,title in (('beauty','PBR'),('clusters','GPU cluster IDs'),('levels','Parent level'))]
        canvas = Image.new('RGB',(1260,470),BG)
        draw = ImageDraw.Draw(canvas)
        draw.text((18,10),args.scene+' / recorded LOD poses',font=font(25),fill='white')
        for i,item in enumerate(columns):
            canvas.paste(item,(i*420,42))
        draw.text((18,355),f"{f['triangles']:,} submitted triangles / {f['clusters']:,} clusters / reduction {f['lod_reduction_percent']:.2f}%",font=font(19),fill='white')
        x, total = 18, f['triangles']
        for level,triangles in sorted(f['triangles_by_level'].items(),key=lambda item:int(item[0])):
            tint = tuple(round(c*255) for c in data['level_colors'][int(level)])
            length = round(1224*triangles/total)
            draw.rectangle((x,386,x+length,403),fill=tint)
            if length > 100:
                draw.text((x+5,408),f"L{level} {100*triangles/total:.1f}%",font=font(15),fill=tint)
            x += length
        draw.text((18,444),note+' / discrete poses, no FPS claim',font=font(14),fill=(175,193,209))
        animation.append(canvas)
    if animation:
        palette = animation[0].quantize(colors=192)
        converted = [frame.quantize(palette=palette,dither=Image.Dither.NONE) for frame in animation]
        converted[0].save(args.output/'dynamic-lod.gif',save_all=True,append_images=converted[1:],duration=1400,loop=0,disposal=2)
    if (source/'1-clusters-wire.png').exists():
        closeups = []
        crop = (0,int(height*.16),int(width*.48),int(height*.83))
        for name in ('0','1'):
            f = frames[name]
            for kind,title in (('beauty','Original PBR'),('clusters','Cluster boundaries'),('clusters-wire','Actual triangle wire')):
                closeups.append(panel(picture(name,kind).crop(crop),f"{f['pixel_error']:g} px / {title}",
                                      f"Same camera / {f['triangles']:,} submitted triangles",width=550,height=430))
        grid(closeups,3,args.scene+': geometric relief and meshlet topology',
             'Native triangle wire overlay / '+note,args.output/'detail.png')
    if '7' in frames and '8' in frames:
        grid(sum((row(name) for name in ('0','7','1','8')),[]),3,
             args.scene+': fixed-camera 0 / 2 / 4 / 8 px LOD budget',
             'Same materials, camera and lighting / '+note,args.output/'pixel-budget.png')
    reference = Image.open(source/'1-reference.png').convert('RGB')
    selected = Image.open(source/'1-beauty.png').convert('RGB')
    difference = Image.fromarray(np.clip(np.abs(np.array(reference,dtype=np.int16)-np.array(selected,dtype=np.int16))*8,0,255).astype('uint8'))
    grid([panel(image,title,subtitle,width=550,height=370) for image,title,subtitle in (
        (reference,'0 px / same-view reference','Original geometry and materials'),
        (selected,'4 px / selected LOD',f"HDR relative RMSE {frames['1']['zero_error_hdr_relative_rmse_percent']:.3f}%"),
        (difference,'Display difference / 8x','Geometric pixel error does not bound shading error'))],3,
        args.scene+': appearance comparison','Native readbacks, same materials, lighting and camera.',args.output/'quality.png')
    data['capture_provenance_note']=note
    data['animation_camera_frames']=route
    data['animation_note']='Discrete native captures; GIF playback rate is not renderer FPS.'
    data['viewer_note']='Detailed membership arrays omitted by streaming; GPU/CPU proof comes from the capture producer.'
    (args.output/'summary.json').write_text(json.dumps(data,indent=2)+'\n')
    print(f'Wrote compact diagnostics to {args.output}')


if __name__ == '__main__':
    main()
