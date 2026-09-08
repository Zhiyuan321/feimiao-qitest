#!/bin/zsh
set -euo pipefail

PROJECT_DIR="${0:A:h}"
"$PROJECT_DIR/configure.sh"
"$PROJECT_DIR/.tools/venv/bin/cmake" --build "$PROJECT_DIR/build-qt610" --parallel
"$PROJECT_DIR/.tools/venv/bin/ctest" --test-dir "$PROJECT_DIR/build-qt610" --output-on-failure
