#!/usr/bin/env python3
"""Fetch Stanford's credited Dragon reconstruction, without extracting archive paths."""
import argparse
import hashlib
import json
from pathlib import Path
import tarfile
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
URL = "https://graphics.stanford.edu/pub/3Dscanrep/dragon/dragon_recon.tar.gz"
SHA256 = "74ac1d90989c9b1732edee82d57e9ce71452144cf4355f108d8c9c616d28d02f"

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--resolution", choices=["full", "2", "3", "4"], default="full")
    args = parser.parse_args()
    archive = ROOT / "samples/downloads/stanford-dragon-recon.tar.gz"
    archive.parent.mkdir(parents=True, exist_ok=True)
    if not archive.exists():
        temporary = archive.with_suffix(".part")
        request = urllib.request.Request(URL, headers={"User-Agent": "SceneRenderer-PT"})
        with urllib.request.urlopen(request, timeout=60) as response, temporary.open("wb") as out:
            while chunk := response.read(1024 * 1024):
                out.write(chunk)
        if hashlib.sha256(temporary.read_bytes()).hexdigest() != SHA256:
            temporary.unlink()
            raise RuntimeError("Stanford Dragon archive checksum mismatch")
        temporary.replace(archive)
    if hashlib.sha256(archive.read_bytes()).hexdigest() != SHA256:
        raise RuntimeError(f"Archive checksum mismatch: remove {archive} and retry")
    name = "dragon_vrip" + ("" if args.resolution == "full" else f"_res{args.resolution}") + ".ply"
    directory = ROOT / "samples/assets/pt/dragon"
    directory.mkdir(parents=True, exist_ok=True)
    with tarfile.open(archive) as package:
        data = package.extractfile("dragon_recon/" + name).read()
    (directory / name).write_bytes(data)
    (directory / (name + ".json")).write_text(json.dumps({"source": URL, "credit": "Stanford University Computer Graphics Laboratory", "file": name, "sha256": hashlib.sha256(data).hexdigest(), "resolution": args.resolution}, indent=2) + "\n")
    print(directory / name)

if __name__ == "__main__":
    main()
