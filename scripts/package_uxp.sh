#!/bin/sh
set -eu

PROJECT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
NODE="$PROJECT_DIR/.toolchain/node/bin/node"
DIST_DIR="$PROJECT_DIR/dist"
OUTPUT_DIR=${ROSTO_PACKAGE_OUTPUT_DIR:-"$DIST_DIR"}
VERSION=$("$NODE" -e \
  "process.stdout.write(require(process.argv[1]).version)" \
  "$PROJECT_DIR/uxp/manifest.json")
OUTPUT_NAME="Rosto-Natural-UXP-$VERSION.zip"
CHECKSUM_NAME="$OUTPUT_NAME.sha256"

if ! "$NODE" -e \
  'if (!/^\d+\.\d+\.\d+$/.test(process.argv[1])) process.exit(1)' \
  "$VERSION"; then
  echo "Versão UXP inválida: $VERSION" >&2
  exit 2
fi
if /usr/bin/find "$PROJECT_DIR/uxp" -type l -print | /usr/bin/grep -q .; then
  echo "Fonte UXP recusada: links simbólicos não são permitidos." >&2
  exit 2
fi

"$NODE" "$PROJECT_DIR/tests/uxp_analysis_test.js"
"$NODE" "$PROJECT_DIR/tests/uxp_premiere_test.js"
/usr/bin/python3 -m json.tool "$PROJECT_DIR/uxp/manifest.json" >/dev/null
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
FINAL_OUTPUT="$OUTPUT_DIR/$OUTPUT_NAME"
FINAL_CHECKSUM="$OUTPUT_DIR/$CHECKSUM_NAME"
BACKUP_DIR=""
BACKUP_OUTPUT=""
BACKUP_CHECKSUM=""
HAD_OUTPUT=false
HAD_CHECKSUM=false
PUBLICATION_PENDING=false
STAGE_DIR=""
LOCK_DIR="$OUTPUT_DIR/.uxp-package.lock"
LOCK_ACQUIRED=false
rollback_publication() {
  [ "$PUBLICATION_PENDING" = true ] || return 0
  rollback_failed=false
  if [ "$HAD_OUTPUT" = true ]; then
    if [ ! -f "$BACKUP_OUTPUT" ] ||
       ! /bin/mv -f "$BACKUP_OUTPUT" "$FINAL_OUTPUT"; then
      echo "ATENÇÃO: rollback falhou para $OUTPUT_NAME" >&2
      rollback_failed=true
    fi
  elif [ -e "$FINAL_OUTPUT" ] || [ -L "$FINAL_OUTPUT" ]; then
    if ! /bin/mv "$FINAL_OUTPUT" "$BACKUP_DIR/failed.$OUTPUT_NAME"; then
      echo "ATENÇÃO: quarentena falhou para $OUTPUT_NAME" >&2
      rollback_failed=true
    fi
  fi
  if [ "$HAD_CHECKSUM" = true ]; then
    if [ ! -f "$BACKUP_CHECKSUM" ] ||
       ! /bin/mv -f "$BACKUP_CHECKSUM" "$FINAL_CHECKSUM"; then
      echo "ATENÇÃO: rollback falhou para $CHECKSUM_NAME" >&2
      rollback_failed=true
    fi
  elif [ -e "$FINAL_CHECKSUM" ] || [ -L "$FINAL_CHECKSUM" ]; then
    if ! /bin/mv "$FINAL_CHECKSUM" \
         "$BACKUP_DIR/failed.$CHECKSUM_NAME"; then
      echo "ATENÇÃO: quarentena falhou para $CHECKSUM_NAME" >&2
      rollback_failed=true
    fi
  fi
  if [ "$rollback_failed" = true ]; then return 1; fi
  PUBLICATION_PENDING=false
  return 0
}
cleanup() {
  trap '' HUP INT TERM
  rollback_ok=true
  if ! rollback_publication; then
    echo "ATENÇÃO: rollback do pacote UXP falhou." >&2
    rollback_ok=false
  fi
  case "$STAGE_DIR" in
    "$OUTPUT_DIR"/.uxp-package-stage.*)
      [ ! -e "$STAGE_DIR" ] || /bin/rm -rf -- "$STAGE_DIR"
      ;;
  esac
  if [ "$LOCK_ACQUIRED" = true ] && [ "$rollback_ok" = true ]; then
    /bin/rm -f "$LOCK_DIR/pid"
    /bin/rmdir "$LOCK_DIR" 2>/dev/null ||
      echo "ATENÇÃO: lock UXP não pôde ser removido: $LOCK_DIR" >&2
  elif [ "$LOCK_ACQUIRED" = true ]; then
    echo "Lock preservado para inspeção: $LOCK_DIR" >&2
  fi
  if [ "$rollback_ok" = true ]; then
    rosto_publish_lock_release || true
  else
    echo "Lock global preservado porque o rollback falhou." >&2
    rosto_publish_lock_preserve "rollback UXP falhou" || true
  fi
}
trap cleanup EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM

rosto_publish_lock_acquire

if ! /bin/mkdir "$LOCK_DIR" 2>/dev/null; then
  echo "Outra publicação UXP está ativa ou deixou lock: $LOCK_DIR" >&2
  exit 2
fi
LOCK_ACQUIRED=true
printf '%s\n' "$$" > "$LOCK_DIR/pid"
STAGE_DIR=$(/usr/bin/mktemp -d "$OUTPUT_DIR/.uxp-package-stage.XXXXXX")

STAGED_OUTPUT="$STAGE_DIR/$OUTPUT_NAME"
/usr/bin/ditto -c -k --norsrc --noextattr --noqtn --noacl --keepParent \
  "$PROJECT_DIR/uxp" "$STAGED_OUTPUT"
/usr/bin/unzip -t "$STAGED_OUTPUT" >/dev/null
/usr/bin/zipinfo -1 "$STAGED_OUTPUT" | LC_ALL=C /usr/bin/sort \
  > "$STAGE_DIR/actual-entries"
printf '%s\n' \
  "uxp/" \
  "uxp/index.html" \
  "uxp/js/" \
  "uxp/js/analysis.js" \
  "uxp/js/main.js" \
  "uxp/js/premiere.js" \
  "uxp/manifest.json" \
  "uxp/styles.css" |
  LC_ALL=C /usr/bin/sort > "$STAGE_DIR/expected-entries"
if ! /usr/bin/cmp -s \
     "$STAGE_DIR/expected-entries" "$STAGE_DIR/actual-entries"; then
  echo "Pacote UXP recusado: inventário inesperado." >&2
  /usr/bin/diff -u "$STAGE_DIR/expected-entries" \
    "$STAGE_DIR/actual-entries" >&2 || true
  exit 2
fi
for relative in \
  index.html js/analysis.js js/main.js js/premiere.js manifest.json styles.css
do
  /usr/bin/unzip -p "$STAGED_OUTPUT" "uxp/$relative" \
    > "$STAGE_DIR/payload"
  if ! /usr/bin/cmp -s "$PROJECT_DIR/uxp/$relative" "$STAGE_DIR/payload"; then
    echo "Pacote UXP recusado: payload divergente: $relative" >&2
    exit 2
  fi
done
(
  cd "$STAGE_DIR"
  /usr/bin/shasum -a 256 "$OUTPUT_NAME" > "$CHECKSUM_NAME"
  /usr/bin/shasum -a 256 -c "$CHECKSUM_NAME" >/dev/null
)

for destination in "$FINAL_OUTPUT" "$FINAL_CHECKSUM"; do
  if [ -L "$destination" ] ||
     { [ -e "$destination" ] && [ ! -f "$destination" ]; }; then
    echo "Destino UXP não é arquivo regular; publicação recusada:" >&2
    echo "$destination" >&2
    exit 2
  fi
done
BACKUPS_DIR="$OUTPUT_DIR/backups"
BACKUP_ROOT="$BACKUPS_DIR/uxp-package"
for directory in "$BACKUPS_DIR" "$BACKUP_ROOT"; do
  if [ -L "$directory" ] ||
     { [ -e "$directory" ] && [ ! -d "$directory" ]; }; then
    echo "Diretório de backup UXP inseguro: $directory" >&2
    exit 2
  fi
  if [ ! -d "$directory" ]; then /bin/mkdir "$directory"; fi
  if [ -L "$directory" ] || [ ! -d "$directory" ]; then
    echo "Falha ao criar diretório de backup UXP seguro: $directory" >&2
    exit 2
  fi
done
BACKUP_DIR=$(/usr/bin/mktemp -d "$BACKUP_ROOT/run.XXXXXX")
BACKUP_OUTPUT="$BACKUP_DIR/$OUTPUT_NAME"
BACKUP_CHECKSUM="$BACKUP_DIR/$CHECKSUM_NAME"
if [ -e "$FINAL_OUTPUT" ]; then
  HAD_OUTPUT=true
  /bin/cp -p "$FINAL_OUTPUT" "$BACKUP_OUTPUT"
  /usr/bin/cmp -s "$FINAL_OUTPUT" "$BACKUP_OUTPUT" || exit 2
fi
if [ -e "$FINAL_CHECKSUM" ]; then
  HAD_CHECKSUM=true
  /bin/cp -p "$FINAL_CHECKSUM" "$BACKUP_CHECKSUM"
  /usr/bin/cmp -s "$FINAL_CHECKSUM" "$BACKUP_CHECKSUM" || exit 2
fi
PUBLICATION_PENDING=true
if ! /bin/mv -f "$STAGED_OUTPUT" "$FINAL_OUTPUT" ||
   ! /bin/mv -f "$STAGE_DIR/$CHECKSUM_NAME" "$FINAL_CHECKSUM"; then
  echo "Falha ao publicar o pacote UXP; artefatos anteriores restaurados." >&2
  exit 2
fi
if ! (cd "$OUTPUT_DIR" &&
      /usr/bin/shasum -a 256 -c "$CHECKSUM_NAME" >/dev/null); then
  echo "Pacote UXP publicado falhou na revalidação." >&2
  exit 2
fi
PUBLICATION_PENDING=false
if [ "$HAD_OUTPUT" = false ] && [ "$HAD_CHECKSUM" = false ]; then
  /bin/rmdir "$BACKUP_DIR" 2>/dev/null || true
  /bin/rmdir "$BACKUP_ROOT" 2>/dev/null || true
fi
printf '%s\n' "$FINAL_OUTPUT"
