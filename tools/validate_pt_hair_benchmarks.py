#!/usr/bin/env python3
"""Save raw HDR comparisons and credited previews for Yuksel hair benchmarks.

Requires NumPy/Pillow. PBRT native curves differ from SceneRenderer ribbons;
image errors are recorded, not accepted as an appearance-accuracy pass.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess

import numpy as np
from PIL import Image
from compare_pt_references import read, metrics, blocks


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input',type=Path,default=Path('build/pt-hair-benchmarks'))
    parser.add_argument('--output',type=Path,default=Path('img/path-tracing'))
    parser.add_argument('--evidence',type=Path,default=Path('img/diagnostics/materials/hair-benchmarks'))
    parser.add_argument('--pbrt-source',type=Path,default=Path('build/deps/pbrt-v3'))
    parser.add_argument('--source-freeze',type=Path,help='Optional source/executable hashes captured before running checks')
    args=parser.parse_args()
    args.output.mkdir(parents=True,exist_ok=True);args.evidence.mkdir(parents=True,exist_ok=True)
    manifest=json.loads(Path('samples/hair-benchmarks.json').read_text())
    freeze_path=args.source_freeze or Path('build/pt-hair-benchmark-source-freeze.json')
    preexisting_freeze=freeze_path.is_file()
    if preexisting_freeze:
        freeze=json.loads(freeze_path.read_text())
    else:
        paths=['tools/fetch_hair_benchmarks.py','tools/pt_hair_file.py','tools/create_pt_hair_benchmarks.py',
               'tools/run_pbrt_hair_probe.py','tools/validate_pt_hair_benchmarks.py','tools/pt_groom.py',
               'tests/PT/HairBenchmarkTests.py','tests/PT/PbrtHairProbe.cpp','samples/hair-benchmarks.json',
               'src/rhi/shaders/pt-fiber.glsl','src/PT/FiberScattering.h','build/Scene-Renderer']
        freeze={p:sha(p) for p in paths}
    for path,digest in freeze.items():
        if sha(path)!=digest:raise ValueError('Implementation changed after freeze: '+path)
    checks={};inputs={};outputs={}
    for name in ('straight','wavy'):
        root=args.input/name
        imported=json.loads((root/'import.json').read_text());scene=json.loads((root/'scene.json').read_text())
        assert imported['strands']==50000 and imported['points']==manifest['assets'][name]['points']
        assert imported['original_hair_sha256']==manifest['assets'][name]['member_sha256']['wStraight.hair' if name=='straight' else 'wWavy.hair']
        for p in root.glob('*.bin'):inputs[str(p)]=sha(p)
        for pattern in ('scene.json','import.json','reference.pbrt','curves.pbrt','mesh-*.ply'):
            for p in root.glob(pattern):inputs[str(p)]=sha(p)
        beauty=json.loads((root/'metal.json').read_text());filtered=json.loads((root/'filtered.json').read_text())
        assert [beauty[k] for k in ('width','height','samples','max_depth','seed')]==[512,512,256,12,1]
        assert beauty['non_finite_samples']==0 and not beauty['adaptive']
        assert filtered['denoiser']=='Open Image Denoise 2.5.1' and filtered['denoise_device']=='CPU'
        images={};reports={}
        for backend in ('cpu','metal','vulkan'):
            prefix=root/('check-'+backend);reports[backend]=json.loads(prefix.with_suffix('.json').read_text())
            r=reports[backend]
            assert [r[k] for k in ('width','height','samples','max_depth','seed')]==[128,128,256,12,1]
            assert r['non_finite_samples']==0 and not r['adaptive'] and not r['denoiser']
            images[backend]=read(prefix.with_suffix('.pfm'));assert np.isfinite(images[backend]).all()
        reference=read(root/'reference.pfm');assert reference.shape==(128,128,3) and np.isfinite(reference).all()
        native=(root/'reference.pbrt').read_text()
        assert re.search(r'pixelsamples" \[1024\]',native) and re.search(r'maxdepth" \[12\]',native)
        parity={a+'_vs_'+b:metrics(images[a],images[b]) for a,b in [('metal','cpu'),('vulkan','cpu'),('vulkan','metal')]}
        assert parity['metal_vs_cpu']['relative_rgb_l1']<.05
        assert np.array_equal(images['metal'],images['vulkan'])
        comparison=dict(raw=metrics(images['metal'],reference),block4=metrics(blocks(images['metal']),blocks(reference)),
                        status='Measured residual; different geometry representations and sample budgets, not an accuracy pass',
                        settings=dict(size=[128,128],metal_spp=256,pbrt_spp=1024,max_depth=12,exposure_fit=False,pixel_alignment=False))
        v=np.power(1-np.exp(-np.maximum(reference,0)),1/2.2)
        ref_png=args.output/('yuksel-'+name+'-pbrt.png')
        Image.fromarray(np.uint8(np.clip(v,0,1)*255+.5)).save(ref_png)
        outputs[str(ref_png)]=sha(ref_png)
        for source,destination in [('metal.png','-raw.png'),('filtered-denoised.png','.png')]:
            target=args.output/('yuksel-'+name+destination)
            assert Image.open(root/source).size==(512,512)
            shutil.copyfile(root/source,target);outputs[str(target)]=sha(target)
        for p in root.iterdir():
            if p.suffix=='.pfm' or p.name in ('metal.json','filtered.json','check-cpu.json','check-metal.json','check-vulkan.json'):
                outputs[str(p)]=sha(p)
        for filename in ('metal.txt','denoise.txt','check-cpu.txt','check-metal.txt','check-vulkan.txt','pbrt-reference.txt'):
            target=args.evidence/(name+'-'+filename);shutil.copyfile(root/filename,target)
        report=dict(imported=imported,beauty=beauty,filter=filtered,parity_reports=reports,
                    backend_comparison=parity,metal_vulkan_raw_identical=True,pbrt_comparison=comparison)
        (args.evidence/(name+'.json')).write_text(json.dumps(report,indent=2)+'\n')
        checks[name]=report
    probe=json.loads((args.input/'pbrt-hair-probe.json').read_text());assert probe['relative_rgb_l1']<.01 and probe['direction_pairs']==61440
    shutil.copyfile(args.input/'pbrt-hair-probe.json',args.evidence/'pbrt-hair-probe.json')
    shutil.copyfile('build/pt-hair-benchmark-tests.txt',args.evidence/'import-tests.txt')
    commit=subprocess.check_output(['git','-C',str(args.pbrt_source),'rev-parse','HEAD'],text=True).strip()
    submodules=subprocess.check_output(['git','-C',str(args.pbrt_source),'submodule','status'],text=True)
    data=dict(date='2026-10-09',device='Apple M4',source_page=manifest['source_page'],credit=manifest['credit'],
              implementation_sha256=freeze,preexisting_source_freeze_checked=preexisting_freeze,
              input_sha256=inputs,output_sha256=outputs,
              evidence_sha256={str(p):sha(p) for p in args.evidence.iterdir() if p.is_file()},
              pbrt=dict(repository='https://github.com/mmp/pbrt-v3',commit=commit,submodules=submodules,
                        build='Release, RGBSpectrum, no upstream source modifications'),
              equation_probe=probe,scenes=checks,
              limits=['All 50000 source strands/points/thickness kept; no synthetic taper/resampling',
                      'Source attributes retained in original HAIR; fixed absorption preset and neutral diffuse head',
                      'SceneRenderer uses fixed-eye triangle ribbons; PBRT native flat curves face each ray',
                      'Equation probe is independent of geometry and integration; full images remain unaligned',
                      'PBRT 1024 spp is a reference image with residual sampling noise, not analytic ground truth',
                      'Preview uses OIDN; all numerical comparisons use raw HDR without exposure fitting',
                      'Vulkan via MoltenVK, no Khronos validation layer; no fair throughput comparison claimed'])
    (args.output/'yuksel-hair-validation.json').write_text(json.dumps(data,indent=2)+'\n')
    print(json.dumps({name:dict(backend_l1=r['backend_comparison']['metal_vs_cpu']['relative_rgb_l1'],pbrt_l1=r['pbrt_comparison']['raw']['relative_rgb_l1'],pbrt_block4_l1=r['pbrt_comparison']['block4']['relative_rgb_l1']) for name,r in checks.items()},indent=2))


if __name__=='__main__':main()
