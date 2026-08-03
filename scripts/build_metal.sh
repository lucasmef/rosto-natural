#!/bin/sh
set -eu

PROJECT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
OUT_DIR="$PROJECT_DIR/dist/native"
DEVELOPER_DIR=${DEVELOPER_DIR:-/Applications/Xcode.app/Contents/Developer}

if [ ! -x "$DEVELOPER_DIR/usr/bin/xcodebuild" ]; then
  echo "Xcode completo ausente; não é possível compilar Metal." >&2
  exit 2
fi
mkdir -p "$OUT_DIR"
DEVELOPER_DIR="$DEVELOPER_DIR" /usr/bin/xcrun -sdk macosx metal \
  -c "$PROJECT_DIR/metal/RostoNatural.metal" \
  -o "$OUT_DIR/RostoNatural.air"
DEVELOPER_DIR="$DEVELOPER_DIR" /usr/bin/xcrun -sdk macosx metallib \
  "$OUT_DIR/RostoNatural.air" \
  -o "$OUT_DIR/RostoNatural.metallib"
