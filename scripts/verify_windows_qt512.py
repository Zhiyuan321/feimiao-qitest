#!/usr/bin/env python3
"""Check actual Win7 package hashes and per-process DLL resolution (not a Win7 emulator)."""
import argparse
import hashlib
import json
import re
import shutil
import subprocess
from pathlib import Path

SYSTEM = set("""kernel32.dll user32.dll advapi32.dll shell32.dll ole32.dll oleaut32.dll
ws2_32.dll gdi32.dll comdlg32.dll imm32.dll winmm.dll version.dll netapi32.dll userenv.dll
shlwapi.dll dwmapi.dll dnsapi.dll iphlpapi.dll setupapi.dll secur32.dll wtsapi32.dll
uxtheme.dll ntdll.dll bcrypt.dll crypt32.dll comctl32.dll propsys.dll mpr.dll d3d9.dll
d3d11.dll dxgi.dll d2d1.dll dwrite.dll opengl32.dll glu32.dll winspool.drv oleacc.dll
msimg32.dll authz.dll winhttp.dll msvcrt.dll rpcrt4.dll imagehlp.dll psapi.dll
powrprof.dll hid.dll wintrust.dll usp10.dll mswsock.dll normaliz.dll pdh.dll avrt.dll
ncrypt.dll cryptbase.dll""".split())
NEW_API = {"WaitOnAddress", "WakeByAddressSingle", "WakeByAddressAll", "SetThreadDescription",
           "CreateFile2", "CreateFileMappingFromApp", "MapViewOfFileFromApp",
           "GetThreadDescription", "GetSystemTimePreciseAsFileTime", "GetOverlappedResultEx",
           "GetTempPath2W", "GetTempPath2A", "VirtualAlloc2", "MapViewOfFile3"}

def sha(path):
    result = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1048576), b""):
            result.update(block)
    return result.hexdigest()

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("package", type=Path)
    parser.add_argument("--objdump", default=shutil.which("x86_64-w64-mingw32-objdump"))
    args = parser.parse_args()
    package = args.package.resolve()
    required = ["飞秒质谱工作站.exe", "Qt5Core.dll", "Qt5Widgets.dll", "platforms/qwindows.dll",
                "sqldrivers/qsqlite.dll", "resources/ai/llama-server.exe",
                "resources/config/ai-model-manifest.json",
                "resources/data/qitest_spectral_library.sqlite", "SHA256SUMS.txt"]
    for name in required:
        if not (package / name).is_file() or (package / name).stat().st_size == 0:
            raise RuntimeError("Missing required file: " + name)
    model_config = json.loads((package / "resources/config/ai-model-manifest.json").read_text())
    model_name = model_config["defaultModelFile"]
    if Path(model_name).name != model_name:
        raise RuntimeError("Invalid model filename")
    model = package / "resources/ai" / model_name
    if not model.is_file() or not model.stat().st_size:
        raise RuntimeError("Missing configured model")
    if model_config.get("modelSha256") and sha(model) != model_config["modelSha256"]:
        raise RuntimeError("Configured model SHA256 mismatch")
    files = sorted(package.rglob("*"))
    if any(p.is_symlink() for p in files):
        raise RuntimeError("Portable packages must not contain symlinks")
    if any(p.name.startswith("Qt6") for p in files):
        raise RuntimeError("Qt6 runtime found in Qt5.12 package")
    errors = []
    for binary in (p for p in files if p.suffix.lower() in (".exe", ".dll")):
        text = subprocess.check_output([args.objdump, "-p", str(binary)], text=True)
        relative = binary.relative_to(package).as_posix()
        if "pei-x86-64" not in text:
            errors.append("Not x64 PE: " + relative)
        major = re.search(r"MajorSubsystemVersion\s+(\d+)", text)
        minor = re.search(r"MinorSubsystemVersion\s+(\d+)", text)
        # The subsystem minimum governs EXE startup. SDK UCRT forwarder DLLs
        # carry newer header versions yet support app-local deployment on Win7;
        # for DLLs, inspect architecture/imports instead of treating this as an EXE gate.
        if binary.suffix.lower() == ".exe" and major and minor and (int(major[1]), int(minor[1])) > (6, 1):
            errors.append("PE subsystem newer than Win7: " + relative)
        # The child AI process cannot assume the GUI executable's DLL search directory.
        root = package / "resources/ai" if relative.startswith("resources/ai/") else package
        available = {p.name.lower() for p in root.iterdir() if p.is_file()}
        for dependency in re.findall(r"DLL Name:\s+(\S+)", text):
            if dependency.lower() not in SYSTEM | available:
                errors.append(relative + " -> " + dependency)
        imports = text.split("The Import Tables", 1)[-1].split("The Export Tables", 1)[0]
        for name in NEW_API:
            if re.search(r"\b" + name + r"\b", imports):
                errors.append(relative + " imports a post-Win7 API: " + name)
    for line in (package / "SHA256SUMS.txt").read_text().splitlines():
        expected, name = line.split("  ", 1)
        path = (package / name).resolve()
        if package not in path.parents or sha(path) != expected:
            errors.append("Checksum mismatch: " + name)
    if errors:
        raise RuntimeError("\n".join(errors))
    print("PASS: Qt5 x64 package hashes, per-process DLL closure and known post-Win7 import checks")

if __name__ == "__main__":
    main()
