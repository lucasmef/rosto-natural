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

version_at_least_2_2() {
  plist=$1
  version=$(/usr/bin/plutil -extract CFBundleShortVersionString raw -o - \
    "$plist" 2>/dev/null) || return 1
  major=${version%%.*}
  remainder=${version#*.}
  minor=${remainder%%.*}
  case "$major:$minor" in
    *[!0-9:]*|:*) return 1 ;;
  esac
  [ "$major" -gt 2 ] || { [ "$major" -eq 2 ] && [ "$minor" -ge 2 ]; }
}

uxp_developer_tool_ready() {
  for app in \
    "/Applications/Adobe UXP Developer Tool.app" \
    "/Applications/Adobe UXP Developer Tools/Adobe UXP Developer Tools.app"
  do
    [ -f "$app/Contents/Info.plist" ] || continue
    version_at_least_2_2 "$app/Contents/Info.plist" && return 0
  done
  return 1
}

adobe_sdk_ready() {
  root=$1
  for required in \
    Examples/Headers/AE_Effect.h \
    Examples/Headers/AE_EffectVers.h \
    Examples/Util/entry.h \
    Examples/Util/Param_Utils.h \
    Examples/Resources/AE_General.r
  do
    [ -f "$root/$required" ] || return 1
  done
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
check "Adobe C++ SDK" adobe_sdk_ready "$AE_ROOT"
check "Xcode completo" test -x "/Applications/Xcode.app/Contents/Developer/usr/bin/xcodebuild"
check "Compilador Metal" env \
  DEVELOPER_DIR="/Applications/Xcode.app/Contents/Developer" \
  /usr/bin/xcrun --find metal
check "Linker Metal (metallib)" env \
  DEVELOPER_DIR="/Applications/Xcode.app/Contents/Developer" \
  /usr/bin/xcrun --find metallib
check "UXP Developer Tool 2.2+" uxp_developer_tool_ready
check_optional "Identidade de distribuição Apple" sh -c \
  '/usr/bin/security find-identity -v -p codesigning 2>/dev/null | grep -q "\".*\""'

printf '\n'
if [ "$missing" -eq 0 ]; then
  printf 'READY: todos os pré-requisitos estão disponíveis.\n'
  exit 0
fi
printf '%s pré-requisito(s) externo(s) ainda pendente(s).\n' "$missing"
[ "$STRICT" -eq 0 ] || exit 2
