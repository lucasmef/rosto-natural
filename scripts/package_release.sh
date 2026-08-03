#!/bin/sh
set -eu

PROJECT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
DIST_DIR="$PROJECT_DIR/dist"

"$PROJECT_DIR/scripts/build_core.sh"
/usr/bin/python3 "$PROJECT_DIR/tools/validate_adobe_contract.py"
"$PROJECT_DIR/scripts/package_uxp.sh"
mkdir -p "$DIST_DIR"

PLUGIN="$DIST_DIR/native/Rosto Natural.plugin"
if [ -d "$PLUGIN" ]; then
  /usr/bin/ditto -c -k --norsrc --noextattr --noqtn --noacl --keepParent \
    "$PLUGIN" "$DIST_DIR/Rosto-Natural-Native-0.11.0-mac-arm64.zip"
fi

MANIFEST="$DIST_DIR/SHA256SUMS"
: > "$MANIFEST"
for item in \
  "$DIST_DIR"/Rosto-Natural-UXP-*.zip \
  "$DIST_DIR"/Rosto-Natural-Native-*.zip \
  "$DIST_DIR"/native/*.dylib \
  "$DIST_DIR"/native/*.metallib
do
  [ -e "$item" ] || continue
  relative=${item#"$DIST_DIR/"}
  (
    cd "$DIST_DIR"
    /usr/bin/shasum -a 256 "$relative"
  ) >> "$MANIFEST"
done
printf 'Pacote offline preparado em %s\n' "$DIST_DIR"
