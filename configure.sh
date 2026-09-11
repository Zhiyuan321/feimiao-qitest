#!/bin/zsh
set -euo pipefail

PROJECT_DIR="${0:A:h}"
QT_DIR="$PROJECT_DIR/.tools/qt512-host/5.12.12/clang_64"
CMAKE="$PROJECT_DIR/.tools/venv/bin/cmake"

"$CMAKE" -S "$PROJECT_DIR" -B "$PROJECT_DIR/build-macos-qt512" -G "Unix Makefiles" \
  -DCMAKE_PREFIX_PATH="$QT_DIR" \
  -DQITEST_QT_VERSION=5.12.12 \
  -DQITEST_WIN7=OFF \
  -DCMAKE_OSX_ARCHITECTURES=x86_64 \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
