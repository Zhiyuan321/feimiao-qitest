#!/usr/bin/env python3
"""Verify and extract a release ZIP to a new project-local acceptance directory."""
import argparse
import hashlib
from pathlib import Path, PurePosixPath
import zipfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("archive", type=Path)
parser.add_argument("destination", type=Path, nargs="?", help="Omit to verify without extraction")
args = parser.parse_args()
project = Path(__file__).resolve().parent.parent
destination = args.destination.resolve() if args.destination else None
if destination is not None and (project not in destination.parents or destination.exists()):
    raise RuntimeError("Use a new acceptance directory inside the source project")
with zipfile.ZipFile(args.archive) as archive:
    names = archive.namelist()
    if len(set(names)) != len(names):
        raise RuntimeError("Duplicate ZIP entries")
    roots = set()
    for name in names:
        path = PurePosixPath(name)
        if path.is_absolute() or ".." in path.parts or "\\" in name:
            raise RuntimeError("Unsafe ZIP entry: " + name)
        roots.add(path.parts[0])
    if len(roots) != 1:
        raise RuntimeError("Expected one portable application folder")
    root = next(iter(roots))
    manifest_name = root + "/SHA256SUMS.txt"
    manifest = archive.read(manifest_name).decode("utf-8").splitlines()
    expected_files = {manifest_name}
    for line in manifest:
        digest, relative = line.split("  ", 1)
        name = root + "/" + str(PurePosixPath(relative))
        expected_files.add(name)
        actual = hashlib.sha256()
        with archive.open(name) as stream:
            for block in iter(lambda: stream.read(1024 * 1024), b""):
                actual.update(block)
        if actual.hexdigest() != digest:
            raise RuntimeError("Archive checksum mismatch: " + name)
    if set(names) != expected_files:
        raise RuntimeError("Unlisted or missing archive files")
    # Reading every entry above also checks ZIP CRC, including the model.
    if destination is not None:
        archive.extractall(destination)
print("PASS: every archived file matches SHA256 and ZIP CRC, including model weights")
if destination is not None:
    print(destination / root)
