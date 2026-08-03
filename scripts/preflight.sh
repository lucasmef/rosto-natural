#!/bin/sh
set -eu

PROJECT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
CMAKE="$PROJECT_DIR/.toolchain/python/bin/cmake"
NODE="$PROJECT_DIR/.toolchain/node/bin/node"
PYTHONPATH_LOCAL="$PROJECT_DIR/.toolchain/python"
STRICT=0
[ "${1:-}" = "--strict" ] && STRICT=1
missing=0

check() {
  label=$1
  shift
  if "$@" >/dev/null 2>&1; then
    printf 'OK      %s\n' "$label"
  else
    printf 'PENDENTE %s\n' "$label"
    missing=$((missing + 1))
  fi
}

check_optional() {
  label=$1
  shift
  if "$@" >/dev/null 2>&1; then
    printf 'OK      %s\n' "$label"
  else
    printf 'OPCIONAL %s\n' "$label"
  fi
}

printf 'Rosto Natural — preflight sem abrir o Premiere\n'
printf 'Projeto: %s\n\n' "$PROJECT_DIR"

check "Apple Silicon" test "$(/usr/bin/uname -m)" = "arm64"
check "Apple clang++" test -x "$(/usr/bin/xcrun --find clang++ 2>/dev/null)"
check "macOS SDK" test -d "$(/usr/bin/xcrun --sdk macosx --show-sdk-path 2>/dev/null)"
check "CMake local" env PYTHONPATH="$PYTHONPATH_LOCAL" "$CMAKE" --version
check "Ninja local" test -x "$PROJECT_DIR/.toolchain/python/bin/ninja"
check "Node.js local" test -x "$NODE"
check "Bazel local" test -x "$PROJECT_DIR/.toolchain/bazel-7.4.1/bazel"
check "MediaPipe 0.10.35 local" test -d \
  "$PROJECT_DIR/.deps/mediapipe-0.10.35/.git"
check "Face Landmarker" test -s "$PROJECT_DIR/models/face_landmarker.task"
check "Hair Segmenter" test -s "$PROJECT_DIR/models/hair_segmenter.tflite"
check "Premiere Pro 26.3" test -d \
  "/Applications/Adobe Premiere Pro 2026/Adobe Premiere Pro 2026.app"
check "After Effects 26.3" test -d \
  "/Applications/Adobe After Effects 2026/Adobe After Effects 2026.app"
check "Ponte MediaPipe compilada" test -s \
  "$PROJECT_DIR/dist/native/libRostoMediaPipe.dylib"

AE_ROOT=${AE_SDK_ROOT:-"$PROJECT_DIR/vendor/AfterEffectsSDK"}
check "Adobe C++ SDK" test -f "$AE_ROOT/Examples/Headers/AE_Effect.h"
check "Xcode completo" test -x "/Applications/Xcode.app/Contents/Developer/usr/bin/xcodebuild"
check "Compilador Metal" /usr/bin/xcrun --find metal
check "UXP Developer Tool" test -d \
  "/Applications/Adobe UXP Developer Tool.app"
check_optional "Identidade de distribuição Apple" sh -c \
  '/usr/bin/security find-identity -v -p codesigning 2>/dev/null | grep -q "\".*\""'

printf '\n'
if [ "$missing" -eq 0 ]; then
  printf 'READY: todos os pré-requisitos estão disponíveis.\n'
  exit 0
fi
printf '%s pré-requisito(s) externo(s) ainda pendente(s).\n' "$missing"
[ "$STRICT" -eq 0 ] || exit 2
