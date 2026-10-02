#!/usr/bin/env python3
"""Compile the existing effect sources to MSL + resource reflection, without OpenGL.

Geometry amplification is expressed as layered Metal passes. Tessellation uses
compute control-point/factor generation followed by native Metal patch drawing.
Requires glslangValidator, spirv-cross and Xcode's Metal compiler.
"""
import argparse, json, re, subprocess, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
STAGES = {'.vs':'vert', '.fs':'frag', '.comp':'comp', '.tesc':'tesc', '.tese':'tese'}

def run(args):
    p = subprocess.run(list(map(str,args)), capture_output=True, text=True)
    if p.returncode: raise RuntimeError(p.stdout + p.stderr)
    return p.stdout

def compile_one(path, out, temp, variant=''):
    stage = STAGES[path.suffix]
    source = path.read_text(encoding='utf-8-sig', errors='replace')
    source = re.sub(r'/\*.*?\*/|//[^\n]*', '', source, flags=re.S)
    source = re.sub(r'#version[^\n]*', '#version 460 core', source)
    # Geometry stages have no Metal equivalent: select the face/cascade in VS.
    if path.name == 'point_shadow_depth.vs':
        source = source.replace('void main()', 'uniform mat4 shadowMatrices[6];\nuniform int srLayer;\nout vec4 FragPos;\nvoid main()')
        source = source.replace('gl_Position = model * vec4(aPos, 1.0);', 'FragPos = model * vec4(aPos,1.0); gl_Position = shadowMatrices[srLayer] * FragPos;')
    if path.name == 'cascaded_shadow_depth.vs':
        source = source.replace('void main()', 'uniform mat4 lightSpaceMatrices[16];\nuniform int srLayer;\nvoid main()')
        source = source.replace('gl_Position=model*vec4(aPos,1.0);', 'gl_Position=lightSpaceMatrices[srLayer]*model*vec4(aPos,1.0);')
    if path.name in ("point_shadow_depth.vs", "cascaded_shadow_depth.vs"):
        source = source.replace("VertexUV", "ShadowUV")
    # Fix inter-stage locations by semantic name. Independent SPIR-V stages
    # cannot rely on declaration order (the original GL linker matched names).
    varyings={'Normal':0,'FragPos':1,'FragPosLightSpace':2,'texCoords':3,
              'FragTexCoord':4,'TexCoords':0,'TexCoord':0,'TextureCoord':0,
              'viewDirection':0,'FragColor':0,'VertexUV':2,'ShadowUV':2}
    source = re.sub(r'(?m)^(\s*)((?:in|out)\s+(?:vec[234]|float)\s+(\w+)\s*;)',
                    lambda m:m[1]+(f'layout(location={varyings[m[3]]}) ' if m[3] in varyings else '')+m[2],source)
    # Old GLSL contains numeric overloads accepted by drivers but rejected by SPIR-V.
    source = source.replace('texture2D(', 'texture(')
    source = re.sub(r'layout\(([^)]*)\)(\s*(?:readonly\s+|writeonly\s+)?buffer\b)', r'layout(set=1,\1)\2',source)
    source = re.sub(r'\batan2\b','oceanAtan2',source)
    src = temp / ('input.' + stage)
    src.write_text(source)
    spv = temp / 'shader.spv'
    run(['glslangValidator','-V','-R','--auto-map-bindings','--auto-map-locations',
         '--set-default-uniform-block','DefaultUniforms','0','30',src,'-o',spv])
    flags = ['--msl','--msl-version','23000','--msl-pad-fragment-output']
    if stage in ('vert','tese'): flags += ['--fixup-clipspace']
    if stage == 'tesc': flags += ['--msl-multi-patch-workgroup']
    if stage == 'tese': flags += ['--msl-raw-buffer-tese-input']
    if variant: flags += ['--msl-vertex-for-tessellation']
    msl = run(['spirv-cross',spv,*flags])
    reflection = json.loads(run(['spirv-cross',spv,'--reflect']))
    # Cross's CLI doesn't accept the linked tessellation domain/control-point count.
    points = 3 if 'pbr' in str(path) else 4
    if stage == 'tesc' and points == 3:
        msl = msl.replace('MTLQuadTessellationFactorsHalf','MTLTriangleTessellationFactorsHalf')
        msl = msl.replace('.insideTessellationFactor[0]', '.insideTessellationFactor')
    if stage == 'tese':
        msl = msl.replace('patch(triangle, 0)',f'patch(triangle, {points})').replace('patch(quad, 0)',f'patch(quad, {points})')
        msl = msl.replace('gl_PrimitiveID * 0',f'gl_PrimitiveID * {points}')
    # Sampler arrays reuse a sampler state, preserving per-resource filtering
    # without exhausting Metal's sampler slots for shadow map arrays.
    sampler_args = re.findall(r'(?:array<sampler,\s*\d+>|sampler) (\w+) \[\[sampler\((\d+)\)\]\]',msl)
    for index,(name, _) in enumerate(sampler_args):
        msl = re.sub(r'array<sampler,\s*\d+> '+name+r' \[\[sampler\(\d+\)\]\]', 'sampler '+name+f' [[sampler({index})]]', msl)
        msl = re.sub(r'thread const array<sampler,\s*\d+>& '+name, 'sampler '+name,msl)
        msl = re.sub(r'\b'+name+r'\[[^\]]+\]',name,msl)
        msl = re.sub(r'sampler '+name+r' \[\[sampler\(\d+\)\]\]', 'sampler '+name+f' [[sampler({index})]]',msl)
    # Cross emits explicit std140 padding; a zero-padding specialization avoids
    # illegal zero-length arrays while retaining the reflected buffer stride.
    for typename in ('Patch_1','Patch_2'):
        definition = re.search(r'struct '+typename+r'\s*\{[^}]*\};',msl)
        if definition and f'spvPaddedArrayElement<{typename}, 128>' in msl:
            at=definition.end()
            msl=msl[:at]+f'\ntemplate<> struct spvPaddedArrayElement<{typename}, 128> {{ {typename} data; }};\n'+msl[at:]
    if stage in ('vert','tese') and not variant:
        msl = msl.replace('main0(', 'main0(constant float& srTargetFlip [[buffer(27)]], ',1)
        msl = msl.replace('return out;', 'out.gl_Position.y *= srTargetFlip;\n    return out;')
    # Reflect actual MSL indices, because Cross allocates separate namespaces.
    for kind in ('ubos','ssbos','textures','images'):
        for resource in reflection.get(kind,[]):
            typ = reflection.get('types',{}).get(resource['type'],{}).get('name','')
            name = re.sub(r'[^A-Za-z0-9_]','_',resource['name'])
            annotation = 'buffer' if kind in ('ubos','ssbos') else 'texture'
            pattern = (r'\b'+re.escape(typ)+r'[&*] \w+ \[\[buffer\((\d+)\)\]\]') if typ else (r'\b'+re.escape(name)+r' \[\[texture\((\d+)\)\]\]')
            match = re.search(pattern,msl)
            if match: resource['msl_index'] = int(match.group(1))
            else: resource['inactive'] = True
    reflection['samplers'] = {name.removesuffix('Smplr'):index for index,(name,_) in enumerate(sampler_args)}
    reflection['metal_stage'] = 'comp' if variant or stage == 'tesc' else stage
    reflection['source'] = str(path.relative_to(ROOT))
    reflection['control_points'] = points
    name = str(path.relative_to(ROOT / 'src/shader')) + variant
    dst = out / (name + '.metal')
    dst.parent.mkdir(parents=True, exist_ok=True)
    dst.write_text(msl)
    dst.with_suffix('.json').write_text(json.dumps(reflection,indent=2))
    run(['xcrun','-sdk','macosx','metal','-std=macos-metal2.3','-fmodules-cache-path='+str(temp/'cache'),'-c',dst,'-o',temp/'shader.air'])
    # Persist metallib per stage: no source compilation or GLSL tools at runtime.
    run(['xcrun','-sdk','macosx','metallib',temp/'shader.air','-o',dst.with_suffix('.metallib')])

if __name__ == '__main__':
    p = argparse.ArgumentParser()
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--filter',default='')
    a = p.parse_args()
    failures = []
    with tempfile.TemporaryDirectory(prefix='sr-metal-') as d:
        for path in sorted((ROOT/'src/shader').rglob('*')):
            if path.suffix not in STAGES or a.filter not in str(path): continue
            if '副本' in path.name: continue
            try:
                compile_one(path,a.output,Path(d))
                if path.name in ('pbr_tess.vs','terrain_.vs'): compile_one(path,a.output,Path(d),'.capture')
                print('OK',path.relative_to(ROOT),flush=True)
            except RuntimeError as e:
                failures.append(path)
                print('FAIL',path.relative_to(ROOT),str(e),flush=True)
    if failures: raise SystemExit(f'{len(failures)} shader stages failed')
