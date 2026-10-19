#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
DEST=${1:-"$ROOT/integrations/editor-metal-v1/build"}
mkdir -p "$DEST"
/usr/bin/clang++ -std=c++20 -O3 -DNDEBUG -fobjc-arc -Wall -Wextra \
  -dynamiclib -I"$ROOT/core/include" -framework Foundation -framework Metal \
  "$ROOT/integrations/editor-metal-v1/engine.mm" "$ROOT/core/src/retouch.cpp" \
  "$ROOT/core/src/finishing.cpp" "$ROOT/core/src/temporal.cpp" \
  "$ROOT/local-dev/v004/bridge.cpp" "$ROOT/local-dev/v002/bridge.cpp" \
  -o "$DEST/libRostoMetalF5.dylib"
