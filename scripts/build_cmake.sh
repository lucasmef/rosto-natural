#!/bin/sh
set -eu

PROJECT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
export PYTHONPATH="$PROJECT_DIR/.toolchain/python${PYTHONPATH:+:$PYTHONPATH}"
CMAKE="$PROJECT_DIR/.toolchain/python/bin/cmake"
NINJA="$PROJECT_DIR/.toolchain/python/bin/ninja"
BUILD_DIR="$PROJECT_DIR/build-cmake"

"$CMAKE" -S "$PROJECT_DIR" -B "$BUILD_DIR" -G Ninja \
  -DCMAKE_MAKE_PROGRAM="$NINJA" \
  -DCMAKE_BUILD_TYPE=Release \
  -DROSTO_BUILD_TESTS=ON \
  -DROSTO_BUILD_ADOBE_PLUGIN=OFF
"$CMAKE" --build "$BUILD_DIR"
"$BUILD_DIR/rosto_tests"
"$BUILD_DIR/rosto_mediapipe_adapter_tests"
"$BUILD_DIR/rosto_benchmark" 3840 2160
"$PROJECT_DIR/.toolchain/node/bin/node" \
  "$PROJECT_DIR/tests/uxp_analysis_test.js"
