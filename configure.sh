#!/bin/zsh
set -euo pipefail

PROJECT_DIR="${0:A:h}"
QT_DIR="$PROJECT_DIR/.tools/Qt/6.10.2/macos"
CMAKE="$PROJECT_DIR/.tools/venv/bin/cmake"

"$CMAKE" -S "$PROJECT_DIR" -B "$PROJECT_DIR/build-qt610" -G "Unix Makefiles" \
  -DCMAKE_PREFIX_PATH="$QT_DIR" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
