#!/bin/zsh
set -euo pipefail

PROJECT_DIR="${0:A:h}"
QT_DIR="$PROJECT_DIR/.tools/Qt/6.10.2/macos"
DELIVERY_DIR="${QITEST_MAC_DELIVERY_DIR:-$PROJECT_DIR/../05-交付/Mac}"
APP_SOURCE="$PROJECT_DIR/build-qt610/飞秒质谱工作站.app"
APP_TARGET="$DELIVERY_DIR/飞秒质谱工作站.app"

if [[ "${QITEST_SKIP_BUILD:-0}" != 1 ]]; then
  "$PROJECT_DIR/build.sh"
fi
mkdir -p "$DELIVERY_DIR"
if [[ -e "$APP_TARGET" ]]; then
  echo "App already exists; choose a new QITEST_MAC_DELIVERY_DIR: $APP_TARGET" >&2
  exit 2
fi
ditto "$APP_SOURCE" "$APP_TARGET"
mkdir -p "$APP_TARGET/Contents/Resources/data"
ditto "$PROJECT_DIR/data/library/qitest_spectral_library.sqlite" \
  "$APP_TARGET/Contents/Resources/data/qitest_spectral_library.sqlite"
mkdir -p "$APP_TARGET/Contents/Resources/notices"
mkdir -p "$APP_TARGET/Contents/Resources/config"
ditto "$PROJECT_DIR/config/ai-model-manifest.json" \
  "$APP_TARGET/Contents/Resources/config/ai-model-manifest.json"
mkdir -p "$APP_TARGET/Contents/Resources/knowledge"
ditto "$PROJECT_DIR/resources/knowledge/operator_manual_zh.md" \
  "$APP_TARGET/Contents/Resources/knowledge/operator_manual_zh.md"
ditto "$PROJECT_DIR/THIRD_PARTY_NOTICES.md" \
  "$APP_TARGET/Contents/Resources/notices/THIRD_PARTY_NOTICES.md"
mkdir -p "$DELIVERY_DIR/示例数据"
for SAMPLE in openms_bsa.scan.csv README.md LICENSE-OpenMS.txt; do
  ditto "$PROJECT_DIR/tests/fixtures/public_ms/$SAMPLE" "$DELIVERY_DIR/示例数据/$SAMPLE"
done
ditto "$PROJECT_DIR/third_party/notices" "$APP_TARGET/Contents/Resources/notices"
AI_RUNTIME="$PROJECT_DIR/.tools/llama-runtime"
AI_MODEL="$PROJECT_DIR/models/qwen/Qwen3.5-0.8B-Q4_0.gguf"
if [[ -x "$AI_RUNTIME/llama-server" && -f "$AI_MODEL" ]]; then
  mkdir -p "$APP_TARGET/Contents/Resources/ai"
  ditto "$AI_RUNTIME" "$APP_TARGET/Contents/Resources/ai"
  ditto "$AI_MODEL" "$APP_TARGET/Contents/Resources/ai/Qwen3.5-0.8B-Q4_0.gguf"
  ditto "$PROJECT_DIR/models/qwen/Qwen3.5-LICENSE" \
    "$APP_TARGET/Contents/Resources/ai/Qwen3.5-LICENSE"
else
  echo "警告：离线 AI 运行时或模型尚未就绪，交付包将保留确定性核心但不含 AI 解释。" >&2
fi
"$QT_DIR/bin/macdeployqt" "$APP_TARGET" -always-overwrite -no-plugins
for PLUGIN in \
  platforms/libqcocoa.dylib \
  sqldrivers/libqsqlite.dylib \
  iconengines/libqsvgicon.dylib \
  imageformats/libqsvg.dylib; do
  mkdir -p "$APP_TARGET/Contents/PlugIns/${PLUGIN:h}"
  ditto "$QT_DIR/plugins/$PLUGIN" "$APP_TARGET/Contents/PlugIns/$PLUGIN"
done
/usr/libexec/PlistBuddy -c "Delete :CFBundleShortVersionString" "$APP_TARGET/Contents/Info.plist" 2>/dev/null || true
/usr/libexec/PlistBuddy -c "Delete :CFBundleVersion" "$APP_TARGET/Contents/Info.plist" 2>/dev/null || true
codesign --force --deep --sign - "$APP_TARGET"
codesign --verify --deep --strict "$APP_TARGET"
echo "$APP_TARGET"
