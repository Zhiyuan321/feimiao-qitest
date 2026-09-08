#!/usr/bin/env python3
"""Export project and patched Win7 AI runtime source; optionally include model weights."""
import argparse
import hashlib
import json
import os
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PROJECT_ROOT = ROOT.parent.parent
PROJECT_SKILL = Path.home() / ".codex/skills/feimiao-qitest"
DIRECTORIES = ("src", "tests", "resources", "config", "data", "design", "docs", "cmake",
               "scripts", "tools", "third_party", "models", "examples")
SKIP_DIRS = {".git", ".qa", ".tools", "__pycache__", "node_modules", ".cache", ".venv"}
SKIP_SUFFIXES = {".gguf", ".safetensors", ".bin", ".exe", ".dll", ".dylib", ".o", ".obj",
                 ".pyc", ".pdb", ".log", ".dmg", ".7z", ".a", ".so"}

def sha256_file(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()

def regular_files(directory):
    for parent, dirs, names in os.walk(directory, followlinks=False):
        dirs[:] = sorted(d for d in dirs if d not in SKIP_DIRS and not d.startswith("build")
                         and not (Path(parent) / d).is_symlink())
        for name in sorted(names):
            source = Path(parent) / name
            if source.is_symlink() or name == ".DS_Store" or source.suffix.lower() in SKIP_SUFFIXES:
                continue
            if name.endswith(("-wal", "-shm", ".sqlite-journal")):
                continue
            yield source

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=ROOT.parent / "05-交付/Windows源码.zip")
    parser.add_argument("--list", action="store_true", help="Show counts and bytes only, without writing an archive")
    parser.add_argument("--include-model", action="store_true", help="Also include the existing Qwen GGUF; default source ZIP excludes weights")
    args = parser.parse_args()
    entries = {}
    for directory in DIRECTORIES:
        for source in regular_files(ROOT / directory):
            entries["QITestQt/" + source.relative_to(ROOT).as_posix()] = source
    for source in ROOT.iterdir():
        if source.is_file() and not source.is_symlink() and (
            source.suffix in (".md", ".sh", ".ps1", ".cmake") or source.name == "CMakeLists.txt"):
            entries["QITestQt/" + source.name] = source
    # 让接手源码的工程师同时拿到项目规则和专用 Skill 快照；它们不参与编译。
    agents = PROJECT_ROOT / "AGENTS.md"
    skill = PROJECT_SKILL / "SKILL.md"
    skill_ui = PROJECT_SKILL / "agents/openai.yaml"
    for source, name in (
        (agents, "QITestQt/AGENTS.md"),
        (skill, "QITestQt/项目专用Skill/SKILL.md"),
        (skill_ui, "QITestQt/项目专用Skill/agents/openai.yaml"),
    ):
        if not source.is_file() or source.is_symlink():
            raise RuntimeError("Source handoff missing project guidance: " + str(source))
        entries[name] = source
    # Explicit exception: the Win7 llama.cpp fork contains necessary source changes.
    # Export only this source tree from .tools, not SDKs, binaries or Wine prefixes.
    runtime = ROOT / ".tools/win7-src/llama.cpp-b10752"
    if not runtime.is_dir():
        runtime = ROOT / "third_party/llama.cpp-b10752"
    if not runtime.is_dir():
        raise RuntimeError("The patched llama.cpp-b10752 source is required for the full handoff")
    for source in regular_files(runtime):
        entries["QITestQt/third_party/llama.cpp-b10752/" + source.relative_to(runtime).as_posix()] = source
    total = sum(p.stat().st_size for p in entries.values())
    if total > 300 * 1024 * 1024:
        raise RuntimeError(f"Unexpected source payload size: {total}; inspect the whitelist")
    if args.include_model:
        model_name = json.loads((ROOT / "config/ai-model-manifest.json").read_text())["defaultModelFile"]
        if Path(model_name).name != model_name:
            raise RuntimeError("Invalid model filename")
        model = ROOT / "models/qwen" / model_name
        if not model.is_file() or model.is_symlink():
            raise RuntimeError("The configured model must be a regular local file")
        entries["QITestQt/models/qwen/" + model.name] = model
        total += model.stat().st_size
    print(f"Selected {len(entries)} source/resource files; {total / 1024**2:.1f} MiB uncompressed")
    if args.list:
        return
    args.output.parent.mkdir(parents=True, exist_ok=True)
    manifest = []
    with zipfile.ZipFile(args.output, "x", compression=zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
        for name, source in sorted(entries.items()):
            manifest.append(sha256_file(source) + "  " + name.removeprefix("QITestQt/"))
            archive.write(source, name, compress_type=zipfile.ZIP_STORED if source.suffix == ".gguf" else zipfile.ZIP_DEFLATED)
        archive.writestr("QITestQt/SOURCE-SHA256SUMS.txt", "\n".join(manifest) + "\n")
    with zipfile.ZipFile(args.output) as archive:
        bad = archive.testzip()
        if bad:
            raise RuntimeError("ZIP CRC failure: " + bad)
        for required in ("src/core/QtCompat.h", "src/ai/LocalAiBridge.cpp", "CMakeLists.txt",
                         "Windows7编译说明.md", "AGENTS.md", "项目专用Skill/SKILL.md",
                         "third_party/llama.cpp-b10752/vendor/cpp-httplib/httplib.cpp"):
            if "QITestQt/" + required not in archive.namelist():
                raise RuntimeError("Source handoff missing " + required)
    digest = sha256_file(args.output)
    args.output.with_suffix(".zip.sha256").write_text(digest + "  " + args.output.name + "\n", encoding="utf-8")
    print(str(args.output))
    print(f"{args.output.stat().st_size / 1024**2:.2f} MiB; ZIP integrity passed")

if __name__ == "__main__":
    main()
