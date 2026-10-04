#!/usr/bin/env python3
"""Fetch pinned classic benchmarks; large models remain outside Git."""
import argparse
import hashlib
import json
import shutil
import tarfile
import urllib.request
import zipfile
from pathlib import Path, PurePosixPath

PROJECT = Path(__file__).resolve().parents[1]
MANIFEST = PROJECT / 'samples' / 'benchmark-assets.json'


def digest(path):
    sha = hashlib.sha256()
    with path.open('rb') as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b''):
            sha.update(chunk)
    return sha.hexdigest()


def acquire(name, entry):
    archive = PROJECT / entry['cache']
    archive.parent.mkdir(parents=True, exist_ok=True)
    if not archive.exists():
        temporary = archive.with_suffix('.partial')
        request = urllib.request.Request(entry['url'], headers={'User-Agent': 'SceneRenderer-classic-gallery'})
        try:
            with urllib.request.urlopen(request, timeout=60) as source, temporary.open('wb') as output:
                shutil.copyfileobj(source, output, 1024 * 1024)
            if digest(temporary) != entry['sha256']:
                raise RuntimeError(f'{name}: archive checksum mismatch')
            temporary.replace(archive)
        finally:
            temporary.unlink(missing_ok=True)
    if digest(archive) != entry['sha256']:
        raise RuntimeError(f'{name}: cached archive checksum mismatch: {archive}')
    model = PROJECT / entry['model']
    model.parent.mkdir(parents=True, exist_ok=True)
    if entry['kind'] == 'zip':
        with zipfile.ZipFile(archive) as bundle:
            for item in bundle.infolist():
                path = PurePosixPath(item.filename.replace('\\', '/'))
                if path.is_absolute() or '..' in path.parts or ':' in str(path):
                    raise ValueError('Unsafe archive path')
                if item.is_dir():
                    continue
                if '__MACOSX' in path.parts:
                    continue
                if str(path) != entry['member'] and path.name.lower() not in ('readme', 'license', 'copyright') and path.suffix.lower() not in ('.mtl', '.png', '.jpg', '.jpeg', '.tga', '.txt', '.md'):
                    continue
                output = model if str(path) == entry['member'] else model.parent / str(path)
                output.parent.mkdir(parents=True, exist_ok=True)
                with bundle.open(item) as source, output.open('wb') as target:
                    shutil.copyfileobj(source, target)
    elif entry['kind'] == 'tar':
        with tarfile.open(archive, 'r:gz') as bundle:
            for member_name in [entry['member'], *entry.get('notices', [])]:
                member = bundle.getmember(member_name)
                if not member.isfile():
                    raise ValueError('Expected a regular file')
                output = model if member_name == entry['member'] else model.parent / PurePosixPath(member_name).name
                with bundle.extractfile(member) as source, output.open('wb') as target:
                    shutil.copyfileobj(source, target)
    else:
        raise ValueError('Unsupported archive type')
    if not model.is_file():
        raise RuntimeError(f'{name}: model missing after extraction')
    if entry.get('mesh_sha256') and digest(model) != entry['mesh_sha256']:
        raise RuntimeError(f'{name}: extracted mesh checksum mismatch')
    print(f'{name}: {model.relative_to(PROJECT)} ({model.stat().st_size:,} bytes)', flush=True)


def main():
    entries = json.loads(MANIFEST.read_text())
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--scene', choices=['all', *entries], default='all')
    args = parser.parse_args()
    for name in entries if args.scene == 'all' else [args.scene]:
        acquire(name, entries[name])


if __name__ == '__main__':
    main()
