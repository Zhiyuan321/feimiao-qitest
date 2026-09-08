#!/usr/bin/env python3
"""Refresh an existing staged Qt5 package, verify it, then promote without overwrite."""
import argparse
import shutil
import subprocess
import sys
from pathlib import Path
from verify_windows_qt512 import sha

root = Path(__file__).resolve().parent.parent
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("stage", type=Path)
parser.add_argument("destination", type=Path)
parser.add_argument("--objdump", default=shutil.which("x86_64-w64-mingw32-objdump"))
args = parser.parse_args()
stage, destination = args.stage.resolve(), args.destination.resolve()
if not stage.parent.name.startswith(".qt512-stage.") or not stage.is_dir() or destination.exists():
    raise RuntimeError("Expected a known staging folder and a new destination")
if root.parent not in stage.parents or root.parent not in destination.parents:
    raise RuntimeError("Both paths must remain inside this project's development directory")
shutil.copy2(root / "build-windows7-qt512/飞秒质谱工作站.exe", stage / "飞秒质谱工作站.exe")
shutil.copy2(root / "config/ai-model-manifest.json", stage / "resources/config/ai-model-manifest.json")
shutil.copy2(root / "docs/WINDOWS7_PORTABLE_README.txt", stage / "使用说明.txt")
objdump = args.objdump
if not objdump or not Path(objdump).is_file():
    raise RuntimeError("Specify --objdump with the cross-toolchain executable")
imports = []
for binary in sorted(stage.rglob("*")):
    if binary.is_symlink(): raise RuntimeError("Symlink in stage")
    if binary.suffix.lower() in (".exe", ".dll"):
        output = subprocess.check_output([objdump, "-p", str(binary)], text=True)
        imports.append(binary.relative_to(stage).as_posix())
        imports.extend("  " + line.split("DLL Name:", 1)[1].strip() for line in output.splitlines() if "DLL Name:" in line)
(stage / "WINDOWS-IMPORTS.txt").write_text("\n".join(imports) + "\n", encoding="utf-8")
manifest = [sha(path) + "  " + path.relative_to(stage).as_posix()
            for path in sorted(stage.rglob("*")) if path.is_file() and path.name != "SHA256SUMS.txt"]
(stage / "SHA256SUMS.txt").write_text("\n".join(manifest) + "\n", encoding="utf-8")
subprocess.run([sys.executable, str(root / "scripts/verify_windows_qt512.py"), str(stage), "--objdump", objdump], check=True)
destination.parent.mkdir(parents=True, exist_ok=True)
stage.rename(destination)
print(destination)
