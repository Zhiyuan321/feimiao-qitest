#!/usr/bin/env python3
"""One bounded CPU-only model smoke test; keeps at most 200 runtime log lines."""
import argparse
import collections
import json
import os
import subprocess
import threading
import time
import urllib.error
import urllib.request
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--wine")
parser.add_argument("--prefix", default=".tools/wine-win7-check")
parser.add_argument("--package", required=True, type=Path)
parser.add_argument("--native", action="store_true")
parser.add_argument("--model", type=Path, help="Test a candidate before changing the deployed profile")
args = parser.parse_args()
root = Path(__file__).resolve().parent.parent
resources = args.package.resolve() / ("Contents/Resources" if args.native else "resources")
runtime = resources / "ai"
profile = json.loads((resources / "config/ai-model-manifest.json").read_text())
if args.model:
    candidate = args.model.resolve()
    if not candidate.is_file():
        raise FileNotFoundError(candidate)
    profile["defaultModelFile"] = str(candidate) if args.native else "Z:" + str(candidate).replace("/", "\\")
env = os.environ.copy()
env.update(WINEPREFIX=str(Path(args.prefix).resolve()), WINEDEBUG="-all", MVK_CONFIG_LOG_LEVEL="0")
env.pop("WINEPATH", None)
port = 18193
token = "qitest-local-smoke"
command = ([str(runtime / "llama-server")] if args.native else [args.wine, str(runtime / "llama-server.exe")]) + ["--model", profile["defaultModelFile"],
           "--host", "127.0.0.1", "--port", str(port), "--ctx-size", "4096", "--n-gpu-layers", "0",
           "--parallel", "1", "--threads", "2", "--threads-batch", "2", "--batch-size", "128",
           "--ubatch-size", "64", "--jinja", "--reasoning", "off", "--no-webui", "--api-key", token]
lines = collections.deque(maxlen=200)
start = time.monotonic()
process = subprocess.Popen(command, cwd=runtime, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
def drain():
    for line in iter(process.stdout.readline, b""):
        lines.append(line[:4096].decode("utf-8", "replace"))
thread = threading.Thread(target=drain, daemon=True)
thread.start()
result = {}
try:
    deadline = start + 150
    while True:
        if process.poll() is not None:
            raise RuntimeError("llama-server exited before readiness")
        try:
            request = urllib.request.Request(f"http://127.0.0.1:{port}/health", headers={"Authorization": "Bearer " + token})
            with urllib.request.urlopen(request, timeout=2) as response:
                if response.status == 200: break
        except (urllib.error.URLError, TimeoutError):
            pass
        if time.monotonic() > deadline: raise TimeoutError("Model startup exceeded 150 seconds")
        time.sleep(1)
    result["startupSeconds"] = round(time.monotonic() - start, 2)
    print("CPU model ready after " + str(result["startupSeconds"]) + " seconds", flush=True)
    payload = json.loads((root / "tests/fixtures/assistant_acceptance_request.json").read_text())
    payload["model"] = Path(profile["defaultModelFile"]).stem
    payload["max_tokens"] = 96
    request = urllib.request.Request(f"http://127.0.0.1:{port}/v1/chat/completions",
        data=json.dumps(payload, ensure_ascii=False).encode("utf-8"),
        headers={"Content-Type": "application/json", "Authorization": "Bearer " + token})
    with urllib.request.urlopen(request, timeout=240) as response:
        answer = json.load(response)
    content = answer["choices"][0]["message"].get("content", "")
    if not content.strip(): raise RuntimeError("Empty model answer")
    result.update(passed=True, answer=content, usage=answer.get("usage"), totalSeconds=round(time.monotonic()-start, 2))
    print(json.dumps(result, ensure_ascii=False), flush=True)
except Exception as error:
    result.update(passed=False, error=str(error))
    print(json.dumps(result, ensure_ascii=False), flush=True)
finally:
    if process.poll() is None:
        process.terminate()
        try: process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill(); process.wait(timeout=5)
    thread.join(timeout=2)
    folder = root / ".qa/qt512-tests"
    folder.mkdir(parents=True, exist_ok=True)
    label = "light-ai-mac" if args.native else "light-ai-windows"
    (folder / (label + ".json")).write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")
    (folder / (label + "-tail.txt")).write_text("".join(lines), encoding="utf-8")
raise SystemExit(0 if result.get("passed") else 1)
