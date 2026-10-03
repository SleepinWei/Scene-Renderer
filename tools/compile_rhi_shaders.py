#!/usr/bin/env python3
"""Cook explicit RHI GLSL into SPIR-V, GL 410, reflection and optional metallib.

Metal buffers flatten groups (group * 8 + binding); textures and samplers
use compact indices independently per shader stage. Remapping operates on SPIR-V decorations, not shader source text.
"""
import argparse
import json
import hashlib
import re
import struct
import subprocess
from pathlib import Path


def run(args):
    subprocess.run([str(v) for v in args], check=True)


def flatten_bindings(src, dst, reflection):
    raw = src.read_bytes()
    words = list(struct.unpack(f'<{len(raw) // 4}I', raw))
    decorations = {}
    at = 5
    while at < len(words):
        count, opcode = words[at] >> 16, words[at] & 65535
        if not count or at + count > len(words):
            raise ValueError('malformed SPIR-V')
        if opcode == 71 and count == 4 and words[at + 2] in (33, 34):
            decorations.setdefault(words[at + 1], {})[words[at + 2]] = at + 3
        at += count
    # Combined image samplers share texture/sampler indices in MSL. Compact
    # each stage independently to respect Metal's 16 sampler argument slots.
    resource_map={}
    sampled=sorted((r.get('set',0)*8+r['binding']) for r in reflection.get('textures',[]))
    if len(sampled)>16:raise ValueError('Metal stage exceeds 16 sampled textures')
    images=sorted((r.get('set',0)*8+r['binding']) for r in reflection.get('images',[]))
    for index,binding in enumerate(sampled+images):resource_map[binding]=index
    for mapping in decorations.values():
        if 33 in mapping:
            group = words[mapping[34]] if 34 in mapping else 0
            binding = words[mapping[33]]
            if group >= 3 or binding >= 8:
                raise ValueError('RHI binding exceeds initial ABI')
            words[mapping[33]] = resource_map.get(group * 8 + binding,group * 8 + binding)
            if 34 in mapping:
                words[mapping[34]] = 0
    dst.write_bytes(struct.pack(f'<{len(words)}I', *words))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--metal', action='store_true')
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    root = Path(__file__).resolve().parents[1] / 'src/rhi/shaders'
    recipe=Path(__file__).read_bytes()+str(args.metal).encode()
    for tool in (['glslangValidator','--version'],['spirv-cross','--version']):
        version=subprocess.run(tool,capture_output=True,check=False);recipe+=version.stdout+version.stderr
    if args.metal:
        version=subprocess.run(['xcrun','-sdk','macosx','metal','--version'],capture_output=True,check=True);recipe+=version.stdout+version.stderr
    def inputs(path,visited=None):
        visited=set() if visited is None else visited
        if path in visited:return b''
        visited.add(path);body=path.read_bytes();result=str(path.relative_to(root)).encode()+body
        for include in re.findall(rb'^\s*#include\s+"([^"]+)"',body,re.MULTILINE):result+=inputs(root/include.decode(),visited)
        return result
    for src in sorted(root.iterdir()):
        if src.suffix not in ('.vert', '.frag', '.comp'):
            continue
        name = args.output / src.name
        spv = Path(str(name) + '.spv')
        digest=hashlib.sha256(recipe+inputs(src)).hexdigest();manifest=Path(str(name)+'.sha256')
        suffixes=['.spv','.json','.glsl']+(['.metallib','.metal'] if args.metal else [])
        if manifest.exists() and manifest.read_text()==digest and all(Path(str(name)+suffix).is_file() for suffix in suffixes):continue
        run(['glslangValidator', '-V', '--target-env', 'vulkan1.1', '-I' + str(root), src, '-o', spv])
        run(['spirv-cross', spv, '--reflect', '--output', str(name) + '.json'])
        flags = ['--flip-vert-y', '--fixup-clipspace'] if src.suffix == '.vert' else []
        run(['spirv-cross', spv, '--version', '430' if src.suffix == '.comp' or json.loads(Path(str(name)+'.json').read_text()).get('ssbos') else '410', '--no-420pack-extension', *flags, '--output', str(name) + '.glsl'])
        if args.metal:
            flattened = Path(str(name) + '.msl.spv')
            flatten_bindings(spv, flattened,json.loads(Path(str(name)+".json").read_text()))
            msl = Path(str(name) + '.metal')
            run(['spirv-cross', flattened, '--msl', '--msl-version', '23000', '--msl-decoration-binding', '--output', msl])
            air = Path(str(name) + '.air')
            run(['xcrun', '-sdk', 'macosx', 'metal', '-std=macos-metal2.3', '-fmodules-cache-path=' + str(args.output / 'cache'), '-c', msl, '-o', air])
            run(['xcrun', '-sdk', 'macosx', 'metallib', air, '-o', str(name) + '.metallib'])
        manifest.write_text(digest)


if __name__ == '__main__':
    main()
