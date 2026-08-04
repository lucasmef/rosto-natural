#!/bin/sh
set -eu

PROJECT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
DIST_DIR="$PROJECT_DIR/dist"
OUTPUT_DIR=${ROSTO_PACKAGE_OUTPUT_DIR:-"$DIST_DIR"}
MANIFEST="$PROJECT_DIR/uxp/manifest.json"
UDT_APP="/Applications/Adobe UXP Developer Tools/Adobe UXP Developer Tools.app"
UDT_NODE="$UDT_APP/Contents/MacOS/Adobe UXP Developer Tools"
NODE="$PROJECT_DIR/.toolchain/node/bin/node"
REPLACE=false

if [ "$#" -eq 1 ] && [ "$1" = "--replace" ]; then
  REPLACE=true
elif [ "$#" -ne 0 ]; then
  echo "Uso: $0 [--replace]" >&2
  exit 2
fi

if [ ! -x "$UDT_NODE" ]; then
  echo "Adobe UXP Developer Tools 2.2+ ausente." >&2
  exit 2
fi
if [ ! -x "$NODE" ]; then
  echo "Node.js empacotado ausente em .toolchain/node." >&2
  exit 2
fi

VERSION=$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' \
  "$UDT_APP/Contents/Info.plist")
if ! /usr/bin/awk -F. '
  NF < 2 || $1 !~ /^[0-9]+$/ || $2 !~ /^[0-9]+$/ { exit 1 }
  ($1 + 0) > 2 || (($1 + 0) == 2 && ($2 + 0) >= 2) { exit 0 }
  { exit 1 }
' <<EOF
$VERSION
EOF
then
  echo "Adobe UXP Developer Tools 2.2+ necessário; encontrado $VERSION." >&2
  exit 2
fi

PLUGIN_ID=$("$NODE" -e \
  'process.stdout.write(require(process.argv[1]).id)' "$MANIFEST")
if ! "$NODE" -e \
  'if (!/^[A-Za-z0-9._-]+$/.test(process.argv[1])) process.exit(1)' \
  "$PLUGIN_ID"; then
  echo "ID UXP inválido para nome de pacote: $PLUGIN_ID" >&2
  exit 2
fi
OUTPUT_NAME="${PLUGIN_ID}_premierepro.ccx"
if [ -L "$DIST_DIR" ] ||
   { [ -e "$DIST_DIR" ] && [ ! -d "$DIST_DIR" ]; }; then
  echo "Diretório dist inseguro; publicação recusada: $DIST_DIR" >&2
  exit 2
fi
if [ "$OUTPUT_DIR" = "$DIST_DIR" ]; then
  mkdir -p "$DIST_DIR"
else
  OUTPUT_PARENT=$(/usr/bin/dirname "$OUTPUT_DIR")
  OUTPUT_BASENAME=$(/usr/bin/basename "$OUTPUT_DIR")
  case "$OUTPUT_BASENAME" in
    .release-stage.*) ;;
    *)
      echo "ROSTO_PACKAGE_OUTPUT_DIR fora do stage de release: $OUTPUT_DIR" >&2
      exit 2
      ;;
  esac
  if [ "$OUTPUT_PARENT" != "$DIST_DIR" ] || [ -L "$OUTPUT_DIR" ] ||
     [ ! -d "$OUTPUT_DIR" ]; then
    echo "Stage de release inseguro ou inexistente: $OUTPUT_DIR" >&2
    exit 2
  fi
fi
. "$PROJECT_DIR/scripts/publish_lock.sh"
OUTPUT="$OUTPUT_DIR/$OUTPUT_NAME"
BACKUP=""
BACKUP_DIR=""
HAD_OUTPUT=false
PUBLICATION_PENDING=false
STAGE_DIR=""
LOCK_DIR="$OUTPUT_DIR/.ccx-package.lock"
LOCK_ACQUIRED=false
rollback_publication() {
  [ "$PUBLICATION_PENDING" = true ] || return 0
  if [ "$HAD_OUTPUT" = true ]; then
    if [ ! -f "$BACKUP" ]; then
      echo "ATENÇÃO: backup CCX esperado está ausente: $BACKUP" >&2
      return 1
    fi
    /bin/mv -f "$BACKUP" "$OUTPUT" || return 1
  elif [ -e "$OUTPUT" ] || [ -L "$OUTPUT" ]; then
    /bin/mv "$OUTPUT" "$BACKUP_DIR/failed.$OUTPUT_NAME" || return 1
  fi
  PUBLICATION_PENDING=false
  return 0
}
cleanup() {
  trap '' HUP INT TERM
  rollback_ok=true
  if ! rollback_publication; then
    echo "ATENÇÃO: rollback do CCX falhou." >&2
    rollback_ok=false
  fi
  case "$STAGE_DIR" in
    "$OUTPUT_DIR"/.ccx-stage.*)
      [ ! -e "$STAGE_DIR" ] || /bin/rm -rf -- "$STAGE_DIR"
      ;;
  esac
  if [ "$LOCK_ACQUIRED" = true ] && [ "$rollback_ok" = true ]; then
    /bin/rm -f "$LOCK_DIR/pid"
    /bin/rmdir "$LOCK_DIR" 2>/dev/null ||
      echo "ATENÇÃO: lock CCX não pôde ser removido: $LOCK_DIR" >&2
  elif [ "$LOCK_ACQUIRED" = true ]; then
    echo "Lock preservado para inspeção: $LOCK_DIR" >&2
  fi
  if [ "$rollback_ok" = true ]; then
    rosto_publish_lock_release || true
  else
    echo "Lock global preservado porque o rollback falhou." >&2
    rosto_publish_lock_preserve "rollback CCX falhou" || true
  fi
}
trap cleanup EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM

rosto_publish_lock_acquire

if ! /bin/mkdir "$LOCK_DIR" 2>/dev/null; then
  echo "Outra publicação CCX está ativa ou deixou lock: $LOCK_DIR" >&2
  exit 2
fi
LOCK_ACQUIRED=true
printf '%s\n' "$$" > "$LOCK_DIR/pid"
if [ -L "$OUTPUT" ] || { [ -e "$OUTPUT" ] && [ ! -f "$OUTPUT" ]; }; then
  echo "Destino CCX não é arquivo regular; publicação recusada: $OUTPUT" >&2
  exit 2
fi
if [ -e "$OUTPUT" ] && [ "$REPLACE" != true ]; then
  echo "Pacote já existe; use --replace para substituição recuperável: $OUTPUT" >&2
  exit 2
fi
STAGE_DIR=$(/usr/bin/mktemp -d "$OUTPUT_DIR/.ccx-stage.XXXXXX")

/usr/bin/env ELECTRON_RUN_AS_NODE=1 "$UDT_NODE" \
  "$PROJECT_DIR/tools/package_uxp_ccx.js" "$MANIFEST" "$STAGE_DIR"
STAGED="$STAGE_DIR/$OUTPUT_NAME"
/usr/bin/unzip -t "$STAGED" >/dev/null

EXPECTED_LIST="$STAGE_DIR/expected-files.txt"
ACTUAL_LIST="$STAGE_DIR/actual-files.txt"
printf '%s\n' \
  index.html \
  js/analysis.js \
  js/main.js \
  js/premiere.js \
  manifest.json \
  styles.css > "$EXPECTED_LIST"
/usr/bin/unzip -Z1 "$STAGED" | LC_ALL=C /usr/bin/sort > "$ACTUAL_LIST"
/usr/bin/diff -u "$EXPECTED_LIST" "$ACTUAL_LIST"

PACKAGED_MANIFEST="$STAGE_DIR/manifest.json"
/usr/bin/unzip -p "$STAGED" manifest.json > "$PACKAGED_MANIFEST"
"$NODE" -e '
  const fs = require("fs");
  const a = JSON.parse(fs.readFileSync(process.argv[1], "utf8"));
  const b = JSON.parse(fs.readFileSync(process.argv[2], "utf8"));
  const normalize = value => Array.isArray(value) ? value.map(normalize) :
    value && typeof value === "object" ? Object.fromEntries(
      Object.keys(value).sort().map(key => [key, normalize(value[key])])) :
    value;
  if (JSON.stringify(normalize(a)) !== JSON.stringify(normalize(b)))
    process.exit(1);
' "$MANIFEST" "$PACKAGED_MANIFEST"
for relative in index.html js/analysis.js js/main.js js/premiere.js styles.css; do
  /usr/bin/unzip -p "$STAGED" "$relative" > "$STAGE_DIR/payload"
  if ! /usr/bin/cmp -s "$PROJECT_DIR/uxp/$relative" "$STAGE_DIR/payload"; then
    echo "CCX recusado: payload divergente: $relative" >&2
    exit 2
  fi
done
STAGED_HASH_FILE="$STAGE_DIR/staged.sha256"
/usr/bin/shasum -a 256 "$STAGED" > "$STAGED_HASH_FILE"
STAGED_HASH=$(/usr/bin/awk '{print $1}' "$STAGED_HASH_FILE")

BACKUPS_DIR="$OUTPUT_DIR/backups"
BACKUP_ROOT="$BACKUPS_DIR/ccx"
for directory in "$BACKUPS_DIR" "$BACKUP_ROOT"; do
  if [ -L "$directory" ] ||
     { [ -e "$directory" ] && [ ! -d "$directory" ]; }; then
    echo "Diretório de backup CCX inseguro: $directory" >&2
    exit 2
  fi
  if [ ! -d "$directory" ]; then /bin/mkdir "$directory"; fi
  if [ -L "$directory" ] || [ ! -d "$directory" ]; then
    echo "Falha ao criar diretório de backup CCX seguro: $directory" >&2
    exit 2
  fi
done
BACKUP_DIR=$(/usr/bin/mktemp -d "$BACKUP_ROOT/run.XXXXXX")
BACKUP="$BACKUP_DIR/$OUTPUT_NAME"
if [ -e "$OUTPUT" ]; then
  HAD_OUTPUT=true
  /bin/cp -p "$OUTPUT" "$BACKUP"
  /usr/bin/cmp -s "$OUTPUT" "$BACKUP" || {
    echo "Falha ao validar backup do CCX." >&2
    exit 2
  }
fi
PUBLICATION_PENDING=true
/bin/mv -f "$STAGED" "$OUTPUT"
FINAL_HASH_FILE="$STAGE_DIR/final.sha256"
/usr/bin/shasum -a 256 "$OUTPUT" > "$FINAL_HASH_FILE"
FINAL_HASH=$(/usr/bin/awk '{print $1}' "$FINAL_HASH_FILE")
if [ "$FINAL_HASH" != "$STAGED_HASH" ]; then
  echo "CCX publicado divergiu do artefato validado." >&2
  exit 2
fi
PUBLICATION_PENDING=false
if [ "$HAD_OUTPUT" = false ]; then
  /bin/rmdir "$BACKUP_DIR" 2>/dev/null || true
  /bin/rmdir "$BACKUP_ROOT" 2>/dev/null || true
fi
/usr/bin/shasum -a 256 "$OUTPUT"
printf 'CCX oficial UDT %s preparado em %s\n' "$VERSION" "$OUTPUT"
