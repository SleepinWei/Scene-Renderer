#!/usr/bin/env python3
"""Compare actual shared-PT probes / clustered worlds; never synthesize GI."""
import argparse
import hashlib
import gzip
import json
from pathlib import Path
import numpy as np
from PIL import Image, ImageDraw, ImageFont
from compare_ddgi_gt import pfm, metrics


def main():
    p = argparse.ArgumentParser()
    p.add_argument('reference', type=Path)
    p.add_argument('capture', type=Path)
    p.add_argument('output', type=Path)
    p.add_argument('--lod', type=Path)
    a = p.parse_args()
    ref, cap = str(a.reference), str(a.capture)
    gt = pfm(ref+'.pfm')
    masks = np.load(ref+'-errors.npz')
    core = masks['core_mask']
    setup = json.loads(Path(cap+'-ddgi-reference.json').read_text())
    raster = pfm('img/diagnostics/aa/metal/sponza-raster-ddgi-taa.pfm')
    capture = pfm(cap+'-raster-ddgi-taa.pfm')
    no_spec = pfm(cap+'-raster-ddgi-taa-no-sky-specular.pfm')
    report = {'reference':ref, 'capture':cap, 'hash_scope':'PFM hashes refer to decompressed bytes; files may be stored as .gz', 'scope':'AOV-aligned eroded fixed core; complete real-time shading, not isolated probe error',
              'backend':setup['probe_capture_backend'], 'probe_path_samples':setup['probe_path_samples'],
              'probe_path_depth':setup['probe_path_depth'], 'probe_frames':setup['raster_frames'],
              'probe_bake_wall_seconds':setup['probe_bake_wall_seconds'],
              'surface':{name:metrics(img,gt,core) for name,img in [('raster_probe_hq',raster),('pt_probe_hq',capture),('pt_probe_hq_no_sky_specular',no_spec)]}}
    points=setup['points']
    estimated=np.array([v['ddgi'] for v in points]);reference=np.array([v['pt_E_over_pi'] for v in points])
    report['irradiance']={'points':len(points), 'normalized_l1':float(abs(estimated-reference).sum()/abs(reference).sum()),
                          'relative_mean_rgb_bias':float((estimated-reference).sum()/reference.sum())}
    report['hashes']={}
    for prefix in [cap]+([str(a.lod)] if a.lod else []):
        for suffix in ['.pfm','-raster-ddgi-taa.pfm','-ddgi-reference.json']:
            path=Path(prefix+suffix)
            if path.exists():report['hashes'][str(path)]=hashlib.sha256(path.read_bytes()).hexdigest()
            elif Path(str(path)+'.gz').exists():
                with gzip.open(str(path)+'.gz','rb') as f:report['hashes'][str(path)]=hashlib.sha256(f.read()).hexdigest()
    if a.lod:
        low=str(a.lod);full_meta=json.loads(Path(cap+'.json').read_text());low_meta=json.loads(Path(low+'.json').read_text())
        lod=json.loads(Path(low+'-world-lod.json').read_text())
        report['world_lod']={'source_unique_triangles':lod['source_unique_triangles'],'selected_unique_triangles':lod['selected_unique_triangles'],
                             'build_seconds':lod['build_seconds'],'fixed_spp_lod_vs_full':metrics(pfm(low+'.pfm'),pfm(cap+'.pfm'),core),
                             'full_vs_2048spp':metrics(pfm(cap+'.pfm'),gt,core),'lod_vs_2048spp':metrics(pfm(low+'.pfm'),gt,core),
                             'full_render_seconds':full_meta['render_seconds'],'lod_render_seconds':low_meta['render_seconds'],
                             'full_native_acceleration':{k:v for k,v in full_meta.items() if k.startswith('native_')}, 'lod_native_acceleration':{k:v for k,v in low_meta.items() if k.startswith('native_')},
                             'timing_scope':'single capture run per variant; render_seconds includes checkpoint IO; repeated alternating benchmark is in benchmark.json. CPU wall time includes GPU waits, not GPU timestamps',
                             'image_difference_scope':'LOD/full fixed-64-spp difference includes geometric changes and changed Monte Carlo paths; not a pure geometry-error estimate' }
        low_setup=json.loads(Path(low+'-ddgi-reference.json').read_text());e=np.array([v['ddgi'] for v in low_setup['points']]);r=np.array([v['pt_E_over_pi'] for v in low_setup['points']])
        report['world_lod']['probe_irradiance']={'normalized_l1':float(abs(e-r).sum()/abs(r).sum()),'relative_mean_rgb_bias':float((e-r).sum()/r.sum())}
        report['world_lod']['probe_surface']=metrics(pfm(low+'-raster-ddgi-taa.pfm'),gt,core)
    a.output.parent.mkdir(parents=True,exist_ok=True)
    Path(str(a.output)+'.json').write_text(json.dumps(report,indent=2)+'\n')
    h,w=gt.shape[:2];sheet=Image.new('RGB',(w*3,h+64),'#181d24');draw=ImageDraw.Draw(sheet)
    font=ImageFont.truetype('/System/Library/Fonts/Supplemental/Arial.ttf',16)
    for i,(title,im) in enumerate([('Raster probes / HQ TAA',raster),('Shared PT probes / HQ TAA',capture),('Raw PT / 2048 spp',gt)]):
        exposed=np.maximum(im*setup['exposure'],0);mapped=np.clip(exposed/(1+exposed),0,1)**(1/2.2)
        sheet.paste(Image.fromarray((mapped*255+.5).astype('uint8')),(i*w,34));draw.text((i*w+10,10),title,font=font,fill='white')
    draw.text((10,h+41),'Same exposure; probe grid / queries unchanged. PT probes do not fix final sky specular occlusion.',font=font,fill='white')
    sheet.save(str(a.output)+'.png')
    print(json.dumps(report,indent=2))


if __name__=='__main__':
    main()
