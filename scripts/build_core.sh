#!/bin/sh
set -eu

PROJECT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
SDK_PATH=${SDK_PATH:-$(/usr/bin/xcrun --sdk macosx --show-sdk-path)}
CXX=${CXX:-$(/usr/bin/xcrun --find clang++)}
BUILD_DIR="$PROJECT_DIR/build-manual"

mkdir -p "$BUILD_DIR"
"$CXX" -std=c++20 -O3 -DNDEBUG -stdlib=libc++ -isysroot "$SDK_PATH" \
  -Wall -Wextra -Wpedantic -I"$PROJECT_DIR/core/include" \
  "$PROJECT_DIR/core/src/cache.cpp" \
  "$PROJECT_DIR/core/src/flyaway.cpp" \
  "$PROJECT_DIR/core/src/retouch.cpp" \
  "$PROJECT_DIR/core/src/temporal.cpp" \
  "$PROJECT_DIR/tests/core_tests.cpp" \
  -o "$BUILD_DIR/rosto_tests"
"$BUILD_DIR/rosto_tests"

"$CXX" -std=c++20 -O3 -DNDEBUG -stdlib=libc++ -isysroot "$SDK_PATH" \
  -Wall -Wextra -Wpedantic -DROSTO_WITH_MEDIAPIPE=1 \
  -I"$PROJECT_DIR/core/include" \
  -I"$PROJECT_DIR/mediapipe_bridge" \
  "$PROJECT_DIR/core/src/mediapipe_detector.cpp" \
  "$PROJECT_DIR/tests/mediapipe_adapter_test.cpp" \
  -o "$BUILD_DIR/rosto_mediapipe_adapter_tests"
"$BUILD_DIR/rosto_mediapipe_adapter_tests"

"$CXX" -std=c++20 -O3 -DNDEBUG -stdlib=libc++ -isysroot "$SDK_PATH" \
  -I"$PROJECT_DIR/core/include" \
  "$PROJECT_DIR/core/src/retouch.cpp" \
  "$PROJECT_DIR/core/src/finishing.cpp" \
  "$PROJECT_DIR/core/src/temporal.cpp" \
  "$PROJECT_DIR/tools/rosto_cli.cpp" \
  -o "$BUILD_DIR/rosto_cli"

"$CXX" -std=c++20 -O3 -DNDEBUG -stdlib=libc++ -isysroot "$SDK_PATH" \
  -I"$PROJECT_DIR/core/include" \
  "$PROJECT_DIR/core/src/retouch.cpp" \
  "$PROJECT_DIR/tools/benchmark.cpp" \
  -o "$BUILD_DIR/rosto_benchmark"
"$BUILD_DIR/rosto_benchmark" 3840 2160

"$CXX" -std=c++20 -O3 -DNDEBUG -stdlib=libc++ -isysroot "$SDK_PATH" \
  -I"$PROJECT_DIR/core/include" \
  "$PROJECT_DIR/core/src/flyaway.cpp" \
  "$PROJECT_DIR/tools/flyaway_analysis_cli.cpp" \
  -o "$BUILD_DIR/rosto_flyaway_analysis"

"$CXX" -std=c++20 -O3 -DNDEBUG -stdlib=libc++ -isysroot "$SDK_PATH" \
  -I"$PROJECT_DIR/core/include" \
  "$PROJECT_DIR/core/src/flyaway.cpp" \
  "$PROJECT_DIR/tools/flyaway_render_cli.cpp" \
  -o "$BUILD_DIR/rosto_flyaway_render"

if [ -x "$PROJECT_DIR/.toolchain/node/bin/node" ]; then
  "$PROJECT_DIR/.toolchain/node/bin/node" \
    "$PROJECT_DIR/tests/uxp_analysis_test.js"
elif command -v node >/dev/null 2>&1; then
  node "$PROJECT_DIR/tests/uxp_analysis_test.js"
else
  echo "warning: Node.js unavailable; UXP tests skipped" >&2
fi

"$CXX" -std=c++20 -O3 -DNDEBUG -stdlib=libc++ -isysroot "$SDK_PATH" \
 -Wall -Wextra -Wpedantic -I"$PROJECT_DIR/core/include" \
 "$PROJECT_DIR/core/src/retouch.cpp" "$PROJECT_DIR/core/src/temporal.cpp" \
 "$PROJECT_DIR/core/src/finishing.cpp" "$PROJECT_DIR/tests/finishing_tests.cpp" \
 -o "$BUILD_DIR/rosto_finishing_tests"
"$BUILD_DIR/rosto_finishing_tests"
