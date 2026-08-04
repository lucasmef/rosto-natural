#!/bin/sh
set -eu

PROJECT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
DIST_DIR="$PROJECT_DIR/dist"
VERSION=$(/usr/bin/python3 -c \
  'import json,sys; print(json.load(open(sys.argv[1]))["version"])' \
  "$PROJECT_DIR/uxp/manifest.json")
PLUGIN_ID=$(/usr/bin/python3 -c \
  'import json,sys; print(json.load(open(sys.argv[1]))["id"])' \
  "$PROJECT_DIR/uxp/manifest.json")
if ! /usr/bin/python3 -c '
import re, sys
if not re.fullmatch(r"\d+\.\d+\.\d+", sys.argv[1]): raise SystemExit(1)
if not re.fullmatch(r"[A-Za-z0-9._-]+", sys.argv[2]): raise SystemExit(1)
' "$VERSION" "$PLUGIN_ID"; then
  echo "Versão ou ID UXP impróprio para uma release." >&2
  exit 2
fi

UXP_NAME="Rosto-Natural-UXP-$VERSION.zip"
UXP_CHECKSUM_NAME="$UXP_NAME.sha256"
NATIVE_NAME="Rosto-Natural-Native-$VERSION-mac-arm64.zip"
CCX_NAME="${PLUGIN_ID}_premierepro.ccx"
RELEASE_ITEMS="$UXP_NAME $UXP_CHECKSUM_NAME $NATIVE_NAME $CCX_NAME SHA256SUMS"

if [ -L "$DIST_DIR" ] ||
   { [ -e "$DIST_DIR" ] && [ ! -d "$DIST_DIR" ]; }; then
  echo "Diretório dist inseguro; release recusada: $DIST_DIR" >&2
  exit 2
fi
mkdir -p "$DIST_DIR"
. "$PROJECT_DIR/scripts/publish_lock.sh"
STAGE_DIR=""
BACKUP_RUN_DIR=""
PUBLICATION_PENDING=false
LOCK_DIR="$DIST_DIR/.release.lock"
LOCK_ACQUIRED=false
rollback_publication() {
  [ "$PUBLICATION_PENDING" = true ] || return 0
  rollback_failed=false
  for name in $RELEASE_ITEMS; do
    destination="$DIST_DIR/$name"
    backup="$BACKUP_RUN_DIR/$name"
    absent="$BACKUP_RUN_DIR/.absent.$name"
    if [ -f "$backup" ]; then
      if ! /bin/mv -f "$backup" "$destination"; then
        echo "ATENÇÃO: rollback falhou para $name" >&2
        rollback_failed=true
      fi
    elif [ ! -f "$absent" ]; then
      echo "ATENÇÃO: estado anterior desconhecido para $name" >&2
      rollback_failed=true
    elif [ -f "$absent" ] &&
         { [ -e "$destination" ] || [ -L "$destination" ]; }; then
      if ! /bin/mv "$destination" "$BACKUP_RUN_DIR/failed.$name"; then
        echo "ATENÇÃO: quarentena falhou para $name" >&2
        rollback_failed=true
      fi
    fi
  done
  if [ "$rollback_failed" = true ]; then return 1; fi
  PUBLICATION_PENDING=false
  return 0
}
cleanup() {
  trap '' HUP INT TERM
  rollback_ok=true
  if ! rollback_publication; then
    echo "ATENÇÃO: rollback da release falhou." >&2
    rollback_ok=false
  fi
  case "$STAGE_DIR" in
    "$DIST_DIR"/.release-stage.*)
      [ ! -e "$STAGE_DIR" ] || /bin/rm -rf -- "$STAGE_DIR"
      ;;
  esac
  if [ "$LOCK_ACQUIRED" = true ] && [ "$rollback_ok" = true ]; then
    /bin/rm -f "$LOCK_DIR/pid"
    /bin/rmdir "$LOCK_DIR" 2>/dev/null ||
      echo "ATENÇÃO: lock da release não pôde ser removido: $LOCK_DIR" >&2
  elif [ "$LOCK_ACQUIRED" = true ]; then
    echo "Lock preservado para inspeção: $LOCK_DIR" >&2
  fi
  if [ "$rollback_ok" = true ]; then
    rosto_publish_lock_release || true
  else
    echo "Lock global preservado porque o rollback falhou." >&2
    rosto_publish_lock_preserve "rollback da release falhou" || true
  fi
}
trap cleanup EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM

rosto_publish_lock_acquire

if ! /bin/mkdir "$LOCK_DIR" 2>/dev/null; then
  echo "Outra release está ativa ou deixou lock: $LOCK_DIR" >&2
  exit 2
fi
LOCK_ACQUIRED=true
printf '%s\n' "$$" > "$LOCK_DIR/pid"

"$PROJECT_DIR/scripts/build_core.sh"
/usr/bin/python3 "$PROJECT_DIR/tools/validate_adobe_contract.py"
ROSTO_PUBLISH_LOCK_TOKEN="$ROSTO_PUBLISH_LOCK_TOKEN" \
  "$PROJECT_DIR/scripts/build_adobe.sh"
ROSTO_PUBLISH_LOCK_TOKEN="$ROSTO_PUBLISH_LOCK_TOKEN" \
  "$PROJECT_DIR/scripts/build_metal.sh"

PLUGIN="$DIST_DIR/native/Rosto Natural.plugin"
if [ ! -d "$PLUGIN" ]; then
  echo "Bundle nativo ausente depois do build: $PLUGIN" >&2
  exit 2
fi
"$PROJECT_DIR/scripts/validate_native_payload.sh" "$PLUGIN" "$VERSION"
STAGE_DIR=$(/usr/bin/mktemp -d "$DIST_DIR/.release-stage.XXXXXX")

ROSTO_PACKAGE_OUTPUT_DIR="$STAGE_DIR" \
ROSTO_PUBLISH_LOCK_TOKEN="$ROSTO_PUBLISH_LOCK_TOKEN" \
  "$PROJECT_DIR/scripts/package_uxp.sh"
ROSTO_PACKAGE_OUTPUT_DIR="$STAGE_DIR" \
ROSTO_PUBLISH_LOCK_TOKEN="$ROSTO_PUBLISH_LOCK_TOKEN" \
  "$PROJECT_DIR/scripts/package_uxp_ccx.sh"

STAGED_NATIVE="$STAGE_DIR/$NATIVE_NAME"
/usr/bin/ditto -c -k --norsrc --noextattr --noqtn --noacl --keepParent \
  "$PLUGIN" "$STAGED_NATIVE"
/usr/bin/unzip -t "$STAGED_NATIVE" >/dev/null
/usr/bin/zipinfo -1 "$STAGED_NATIVE" > "$STAGE_DIR/native-entries"
while IFS= read -r entry; do
  case "$entry" in
    "Rosto Natural.plugin"|"Rosto Natural.plugin/"|"Rosto Natural.plugin/"*)
      ;;
    *)
      echo "ZIP nativo recusado: caminho fora do bundle: $entry" >&2
      exit 2
      ;;
  esac
  case "$entry" in
    /*|../*|*/../*|*/..)
      echo "ZIP nativo recusado: travessia de caminho: $entry" >&2
      exit 2
      ;;
  esac
done < "$STAGE_DIR/native-entries"
mkdir -p "$STAGE_DIR/extracted"
/usr/bin/ditto -x -k "$STAGED_NATIVE" "$STAGE_DIR/extracted"
"$PROJECT_DIR/scripts/validate_native_payload.sh" \
  "$STAGE_DIR/extracted/Rosto Natural.plugin" "$VERSION"

for required in \
  "$STAGE_DIR/$UXP_NAME" \
  "$STAGE_DIR/$UXP_CHECKSUM_NAME" \
  "$STAGE_DIR/$NATIVE_NAME" \
  "$STAGE_DIR/$CCX_NAME"
do
  if [ -L "$required" ] || [ ! -s "$required" ] || [ ! -f "$required" ]; then
    echo "Artefato staged inválido: $required" >&2
    exit 2
  fi
done
(
  cd "$STAGE_DIR"
  /usr/bin/shasum -a 256 \
    "$UXP_NAME" "$NATIVE_NAME" "$CCX_NAME" > SHA256SUMS
  /usr/bin/shasum -a 256 -c SHA256SUMS >/dev/null
  /usr/bin/shasum -a 256 -c "$UXP_CHECKSUM_NAME" >/dev/null
)

BACKUPS_DIR="$DIST_DIR/backups"
BACKUP_ROOT="$BACKUPS_DIR/release"
for directory in "$BACKUPS_DIR" "$BACKUP_ROOT"; do
  if [ -L "$directory" ] ||
     { [ -e "$directory" ] && [ ! -d "$directory" ]; }; then
    echo "Diretório de backup da release inseguro: $directory" >&2
    exit 2
  fi
  if [ ! -d "$directory" ]; then /bin/mkdir "$directory"; fi
  if [ -L "$directory" ] || [ ! -d "$directory" ]; then
    echo "Falha ao criar backup seguro da release: $directory" >&2
    exit 2
  fi
done
BACKUP_RUN_DIR=$(/usr/bin/mktemp -d "$BACKUP_ROOT/run.XXXXXX")
for name in $RELEASE_ITEMS; do
  destination="$DIST_DIR/$name"
  if [ -L "$destination" ] ||
     { [ -e "$destination" ] && [ ! -f "$destination" ]; }; then
    echo "Destino da release não é arquivo regular: $destination" >&2
    exit 2
  fi
  if [ -f "$destination" ]; then
    /bin/cp -p "$destination" "$BACKUP_RUN_DIR/$name"
    /usr/bin/cmp -s "$destination" "$BACKUP_RUN_DIR/$name" || {
      echo "Backup da release divergiu: $name" >&2
      exit 2
    }
  else
    : > "$BACKUP_RUN_DIR/.absent.$name"
  fi
done

PUBLICATION_PENDING=true
for name in $RELEASE_ITEMS; do
  /bin/mv -f "$STAGE_DIR/$name" "$DIST_DIR/$name"
done
(
  cd "$DIST_DIR"
  /usr/bin/shasum -a 256 -c SHA256SUMS >/dev/null
  /usr/bin/shasum -a 256 -c "$UXP_CHECKSUM_NAME" >/dev/null
)
PUBLICATION_PENDING=false

printf 'Release offline %s validada e publicada em %s\n' \
  "$VERSION" "$DIST_DIR"
