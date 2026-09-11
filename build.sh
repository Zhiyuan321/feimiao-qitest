#!/bin/zsh
set -euo pipefail

PROJECT_DIR="${0:A:h}"
QT_DIR="$PROJECT_DIR/.tools/qt512-host/5.12.12/clang_64"
"$PROJECT_DIR/configure.sh"
"$PROJECT_DIR/.tools/venv/bin/cmake" --build "$PROJECT_DIR/build-macos-qt512" --parallel
QT_PLUGIN_PATH="$QT_DIR/plugins" \
  "$PROJECT_DIR/.tools/venv/bin/ctest" --test-dir "$PROJECT_DIR/build-macos-qt512" --output-on-failure
