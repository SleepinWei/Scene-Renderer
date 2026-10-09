#!/usr/bin/env python3
"""Render knitted fixtures, three-backend raw checks and optional OIDN display."""
import argparse
import os
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input',type=Path,default=Path('build/pt-knit'))
    parser.add_argument('--renderer',default='./build/pt-knit/runtime/pt-package-render')
    parser.add_argument('--oidn-renderer',default='./build/pt/Scene-Renderer')
    parser.add_argument('--size',default='768x768')
    parser.add_argument('--samples',type=int,default=1024)
    parser.add_argument('--preview',action='store_true')
    args=parser.parse_args()
    env=dict(os.environ,MTL_DEBUG_LAYER='0',MTL_SHADER_VALIDATION='0')
    for name in ['swatch','sweater']:
        folder=args.input/name
        jobs=[('Metal','preview','384x384',128)] if args.preview else [
            ('Metal','check-metal','128x128',64),('Vulkan','check-vulkan','128x128',64),
            ('CPU','check-cpu','128x128',64),('Metal','metal',args.size,args.samples)]
        for backend,label,size,samples in jobs:
            prefix=folder/label
            command=[args.renderer,'--backend',backend,'--scene',str(folder/'scene.json'),
                     '--size',size,'--samples',str(samples),'--bounces','12','--seed','1',
                     '--threads','6','--output',str(prefix)]
            with prefix.with_suffix('.txt').open('w') as log:
                subprocess.run(command,env=env,stdout=log,stderr=subprocess.STDOUT,check=True)
            print(name,label,'finished',flush=True)
        if not args.preview:
            with (folder/'denoise.txt').open('w') as log:
                subprocess.run([args.oidn_renderer,'--path-trace','--pt-denoise-input',str(folder/'metal.pfm'),
                                '--pt-denoise-device','cpu','--pt-exposure','0.7','--pt-output',str(folder/'filtered')],
                               env=env,stdout=log,stderr=subprocess.STDOUT,check=True)
            print(name,'OIDN finished',flush=True)


if __name__=='__main__':main()
