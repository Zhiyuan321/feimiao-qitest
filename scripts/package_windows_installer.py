"""Build a self-contained, per-user Win7 x64 installer from the verified runtime."""
import hashlib
from pathlib import Path
import shutil
import subprocess

root = Path(__file__).resolve().parent.parent
package = root.parent / "05-交付/Windows"
output = root.parent / "05-交付/飞秒质谱工作站安装程序.exe"
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
