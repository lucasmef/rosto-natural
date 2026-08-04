#!/bin/sh
set -eu

PROJECT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
export PYTHONPATH="$PROJECT_DIR/.toolchain/python${PYTHONPATH:+:$PYTHONPATH}"
CMAKE="$PROJECT_DIR/.toolchain/python/bin/cmake"
NINJA="$PROJECT_DIR/.toolchain/python/bin/ninja"
AE_ROOT=${AE_SDK_ROOT:-"$PROJECT_DIR/vendor/AfterEffectsSDK"}
DIST_DIR="$PROJECT_DIR/dist"
NATIVE_DIR="$DIST_DIR/native"
MEDIAPIPE_BRIDGE="$NATIVE_DIR/libRostoMediaPipe.dylib"
SELECTED_DEVELOPER_DIR=$(/usr/bin/xcode-select -p 2>/dev/null || true)
VERSION=$(/usr/bin/python3 -c \
  'import json,sys; print(json.load(open(sys.argv[1]))["version"])' \
  "$PROJECT_DIR/uxp/manifest.json")

if [ ! -x "$CMAKE" ] || [ ! -x "$NINJA" ]; then
  echo "CMake/Ninja locais ausentes; execute scripts/bootstrap_toolchain.sh." >&2
  exit 2
fi
for required in \
  Examples/Headers/AE_Effect.h \
  Examples/Headers/AE_EffectVers.h \
  Examples/Util/entry.h \
  Examples/Util/Param_Utils.h \
  Examples/Resources/AE_General.r
do
  if [ ! -f "$AE_ROOT/$required" ]; then
    echo "Adobe C++ SDK incompleto; ausente: $AE_ROOT/$required" >&2
    echo "Defina AE_SDK_ROOT ou extraia o SDK oficial completo." >&2
    exit 2
  fi
done
if [ -z "$SELECTED_DEVELOPER_DIR" ] ||
   ! DEVELOPER_DIR="$SELECTED_DEVELOPER_DIR" /usr/bin/xcrun -f clang >/dev/null 2>&1 ||
   ! DEVELOPER_DIR="$SELECTED_DEVELOPER_DIR" /usr/bin/xcrun -f Rez >/dev/null 2>&1
then
  echo "Ferramentas Apple clang/Rez ausentes." >&2
  exit 2
fi

if [ -L "$DIST_DIR" ] ||
   { [ -e "$DIST_DIR" ] && [ ! -d "$DIST_DIR" ]; }; then
  echo "Diretório dist inseguro; build recusado: $DIST_DIR" >&2
  exit 2
fi
if [ ! -d "$DIST_DIR" ]; then /bin/mkdir "$DIST_DIR"; fi
. "$PROJECT_DIR/scripts/publish_lock.sh"

BUILD_DIR=""
STAGE_DIR=""
FINAL_PLUGIN="$NATIVE_DIR/Rosto Natural.plugin"
BACKUP_RUN_DIR=""
BACKUP=""
HAD_FINAL=false
PUBLICATION_PENDING=false
OLD_MOVED=false
NEW_PUBLISHED=false
LOCK_DIR="$DIST_DIR/.native-build.lock"
LOCK_ACQUIRED=false
rollback_publication() {
  [ "$PUBLICATION_PENDING" = true ] || return 0
  if [ "$NEW_PUBLISHED" = true ] &&
     { [ -e "$FINAL_PLUGIN" ] || [ -L "$FINAL_PLUGIN" ]; }; then
    if ! /bin/mv "$FINAL_PLUGIN" \
         "$BACKUP_RUN_DIR/Rosto Natural.plugin.failed"; then
      echo "ATENÇÃO: não foi possível quarentenar o bundle novo." >&2
      return 1
    fi
  fi
  if [ "$OLD_MOVED" = true ]; then
    if [ ! -d "$BACKUP" ]; then
      echo "ATENÇÃO: backup nativo esperado está ausente: $BACKUP" >&2
      return 1
    fi
    if [ ! -e "$FINAL_PLUGIN" ] && [ ! -L "$FINAL_PLUGIN" ]; then
      /bin/mv "$BACKUP" "$FINAL_PLUGIN" || {
        echo "ATENÇÃO: restauração automática do bundle anterior falhou." >&2
        return 1
      }
    fi
  fi
  PUBLICATION_PENDING=false
  return 0
}
cleanup() {
  trap '' HUP INT TERM
  rollback_ok=true
  if ! rollback_publication; then
    echo "ATENÇÃO: rollback do build nativo falhou." >&2
    rollback_ok=false
  fi
  case "$BUILD_DIR" in
    /private/tmp/rosto-natural-adobe-build.*)
      [ ! -e "$BUILD_DIR" ] || /bin/rm -rf -- "$BUILD_DIR"
      ;;
  esac
  case "$STAGE_DIR" in
    /private/tmp/rosto-natural-native-stage.*)
      [ ! -e "$STAGE_DIR" ] || /bin/rm -rf -- "$STAGE_DIR"
      ;;
  esac
  if [ "$LOCK_ACQUIRED" = true ] && [ "$rollback_ok" = true ]; then
    /bin/rm -f "$LOCK_DIR/pid"
    /bin/rmdir "$LOCK_DIR" 2>/dev/null ||
      echo "ATENÇÃO: lock nativo não pôde ser removido: $LOCK_DIR" >&2
  elif [ "$LOCK_ACQUIRED" = true ]; then
    echo "Lock preservado para inspeção: $LOCK_DIR" >&2
  fi
  if [ "$rollback_ok" = true ]; then
    rosto_publish_lock_release || true
  else
    echo "Lock global preservado porque o rollback falhou." >&2
    rosto_publish_lock_preserve "rollback nativo falhou" || true
  fi
}
trap cleanup EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM

rosto_publish_lock_acquire

if [ -L "$NATIVE_DIR" ] ||
   { [ -e "$NATIVE_DIR" ] && [ ! -d "$NATIVE_DIR" ]; }; then
  echo "Diretório nativo inseguro; build recusado: $NATIVE_DIR" >&2
  exit 2
fi
if [ ! -d "$NATIVE_DIR" ]; then /bin/mkdir "$NATIVE_DIR"; fi
if [ -L "$NATIVE_DIR" ] || [ ! -d "$NATIVE_DIR" ]; then
  echo "Falha ao criar diretório nativo seguro: $NATIVE_DIR" >&2
  exit 2
fi

if ! /bin/mkdir "$LOCK_DIR" 2>/dev/null; then
  echo "Outro build nativo está ativo ou deixou lock: $LOCK_DIR" >&2
  exit 2
fi
LOCK_ACQUIRED=true
printf '%s\n' "$$" > "$LOCK_DIR/pid"

# Bazel is incremental, but this call is intentional on every native release:
# it proves that the published bridge matches the current bridge sources.
ROSTO_PUBLISH_LOCK_TOKEN="$ROSTO_PUBLISH_LOCK_TOKEN" \
  "$PROJECT_DIR/scripts/prepare_mediapipe.sh"
if [ ! -s "$MEDIAPIPE_BRIDGE" ]; then
  echo "Ponte MediaPipe ausente; o efeito não seria funcional." >&2
  exit 2
fi

BUILD_DIR=$(/usr/bin/mktemp -d /private/tmp/rosto-natural-adobe-build.XXXXXX)
STAGE_DIR=$(/usr/bin/mktemp -d /private/tmp/rosto-natural-native-stage.XXXXXX)
if [ "$(/usr/bin/stat -f %d "$STAGE_DIR")" != \
     "$(/usr/bin/stat -f %d "$NATIVE_DIR")" ]; then
  echo "Stage e destino nativo não estão no mesmo volume; publicação recusada." >&2
  exit 2
fi

DEVELOPER_DIR="$SELECTED_DEVELOPER_DIR" \
"$CMAKE" -S "$PROJECT_DIR" -B "$BUILD_DIR" -G Ninja \
  -DCMAKE_MAKE_PROGRAM="$NINJA" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=14.0 \
  -DROSTO_BUILD_TESTS=ON \
  -DROSTO_BUILD_ADOBE_PLUGIN=ON \
  -DROSTO_WITH_MEDIAPIPE=ON \
  -DROSTO_MEDIAPIPE_BRIDGE_LIBRARY="$MEDIAPIPE_BRIDGE" \
  -DAE_SDK_ROOT="$AE_ROOT"
DEVELOPER_DIR="$SELECTED_DEVELOPER_DIR" \
  "$CMAKE" --build "$BUILD_DIR"
"$CMAKE" --install "$BUILD_DIR" --prefix "$STAGE_DIR"

STAGED_PLUGIN="$STAGE_DIR/Rosto Natural.plugin"
if [ ! -d "$STAGED_PLUGIN" ]; then
  echo "Build Adobe não produziu o bundle esperado: $STAGED_PLUGIN" >&2
  exit 2
fi
if /usr/bin/find "$STAGED_PLUGIN" -type l -print | /usr/bin/grep -q .; then
  echo "Bundle staged contém link simbólico; assinatura recusada." >&2
  exit 2
fi
# Files copied from an official downloaded SDK/model can carry Finder metadata.
# Strip it only from the disposable staged bundle: codesign rejects resource
# forks and FinderInfo even though they are not part of the plugin payload.
/bin/chmod -R u+w "$STAGED_PLUGIN"
/usr/bin/xattr -cr "$STAGED_PLUGIN"
for dylib in "$STAGED_PLUGIN/Contents/Frameworks"/*.dylib; do
  [ -f "$dylib" ] || {
    echo "Bundle sem dependências MediaPipe/OpenCV." >&2
    exit 2
  }
  /usr/bin/codesign --force --sign - "$dylib"
done
# Signing nested code can make File Provider rematerialize Finder metadata on
# the enclosing .plugin. Clear it again immediately before sealing the bundle.
/usr/bin/xattr -cr "$STAGED_PLUGIN"
if ! /usr/bin/codesign --force --deep --sign - "$STAGED_PLUGIN"; then
  echo "Assinatura do bundle falhou; atributos estendidos presentes:" >&2
  /usr/bin/xattr -lr "$STAGED_PLUGIN" >&2 || true
  exit 2
fi
# File Provider can attach FinderInfo immediately after codesign touches a
# bundle inside Documents. It is not signed payload; remove it before strict
# verification. The protected provenance attribute may remain and is allowed.
/usr/bin/xattr -cr "$STAGED_PLUGIN"
"$PROJECT_DIR/scripts/validate_native_bundle.sh" \
  "$STAGED_PLUGIN" "$VERSION"

if [ -L "$FINAL_PLUGIN" ] ||
   { [ -e "$FINAL_PLUGIN" ] && [ ! -d "$FINAL_PLUGIN" ]; }; then
  echo "Destino nativo não é um bundle regular; publicação recusada:" >&2
  echo "$FINAL_PLUGIN" >&2
  exit 2
fi
if [ -d "$FINAL_PLUGIN" ]; then
  INSTALLED_PLIST="$FINAL_PLUGIN/Contents/Info.plist"
  if [ -L "$INSTALLED_PLIST" ] || [ ! -f "$INSTALLED_PLIST" ] ||
     /usr/bin/find "$FINAL_PLUGIN" -type l -print | /usr/bin/grep -q .; then
    echo "Bundle anterior não é uma instalação regular do produto." >&2
    exit 2
  fi
  INSTALLED_ID=$(/usr/libexec/PlistBuddy -c 'Print :CFBundleIdentifier' \
    "$INSTALLED_PLIST" 2>/dev/null || true)
  if [ "$INSTALLED_ID" != "com.lucas.RostoNatural" ]; then
    echo "Bundle anterior pertence a outro produto; substituição recusada." >&2
    exit 2
  fi
  HAD_FINAL=true
fi
BACKUPS_DIR="$DIST_DIR/backups"
BACKUP_ROOT="$BACKUPS_DIR/native-build"
for directory in "$BACKUPS_DIR" "$BACKUP_ROOT"; do
  if [ -L "$directory" ] ||
     { [ -e "$directory" ] && [ ! -d "$directory" ]; }; then
    echo "Diretório de backup nativo inseguro: $directory" >&2
    exit 2
  fi
  if [ ! -d "$directory" ]; then /bin/mkdir "$directory"; fi
  if [ -L "$directory" ] || [ ! -d "$directory" ]; then
    echo "Falha ao criar backup nativo seguro: $directory" >&2
    exit 2
  fi
done
BACKUP_RUN_DIR=$(/usr/bin/mktemp -d "$BACKUP_ROOT/run.XXXXXX")
BACKUP="$BACKUP_RUN_DIR/Rosto Natural.plugin"
PUBLICATION_PENDING=true
trap '' HUP INT TERM
if [ "$HAD_FINAL" = true ]; then
  if ! /bin/mv "$FINAL_PLUGIN" "$BACKUP"; then
    trap 'exit 129' HUP
    trap 'exit 130' INT
    trap 'exit 143' TERM
    echo "Falha ao preservar o bundle nativo anterior." >&2
    exit 2
  fi
  OLD_MOVED=true
fi
if ! /bin/mv "$STAGED_PLUGIN" "$FINAL_PLUGIN"; then
  trap 'exit 129' HUP
  trap 'exit 130' INT
  trap 'exit 143' TERM
  echo "Falha ao publicar o bundle nativo; rollback será executado." >&2
  exit 2
fi
NEW_PUBLISHED=true
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM
if ! "$PROJECT_DIR/scripts/validate_native_payload.sh" \
     "$FINAL_PLUGIN" "$VERSION"; then
  echo "Bundle publicado falhou na revalidação; rollback será executado." >&2
  exit 2
fi
PUBLICATION_PENDING=false
if [ "$HAD_FINAL" = false ]; then
  /bin/rmdir "$BACKUP_RUN_DIR" 2>/dev/null || true
fi

printf 'Bundle Adobe publicado com backup recuperável: %s\n' "$FINAL_PLUGIN"
if [ "$HAD_FINAL" = true ]; then printf 'Backup anterior: %s\n' "$BACKUP"; fi
