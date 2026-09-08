#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 2 ]]; then
  echo "usage: $0 <unpacked-package-dir> <llvm-mingw-root>" >&2
  exit 2
fi
package=$1
llvm_root=$2
objdump="$llvm_root/bin/llvm-objdump"

for required in \
  "$package/飞秒质谱工作站.exe" \
  "$package/Qt6Core.dll" \
  "$package/platforms/qwindows.dll" \
  "$package/sqldrivers/qsqlite.dll" \
  "$package/resources/data/qitest_spectral_library.sqlite" \
  "$package/resources/config/ai-model-manifest.json" \
  "$package/resources/knowledge/operator_manual_zh.md" \
  "$package/resources/ai/llama-server.exe" \
  "$package/resources/ai/Qwen3.5-4B-Q4_K_M.gguf" \
  "$package/prerequisites/VC_redist.x64.exe" \
  "$package/WINDOWS-IMPORTS.txt" \
  "$package/SHA256SUMS.txt"; do
  [[ -s "$required" ]] || { echo "missing or empty package file: $required" >&2; exit 3; }
done

file "$package/飞秒质谱工作站.exe" | grep -q 'PE32+ executable.*x86-64'
file "$package/resources/ai/llama-server.exe" | grep -q 'PE32+ executable.*x86-64'
(cd "$package" && shasum -a 256 -c SHA256SUMS.txt >/dev/null)

missing=$(mktemp "${TMPDIR:-/tmp}/qitest-win-missing.XXXXXX")
trap 'rm -f "$missing"' EXIT
while IFS= read -r binary; do
  while IFS= read -r dependency; do
    [[ -n "$dependency" ]] || continue
    if find "$package" -type f -iname "$dependency" -print -quit | grep -q .; then
      continue
    fi
    lower=$(printf '%s' "$dependency" | tr '[:upper:]' '[:lower:]')
    case "$lower" in
      api-ms-win-*|ext-ms-win-*|kernel32.dll|user32.dll|advapi32.dll|shell32.dll|ole32.dll|oleaut32.dll|ws2_32.dll|gdi32.dll|comdlg32.dll|imm32.dll|winmm.dll|version.dll|netapi32.dll|userenv.dll|shlwapi.dll|dwmapi.dll|dnsapi.dll|iphlpapi.dll|setupapi.dll|secur32.dll|wtsapi32.dll|uxtheme.dll|ntdll.dll|bcrypt.dll|crypt32.dll|comctl32.dll|propsys.dll|mpr.dll|d3d9.dll|d3d11.dll|d3d12.dll|dxgi.dll|dxguid.dll|d2d1.dll|dwrite.dll|opengl32.dll|glu32.dll|winspool.drv|oleacc.dll|msimg32.dll|authz.dll|winhttp.dll|msvcrt.dll|rpcrt4.dll|imagehlp.dll|shcore.dll|psapi.dll|ucrtbase.dll|vcruntime140.dll|vcruntime140_1.dll|msvcp140.dll)
        ;;
      *) printf '%s -> %s\n' "${binary#$package/}" "$dependency" >> "$missing" ;;
    esac
  done < <("$objdump" -p "$binary" | awk '/DLL Name:/ {print $3}')
done < <(find "$package" -type f \( -iname '*.exe' -o -iname '*.dll' \) | LC_ALL=C sort)

if [[ -s "$missing" ]]; then
  echo "unresolved non-system Windows imports:" >&2
  cat "$missing" >&2
  exit 4
fi

echo "Windows static package verification passed: $package"
