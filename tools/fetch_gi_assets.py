#!/usr/bin/env python3
"""Download official GI benchmark scenes and extract their runtime variants."""
import argparse
import hashlib
import json
import shutil
import urllib.request
import zipfile
from pathlib import Path

PROJECT = Path(__file__).resolve().parents[1]
MANIFEST = PROJECT / 'samples' / 'gi-assets.json'
CACHE = PROJECT / 'samples' / 'downloads'
DEST = PROJECT / 'samples' / 'assets' / 'gi'
SOURCES = {
    'sponza': 'https://casual-effects.com/g3d/data10/common/model/crytek_sponza/sponza.zip',
    'san-miguel': 'https://casual-effects.com/g3d/data10/research/model/San_Miguel/San_Miguel.zip',
}

def digest(path):
    sha = hashlib.sha256()
    with path.open('rb') as source:
        for block in iter(lambda: source.read(1024 * 1024), b''):
            sha.update(block)
    return sha.hexdigest()

def acquire(name, entry):
    CACHE.mkdir(parents=True, exist_ok=True)
    archive = CACHE / (name + '.zip')
    if not archive.exists():
        request = urllib.request.Request(entry['url'], headers={'User-Agent': 'SceneRenderer-GI-gallery'})
        temporary = archive.with_suffix('.partial')
        with urllib.request.urlopen(request, timeout=60) as response, temporary.open('wb') as output:
            total = 0
            while block := response.read(1024 * 1024):
                output.write(block)
                total += len(block)
                if total % (64 * 1024 * 1024) == 0:
                    print(f'{name}: downloaded {total // (1024 * 1024)} MiB', flush=True)
        temporary.replace(archive)
    checksum = digest(archive)
    if entry.get('sha256') and checksum != entry['sha256']:
        raise RuntimeError(f'Archive checksum changed for {name}; remove {archive} and retry')
    entry['sha256'] = checksum
    with zipfile.ZipFile(archive) as bundle:
        meshes = [item for item in bundle.infolist() if item.filename.lower().endswith('.obj') and '__MACOSX' not in item.filename]
        if name == 'sponza':
            model = max(meshes, key=lambda item: item.file_size)
        else:
            low = [item for item in meshes if 'low' in item.filename.lower()]
            model = min(low or meshes, key=lambda item: item.file_size)
        print(f'{name}: using {model.filename} ({model.file_size // (1024 * 1024)} MiB OBJ)', flush=True)
        target = DEST / name
        target.mkdir(parents=True, exist_ok=True)
        entries = []
        for item in bundle.infolist():
            path = Path(item.filename.replace('\\', '/'))
            if path.is_absolute() or '..' in path.parts:
                raise ValueError('Unsafe archive path')
            if item.is_dir() or '__MACOSX' in path.parts:
                continue
            # Omit high-resolution meshes and authoring files; keep material resources and notices.
            if item.filename != model.filename and path.suffix.lower() not in ('.mtl', '.png', '.jpg', '.jpeg', '.tga', '.bmp', '.txt', '.md', '.pdf'):
                continue
            output = target / path
            output.parent.mkdir(parents=True, exist_ok=True)
            with bundle.open(item) as source, output.open('wb') as destination:
                shutil.copyfileobj(source, destination)
            entries.append(str(path))
        entry['model'] = str((target / model.filename).relative_to(PROJECT))
        entry['runtime_files'] = len(entries)
        print(f'{name}: extracted {len(entries)} runtime files', flush=True)
    return entry

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--scene', choices=['all', *SOURCES], default='all')
    args = parser.parse_args()
    entries = json.loads(MANIFEST.read_text()) if MANIFEST.exists() else {name: {'url': url} for name, url in SOURCES.items()}
    names = list(SOURCES) if args.scene == 'all' else [args.scene]
    for name in names:
        entries[name] = acquire(name, entries[name])
        MANIFEST.write_text(json.dumps(entries, indent=2) + '\n')

if __name__ == '__main__':
    main()
