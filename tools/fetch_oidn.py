#!/usr/bin/env python3
"""Fetch the pinned official macOS ARM64 OIDN package into an ignored build directory."""
import argparse
import hashlib
import pathlib
import platform
import tarfile
import urllib.request

VERSION = "2.5.1"
ARCHIVE = f"oidn-{VERSION}.arm64.macos.tar.gz"
URL = f"https://github.com/RenderKit/oidn/releases/download/v{VERSION}/{ARCHIVE}"
# Digest of the official HTTPS release archive, pinned to make repeat fetches reproducible.
SHA256 = "98e0aca8e7ab69e9f4f0191582a500fd8b0d9085d68662f18ab6469e934a6efd"

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--destination", type=pathlib.Path, default=pathlib.Path("build/deps"))
    args = parser.parse_args()
    if platform.system() != "Darwin" or platform.machine() not in ("arm64", "aarch64"):
        parser.error("This helper pins macOS ARM64 only; use the official OIDN package for your platform.")
    args.destination.mkdir(parents=True, exist_ok=True)
    archive = args.destination / ARCHIVE
    if not archive.exists():
        temporary = archive.with_suffix(archive.suffix + ".part")
        with urllib.request.urlopen(URL) as response, temporary.open("wb") as output:
            while chunk := response.read(1024 * 1024):
                output.write(chunk)
        if hashlib.sha256(temporary.read_bytes()).hexdigest() != SHA256:
            temporary.unlink()
            raise RuntimeError("Downloaded official archive checksum mismatch")
        temporary.replace(archive)
    if hashlib.sha256(archive.read_bytes()).hexdigest() != SHA256:
        raise RuntimeError(f"Checksum mismatch: remove {archive} and retry")
    with tarfile.open(archive) as package:
        root = args.destination.resolve()
        for member in package.getmembers():
            target = (root / member.name).resolve()
            if root not in target.parents or member.islnk() or member.isdev():
                raise RuntimeError(f"Unsafe archive entry: {member.name}")
            if member.issym():
                link = (target.parent / member.linkname).resolve()
                if root not in link.parents:
                    raise RuntimeError(f"Unsafe archive link: {member.name}")
        package.extractall(root)
    print((root / f"oidn-{VERSION}.arm64.macos/lib/cmake/OpenImageDenoise-{VERSION}").as_posix())

if __name__ == "__main__":
    main()
