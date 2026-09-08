#!/usr/bin/env bash
set -euo pipefail

script_dir=$(cd "$(dirname "$0")" && pwd)
project_dir=$(cd "$script_dir/.." && pwd)
qt_root=${QITEST_QT_WINDOWS_ROOT:-/private/tmp/scientz-qt-win/6.9.3/llvm-mingw_64}
qt_host_root=${QITEST_QT_HOST_ROOT:-/private/tmp/scientz-qt-693/6.9.3/macos}
llvm_root=${QITEST_LLVM_MINGW_ROOT:-/private/tmp/scientz-llvm-mingw}
build_dir=${QITEST_WINDOWS_BUILD_DIR:-"$project_dir/build-windows-cross"}
delivery_root=${QITEST_WINDOWS_DELIVERY_ROOT:-"$project_dir/../06-Windows交付"}
runtime_root=${QITEST_LLAMA_WINDOWS_RUNTIME:-"$project_dir/.tools/llama-runtime-windows"}
model="$project_dir/models/qwen/Qwen3.5-4B-Q4_K_M.gguf"
vc_redist="$project_dir/.tools/windows-redist/VC_redist.x64.exe"
cmake_bin="$project_dir/.tools/venv/lib/python3.9/site-packages/cmake/data/bin/cmake"
toolchain="$project_dir/cmake/toolchains/windows-llvm-mingw.cmake"

for required in "$cmake_bin" "$toolchain" \
  "$llvm_root/bin/x86_64-w64-mingw32-clang++" \
  "$qt_host_root/libexec/moc" \
  "$qt_root/lib/cmake/Qt6/Qt6Config.cmake" \
  "$runtime_root/llama-server.exe" "$model" "$vc_redist"; do
  [[ -f "$required" ]] || { echo "missing Windows package input: $required" >&2; exit 3; }
done

"$cmake_bin" -S "$project_dir" -B "$build_dir" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE="$toolchain" \
  -DQITEST_LLVM_MINGW_ROOT="$llvm_root" \
  -DQITEST_QT_WINDOWS_ROOT="$qt_root" \
  -DQITEST_QT_HOST_ROOT="$qt_host_root" \
  -DQT_HOST_PATH="$qt_host_root" \
  -DQT_HOST_PATH_CMAKE_DIR="$qt_host_root/lib/cmake" \
  -DQITEST_BUILD_TESTS=OFF
"$cmake_bin" --build "$build_dir" --parallel "${QITEST_BUILD_JOBS:-8}"

app_exe="$build_dir/飞秒质谱工作站.exe"
[[ -f "$app_exe" ]] || { echo "Windows executable was not generated: $app_exe" >&2; exit 4; }

mkdir -p "$delivery_root"
staging=$(mktemp -d "${TMPDIR:-/tmp}/qitest-win-package.XXXXXX")
trap 'rm -rf "$staging"' EXIT
package_name="飞秒质谱工作站-Windows-x64"
package="$staging/$package_name"
mkdir -p "$package/platforms" "$package/sqldrivers" "$package/imageformats" \
  "$package/iconengines" "$package/resources/data" "$package/resources/config" \
  "$package/resources/knowledge" "$package/resources/notices" \
  "$package/resources/ai" "$package/prerequisites"

cp "$app_exe" "$package/"
for dll in Qt6Core Qt6Gui Qt6Widgets Qt6Network Qt6Sql Qt6Svg libc++ libunwind; do
  cp "$qt_root/bin/$dll.dll" "$package/"
done
for optional in d3dcompiler_47.dll opengl32sw.dll; do
  [[ ! -f "$qt_root/bin/$optional" ]] || cp "$qt_root/bin/$optional" "$package/"
done
cp "$qt_root/plugins/platforms/qwindows.dll" "$package/platforms/"
cp "$qt_root/plugins/sqldrivers/qsqlite.dll" "$package/sqldrivers/"
for plugin in qico qjpeg qsvg; do
  cp "$qt_root/plugins/imageformats/$plugin.dll" "$package/imageformats/"
done
cp "$qt_root/plugins/iconengines/qsvgicon.dll" "$package/iconengines/"

cp "$project_dir/data/library/qitest_spectral_library.sqlite" "$package/resources/data/"
cp "$project_dir/config/ai-model-manifest.json" "$package/resources/config/"
cp "$project_dir/resources/knowledge/operator_manual_zh.md" "$package/resources/knowledge/"
cp "$project_dir/THIRD_PARTY_NOTICES.md" "$package/resources/notices/"
cp -R "$project_dir/third_party/notices/." "$package/resources/notices/"
cp "$runtime_root/"*.dll "$package/resources/ai/"
cp "$runtime_root/llama-server.exe" "$runtime_root/LICENSE-LLVM-OpenMP" "$package/resources/ai/"
cp "$model" "$package/resources/ai/"
cp "$vc_redist" "$package/prerequisites/"

printf '[Paths]\nPlugins = .\n' > "$package/qt.conf"
printf '%s\n' \
  '飞秒质谱工作站 Windows x64 Demo' \
  '' \
  '1. 完整解压 ZIP 后，双击“飞秒质谱工作站.exe”。' \
  '2. 本包是 Qt/C++ 原生便携版，不需要安装完整 Windows 环境。' \
  '3. 如果智能台提示缺少 Microsoft C++ 运行库，请运行 prerequisites/VC_redist.x64.exe 后重试。' \
  '4. 仪器通信仍为演示适配器；接入真实设备前必须由厂家提供并确认通信协议。' \
  '5. 本包在 macOS 上交叉编译并完成静态完整性检查，真实 Windows 交互由接收方测试。' \
  > "$package/README-WINDOWS.txt"

imports="$package/WINDOWS-IMPORTS.txt"
: > "$imports"
while IFS= read -r binary; do
  printf '%s\n' "${binary#$package/}" >> "$imports"
  "$llvm_root/bin/llvm-objdump" -p "$binary" \
    | awk '/DLL Name:/ {print "  " $3}' >> "$imports"
done < <(find "$package" -type f \( -iname '*.exe' -o -iname '*.dll' \) | LC_ALL=C sort)

(cd "$package" && find . -type f ! -name SHA256SUMS.txt -print0 \
  | LC_ALL=C sort -z | xargs -0 shasum -a 256 > SHA256SUMS.txt)

output_dir="$delivery_root/$package_name"
output_zip="$delivery_root/$package_name.zip"
rm -rf "$output_dir"
rm -f "$output_zip" "$output_zip.sha256"
cp -R "$package" "$output_dir"
(cd "$staging" && /usr/bin/zip -0 -q -r -X "$output_zip" "$package_name")
(cd "$delivery_root" && shasum -a 256 "$(basename "$output_zip")" \
  > "$(basename "$output_zip").sha256")

"$script_dir/verify_windows_package.sh" "$output_dir" "$llvm_root"
echo "$output_zip"
