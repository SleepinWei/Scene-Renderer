#!/usr/bin/env python3
"""Validate native knit renders and publish the gallery with provenance."""
import argparse
import json
from pathlib import Path
import shutil

import numpy as np
from PIL import Image

from compare_pt_references import read, metrics
from create_pt_hair_sky import sha


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input',type=Path,default=Path('build/pt-knit'))
    args=parser.parse_args();root=args.input
    frozen=json.loads((root/'source-freeze.json').read_text())
    for path,digest in frozen.items():
        if sha(path)!=digest: raise ValueError('Changed knit rendering input: '+path)
    gallery=Path('img/path-tracing'); evidence=Path('img/diagnostics/materials/knitted-yarn')
    gallery.mkdir(parents=True,exist_ok=True);evidence.mkdir(parents=True,exist_ok=True)
    scenes,outputs={},{}
    for name,label in [('swatch','knitted-honeycomb-sunset'),('sweater','knitted-sweater-sunset')]:
        folder=root/name;scene=json.loads((folder/'scene.json').read_text()); reports={}
        assert any(m.get('yarn_specular_weight',0)>0 for m in scene['materials'])
        assert not any(any(m.get('emission',[0,0,0])) for m in scene['materials'])
        assert np.isclose(np.degrees(np.arcsin(scene['sun']['direction'][1])),5,atol=1e-5)
        for prefix in ['metal','check-metal','check-vulkan','check-cpu']:
            report=json.loads((folder/(prefix+'.json')).read_text());settings=[768,768,1024,12,1] if prefix=='metal' else [128,128,64,12,1]
            assert [report[k] for k in ['width','height','samples','max_depth','seed']]==settings
            assert report['non_finite_samples']==0 and not report['adaptive'] and not report['denoiser']
            raw=read(folder/(prefix+'.pfm'));assert raw.shape==(settings[1],settings[0],3) and np.isfinite(raw).all() and raw.min()>=0
            reports[prefix]=report;outputs[str(folder/(prefix+'.pfm'))]=sha(folder/(prefix+'.pfm'))
            for suffix in ['.json','.txt']:shutil.copyfile(folder/(prefix+suffix),evidence/(name+'-'+prefix+suffix))
        metal,vulkan,cpu=[read(folder/('check-'+backend+'.pfm')) for backend in ['metal','vulkan','cpu']]
        gpu=metrics(metal,vulkan);comparison=metrics(metal,cpu)
        assert gpu['relative_rgb_l1']<.0001 and comparison['relative_rgb_l1']<.05
        filtered=json.loads((folder/'filtered.json').read_text())
        assert filtered['denoiser']=='Open Image Denoise 2.5.1' and filtered['denoise_device']=='CPU'
        for filename,suffix in [('metal.png','-raw.png'),('filtered-denoised.png','.png')]:
            assert Image.open(folder/filename).size==(768,768)
            target=gallery/(label+suffix);shutil.copyfile(folder/filename,target);outputs[str(target)]=sha(target)
        for filename in ['scene.json','filtered.json','denoise.txt']:
            shutil.copyfile(folder/filename,evidence/(name+'-'+filename))
        scenes[name]=dict(scene=scene,render=reports['metal'],checks=reports,filter=filtered,
                          metal_vulkan_raw_identical=bool(np.array_equal(metal,vulkan)),metal_vs_vulkan=gpu,metal_vs_cpu=comparison)
    for filename in ['source-freeze.json','determinism.json','tests.txt']:
        shutil.copyfile(root/filename,evidence/filename)
    for filename in ['src/rhi/shaders/pt-fiber.glsl','tools/create_pt_knitted.py','tools/render_pt_knitted.py','tests/PT/KnitPatternsTests.py']:
        shutil.copyfile(filename,evidence/Path(filename).name)
    data=dict(date='2026-10-09',device='Apple M4',scenes=scenes,input_sha256=frozen,output_sha256=outputs,
              validator_sha256=sha(__file__),evidence_sha256={str(p):sha(p) for p in evidence.iterdir() if p.is_file()},
              credit='YarnSim: Jonathan Leaf, Rundong Wu, Eston Schweickart, Doug L. James, Steve Marschner, 2018; source dataset not redistributed',
              limits=['Static tessellated yarn geometry; no yarn dynamics or complete simulated garment',
                      'Anisotropic GGX bundle approximation, not the 2021 ply-based fiber transport model',
                      'Frozen sunset HDR and finite sun; projected cloud shadow is not imported',
                      'Raw CPU/GPU consistency is not independent Mitsuba/Blender ground truth; Vulkan via MoltenVK',
                      'OIDN may smooth fine nap; raw HDR used for backend comparisons'])
    (gallery/'knitted-yarn-validation.json').write_text(json.dumps(data,indent=2)+'\n')
    print(json.dumps({n:dict(cpu_l1=s['metal_vs_cpu']['relative_rgb_l1'],gpu_l1=s['metal_vs_vulkan']['relative_rgb_l1'],
                             render_seconds=s['render']['render_seconds']) for n,s in scenes.items()},indent=2))


if __name__=='__main__':main()
