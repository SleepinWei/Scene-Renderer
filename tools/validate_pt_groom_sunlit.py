#!/usr/bin/env python3
"""Validate fixed sunlit groom renders and publish native-rendered gallery images."""
import argparse
import json
from pathlib import Path
import shutil

import numpy as np
from PIL import Image

from compare_pt_references import read, metrics
from create_pt_hair_sky import sha


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input',type=Path,default=Path('build/pt-grooms-sunlit'))
    parser.add_argument('--output',type=Path,default=Path('img/path-tracing'))
    parser.add_argument('--evidence',type=Path,default=Path('img/diagnostics/materials/grooms-sunlit'))
    args = parser.parse_args()
    frozen = json.loads((args.input/'source-freeze.json').read_text())
    for path,digest in frozen.items():
        if sha(path) != digest:
            raise ValueError('Rendering input changed: '+path)
    args.output.mkdir(parents=True,exist_ok=True)
    args.evidence.mkdir(parents=True,exist_ok=True)
    scenes,outputs = {},{}
    for name,label in [('bunny','bunny-fur-sunlit'),('sweater','knit-sweater-sunlit'),('yarn','knit-yarn-sunlit')]:
        root = args.input/name
        scene = json.loads((root/'scene.json').read_text())
        assert not any(any(m.get('emission',[0,0,0])) for m in scene['materials'])
        assert len(scene['materials']) == len({d['material'] for d in scene['draws']})
        assert np.isclose(np.linalg.norm(scene['sun']['direction']),1,atol=1e-6)
        assert scene['source']['solar_relight']['light_scale'] == 3
        assert scene['source']['solar_relight']['yaw_degrees'] == 70
        if name == 'bunny':
            groom = json.loads((root/'groom.json').read_text())
            assert scene['source']['strands'] == len(groom['strands']) == 72000
            assert scene['source']['input_sha256'] == sha(scene['source']['input'])
        if name == 'sweater':
            assert scene['source']['yarn_loops'] == 1280 and scene['source']['fuzz_strands'] == 23360
        if name == 'yarn':
            assert scene['source']['yarn_plies'] == 3 and scene['source']['fuzz_strands'] == 18000
        reports = {}
        for prefix in ['metal','check-metal','check-vulkan','check-cpu']:
            report = json.loads((root/(prefix+'.json')).read_text())
            expected = [768,768,1024,12,1] if prefix == 'metal' else [128,128,64,12,1]
            assert [report[k] for k in ['width','height','samples','max_depth','seed']] == expected
            assert report['non_finite_samples'] == 0 and not report['adaptive'] and not report['denoiser']
            assert np.isclose(report['exposure'],scene['exposure'])
            assert np.allclose(report['sun_irradiance'],scene['sun']['irradiance'],rtol=1e-6)
            raw = read(root/(prefix+'.pfm'))
            assert raw.shape == (expected[1],expected[0],3) and np.min(raw) >= 0
            reports[prefix] = report
            outputs[str(root/(prefix+'.pfm'))] = sha(root/(prefix+'.pfm'))
            for suffix in ['.json','.txt']:
                shutil.copyfile(root/(prefix+suffix),args.evidence/(name+'-'+prefix+suffix))
        metal,vulkan,cpu = [read(root/('check-'+backend+'.pfm')) for backend in ['metal','vulkan','cpu']]
        assert np.array_equal(metal,vulkan), 'Metal / Vulkan raw mismatch'
        comparison = metrics(metal,cpu)
        assert comparison['relative_rgb_l1'] < .05, 'CPU / GPU difference exceeds fixture tolerance'
        filtered = json.loads((root/'filtered.json').read_text())
        assert filtered['denoiser'] == 'Open Image Denoise 2.5.1' and filtered['denoise_device'] == 'CPU'
        assert np.isclose(filtered['exposure'],scene['exposure'])
        for filename,suffix in [('metal.png','-raw.png'),('filtered-denoised.png','.png')]:
            assert Image.open(root/filename).size == (768,768)
            target = args.output/(label+suffix)
            shutil.copyfile(root/filename,target)
            outputs[str(target)] = sha(target)
        for filename in ['scene.json','filtered.json','denoise.txt']:
            shutil.copyfile(root/filename,args.evidence/(name+'-'+filename))
        outputs[str(root/'filtered-denoised.pfm')] = sha(root/'filtered-denoised.pfm')
        scenes[name] = dict(scene=scene,render=reports['metal'],filter=filtered,
                            checks=reports,metal_vulkan_raw_identical=True,metal_vs_cpu=comparison)
    shutil.copyfile(args.input/'source-freeze.json',args.evidence/'source-freeze.json')
    shutil.copyfile(args.input/'render_all.py',args.evidence/'render-commands.py')
    shutil.copyfile(args.input/'render_sweater.py',args.evidence/'render-sweater-commands.py')
    shutil.copyfile(args.input/'render_yarn.py',args.evidence/'render-yarn-commands.py')
    shutil.copyfile(args.input/'generation-revision.json',args.evidence/'generation-revision.json')
    for filename in ['daylight-environment.hdr','daylight-environment.hdr.json']:
        source = Path('build/pt-hair-sky')/filename
        shutil.copyfile(source,args.evidence/filename)
    report = dict(date='2026-10-09',device='Apple M4',scenes=scenes,input_sha256=frozen,output_sha256=outputs,
                  validator_sha256=sha(__file__),evidence_sha256={str(p):sha(p) for p in args.evidence.iterdir() if p.is_file()},
                  credit='Bunny: Stanford University Computer Graphics Laboratory; knitwear/yarn: procedural project fixtures',
                  limits=['Static reference-camera hair ribbons, no native curve intersection or dynamics',
                          'Knit loops are unrelaxed geometric prototypes; no yarn-level transport or certified stitch topology',
                          'OIDN display can smooth fine fibers; checks use raw scene-linear HDR',
                          'CPU/GPU finite-sample consistency, not an independent physical-reference comparison',
                          'Vulkan runs through MoltenVK; private runtime source audit is retained in hair-sunlit evidence'])
    (args.output/'grooms-sunlit-validation.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({name:dict(cpu_l1=s['metal_vs_cpu']['relative_rgb_l1'],gpu_exact=s['metal_vulkan_raw_identical'],
                              render_seconds=s['render']['render_seconds']) for name,s in scenes.items()},indent=2))


if __name__ == '__main__':
    main()
