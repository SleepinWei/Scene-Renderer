#!/usr/bin/env python3
"""Fetch the credited classic gallery models; run from any directory."""
import hashlib
import io
import json
import tarfile
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1] / 'samples' / 'assets'
BUNNY = 'https://graphics.stanford.edu/pub/3Dscanrep/bunny.tar.gz'
REPO = 'https://api.github.com/repos/KhronosGroup/glTF-Sample-Assets/commits/main'

def fetch(url):
    request = urllib.request.Request(url, headers={'User-Agent': 'SceneRenderer-classic-gallery'})
    with urllib.request.urlopen(request, timeout=60) as response:
        return response.read()

def save(path, data, url, entries):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)
    entries.append({'file': str(path.relative_to(ROOT)), 'source': url,
                    'sha256': hashlib.sha256(data).hexdigest()})
    print(f'{path.relative_to(ROOT)}: {len(data)} bytes', flush=True)

def main():
    ROOT.mkdir(parents=True, exist_ok=True)
    manifest = ROOT / 'manifest.json'
    if manifest.exists():
        old = json.loads(manifest.read_text())
        revision = old['khronos_revision']
    else:
        revision = json.loads(fetch(REPO))['sha']
    entries = []
    with tarfile.open(fileobj=io.BytesIO(fetch(BUNNY)), mode='r:gz') as archive:
        # Extract only this mesh, never paths supplied by an archive.
        data = archive.extractfile('bunny/reconstruction/bun_zipper.ply').read()
        save(ROOT / 'bunny' / 'bun_zipper.ply', data, BUNNY, entries)
    base = f'https://raw.githubusercontent.com/KhronosGroup/glTF-Sample-Assets/{revision}/Models/DamagedHelmet/glTF/'
    gltf = fetch(base + 'DamagedHelmet.gltf')
    save(ROOT / 'damaged-helmet' / 'DamagedHelmet.gltf', gltf, base + 'DamagedHelmet.gltf', entries)
    model = json.loads(gltf)
    for name in dict.fromkeys(item['uri'] for item in model['buffers'] + model['images']):
        if Path(name).name != name:
            raise ValueError('Expected flat model resource paths')
        save(ROOT / 'damaged-helmet' / name, fetch(base + name), base + name, entries)
    manifest.write_text(json.dumps({'khronos_revision': revision, 'files': entries}, indent=2) + '\n')

if __name__ == '__main__':
    main()
