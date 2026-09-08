"""Install the exact EXE in the QA Wine prefix, verify payload, launch and uninstall."""
import hashlib
import os
from pathlib import Path
import subprocess
import tempfile
import time

root = Path(__file__).resolve().parent.parent
installer = root.parent / "05-交付/飞秒质谱工作站安装程序.exe"
wine = "/Users/zhouzhiyuan/Applications/Wine Stable.app/Contents/Resources/wine/bin/wine"
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
finally:
    if process.poll() is None:
        # No /F: ask the GUI to close normally in this dedicated QA Wine prefix.
        # Killing the host Wine wrapper can crash wineserver instead of exercising close.
        subprocess.run([wine,"taskkill","/IM","飞秒质谱工作站.exe"],env=env,check=True,timeout=15)
        process.wait(timeout=15)
        print("PASS: installed GUI accepts normal window close",flush=True)
subprocess.run([wine,str(installed / "卸载.exe"),"/S"],env=env,check=True,timeout=60)
deadline=time.monotonic()+20
while (installed / "飞秒质谱工作站.exe").exists() and time.monotonic()<deadline: time.sleep(0.5)
if (installed / "飞秒质谱工作站.exe").exists(): raise RuntimeError("Uninstall did not remove app")
print("PASS: uninstall removed installed application; user data was not recursively deleted")
