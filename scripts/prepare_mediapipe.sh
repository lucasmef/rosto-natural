#!/bin/sh
set -eu

PROJECT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
DIST_DIR="$PROJECT_DIR/dist"
VERSION=0.10.35
EXPECTED_COMMIT=f8ef212d5c962c0e853db7e59d217056b187084b
BAZEL_VERSION=7.4.1
SOURCE_DIR="$PROJECT_DIR/.deps/mediapipe-$VERSION"
BAZEL_DIR="$PROJECT_DIR/.toolchain/bazel-$BAZEL_VERSION"
BAZEL="$BAZEL_DIR/bazel"
OUTPUT_DIR="$PROJECT_DIR/dist/native"
OUTPUT="$OUTPUT_DIR/libRostoMediaPipe.dylib"
BUILD_LOG="$PROJECT_DIR/build-mediapipe.log"
BUILD_JOBS=${ROSTO_BUILD_JOBS:-4}
MEDIAPIPE_ITEMS="libRostoMediaPipe.dylib
libopencv_calib3d.3.4.dylib
libopencv_core.3.4.dylib
libopencv_features2d.3.4.dylib
libopencv_highgui.3.4.dylib
libopencv_imgcodecs.3.4.dylib
libopencv_imgproc.3.4.dylib
libopencv_video.3.4.dylib
libopencv_videoio.3.4.dylib"
STAGE_DIR=""
STAGED_NATIVE=""
BACKUP_RUN_DIR=""
BACKUP=""
HAD_NATIVE=false
PUBLICATION_PENDING=false
OLD_MOVED=false
NEW_PUBLISHED=false

if [ -L "$DIST_DIR" ] ||
   { [ -e "$DIST_DIR" ] && [ ! -d "$DIST_DIR" ]; }; then
  echo "Diretório dist inseguro; build MediaPipe recusado: $DIST_DIR" >&2
  exit 2
fi
mkdir -p "$DIST_DIR"
. "$PROJECT_DIR/scripts/publish_lock.sh"

rollback_publication() {
  [ "$PUBLICATION_PENDING" = true ] || return 0
  if [ "$NEW_PUBLISHED" = true ] &&
     { [ -e "$OUTPUT_DIR" ] || [ -L "$OUTPUT_DIR" ]; }; then
    if ! /bin/mv "$OUTPUT_DIR" "$BACKUP_RUN_DIR/native.failed"; then
      echo "ATENÇÃO: não foi possível quarentenar dist/native novo." >&2
      return 1
    fi
  fi
  if [ "$OLD_MOVED" = true ]; then
    if [ ! -d "$BACKUP" ] || [ -L "$BACKUP" ] ||
       ! /bin/mv "$BACKUP" "$OUTPUT_DIR"; then
      echo "ATENÇÃO: restauração de dist/native anterior falhou." >&2
      return 1
    fi
  fi
  PUBLICATION_PENDING=false
  return 0
}
cleanup() {
  trap '' HUP INT TERM
  rollback_ok=true
  if ! rollback_publication; then
    echo "ATENÇÃO: rollback do MediaPipe falhou." >&2
    rollback_ok=false
    rosto_publish_lock_preserve "rollback MediaPipe falhou" || true
  fi
  case "$STAGE_DIR" in
    "$DIST_DIR"/.mediapipe-stage.*)
      [ ! -e "$STAGE_DIR" ] || /bin/rm -rf -- "$STAGE_DIR"
      ;;
  esac
  if [ "$rollback_ok" = true ]; then
    rosto_publish_lock_release || true
  fi
}
trap cleanup EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM
rosto_publish_lock_acquire

for directory in "$DIST_DIR" "$OUTPUT_DIR"; do
  if [ -L "$directory" ] ||
     { [ -e "$directory" ] && [ ! -d "$directory" ]; }; then
    echo "Diretório de saída MediaPipe inseguro: $directory" >&2
    exit 2
  fi
done
if [ -d "$OUTPUT_DIR" ]; then
  if /usr/bin/find "$OUTPUT_DIR" -type l -print | /usr/bin/grep -q .; then
    echo "dist/native contém links simbólicos; publicação recusada." >&2
    exit 2
  fi
  HAD_NATIVE=true
fi

validate_mediapipe_dylib() {
  validation_root=$1
  macho=$2
  if [ -L "$macho" ] || [ ! -f "$macho" ] || [ ! -s "$macho" ]; then
    echo "Dylib MediaPipe inválida: $macho" >&2
    return 1
  fi
  if [ "$(/usr/bin/lipo -archs "$macho")" != "arm64" ]; then
    echo "Dylib MediaPipe não é arm64: $macho" >&2
    return 1
  fi
  /usr/bin/codesign --verify --strict "$macho"
  validation_rpaths=$(/usr/bin/otool -l "$macho" |
    /usr/bin/awk '/cmd LC_RPATH/{getline; getline; print $2}')
  if [ "$validation_rpaths" != "@loader_path" ]; then
    echo "LC_RPATH MediaPipe não hermético: $macho" >&2
    return 1
  fi
  /usr/bin/otool -L "$macho" |
    /usr/bin/sed -n '2,$s/^[[:space:]]*\([^[:space:]]*\).*/\1/p' \
      > "$STAGE_DIR/dependencies"
  while IFS= read -r dependency; do
    case "$dependency" in
      /System/Library/*|/usr/lib/*) ;;
      @rpath/*|@loader_path/*)
        dependency_name=${dependency##*/}
        case "$dependency" in
          "@rpath/$dependency_name"|"@loader_path/$dependency_name") ;;
          *)
            echo "Dependência MediaPipe não canônica: $dependency" >&2
            return 1
            ;;
        esac
        dependency_allowed=false
        for allowed_name in $MEDIAPIPE_ITEMS; do
          if [ "$dependency_name" = "$allowed_name" ]; then
            dependency_allowed=true
            break
          fi
        done
        if [ "$dependency_allowed" != true ]; then
          echo "Dependência MediaPipe fora da allowlist: $dependency" >&2
          return 1
        fi
        if [ ! -s "$validation_root/$dependency_name" ] ||
           [ -L "$validation_root/$dependency_name" ]; then
          echo "Dependência MediaPipe local ausente: $dependency" >&2
          return 1
        fi
        ;;
      *)
        echo "Dependência MediaPipe absoluta/inesperada: $dependency" >&2
        return 1
        ;;
    esac
  done < "$STAGE_DIR/dependencies"
  validation_minos=$(/usr/bin/otool -l "$macho" |
    /usr/bin/awk \
      '/cmd LC_BUILD_VERSION/{seen=1} seen && /minos/{print $2; exit}')
  if [ -z "$validation_minos" ] ||
     ! /usr/bin/awk -v version="$validation_minos" \
       'BEGIN { exit !(version + 0 <= 14.0) }'; then
    echo "Requisito macOS acima de 14.0: $macho" >&2
    return 1
  fi
}

sanitize_rpaths() {
  macho=$1
  rpaths=$('/usr/bin/otool' -l "$macho" |
    /usr/bin/awk '/cmd LC_RPATH/{getline; getline; print $2}')
  old_ifs=$IFS
  IFS='
'
  for rpath in $rpaths; do
    [ -n "$rpath" ] || continue
    if [ "$rpath" != "@loader_path" ]; then
      /usr/bin/install_name_tool -delete_rpath "$rpath" "$macho"
    fi
  done
  IFS=$old_ifs
  if ! /usr/bin/otool -l "$macho" |
       /usr/bin/grep -q "path @loader_path "; then
    /usr/bin/install_name_tool -add_rpath "@loader_path" "$macho"
  fi
}

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
  if [ -e "$SOURCE_DIR" ] &&
     [ -n "$(/bin/ls -A "$SOURCE_DIR" 2>/dev/null)" ]
  then
    echo "Checkout MediaPipe sem metadados Git em diretório não vazio:" >&2
    echo "$SOURCE_DIR" >&2
    echo "Recupere .git sem apagar o diretório ou o cache; clone recusado." >&2
    exit 2
  fi
  mkdir -p "$PROJECT_DIR/.deps"
  /usr/bin/git clone --depth 1 --branch "v$VERSION" \
    https://github.com/google-ai-edge/mediapipe.git "$SOURCE_DIR"
fi
TAG=$(/usr/bin/git -C "$SOURCE_DIR" describe --tags --exact-match)
if [ "$TAG" != "v$VERSION" ]; then
  echo "Versão MediaPipe inesperada: $TAG" >&2
  exit 2
fi
COMMIT=$(/usr/bin/git -C "$SOURCE_DIR" rev-parse HEAD)
if [ "$COMMIT" != "$EXPECTED_COMMIT" ]; then
  echo "Commit MediaPipe inesperado: $COMMIT" >&2
  echo "Esperado para v$VERSION: $EXPECTED_COMMIT" >&2
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

mkdir -p "$SOURCE_DIR/rosto_bridge"
/bin/cp -R -f "$PROJECT_DIR/mediapipe_bridge/." "$SOURCE_DIR/rosto_bridge/"
if ! (
  cd "$SOURCE_DIR"
  "$BAZEL" build -c opt --jobs="$BUILD_JOBS" --noshow_progress \
    --define MEDIAPIPE_DISABLE_GPU=1 \
    --define OPENCV=source \
    --define xnn_enable_arm_sme=false \
    --define xnn_enable_arm_sme2=false \
    --copt=-DXNN_ENABLE_ARM_SME=0 \
    --copt=-DXNN_ENABLE_ARM_SME2=0 \
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
if [ -L "$BUILT" ] || [ ! -f "$BUILT" ] || [ ! -s "$BUILT" ]; then
  echo "A compilação não produziu $BUILT" >&2
  exit 2
fi
OPENCV_LIB_DIR="$SOURCE_DIR/bazel-bin/third_party/opencv_cmake/lib"
if [ -L "$OPENCV_LIB_DIR" ] || [ ! -d "$OPENCV_LIB_DIR" ]; then
  echo "Bibliotecas OpenCV do MediaPipe ausentes: $OPENCV_LIB_DIR" >&2
  exit 2
fi

STAGE_DIR=$(/usr/bin/mktemp -d "$DIST_DIR/.mediapipe-stage.XXXXXX")
STAGED_NATIVE="$STAGE_DIR/native"
if [ "$HAD_NATIVE" = true ]; then
  /usr/bin/ditto "$OUTPUT_DIR" "$STAGED_NATIVE"
else
  /bin/mkdir "$STAGED_NATIVE"
fi
if [ -L "$STAGED_NATIVE" ] || [ ! -d "$STAGED_NATIVE" ] ||
   /usr/bin/find "$STAGED_NATIVE" -type l -print | /usr/bin/grep -q .; then
  echo "Stage MediaPipe insegura." >&2
  exit 2
fi

for name in $MEDIAPIPE_ITEMS; do
  if [ "$name" = "libRostoMediaPipe.dylib" ]; then
    source_dylib="$BUILT"
  else
    source_dylib="$OPENCV_LIB_DIR/$name"
  fi
  if [ -L "$source_dylib" ] || [ ! -f "$source_dylib" ] ||
     [ ! -s "$source_dylib" ]; then
    echo "Dependência MediaPipe esperada ausente: $source_dylib" >&2
    exit 2
  fi
  staged_dylib="$STAGED_NATIVE/$name"
  if [ -L "$staged_dylib" ] ||
     { [ -e "$staged_dylib" ] && [ ! -f "$staged_dylib" ]; }; then
    echo "Destino staged MediaPipe inseguro: $staged_dylib" >&2
    exit 2
  fi
  /usr/bin/ditto "$source_dylib" "$staged_dylib"
  /usr/bin/install_name_tool -id "@rpath/$name" "$staged_dylib"
  sanitize_rpaths "$staged_dylib"
  /usr/bin/codesign --force --sign - "$staged_dylib"
done

DYLIB_INVENTORY="$STAGE_DIR/all-dylibs"
/usr/bin/find "$STAGED_NATIVE" -name '*.dylib' -print \
  > "$DYLIB_INVENTORY"
while IFS= read -r staged_dylib; do
  [ "$(/usr/bin/dirname "$staged_dylib")" = "$STAGED_NATIVE" ] || continue
  staged_name=${staged_dylib##*/}
  staged_allowed=false
  for allowed_name in $MEDIAPIPE_ITEMS; do
    if [ "$staged_name" = "$allowed_name" ]; then
      staged_allowed=true
      break
    fi
  done
  if [ "$staged_allowed" != true ]; then
    echo "Dylib top-level inesperada preservada; publicação recusada:" >&2
    echo "$staged_dylib" >&2
    exit 2
  fi
done < "$DYLIB_INVENTORY"

for name in $MEDIAPIPE_ITEMS; do
  validate_mediapipe_dylib "$STAGED_NATIVE" "$STAGED_NATIVE/$name"
done
STAGED_HASHES="$STAGE_DIR/staged.sha256"
(
  cd "$STAGED_NATIVE"
  /usr/bin/shasum -a 256 $MEDIAPIPE_ITEMS > "$STAGED_HASHES"
  /usr/bin/shasum -a 256 -c "$STAGED_HASHES" >/dev/null
)

BACKUPS_DIR="$DIST_DIR/backups"
BACKUP_ROOT="$BACKUPS_DIR/mediapipe"
for directory in "$BACKUPS_DIR" "$BACKUP_ROOT"; do
  if [ -L "$directory" ] ||
     { [ -e "$directory" ] && [ ! -d "$directory" ]; }; then
    echo "Diretório de backup MediaPipe inseguro: $directory" >&2
    exit 2
  fi
  if [ ! -d "$directory" ]; then /bin/mkdir "$directory"; fi
  if [ -L "$directory" ] || [ ! -d "$directory" ]; then
    echo "Falha ao criar backup MediaPipe seguro: $directory" >&2
    exit 2
  fi
done
BACKUP_RUN_DIR=$(/usr/bin/mktemp -d "$BACKUP_ROOT/run.XXXXXX")
BACKUP="$BACKUP_RUN_DIR/native.previous"

PUBLICATION_PENDING=true
trap '' HUP INT TERM
if [ "$HAD_NATIVE" = true ]; then
  if ! /bin/mv "$OUTPUT_DIR" "$BACKUP"; then
    trap 'exit 129' HUP
    trap 'exit 130' INT
    trap 'exit 143' TERM
    echo "Falha ao preservar dist/native anterior." >&2
    exit 2
  fi
  OLD_MOVED=true
fi
if ! /bin/mv "$STAGED_NATIVE" "$OUTPUT_DIR"; then
  trap 'exit 129' HUP
  trap 'exit 130' INT
  trap 'exit 143' TERM
  echo "Falha ao publicar o conjunto MediaPipe; rollback será executado." >&2
  exit 2
fi
NEW_PUBLISHED=true
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM
for name in $MEDIAPIPE_ITEMS; do
  validate_mediapipe_dylib "$OUTPUT_DIR" "$OUTPUT_DIR/$name"
done
(
  cd "$OUTPUT_DIR"
  /usr/bin/shasum -a 256 -c "$STAGED_HASHES" >/dev/null
)
PUBLICATION_PENDING=false
if [ "$HAD_NATIVE" = false ]; then
  /bin/rmdir "$BACKUP_RUN_DIR" 2>/dev/null || true
  /bin/rmdir "$BACKUP_ROOT" 2>/dev/null || true
fi
printf '%s\n' "$OUTPUT"
