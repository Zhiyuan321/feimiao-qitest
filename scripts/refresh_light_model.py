#!/usr/bin/env python3
"""Deploy the configured verified model; preserve the previous weights for rollback."""
import argparse
import hashlib
import json
import shutil
from pathlib import Path

root = Path(__file__).resolve().parent.parent
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("platform", choices=("mac", "windows"))
args = parser.parse_args()
package = root.parent / "05-交付" / ("Windows" if args.platform == "windows" else "Mac/飞秒质谱工作站.app")
resources = package / ("resources" if args.platform == "windows" else "Contents/Resources")
config = json.loads((root / "config/ai-model-manifest.json").read_text())
name = config["defaultModelFile"]
if Path(name).name != name:
    raise ValueError("Unsafe model name")

def sha(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()

source = root / "models/qwen" / name
if sha(source) != config["modelSha256"]:
    raise ValueError("Model does not match official SHA256")
runtime = resources / "ai"
if not runtime.is_dir():
    raise ValueError("Missing existing runtime")
target = runtime / name
if not target.exists() or sha(target) != config["modelSha256"]:
    pending = target.with_suffix(".gguf.next")
    shutil.copy2(source, pending)
    pending.replace(target)
license_name = config.get("modelLicenseFile", "Qwen2.5-LICENSE")
if Path(license_name).name != license_name:
    raise ValueError("Unsafe license name")
shutil.copy2(root / "models/qwen" / license_name, runtime / license_name)
for previous in ("Qwen3.5-4B-Q4_K_M.gguf", "qwen2.5-0.5b-instruct-q4_k_m.gguf"):
    old = runtime / previous
    if old.exists() and previous != name:
        backup = root / ".qa/model-rollback" / args.platform / old.name
        backup.parent.mkdir(parents=True, exist_ok=True)
        if backup.exists():
            raise FileExistsError("Rollback already exists; do not overwrite it")
        old.replace(backup)
shutil.copy2(root / "config/ai-model-manifest.json", resources / "config/ai-model-manifest.json")
if args.platform == "windows":
    manifest = package / "SHA256SUMS.txt"
    lines = []
    for path in sorted(package.rglob("*")):
        if path.is_symlink():
            raise ValueError("Unexpected package symlink")
        if path.is_file() and path != manifest and path.name != ".DS_Store":
            lines.append(sha(path) + "  " + path.relative_to(package).as_posix())
    manifest.write_text("\n".join(lines) + "\n", encoding="utf-8")
print(str(target))
