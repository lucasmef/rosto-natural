#!/bin/sh
set -eu

PROJECT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
DIST_DIR="$PROJECT_DIR/dist"
OUT_DIR="$DIST_DIR/native"
DEVELOPER_DIR=${DEVELOPER_DIR:-/Applications/Xcode.app/Contents/Developer}
METAL_ITEMS="RostoNatural.air RostoNatural.metallib"
STAGE_DIR=""
BACKUP_RUN_DIR=""
BACKUP_AIR=""
BACKUP_METALLIB=""
HAD_AIR=false
HAD_METALLIB=false
PUBLICATION_PENDING=false
OLD_AIR_MOVED=false
OLD_METALLIB_MOVED=false
NEW_AIR_PUBLISHED=false
NEW_METALLIB_PUBLISHED=false
LOCK_DIR="$DIST_DIR/.metal-build.lock"
LOCK_ACQUIRED=false

if [ ! -x "$DEVELOPER_DIR/usr/bin/xcodebuild" ]; then
  echo "Xcode completo ausente; não é possível compilar Metal." >&2
  exit 2
fi
if ! DEVELOPER_DIR="$DEVELOPER_DIR" /usr/bin/xcrun -sdk macosx \
     -f metal >/dev/null 2>&1 ||
   ! DEVELOPER_DIR="$DEVELOPER_DIR" /usr/bin/xcrun -sdk macosx \
     -f metallib >/dev/null 2>&1
then
  echo "Compiladores metal/metallib ausentes no Xcode selecionado." >&2
  exit 2
fi

if [ -L "$DIST_DIR" ] ||
   { [ -e "$DIST_DIR" ] && [ ! -d "$DIST_DIR" ]; }; then
  echo "Diretório dist inseguro; build Metal recusado: $DIST_DIR" >&2
  exit 2
fi
if [ ! -d "$DIST_DIR" ]; then /bin/mkdir "$DIST_DIR"; fi
. "$PROJECT_DIR/scripts/publish_lock.sh"

rollback_publication() {
  [ "$PUBLICATION_PENDING" = true ] || return 0
  rollback_failed=false

  if [ "$NEW_METALLIB_PUBLISHED" = true ]; then
    if [ -L "$OUT_DIR/RostoNatural.metallib" ] ||
       [ ! -f "$OUT_DIR/RostoNatural.metallib" ] ||
       ! /bin/mv "$OUT_DIR/RostoNatural.metallib" \
         "$BACKUP_RUN_DIR/RostoNatural.metallib.failed"; then
      echo "ATENÇÃO: não foi possível quarentenar o metallib novo." >&2
      rollback_failed=true
    fi
  fi
  if [ "$NEW_AIR_PUBLISHED" = true ]; then
    if [ -L "$OUT_DIR/RostoNatural.air" ] ||
       [ ! -f "$OUT_DIR/RostoNatural.air" ] ||
       ! /bin/mv "$OUT_DIR/RostoNatural.air" \
         "$BACKUP_RUN_DIR/RostoNatural.air.failed"; then
      echo "ATENÇÃO: não foi possível quarentenar o AIR novo." >&2
      rollback_failed=true
    fi
  fi
  if [ "$OLD_AIR_MOVED" = true ]; then
    if [ -L "$BACKUP_AIR" ] || [ ! -f "$BACKUP_AIR" ] ||
       [ -e "$OUT_DIR/RostoNatural.air" ] ||
       [ -L "$OUT_DIR/RostoNatural.air" ] ||
       ! /bin/mv "$BACKUP_AIR" "$OUT_DIR/RostoNatural.air"; then
      echo "ATENÇÃO: restauração do AIR anterior falhou." >&2
      rollback_failed=true
    fi
  fi
  if [ "$OLD_METALLIB_MOVED" = true ]; then
    if [ -L "$BACKUP_METALLIB" ] || [ ! -f "$BACKUP_METALLIB" ] ||
       [ -e "$OUT_DIR/RostoNatural.metallib" ] ||
       [ -L "$OUT_DIR/RostoNatural.metallib" ] ||
       ! /bin/mv "$BACKUP_METALLIB" \
         "$OUT_DIR/RostoNatural.metallib"; then
      echo "ATENÇÃO: restauração do metallib anterior falhou." >&2
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
    echo "ATENÇÃO: rollback da publicação Metal falhou." >&2
    rollback_ok=false
  fi
  case "$STAGE_DIR" in
    "$DIST_DIR"/.metal-stage.*)
      [ ! -e "$STAGE_DIR" ] || /bin/rm -rf -- "$STAGE_DIR"
      ;;
  esac
  if [ "$LOCK_ACQUIRED" = true ] && [ "$rollback_ok" = true ]; then
    /bin/rm -f "$LOCK_DIR/pid"
    /bin/rmdir "$LOCK_DIR" 2>/dev/null ||
      echo "ATENÇÃO: lock Metal não pôde ser removido: $LOCK_DIR" >&2
  elif [ "$LOCK_ACQUIRED" = true ]; then
    echo "Lock Metal preservado para inspeção: $LOCK_DIR" >&2
  fi
  if [ "$rollback_ok" = true ]; then
    rosto_publish_lock_release || true
  else
    echo "Lock global preservado porque o rollback falhou." >&2
    rosto_publish_lock_preserve "rollback Metal falhou" || true
  fi
}
trap cleanup EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM

rosto_publish_lock_acquire

if [ -L "$OUT_DIR" ] ||
   { [ -e "$OUT_DIR" ] && [ ! -d "$OUT_DIR" ]; }; then
  echo "Diretório nativo inseguro; build Metal recusado: $OUT_DIR" >&2
  exit 2
fi
if [ ! -d "$OUT_DIR" ]; then /bin/mkdir "$OUT_DIR"; fi
if [ -L "$OUT_DIR" ] || [ ! -d "$OUT_DIR" ]; then
  echo "Falha ao criar diretório nativo seguro: $OUT_DIR" >&2
  exit 2
fi

if ! /bin/mkdir "$LOCK_DIR" 2>/dev/null; then
  echo "Outro build Metal está ativo ou deixou lock: $LOCK_DIR" >&2
  exit 2
fi
LOCK_ACQUIRED=true
(umask 077; printf '%s\n' "$$" > "$LOCK_DIR/pid")

STAGE_DIR=$(/usr/bin/mktemp -d "$DIST_DIR/.metal-stage.XXXXXX")
DEVELOPER_DIR="$DEVELOPER_DIR" /usr/bin/xcrun -sdk macosx metal \
  -c "$PROJECT_DIR/metal/RostoNatural.metal" \
  -o "$STAGE_DIR/RostoNatural.air"
DEVELOPER_DIR="$DEVELOPER_DIR" /usr/bin/xcrun -sdk macosx metallib \
  "$STAGE_DIR/RostoNatural.air" \
  -o "$STAGE_DIR/RostoNatural.metallib"

for name in $METAL_ITEMS; do
  staged="$STAGE_DIR/$name"
  if [ -L "$staged" ] || [ ! -f "$staged" ] || [ ! -s "$staged" ]; then
    echo "Artefato Metal staged inválido: $staged" >&2
    exit 2
  fi
done
STAGED_HASHES="$STAGE_DIR/staged.sha256"
(
  cd "$STAGE_DIR"
  /usr/bin/shasum -a 256 $METAL_ITEMS > "$STAGED_HASHES"
  /usr/bin/shasum -a 256 -c "$STAGED_HASHES" >/dev/null
)

for name in $METAL_ITEMS; do
  destination="$OUT_DIR/$name"
  if [ -L "$destination" ] ||
     { [ -e "$destination" ] && [ ! -f "$destination" ]; }; then
    echo "Destino Metal não é arquivo regular: $destination" >&2
    exit 2
  fi
done
if [ -f "$OUT_DIR/RostoNatural.air" ]; then HAD_AIR=true; fi
if [ -f "$OUT_DIR/RostoNatural.metallib" ]; then HAD_METALLIB=true; fi

BACKUPS_DIR="$DIST_DIR/backups"
BACKUP_ROOT="$BACKUPS_DIR/metal"
for directory in "$BACKUPS_DIR" "$BACKUP_ROOT"; do
  if [ -L "$directory" ] ||
     { [ -e "$directory" ] && [ ! -d "$directory" ]; }; then
    echo "Diretório de backup Metal inseguro: $directory" >&2
    exit 2
  fi
  if [ ! -d "$directory" ]; then /bin/mkdir "$directory"; fi
  if [ -L "$directory" ] || [ ! -d "$directory" ]; then
    echo "Falha ao criar backup Metal seguro: $directory" >&2
    exit 2
  fi
done
BACKUP_RUN_DIR=$(/usr/bin/mktemp -d "$BACKUP_ROOT/run.XXXXXX")
BACKUP_AIR="$BACKUP_RUN_DIR/RostoNatural.air"
BACKUP_METALLIB="$BACKUP_RUN_DIR/RostoNatural.metallib"

PUBLICATION_PENDING=true
trap '' HUP INT TERM
if [ "$HAD_AIR" = true ]; then
  if ! /bin/mv "$OUT_DIR/RostoNatural.air" "$BACKUP_AIR"; then
    trap 'exit 129' HUP
    trap 'exit 130' INT
    trap 'exit 143' TERM
    echo "Falha ao preservar o AIR anterior." >&2
    exit 2
  fi
  OLD_AIR_MOVED=true
fi
if [ "$HAD_METALLIB" = true ]; then
  if ! /bin/mv "$OUT_DIR/RostoNatural.metallib" "$BACKUP_METALLIB"; then
    trap 'exit 129' HUP
    trap 'exit 130' INT
    trap 'exit 143' TERM
    echo "Falha ao preservar o metallib anterior." >&2
    exit 2
  fi
  OLD_METALLIB_MOVED=true
fi
if ! /bin/mv "$STAGE_DIR/RostoNatural.air" \
     "$OUT_DIR/RostoNatural.air"; then
  trap 'exit 129' HUP
  trap 'exit 130' INT
  trap 'exit 143' TERM
  echo "Falha ao publicar o AIR; rollback será executado." >&2
  exit 2
fi
NEW_AIR_PUBLISHED=true
if ! /bin/mv "$STAGE_DIR/RostoNatural.metallib" \
     "$OUT_DIR/RostoNatural.metallib"; then
  trap 'exit 129' HUP
  trap 'exit 130' INT
  trap 'exit 143' TERM
  echo "Falha ao publicar o metallib; rollback será executado." >&2
  exit 2
fi
NEW_METALLIB_PUBLISHED=true
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM

for name in $METAL_ITEMS; do
  published="$OUT_DIR/$name"
  if [ -L "$published" ] || [ ! -f "$published" ] ||
     [ ! -s "$published" ]; then
    echo "Artefato Metal publicado inválido: $published" >&2
    exit 2
  fi
done
(
  cd "$OUT_DIR"
  /usr/bin/shasum -a 256 -c "$STAGED_HASHES" >/dev/null
)
PUBLICATION_PENDING=false

if [ "$HAD_AIR" = false ] && [ "$HAD_METALLIB" = false ]; then
  /bin/rmdir "$BACKUP_RUN_DIR" 2>/dev/null || true
  /bin/rmdir "$BACKUP_ROOT" 2>/dev/null || true
fi
printf 'Artefatos Metal publicados com rollback seguro em %s\n' "$OUT_DIR"
if [ "$HAD_AIR" = true ] || [ "$HAD_METALLIB" = true ]; then
  printf 'Backup anterior: %s\n' "$BACKUP_RUN_DIR"
fi
