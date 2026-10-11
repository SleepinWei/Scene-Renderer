#!/usr/bin/env python3
"""Download/check CC0 source files and extract Coastal Cliff's original LOD0.

Optional --source-folder reuses already downloaded files after MD5 verification.
Assets remain in the existing ignored benchmark directory.
Powered by Poly Haven: https://polyhaven.com/
"""
import argparse
import hashlib
import json
import pathlib
import subprocess
import urllib.request

ROOT = pathlib.Path(__file__).resolve().parents[1]
OUT = ROOT / 'samples/assets/benchmarks/coastal-cliff'

def fetch(url):
    request = urllib.request.Request(url, headers={'User-Agent': 'SceneRenderer-CC0-Benchmark/1.0'})
    with urllib.request.urlopen(request, timeout=120) as response:
        return response.read()

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-folder', type=pathlib.Path)
    args = parser.parse_args()
    OUT.mkdir(parents=True, exist_ok=True)
    catalog = json.loads(fetch('https://api.polyhaven.com/files/coastal_cliff_02'))
    files = {
        'coastal_cliff_02.fbx': catalog['fbx']['2k']['fbx'],
        'coastal_cliff_02_diff_2k.jpg': catalog['Diffuse']['2k']['jpg'],
        'coastal_cliff_02_nor_gl_2k.jpg': catalog['nor_gl']['2k']['jpg'],
        'coastal_cliff_02_rough_2k.jpg': catalog['Rough']['2k']['jpg'],
    }
    provenance = {'source': 'https://polyhaven.com/a/coastal_cliff_02', 'license': 'CC0', 'files': {}}
    for name, info in files.items():
        destination = OUT / name
        source = args.source_folder / name if args.source_folder else None
        data = destination.read_bytes() if destination.exists() else None
        if data is None or hashlib.md5(data).hexdigest() != info['md5']:
            data = source.read_bytes() if source and source.exists() else fetch(info['url'])
            if len(data) != info['size'] or hashlib.md5(data).hexdigest() != info['md5']:
                raise RuntimeError(f'Published checksum mismatch: {name}')
            destination.write_bytes(data)
        provenance['files'][name] = {'url': info['url'], 'bytes': len(data), 'md5': info['md5'], 'sha256': hashlib.sha256(data).hexdigest()}
        print(f'Ready: {name} ({len(data)} bytes)', flush=True)
    binary = ROOT / 'build/extract_cliff_lod0'
    binary.parent.mkdir(parents=True, exist_ok=True)
    # Homebrew is the repository's macOS Assimp dependency provider.
    subprocess.run(['c++', '-std=c++17', str(ROOT / 'tools/extract_cliff_lod0.cpp'),
                    '-I/opt/homebrew/include', '-L/opt/homebrew/lib', '-lassimp', '-o', str(binary)], check=True)
    obj = OUT / 'coastal_cliff_02_lod0.obj'
    subprocess.run([str(binary), str(OUT / 'coastal_cliff_02.fbx'), str(obj)], check=True)
    provenance['extraction'] = {'triangles': 943284, 'simplified': False, 'traditional_lods_excluded': [1, 2, 3],
                                'output_bytes': obj.stat().st_size, 'output_sha256': hashlib.sha256(obj.read_bytes()).hexdigest()}
    (OUT / 'provenance.json').write_text(json.dumps(provenance, indent=2) + '\n')
    project = ROOT / 'build/unreal-cliff'
    (project / 'Config').mkdir(parents=True, exist_ok=True)
    (project / 'CliffNanite.uproject').write_text(json.dumps({
        'FileVersion': 3, 'EngineAssociation': '5.8', 'DisableEnginePluginsByDefault': True,
        'Plugins': [{'Name': 'PythonScriptPlugin', 'Enabled': True}, {'Name': 'EditorScriptingUtilities', 'Enabled': True}]
    }, indent=2) + '\n')
    (project / 'Config/DefaultEngine.ini').write_text('''[/Script/EngineSettings.GameMapsSettings]
EditorStartupMap=/Game/Cliff/Canyon

[/Script/Engine.RendererSettings]
r.Nanite.ProjectEnabled=True
r.DynamicGlobalIlluminationMethod=0
r.ReflectionMethod=0
r.GenerateMeshDistanceFields=False
r.AntiAliasingMethod=1
r.DefaultFeature.AutoExposure=False

[ConsoleVariables]
Interchange.FeatureFlags.Import.FBX=0
''')

if __name__ == '__main__':
    main()
