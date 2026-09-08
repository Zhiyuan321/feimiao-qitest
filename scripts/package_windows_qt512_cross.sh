#!/usr/bin/env bash
set -euo pipefail

script_dir=$(cd "$(dirname "$0")" && pwd)
project_dir=$(cd "$script_dir/.." && pwd)

qt_root=${QITEST_QT_WINDOWS_ROOT:-"$project_dir/.tools/qt512-win/5.12.12/mingw73_64"}
qt_host_root=${QITEST_QT_HOST_ROOT:-"$project_dir/.tools/qt512-host/5.12.12/clang_64"}
llvm_root=${QITEST_LLVM_MINGW_ROOT:-"/opt/homebrew/Cellar/mingw-w64/14.0.0_3/toolchain-x86_64"}
build_dir=${QITEST_WINDOWS_BUILD_DIR:-"$project_dir/build-windows7-qt512"}
delivery_root=${QITEST_WINDOWS_DELIVERY_ROOT:-"$project_dir/../05-交付"}
llama_root=${QITEST_LLAMA_WINDOWS_RUNTIME:-"$project_dir/build-llama-win7/bin"}
model_path=${QITEST_QWEN_MODEL_PATH:-"$project_dir/models/qwen/Qwen3.5-0.8B-Q4_0.gguf"}
cmake_bin="$project_dir/.tools/venv/lib/python3.9/site-packages/cmake/data/bin/cmake"
toolchain="$project_dir/cmake/toolchains/windows7-mingw.cmake"

objdump_cmd="$llvm_root/x86_64-w64-mingw32/bin/objdump"
if [[ ! -x "$objdump_cmd" ]]; then
  objdump_cmd="$(command -v x86_64-w64-mingw32-objdump || true)"
fi

app_exe_name="飞秒质谱工作站.exe"
app_exe="$build_dir/$app_exe_name"
package_name=${QITEST_PACKAGE_NAME:-"Windows"}
output_dir="$delivery_root/$package_name"
output_zip="$delivery_root/$package_name.zip"
if [[ -e "$output_dir" || -e "$output_zip" ]]; then
  echo "Delivery exists; choose another QITEST_PACKAGE_NAME: $output_dir" >&2
  exit 2
fi

for required in \
  "$cmake_bin" "$toolchain" "$objdump_cmd" "$qt_root/bin/Qt5Core.dll" "$qt_root/plugins/platforms/qwindows.dll" \
  "$qt_root/plugins/sqldrivers/qsqlite.dll" "$qt_host_root/bin/moc" "$llama_root/llama-server.exe" "$model_path"; do
  [[ -f "$required" ]] || { echo "missing Windows 5.12 build input: $required" >&2; exit 3; }
done

"$cmake_bin" -S "$project_dir" -B "$build_dir" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE="$toolchain" \
  -DCMAKE_PREFIX_PATH="$qt_root" \
  -DQt5_DIR="$qt_root/lib/cmake/Qt5" \
  -DQITEST_QT5_HOST_ROOT="$qt_host_root" \
  -DQITEST_WIN7=ON \
  -DQITEST_WIN7_QT_VERSION=5.12.12 \
  -DQITEST_LLVM_MINGW_ROOT="$llvm_root" \
  -DQITEST_QT_WINDOWS_ROOT="$qt_root" \
  -DQITEST_QT_HOST_ROOT="$qt_host_root" \
  -DQT_HOST_PATH="$qt_host_root" \
  -DQT_HOST_PATH_CMAKE_DIR="$qt_host_root/lib/cmake" \
  -DQITEST_BUILD_TESTS=ON
"$cmake_bin" --build "$build_dir" --parallel "${QITEST_BUILD_JOBS:-4}"

[[ -f "$app_exe" ]] || { echo "Windows executable was not generated: $app_exe" >&2; exit 4; }

mkdir -p "$delivery_root"
staging=$(mktemp -d "$delivery_root/.qt512-stage.XXXXXX")
package="$staging/$package_name"

mkdir -p "$package/platforms" "$package/sqldrivers" "$package/imageformats" "$package/iconengines" \
  "$package/resources/data" "$package/resources/config" "$package/resources/knowledge" "$package/resources/notices" \
  "$package/resources/ai"

cp "$app_exe" "$package/"

for dll in Qt5Core Qt5Gui Qt5Widgets Qt5Network Qt5Sql Qt5Svg; do
  [[ -f "$qt_root/bin/${dll}.dll" ]] && cp "$qt_root/bin/${dll}.dll" "$package/"
done

cp "$qt_root/plugins/platforms/qwindows.dll" "$package/platforms/"
cp "$qt_root/plugins/sqldrivers/qsqlite.dll" "$package/sqldrivers/"
for plugin in qjpeg qgif qsvg qico; do
  cp "$qt_root/plugins/imageformats/${plugin}.dll" "$package/imageformats/"
done
cp "$qt_root/plugins/iconengines/qsvgicon.dll" "$package/iconengines/"

cp "$project_dir/data/library/qitest_spectral_library.sqlite" "$package/resources/data/"
cp "$project_dir/config/ai-model-manifest.json" "$package/resources/config/"
cp "$project_dir/resources/knowledge/operator_manual_zh.md" "$package/resources/knowledge/"
mkdir -p "$package/示例数据"
for sample in openms_bsa.scan.csv README.md LICENSE-OpenMS.txt; do
  cp "$project_dir/tests/fixtures/public_ms/$sample" "$package/示例数据/"
done
cp "$project_dir/THIRD_PARTY_NOTICES.md" "$package/resources/notices/"
if [[ -d "$project_dir/third_party/notices" ]]; then
  cp "$project_dir/third_party/notices/"*.txt "$package/resources/notices/"
fi

cp "$llama_root/llama-server.exe" "$package/resources/ai/"
for dll in libgcc_s_seh-1.dll libstdc++-6.dll libwinpthread-1.dll; do
  [[ -f "$llama_root/$dll" ]] || { echo "Missing compiler runtime: $dll" >&2; exit 5; }
  cp "$llama_root/$dll" "$package/resources/ai/"
  cp "$llama_root/$dll" "$package/"
done
cp "$model_path" "$package/resources/ai/"
cp "$project_dir/models/qwen/Qwen3.5-LICENSE" "$package/resources/ai/"

if [[ -d "$project_dir/.tools/windows-ucrt-19041" ]]; then
  cp "$project_dir/.tools/windows-ucrt-19041/"* "$package/"
  cp "$project_dir/.tools/windows-ucrt-19041/"*.dll "$package/resources/ai/"
fi

printf '[Paths]\nPlugins=.\n' > "$package/qt.conf"
cp "$project_dir/docs/WINDOWS7_PORTABLE_README.txt" "$package/使用说明.txt"

imports_file="$package/WINDOWS-IMPORTS.txt"
: > "$imports_file"
while IFS= read -r binary; do
  printf '%s\n' "${binary#$package/}" >> "$imports_file"
  "$objdump_cmd" -p "$binary" | awk '/DLL Name:/ {print "  " $3}' >> "$imports_file"
done < <(find "$package" -type f \( -iname '*.exe' -o -iname '*.dll' \) | LC_ALL=C sort)

(cd "$package" && find . -type f ! -name SHA256SUMS.txt -print0 | LC_ALL=C sort -z | xargs -0 shasum -a 256 > SHA256SUMS.txt)

python3 "$script_dir/verify_windows_qt512.py" "$package" --objdump "$objdump_cmd"
mv "$package" "$output_dir"
if [[ "${QITEST_DEFER_ARCHIVE:-0}" != 1 ]]; then
  python3 "$script_dir/archive_windows_qt512.py" "$output_dir"
fi

echo "$output_zip"
