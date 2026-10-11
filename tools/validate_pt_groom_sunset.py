#!/usr/bin/env python3
"""Validate unchanged groom geometry under a coherent frozen sunset environment."""
import json
from pathlib import Path
import shutil

import numpy as np
from PIL import Image

from compare_pt_references import read,metrics
from create_pt_hair_sky import sha


def main():
    root=Path('build/pt-grooms-sunset')
    gallery=Path('img/path-tracing');evidence=Path('img/diagnostics/materials/grooms-sunset')
    gallery.mkdir(parents=True,exist_ok=True);evidence.mkdir(parents=True,exist_ok=True)
    frozen=json.loads((root/'source-freeze.json').read_text())
    for path,digest in frozen.items():
        if sha(path)!=digest:raise ValueError('Changed rendering input: '+path)
    scenes,outputs={},{}
    solar=json.loads((root/'sunset-environment.hdr.json').read_text())
    for name,label in [('bunny','bunny-fur-sunset'),('sweater','knit-sweater-sunset'),('yarn','knit-yarn-sunset')]:
        folder=root/name
        scene=json.loads((folder/'scene.json').read_text());imported=json.loads((folder/'sunset-import.json').read_text())
        source=Path(imported['source']);original=json.loads((source/'scene.json').read_text())
        assert scene['camera']==original['camera'] and scene['draws']==original['draws'] and scene['materials']==original['materials']
        for key in ['geometry','tangents']:
            assert sha(folder/scene[key])==sha(source/original[key])
        assert np.isclose(np.linalg.norm(scene['sun']['direction']),1,atol=1e-6)
        assert np.isclose(np.degrees(np.arcsin(scene['sun']['direction'][1])),5,atol=1e-5)
        assert np.isclose(scene['sun']['direction'][1],solar['sun_direction'][1])
        assert np.allclose(scene['sun']['irradiance'],np.array(solar['sun_irradiance'])*1.2)
        assert scene['source']['solar_relight']['yaw_degrees']==80 and scene['source']['solar_relight']['light_scale']==1.2
        assert not any(any(m.get('emission',[0,0,0])) for m in scene['materials'])
        reports={}
        for prefix in ['metal','check-metal','check-vulkan','check-cpu']:
            report=json.loads((folder/(prefix+'.json')).read_text())
            settings=[768,768,1024,12,1] if prefix=='metal' else [128,128,64,12,1]
            assert [report[k] for k in ['width','height','samples','max_depth','seed']]==settings
            assert report['non_finite_samples']==0 and not report['adaptive'] and not report['denoiser']
            assert np.isclose(report['exposure'],.7) and np.allclose(report['sun_irradiance'],scene['sun']['irradiance'],rtol=1e-6)
            raw=read(folder/(prefix+'.pfm'));assert raw.shape==(settings[1],settings[0],3) and raw.min()>=0
            reports[prefix]=report;outputs[str(folder/(prefix+'.pfm'))]=sha(folder/(prefix+'.pfm'))
            for suffix in ['.json','.txt']:shutil.copyfile(folder/(prefix+suffix),evidence/(name+'-'+prefix+suffix))
        metal,vulkan,cpu=[read(folder/('check-'+backend+'.pfm')) for backend in ['metal','vulkan','cpu']]
        assert np.array_equal(metal,vulkan)
        comparison=metrics(metal,cpu);assert comparison['relative_rgb_l1']<.05
        filtered=json.loads((folder/'filtered.json').read_text())
        assert filtered['denoiser']=='Open Image Denoise 2.5.1' and filtered['denoise_device']=='CPU' and np.isclose(filtered['exposure'],.7)
        for filename,suffix in [('metal.png','-raw.png'),('filtered-denoised.png','.png')]:
            assert Image.open(folder/filename).size==(768,768)
            target=gallery/(label+suffix);shutil.copyfile(folder/filename,target);outputs[str(target)]=sha(target)
        for filename in ['scene.json','sunset-import.json','filtered.json','denoise.txt']:
            shutil.copyfile(folder/filename,evidence/(name+'-'+filename))
        scenes[name]=dict(scene=scene,render=reports['metal'],checks=reports,filter=filtered,
                          metal_vulkan_raw_identical=True,metal_vs_cpu=comparison,geometry_camera_materials_unchanged=True)
    for filename in ['source-freeze.json','render_all.py','sunset-environment.hdr','sunset-environment.hdr.json','sunset.json']:
        shutil.copyfile(root/filename,evidence/filename)
    data=dict(date='2026-10-09',device='Apple M4',scenes=scenes,input_sha256=frozen,output_sha256=outputs,
              validator_sha256=sha(__file__),evidence_sha256={str(p):sha(p) for p in evidence.iterdir() if p.is_file()},
              credit='Bunny: Stanford University Computer Graphics Laboratory; knitwear/yarn: procedural project fixtures',
              limits=['Frozen clouds-sunset HDR plus finite sun; projected cloud-shadow field not imported',
                      'Static hair ribbons and unrelaxed knit prototype; no native curves or dynamics',
                      'OIDN display can smooth fibers; comparisons use raw scene-linear HDR',
                      'CPU/GPU consistency, not an independent physical reference; Vulkan via MoltenVK'])
    (gallery/'grooms-sunset-validation.json').write_text(json.dumps(data,indent=2)+'\n')
    print(json.dumps({name:dict(cpu_l1=s['metal_vs_cpu']['relative_rgb_l1'],gpu_exact=True,
                               render_seconds=s['render']['render_seconds']) for name,s in scenes.items()},indent=2))


if __name__=='__main__':main()
