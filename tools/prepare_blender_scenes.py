#!/usr/bin/env python3
"""Fetch pinned official Blender scenes, then export frozen PT packages."""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import shutil
import subprocess
import urllib.request
import zipfile

ROOT=Path(__file__).resolve().parents[1]
def digest(path):
    h=hashlib.sha256()
    with path.open('rb') as f:
        for block in iter(lambda:f.read(1024*1024),b''):h.update(block)
    return h.hexdigest()

def acquire(entry):
    cache=ROOT/entry['cache'];cache.parent.mkdir(parents=True,exist_ok=True)
    if not cache.exists():
        temporary=cache.with_suffix('.partial')
        try:
            request=urllib.request.Request(entry['url'],headers={'User-Agent':'SceneRenderer-Blender-PT'})
            with urllib.request.urlopen(request,timeout=60) as src,temporary.open('wb') as dest:shutil.copyfileobj(src,dest)
            if digest(temporary)!=entry['sha256']:raise RuntimeError('Blender archive checksum mismatch')
            temporary.replace(cache)
        finally:temporary.unlink(missing_ok=True)
    if digest(cache)!=entry['sha256']:raise RuntimeError('Cached Blender archive checksum mismatch')
    directory=ROOT/entry['directory'];blend=directory/entry['blend']
    if not blend.exists():
        with zipfile.ZipFile(cache) as archive:
            if sum(i.file_size for i in archive.infolist())>2*1024**3:raise ValueError('Oversized archive')
            for item in archive.infolist():
                p=PurePosixPath(item.filename.replace('\\','/'))
                if p.is_absolute() or '..' in p.parts or ':' in str(p) or ((item.external_attr>>16)&0o170000)==0o120000:raise ValueError('Unsafe archive member')
            archive.extractall(directory)
    if not blend.is_file():raise RuntimeError(f'Missing Blender scene: {blend}')
    return directory,blend

def main():
    entries=json.loads((ROOT/'samples/blender-assets.json').read_text())
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--scene',choices=['all',*entries],default='all');p.add_argument('--blender',default='blender');p.add_argument('--atlas-size',type=int,choices=[128,256,512,1024],default=256);p.add_argument('--particle-limit',type=int,default=512);p.add_argument('--water-profile',choices=['source','pool'],default='pool');p.add_argument('--pool-depth',type=float,default=.5);p.add_argument('--fetch-only',action='store_true');args=p.parse_args()
    for name,entry in entries.items():
        if args.scene not in ('all',name):continue
        directory,blend=acquire(entry)
        if args.fetch_only:print(blend);continue
        subprocess.run([args.blender,'--background',str(blend),'--disable-autoexec','--python-exit-code','1','--python',str(ROOT/'tools/export_blender_pt.py'),'--','--output',str(directory),'--source-name',name,'--atlas-size',str(args.atlas_size),'--particle-limit',str(args.particle_limit),'--water-profile',args.water_profile if name=='barcelona' else 'source','--pool-depth',str(args.pool_depth)],cwd=ROOT,check=True)
        manifest=directory/'scene.json';scene=json.loads(manifest.read_text());scene['source'].update({k:entry[k] for k in ['url','author','license','source','sha256']});scene['source']['archive_sha256']=entry['sha256'];scene['source']['blend_sha256']=digest(blend);manifest.write_text(json.dumps(scene,indent=2)+'\n')
        print(name,scene['export']['expanded_triangles'],'triangles ->',manifest)
if __name__=='__main__':main()
