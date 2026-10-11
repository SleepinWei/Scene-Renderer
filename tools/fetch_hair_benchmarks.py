#!/usr/bin/env python3
"""Fetch pinned author-hosted HAIR benchmarks; extract only named regular files."""
import argparse
import hashlib
import json
from pathlib import Path
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def fetch(asset, downloads, output):
    archive = downloads/asset['archive']
    downloads.mkdir(parents=True, exist_ok=True)
    if not archive.exists():
        temporary = archive.with_suffix('.part')
        request = urllib.request.Request(asset['url'], headers={'User-Agent': 'SceneRenderer-HairBenchmark'})
        with urllib.request.urlopen(request, timeout=60) as response, temporary.open('wb') as file:
            while chunk := response.read(1024*1024):
                file.write(chunk)
        if hashlib.sha256(temporary.read_bytes()).hexdigest() != asset['sha256']:
            temporary.unlink()
            raise ValueError('Downloaded hair archive checksum mismatch')
        temporary.replace(archive)
    if hashlib.sha256(archive.read_bytes()).hexdigest() != asset['sha256']:
        raise ValueError(f'Cached archive checksum mismatch: {archive}')
    output.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(archive) as package:
        for name in asset['members']:
            info = package.getinfo(name)
            if info.is_dir() or info.file_size > 150*1024*1024 or info.filename != Path(name).name:
                raise ValueError('Invalid benchmark archive member')
            data = package.read(info)
            if hashlib.sha256(data).hexdigest() != asset['member_sha256'][name]:
                raise ValueError('Extracted hair member checksum mismatch')
            (output/name).write_bytes(data)
            (output/(name+'.source.json')).write_text(json.dumps(dict(url=asset['url'], archive_sha256=asset['sha256'], sha256=hashlib.sha256(data).hexdigest()), indent=2)+'\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--scene', choices=['all', 'straight', 'wavy'], default='all')
    args = parser.parse_args()
    manifest = json.loads((ROOT/'samples/hair-benchmarks.json').read_text())
    for name in ['head', 'straight', 'wavy'] if args.scene == 'all' else ['head', args.scene]:
        asset = manifest['assets'][name]
        fetch(asset, ROOT/'samples/downloads/hair-benchmarks', ROOT/'samples/assets/pt/hair-benchmarks')
        print(asset['members'][0], flush=True)


if __name__ == '__main__':
    main()
