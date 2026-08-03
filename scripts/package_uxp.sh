#!/bin/sh
set -eu

PROJECT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
NODE="$PROJECT_DIR/.toolchain/node/bin/node"
DIST_DIR="$PROJECT_DIR/dist"
VERSION=$("$NODE" -e \
  "process.stdout.write(require(process.argv[1]).version)" \
  "$PROJECT_DIR/uxp/manifest.json")
OUTPUT_NAME="Rosto-Natural-UXP-$VERSION.zip"
OUTPUT="$DIST_DIR/$OUTPUT_NAME"

"$NODE" "$PROJECT_DIR/tests/uxp_analysis_test.js"
/usr/bin/python3 -m json.tool "$PROJECT_DIR/uxp/manifest.json" >/dev/null
mkdir -p "$DIST_DIR"
/usr/bin/ditto -c -k --norsrc --noextattr --noqtn --noacl --keepParent \
  "$PROJECT_DIR/uxp" "$OUTPUT"
(
  cd "$DIST_DIR"
  /usr/bin/shasum -a 256 "$OUTPUT_NAME" > "$OUTPUT_NAME.sha256"
)
printf '%s\n' "$OUTPUT"
