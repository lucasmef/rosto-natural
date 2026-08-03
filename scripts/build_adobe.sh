#!/bin/sh
set -eu

PROJECT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
export PYTHONPATH="$PROJECT_DIR/.toolchain/python${PYTHONPATH:+:$PYTHONPATH}"
CMAKE="$PROJECT_DIR/.toolchain/python/bin/cmake"
NINJA="$PROJECT_DIR/.toolchain/python/bin/ninja"
BUILD_DIR="$PROJECT_DIR/build-adobe"
STAGE_DIR="$PROJECT_DIR/dist/native"
AE_ROOT=${AE_SDK_ROOT:-"$PROJECT_DIR/vendor/AfterEffectsSDK"}
MEDIAPIPE_BRIDGE="$PROJECT_DIR/dist/native/libRostoMediaPipe.dylib"
SELECTED_DEVELOPER_DIR=$(/usr/bin/xcode-select -p 2>/dev/null || true)

if [ ! -f "$AE_ROOT/Examples/Headers/AE_Effect.h" ]; then
  echo "Adobe C++ SDK ausente em: $AE_ROOT" >&2
  echo "Defina AE_SDK_ROOT ou extraia o SDK em vendor/AfterEffectsSDK." >&2
  exit 2
fi
if [ -z "$SELECTED_DEVELOPER_DIR" ] ||
   ! DEVELOPER_DIR="$SELECTED_DEVELOPER_DIR" /usr/bin/xcrun -f clang >/dev/null 2>&1 ||
   ! DEVELOPER_DIR="$SELECTED_DEVELOPER_DIR" /usr/bin/xcrun -f Rez >/dev/null 2>&1
then
  echo "Ferramentas Apple clang/Rez ausentes." >&2
  exit 2
fi
if [ ! -f "$MEDIAPIPE_BRIDGE" ]; then
  "$PROJECT_DIR/scripts/prepare_mediapipe.sh"
fi
if [ ! -f "$MEDIAPIPE_BRIDGE" ]; then
  echo "Ponte MediaPipe ausente; o efeito não seria funcional." >&2
  exit 2
fi

DEVELOPER_DIR="$SELECTED_DEVELOPER_DIR" \
"$CMAKE" -S "$PROJECT_DIR" -B "$BUILD_DIR" -G Ninja \
  -DCMAKE_MAKE_PROGRAM="$NINJA" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DROSTO_BUILD_TESTS=ON \
  -DROSTO_BUILD_ADOBE_PLUGIN=ON \
  -DROSTO_WITH_MEDIAPIPE=ON \
  -DROSTO_MEDIAPIPE_BRIDGE_LIBRARY="$MEDIAPIPE_BRIDGE" \
  -DAE_SDK_ROOT="$AE_ROOT"
DEVELOPER_DIR="$SELECTED_DEVELOPER_DIR" \
"$CMAKE" --build "$BUILD_DIR"
"$CMAKE" --install "$BUILD_DIR" --prefix "$STAGE_DIR"

PLUGIN="$STAGE_DIR/Rosto Natural.plugin"
if [ -d "$PLUGIN" ]; then
  /usr/bin/codesign --force --deep --sign - "$PLUGIN"
  /usr/bin/codesign --verify --deep --strict "$PLUGIN"
fi
