#!/usr/bin/env python3
"""Compile/run the optional hair equation probe against a PBRT v3 Makefiles build."""
import argparse
import hashlib
import json
from pathlib import Path
import shlex
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build',type=Path,default=ROOT/'build/deps/pbrt-v3-build')
    parser.add_argument('--output',type=Path,default=ROOT/'build/pt-hair-benchmarks/pbrt-hair-probe.json')
    args = parser.parse_args()
    build = args.build.resolve()
    directory = build/'CMakeFiles/pbrt_exe.dir'
    flags = dict(line.split(' = ',1) for line in (directory/'flags.make').read_text().splitlines() if ' = ' in line)
    link = shlex.split((directory/'link.txt').read_text())
    obj = build/'hair-probe.o'
    exe = build/'hair-probe'
    includes = shlex.split(flags['CXX_INCLUDES'])
    compile_command = [link[0],'-std=c++17',*shlex.split(flags['CXX_DEFINES']),*includes,
                       '-I'+str(ROOT/'external'),*shlex.split(flags['CXX_FLAGS']),
                       '-c',str(ROOT/'tests/PT/PbrtHairProbe.cpp'),'-o',str(obj)]
    subprocess.run(compile_command,cwd=build,check=True)
    link = [str(obj) if arg.endswith('/pbrt.cpp.o') else arg for arg in link]
    link[link.index('-o')+1] = str(exe)
    subprocess.run(link,cwd=build,check=True)
    result = subprocess.run([str(exe)],check=True,capture_output=True,text=True)
    data = json.loads(result.stdout)
    data['probe_executable_sha256'] = hashlib.sha256(exe.read_bytes()).hexdigest()
    data['source_sha256'] = {str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in
                           [ROOT/'tests/PT/PbrtHairProbe.cpp',ROOT/'src/rhi/shaders/pt-fiber.glsl',ROOT/'src/PT/FiberScattering.h']}
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps(data,indent=2)+'\n')
    print(result.stdout,end='')


if __name__ == '__main__':
    main()
