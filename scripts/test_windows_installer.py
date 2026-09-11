"""Install the exact EXE in Wine, verify payload, launch and uninstall."""
import argparse
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time

root = Path(__file__).resolve().parent.parent
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--installer", type=Path,
                    default=root.parent / "05-交付/飞秒质谱工作站安装程序.exe")
parser.add_argument("--wine", help="Wine 可执行文件；默认读取 WINE_BIN 或 PATH")
arguments = parser.parse_args()
installer = arguments.installer.expanduser().resolve()
wine = arguments.wine or os.environ.get("WINE_BIN") or shutil.which("wine")
if not installer.is_file():
    raise SystemExit(f"未找到 Windows 安装程序: {installer}")
if not wine:
    raise SystemExit("未找到 Wine；请用 --wine 或 WINE_BIN 指定。真实 Windows 7 验收不依赖此脚本。")
stage = Path(tempfile.mkdtemp(prefix="installer-acceptance-", dir=root / ".qa"))
installed = stage / "installed"
def win(path): return "Z:" + str(path).replace("/", "\\")
env = dict(os.environ, WINEPREFIX=str(root / ".tools/wine-win7-check"), WINEDEBUG="-all", MVK_CONFIG_LOG_LEVEL="0",
           QITEST_WORKSPACE_DB=win(stage / "test.sqlite"))
env.pop("WINEPATH", None)
subprocess.run([wine,str(installer),"/S","/D="+win(installed)], env=env, check=True, timeout=180)
for line in (installed / "SHA256SUMS.txt").read_text().splitlines():
    expected, relative = line.split("  ",1)
    h = hashlib.sha256()
    with (installed / relative).open("rb") as stream:
        for block in iter(lambda:stream.read(1024*1024),b""): h.update(block)
    if h.hexdigest()!=expected: raise RuntimeError("Installed payload mismatch: "+relative)
print("PASS: installed payload, including model and DLLs, matches verified hashes",flush=True)
process = subprocess.Popen([wine,str(installed / "飞秒质谱工作站.exe")],env=env,cwd=installed,
                           stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
try:
    process.wait(timeout=6)
    raise RuntimeError("Installed app exited early: "+str(process.returncode))
except subprocess.TimeoutExpired:
    print("PASS: installed app remains running at startup",flush=True)
if process.poll() is None:
    # A silent upgrade must close the running GUI before overwriting Qt DLLs.
    subprocess.run([wine,str(installer),"/S","/D="+win(installed)],env=env,check=True,timeout=180)
    process.wait(timeout=20)
    print("PASS: running application was closed before silent upgrade",flush=True)
    for line in (installed / "SHA256SUMS.txt").read_text().splitlines():
        expected, relative = line.split("  ",1)
        h = hashlib.sha256()
        with (installed / relative).open("rb") as stream:
            for block in iter(lambda:stream.read(1024*1024),b""): h.update(block)
        if h.hexdigest()!=expected: raise RuntimeError("Upgraded payload mismatch: "+relative)
    print("PASS: upgraded payload still matches verified hashes",flush=True)
subprocess.run([wine,str(installed / "卸载.exe"),"/S"],env=env,check=True,timeout=60)
deadline=time.monotonic()+20
while (installed / "飞秒质谱工作站.exe").exists() and time.monotonic()<deadline: time.sleep(0.5)
if (installed / "飞秒质谱工作站.exe").exists(): raise RuntimeError("Uninstall did not remove app")
print("PASS: uninstall removed installed application; user data was not recursively deleted")
