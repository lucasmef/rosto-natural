#!/bin/sh
set -eu

PROJECT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
VERSION=0.10.35
BAZEL_VERSION=7.4.1
SOURCE_DIR="$PROJECT_DIR/.deps/mediapipe-$VERSION"
BAZEL_DIR="$PROJECT_DIR/.toolchain/bazel-$BAZEL_VERSION"
BAZEL="$BAZEL_DIR/bazel"
OUTPUT_DIR="$PROJECT_DIR/dist/native"
OUTPUT="$OUTPUT_DIR/libRostoMediaPipe.dylib"
BUILD_LOG="$PROJECT_DIR/build-mediapipe.log"
BUILD_JOBS=${ROSTO_BUILD_JOBS:-4}

if [ ! -x "$BAZEL" ]; then
  mkdir -p "$BAZEL_DIR"
  BAZEL_ASSET="bazel-$BAZEL_VERSION-darwin-arm64"
  BAZEL_URL="https://github.com/bazelbuild/bazel/releases/download/$BAZEL_VERSION"
  /usr/bin/curl -fL "$BAZEL_URL/$BAZEL_ASSET" -o "$BAZEL_DIR/$BAZEL_ASSET"
  /usr/bin/curl -fL "$BAZEL_URL/$BAZEL_ASSET.sha256" \
    -o "$BAZEL_DIR/$BAZEL_ASSET.sha256"
  (
    cd "$BAZEL_DIR"
    /usr/bin/shasum -a 256 -c "$BAZEL_ASSET.sha256"
  )
  /bin/chmod +x "$BAZEL_DIR/$BAZEL_ASSET"
  /bin/mv "$BAZEL_DIR/$BAZEL_ASSET" "$BAZEL"
fi

if [ ! -d "$SOURCE_DIR/.git" ]; then
  mkdir -p "$PROJECT_DIR/.deps"
  /usr/bin/git clone --depth 1 --branch "v$VERSION" \
    https://github.com/google-ai-edge/mediapipe.git "$SOURCE_DIR"
fi
TAG=$(/usr/bin/git -C "$SOURCE_DIR" describe --tags --exact-match)
if [ "$TAG" != "v$VERSION" ]; then
  echo "Versão MediaPipe inesperada: $TAG" >&2
  exit 2
fi
if /usr/bin/grep -q \
  "https://gitlab.com/libeigen/eigen/-/archive/" "$SOURCE_DIR/WORKSPACE"
then
  (
    cd "$SOURCE_DIR"
    /usr/bin/patch -p0 \
      < "$PROJECT_DIR/mediapipe_bridge/eigen-gitlab-api.patch"
  )
fi
if ! /usr/bin/grep -q "opencv3-macos-fp.patch" "$SOURCE_DIR/WORKSPACE"
then
  (
    cd "$SOURCE_DIR"
    /usr/bin/patch -p0 \
      < "$PROJECT_DIR/mediapipe_bridge/opencv-source-workspace.patch"
  )
fi

/usr/bin/ditto "$PROJECT_DIR/mediapipe_bridge" "$SOURCE_DIR/rosto_bridge"
if ! (
  cd "$SOURCE_DIR"
  "$BAZEL" build -c opt --jobs="$BUILD_JOBS" --noshow_progress \
    --define MEDIAPIPE_DISABLE_GPU=1 \
    --define OPENCV=source \
    --copt=-DHAVE_UNISTD_H \
    --copt=-Dfdopen=fdopen \
    --host_copt=-DHAVE_UNISTD_H \
    --host_copt=-Dfdopen=fdopen \
    //rosto_bridge:libRostoMediaPipe.dylib
) >"$BUILD_LOG" 2>&1
then
  /usr/bin/tail -n 80 "$BUILD_LOG" >&2
  echo "Log completo: $BUILD_LOG" >&2
  exit 2
fi

BUILT="$SOURCE_DIR/bazel-bin/rosto_bridge/libRostoMediaPipe.dylib"
if [ ! -f "$BUILT" ]; then
  echo "A compilação não produziu $BUILT" >&2
  exit 2
fi
mkdir -p "$OUTPUT_DIR"
/usr/bin/ditto "$BUILT" "$OUTPUT"
/usr/bin/install_name_tool -id "@rpath/libRostoMediaPipe.dylib" "$OUTPUT"
/usr/bin/codesign --force --sign - "$OUTPUT"
/usr/bin/codesign --verify --strict "$OUTPUT"
printf '%s\n' "$OUTPUT"
