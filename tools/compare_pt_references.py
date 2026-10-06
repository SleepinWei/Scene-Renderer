#!/usr/bin/env python3
"""Compare raw scene-linear PFM renders; never measures tone-mapped/denoised PNGs."""
import argparse
import hashlib
import json
from pathlib import Path
import numpy as np
from PIL import Image, ImageDraw


def read(path):
    with Path(path).open('rb') as f:
        if f.readline().strip()!=b'PF':raise ValueError('RGB PFM required')
        w,h=map(int,f.readline().split());scale=float(f.readline());data=np.frombuffer(f.read(),'<f4' if scale<0 else '>f4')
    if data.size!=w*h*3 or not np.isfinite(data).all():raise ValueError('Invalid PFM pixels')
    return data.reshape(h,w,3)[::-1].astype(np.float64)*abs(scale)


def metrics(a,b):
    difference=a-b;denominator=float(np.abs(b).sum())
    return dict(mean_absolute_rgb=float(np.abs(difference).mean()),relative_rgb_l1=float(np.abs(difference).sum()/max(denominator,1e-20)),
                rgb_rmse=float(np.sqrt(np.mean(difference*difference))),total_rgb_energy_ratio=float(a.sum()/max(float(b.sum()),1e-20)),
                mean_signed_rgb=list(difference.mean(axis=(0,1))),maximum_absolute_component=float(np.abs(difference).max()))


def blocks(a,n=4):
    h,w=a.shape[:2];return a[:h//n*n,:w//n*n].reshape(h//n,n,w//n,n,3).mean(axis=(1,3))


def compare(engine,reference,output,repeat=None):
    a,b=read(engine),read(reference)
    if a.shape!=b.shape:raise ValueError('Render dimensions differ')
    h,w=a.shape[:2];report=dict(width=w,height=h,domain='raw scene-linear RGB, no exposure fitting or pixel alignment',metrics=metrics(a,b),block4_metrics=metrics(blocks(a),blocks(b)))
    sidecars=[Path(engine).with_suffix('.json'),Path(reference).with_suffix('.json')]
    if all(p.exists() for p in sidecars):
        e,r=[json.loads(p.read_text()) for p in sidecars]
        for key in ('width','height','samples','max_depth'):
            if e[key]!=r[key]:raise ValueError(f'Render parameter differs: {key}')
        if e.get('adaptive') or e.get('denoiser') or r.get('denoise'):raise ValueError('Fixed raw samples required')
        if e.get('scene_package',{}).get('comparison')!=r.get('scene_package',{}).get('comparison'):raise ValueError('Frozen comparison profiles differ')
        report['render_settings']={k:e[k] for k in ('width','height','samples','max_depth','seed')};report['comparison']=r.get('scene_package')
        report['render_seconds']=dict(engine=e['render_seconds'],cycles=r['render_seconds']);report['sidecars_sha256']={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in sidecars}
    # Fixed Barcelona regions are only used for its report, not automatic masks.
    report['regions']={}
    regions={'sky':(0,0,1,.18),'building':(.22,.23,.78,.52),'pool':(.04,.56,.65,.92)} if all(p.exists() for p in sidecars) and e.get('scene')=='barcelona' else {}
    for name,(x0,y0,x1,y1) in regions.items():
        box=[int(x0*w),int(y0*h),int(x1*w),int(y1*h)];x,y,right,bottom=box
        report['regions'][name]=dict(rectangle_pixels=box,metrics=metrics(a[y:bottom,x:right],b[y:bottom,x:right]))
    paths=[Path(engine),Path(reference)]
    if repeat:
        c=read(repeat)
        if c.shape!=b.shape:raise ValueError('Repeat dimensions differ')
        rp=Path(repeat).with_suffix('.json')
        if rp.exists() and all(p.exists() for p in sidecars):
            rr=json.loads(rp.read_text())
            if rr['seed']==r['seed']:raise ValueError('Noise comparison requires independent seeds')
            for key in ('width','height','samples','max_depth','scene_package'):
                if rr[key]!=r[key]:raise ValueError(f'Repeat parameter differs: {key}')
            report['repeat_seed']=rr['seed'];report['sidecars_sha256'][str(rp)]=hashlib.sha256(rp.read_bytes()).hexdigest()
        report['cycles_seed_pair']=metrics(b,c);report['engine_vs_cycles_seed_mean']=metrics(a,(b+c)*.5);report['cycles_seed_pair_block4']=metrics(blocks(b),blocks(c));paths.append(Path(repeat))
        for region in report['regions'].values():
            x,y,right,bottom=region['rectangle_pixels'];region['cycles_seed_pair']=metrics(b[y:bottom,x:right],c[y:bottom,x:right])
    report['inputs']={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in paths}
    report['limits']=['finite samples; a seed pair estimates noise, not a bound on systematic error','regional rectangles include mixed geometry, not semantic masks','controlled package closures, not original Blender source shaders']
    output=Path(output);output.parent.mkdir(parents=True,exist_ok=True)
    def display(v):return np.clip((1-np.exp(-np.maximum(v,0)))**(1/2.2)*255+.5,0,255).astype(np.uint8)
    Image.fromarray(display(a)).save(output.with_name(output.name+'-metal.png'));Image.fromarray(display(b)).save(output.with_name(output.name+'-cycles.png'))
    # Fixed error scale of .05 linear RGB; cap only the visualization, never metrics.
    error=np.clip(np.mean(np.abs(a-b),axis=2)/.05,0,1);heat=np.stack([error,np.maximum(0,error*2-1),np.zeros_like(error)],axis=2)
    Image.fromarray((heat*255+.5).astype(np.uint8)).save(output.with_name(output.name+'-error.png'))
    board=Image.new('RGB',(w*3,h+24),(25,25,25));draw=ImageDraw.Draw(board)
    for i,(title,pixels) in enumerate([('Metal raw',display(a)),('Cycles raw',display(b)),('|dRGB|: yellow>=.05',(heat*255+.5).astype(np.uint8))]):
        board.paste(Image.fromarray(pixels),(w*i,24));draw.text((w*i+8,5),title,fill='white')
    board.save(output.with_suffix('.png'));output.with_suffix('.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report['metrics']))


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('engine');p.add_argument('reference');p.add_argument('--repeat');p.add_argument('--output',required=True);args=p.parse_args();compare(args.engine,args.reference,args.output,args.repeat)
