#!/bin/zsh
set -euo pipefail

PROJECT_DIR="${0:A:h}"
APP="${QITEST_MAC_APP:-$PROJECT_DIR/../05-交付/Mac/飞秒质谱工作站.app}"
MANIFEST="$APP/Contents/Resources/config/ai-model-manifest.json"
MODEL_NAME="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["defaultModelFile"])' "$MANIFEST")"
MODEL_BYTES="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["modelFileBytes"])' "$MANIFEST")"
MODEL_SHA="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["modelSha256"])' "$MANIFEST")"
MODEL_LICENSE="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["modelLicenseFile"])' "$MANIFEST")"
[[ "$MODEL_NAME" == "${MODEL_NAME:t}" && "$MODEL_LICENSE" == "${MODEL_LICENSE:t}" ]]
MODEL="$APP/Contents/Resources/ai/$MODEL_NAME"
LIBRARY="$APP/Contents/Resources/data/qitest_spectral_library.sqlite"

[[ -d "$APP" ]]
[[ "$(file "$APP/Contents/MacOS/飞秒质谱工作站")" == *"arm64"* ]]
codesign --verify --deep --strict "$APP"
[[ "$(sqlite3 "$LIBRARY" 'PRAGMA integrity_check;')" == "ok" ]]
[[ "$(sqlite3 "$LIBRARY" 'SELECT COUNT(*) FROM spectra;')" == "3826" ]]
[[ "$(sqlite3 "$LIBRARY" 'PRAGMA user_version;')" == "2" ]]
[[ "$(sqlite3 "$LIBRARY" 'SELECT COUNT(*) FROM sources WHERE match_eligible != 0;')" == "0" ]]
[[ "$(stat -f '%z' "$MODEL")" == "$MODEL_BYTES" ]]
[[ "$(shasum -a 256 "$MODEL" | awk '{print $1}')" == "$MODEL_SHA" ]]
[[ -f "$APP/Contents/Resources/ai/$MODEL_LICENSE" ]]
[[ -x "$APP/Contents/Resources/ai/llama-server" ]]
[[ -f "$APP/Contents/PlugIns/sqldrivers/libqsqlite.dylib" ]]
[[ -f "$APP/Contents/Resources/notices/THIRD_PARTY_NOTICES.md" ]]
[[ -f "$APP/Contents/Resources/knowledge/operator_manual_zh.md" ]]
[[ -f "$MANIFEST" ]]
[[ -f "$APP/Contents/Resources/notices/Qwen3-Apache-2.0.txt" ]]
[[ -f "$APP/Contents/Resources/notices/llama.cpp-MIT.txt" ]]
[[ -f "$APP/Contents/Resources/notices/Qt-LGPL-3.0.txt" ]]
[[ -f "$APP/Contents/Resources/notices/qt-sbom/qtbase-6.10.2.spdx.json" ]]
echo "Release verification passed: $APP"
