#!/usr/bin/env python3
"""Run Windows test EXEs under the provided Wine installation with bounded output."""
import argparse
import json
import os
import shutil
import subprocess
from pathlib import Path

root = Path(__file__).resolve().parent.parent
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--wine", required=True)
parser.add_argument("--prefix", required=True)
parser.add_argument("--package", required=True, type=Path)
parser.add_argument("--build-dir", type=Path, default=root / "build-windows7-qt512",
                    help="Windows test build directory (default: build-windows7-qt512)")
parser.add_argument("--test", action="append", help="Run a named subset without replacing the full-suite result")
args = parser.parse_args()
build_dir = args.build_dir.resolve()
target = root / ".qa/qt512-tests"
target.mkdir(parents=True, exist_ok=True)
qt = root / ".tools/qt512-win/5.12.12/mingw73_64"
for source in sorted(args.package.glob("*.dll")):
    shutil.copy2(source, target / source.name)
shutil.copy2(qt / "bin/Qt5Test.dll", target / "Qt5Test.dll")
for relative in ("platforms/qoffscreen.dll", "platforms/qwindows.dll", "sqldrivers/qsqlite.dll",
                 "imageformats/qsvg.dll", "iconengines/qsvgicon.dll"):
    (target / relative).parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(qt / "plugins" / relative, target / relative)
env = os.environ.copy()
env.update(WINEPREFIX=str(Path(args.prefix).resolve()), WINEDEBUG="-all", MVK_CONFIG_LOG_LEVEL="0",
           QT_QPA_PLATFORM="offscreen", QITEST_AI_LOW_MEMORY="1", WINEPATH="Z:" + str(target).replace("/", "\\"),
           QITEST_UI_CAPTURE_DIR="captures")
names = ("ai_lifecycle", "device", "core", "ai", "library", "workspace", "security",
         "network", "rs485", "pump", "ui")
if args.test:
    if any(name not in names for name in args.test):
        raise ValueError("Unknown test")
    names = tuple(args.test)
results = []
for name in names:
    source = build_dir / ("qitest_" + name + "_tests.exe")
    exe = target / source.name
    shutil.copy2(source, exe)
    log = target / (name + ".txt")
    if log.exists():
        log.unlink()
    # Qt Test parses narrow argv on Windows; keep its output argument ASCII.
    log_path = log.name
    try:
        timeout = 180 if name == "ui" else 60
        run = subprocess.run([args.wine, str(exe), "-o", log_path + ",txt"], cwd=target, env=env,
                             stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=timeout)
        output = (log.read_text(encoding="utf-8", errors="replace") if log.exists() else "") + run.stdout.decode("utf-8", "replace")
        passed = run.returncode == 0 and "0 failed" in output
        results.append({"test": name, "passed": passed, "exit": run.returncode})
        print(name + ": " + ("PASS" if passed else "FAIL"), flush=True)
        if not passed: print(output[-6000:], flush=True)
    except subprocess.TimeoutExpired:
        results.append({"test": name, "passed": False, "error": str(timeout) + "-second timeout"})
        print(name + ": TIMEOUT", flush=True)
(target / ("results-subset.json" if args.test else "results.json")).write_text(json.dumps(results, indent=2), encoding="utf-8")
raise SystemExit(0 if all(result["passed"] for result in results) else 1)
