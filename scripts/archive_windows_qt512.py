#!/usr/bin/env python3
"""Archive a verified portable directory without following symlinks or overwriting releases."""
import argparse
import zipfile
import os
from pathlib import Path
from verify_windows_qt512 import sha

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("package", type=Path)
parser.add_argument("--replace-existing", action="store_true", help="Atomically refresh this exact release ZIP after rebuilding")
args = parser.parse_args()
package = args.package.resolve()
target = package.with_name(package.name + ".zip")
if target.exists() and not args.replace_existing:
    raise FileExistsError(target)
temporary = target.with_suffix(".zip.pending")
files = sorted(package.rglob("*"))
if any(p.is_symlink() for p in files):
    raise RuntimeError("Symlinks are not allowed in a portable delivery")
with zipfile.ZipFile(temporary, "x", allowZip64=True) as archive:
    for source in files:
        if source.is_file() and source.name != ".DS_Store":
            compression = zipfile.ZIP_STORED if source.suffix == ".gguf" else zipfile.ZIP_DEFLATED
            archive.write(source, str(Path(package.name) / source.relative_to(package)), compress_type=compression)
os.replace(temporary, target)
target.with_suffix(".zip.sha256").write_text(sha(target) + "  " + target.name + "\n", encoding="utf-8")
print(str(target))
print(str(target.stat().st_size) + " bytes")
