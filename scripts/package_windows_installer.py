"""Build a self-contained, per-user Win7 x64 installer from the verified runtime."""
import argparse
import hashlib
from pathlib import Path
import shutil
import subprocess

root = Path(__file__).resolve().parent.parent
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--package", type=Path, default=root.parent / "05-交付/Windows",
                    help="已通过校验的Windows运行目录")
parser.add_argument("--output", type=Path, default=root.parent / "05-交付/飞秒质谱工作站安装程序.exe",
                    help="安装程序输出路径")
arguments = parser.parse_args()
package = arguments.package.expanduser().resolve()
output = arguments.output.expanduser().resolve()
if not (package / "SHA256SUMS.txt").is_file():
    raise SystemExit("未找到已校验运行目录: " + str(package))
output.parent.mkdir(parents=True, exist_ok=True)
stage = root / ".qa/installer-build"
stage.mkdir(parents=True, exist_ok=True)

def digest(path):
    h = hashlib.sha256()
    with path.open("rb") as f:
        for block in iter(lambda: f.read(1024*1024), b""): h.update(block)
    return h.hexdigest()

def quote(value):
    return '"' + str(value).replace('$', '$$').replace('"', '$\\"') + '"'

files = []
for line in (package / "SHA256SUMS.txt").read_text().splitlines():
    expected, relative = line.split("  ", 1)
    path = (package / relative).resolve()
    if package.resolve() not in path.parents or digest(path) != expected:
        raise RuntimeError("Unverified payload: " + relative)
    files.append(Path(relative))
files.append(Path("SHA256SUMS.txt"))
script = r'''
Unicode true
!include "MUI2.nsh"
!include "x64.nsh"
!include "WinVer.nsh"
!include "WinMessages.nsh"
Name "飞秒质谱工作站"
RequestExecutionLevel user
InstallDir "$LOCALAPPDATA\FeimiaoWorkstation"
InstallDirRegKey HKCU "Software\FeimiaoWorkstation" "InstallDir"
SetCompressor zlib
SetOverwrite on
!define MUI_ABORTWARNING
!define MUI_WELCOMEPAGE_TEXT "将安装完整软件、千问小模型与运行库。无需编译，无需另装 Qt。仅适用于 Windows 7 SP1 或更新的 64 位系统。"
!define MUI_FINISHPAGE_RUN "$INSTDIR\飞秒质谱工作站.exe"
!define MUI_FINISHPAGE_RUN_TEXT "打开飞秒质谱工作站"
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "SimpChinese"
Function .onInit
  ${IfNot} ${RunningX64}
    MessageBox MB_ICONSTOP "需要 64 位 Windows。"
    Abort
  ${EndIf}
  ${IfNot} ${AtLeastWin7}
    MessageBox MB_ICONSTOP "需要 Windows 7 SP1 或更新系统。"
    Abort
  ${EndIf}
  FindWindow $0 "" "飞秒质谱工作站"
  ${If} $0 == 0
    Goto running_checked
  ${EndIf}
  IfSilent close_running 0
  MessageBox MB_ICONEXCLAMATION|MB_OKCANCEL "飞秒质谱工作站正在运行。请先保存正在编辑的内容；点击“确定”将关闭软件并继续安装。" IDOK close_running IDCANCEL cancel_install
close_running:
  SendMessage $0 ${WM_CLOSE} 0 0
  Sleep 1500
  FindWindow $0 "" "飞秒质谱工作站"
  ${If} $0 != 0
    MessageBox MB_ICONSTOP "软件仍在运行，请手动关闭后重新安装。"
    Abort
  ${EndIf}
  Goto running_checked
cancel_install:
  Abort
running_checked:
  SetShellVarContext current
FunctionEnd
Section "完整软件"
'''
script = 'OutFile ' + quote(output) + '\nIcon ' + quote(root / 'resources/brand/qitest-app.ico') + '\n' + script
for relative in files:
    directory = str(relative.parent).replace('/', '\\')
    script += 'SetOutPath "$INSTDIR' + ('' if directory == '.' else '\\' + directory) + '"\n'
    script += 'File ' + quote(package / relative) + '\n'
script += r'''
SetOutPath "$INSTDIR"
WriteUninstaller "$INSTDIR\卸载.exe"
CreateShortcut "$DESKTOP\飞秒质谱工作站.lnk" "$INSTDIR\飞秒质谱工作站.exe"
CreateDirectory "$SMPROGRAMS\飞秒质谱工作站"
CreateShortcut "$SMPROGRAMS\飞秒质谱工作站\飞秒质谱工作站.lnk" "$INSTDIR\飞秒质谱工作站.exe"
CreateShortcut "$SMPROGRAMS\飞秒质谱工作站\卸载.lnk" "$INSTDIR\卸载.exe"
WriteRegStr HKCU "Software\FeimiaoWorkstation" "InstallDir" "$INSTDIR"
WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\FeimiaoWorkstation" "DisplayName" "飞秒质谱工作站"
WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\FeimiaoWorkstation" "UninstallString" '$\"$INSTDIR\卸载.exe$\"'
SectionEnd
Section "Uninstall"
SetShellVarContext current
'''
# Delete only installed payload files, never recursively delete user data.
for relative in files:
    script += 'Delete "$INSTDIR\\' + str(relative).replace('/', '\\') + '"\n'
directories = {p for relative in files for p in relative.parents if str(p) != '.'}
for directory in sorted(directories, key=lambda p: len(p.parts), reverse=True):
    script += 'RMDir "$INSTDIR\\' + str(directory).replace('/', '\\') + '"\n'
script += r'''
Delete "$INSTDIR\卸载.exe"
RMDir "$INSTDIR"
Delete "$DESKTOP\飞秒质谱工作站.lnk"
Delete "$SMPROGRAMS\飞秒质谱工作站\飞秒质谱工作站.lnk"
Delete "$SMPROGRAMS\飞秒质谱工作站\卸载.lnk"
RMDir "$SMPROGRAMS\飞秒质谱工作站"
DeleteRegKey HKCU "Software\FeimiaoWorkstation"
DeleteRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\FeimiaoWorkstation"
SectionEnd
'''
source = stage / "setup.nsi"
source.write_text(script, encoding="utf-8")
compiler = shutil.which("makensis") or "/opt/homebrew/bin/makensis"
subprocess.run([compiler, "-V2", str(source)], check=True)
output.with_suffix(".exe.sha256").write_text(digest(output) + "  " + output.name + "\n")
print(output)
print(output.stat().st_size)
