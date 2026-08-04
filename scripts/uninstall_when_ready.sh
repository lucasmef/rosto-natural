#!/bin/sh
set -eu

PROJECT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
DIST_DIR="$PROJECT_DIR/dist"
if [ "$#" -ne 1 ] || [ "$1" != "--confirm" ]; then
  echo "Este script remove a instalação local de forma recuperável." >&2
  echo "Execute novamente com --confirm." >&2
  exit 2
fi

require_host_closed() {
  host=$1
  pattern=$2
  set +e
  /usr/bin/pgrep -f "$pattern" >/dev/null 2>&1
  result=$?
  set -e
  if [ "$result" -eq 0 ]; then
    echo "Feche o $host antes de desinstalar o efeito nativo." >&2
    exit 2
  fi
  if [ "$result" -ne 1 ]; then
    echo "Não foi possível confirmar que o $host está fechado." >&2
    echo "Desinstalação recusada por segurança (pgrep retornou $result)." >&2
    exit 2
  fi
}

check_hosts_closed() {
  require_host_closed "Premiere Pro" \
    '[/]Adobe Premiere Pro[^/]*/Contents/MacOS/Adobe Premiere Pro'
  require_host_closed "Premiere Pro Headless" \
    '[/]Adobe Premiere Pro[^/]*/Contents/MacOS/PProHeadless'
  require_host_closed "Adobe Media Encoder" \
    '[/]Adobe Media Encoder[^/]*/Contents/MacOS/Adobe Media Encoder'
  require_host_closed "After Effects" \
    '[/]Adobe After Effects[^/]*/Contents/MacOS/After Effects'
  require_host_closed "aerender" \
    '[/]Adobe After Effects[^/]*/aerender( |$)'
}

validate_product_identity() {
  bundle=$1
  if [ -L "$bundle" ] || [ ! -d "$bundle" ] ||
     /usr/bin/find "$bundle" -type l -print | /usr/bin/grep -q .; then
    echo "Instalação não é um bundle regular do Rosto Natural." >&2
    return 1
  fi
  plist="$bundle/Contents/Info.plist"
  if [ -L "$plist" ] || [ ! -f "$plist" ]; then return 1; fi
  identifier=$(/usr/libexec/PlistBuddy -c 'Print :CFBundleIdentifier' \
    "$plist" 2>/dev/null || true)
  executable=$(/usr/libexec/PlistBuddy -c 'Print :CFBundleExecutable' \
    "$plist" 2>/dev/null || true)
  [ "$identifier" = "com.lucas.RostoNatural" ] &&
    [ "$executable" = "Rosto Natural" ]
}

check_hosts_closed
if [ -L "$DIST_DIR" ] ||
   { [ -e "$DIST_DIR" ] && [ ! -d "$DIST_DIR" ]; }; then
  echo "Diretório dist inseguro; desinstalação recusada." >&2
  exit 2
fi
mkdir -p "$DIST_DIR"
. "$PROJECT_DIR/scripts/publish_lock.sh"

TARGET_ROOT="$HOME/Library/Application Support/Adobe/Common/Plug-ins/7.0/MediaCore"
TARGET="$TARGET_ROOT/Rosto Natural.plugin"
BACKUP_RUN_DIR=""
BACKUP=""
PUBLICATION_PENDING=false
OLD_MOVED=false
INSTALL_LOCK="$TARGET_ROOT/.rosto-natural-install.lock"
INSTALL_LOCK_ACQUIRED=false

rollback_uninstall() {
  [ "$PUBLICATION_PENDING" = true ] || return 0
  if [ "$OLD_MOVED" != true ]; then
    PUBLICATION_PENDING=false
    return 0
  fi
  if [ -e "$TARGET" ] || [ -L "$TARGET" ]; then
    if ! /bin/mv "$TARGET" "$BACKUP_RUN_DIR/target.failed"; then return 1; fi
  fi
  if [ ! -d "$BACKUP" ] || [ -L "$BACKUP" ] ||
     ! /bin/mv "$BACKUP" "$TARGET"; then return 1; fi
  validate_product_identity "$TARGET" || return 1
  PUBLICATION_PENDING=false
  return 0
}

cleanup() {
  trap '' HUP INT TERM
  rollback_ok=true
  if ! rollback_uninstall; then
    rollback_ok=false
    echo "ATENÇÃO: rollback da desinstalação falhou." >&2
    rosto_publish_lock_preserve "rollback da desinstalação falhou" || true
  fi
  if [ "$INSTALL_LOCK_ACQUIRED" = true ] && [ "$rollback_ok" = true ]; then
    /bin/rm -f "$INSTALL_LOCK/pid"
    /bin/rmdir "$INSTALL_LOCK" 2>/dev/null ||
      echo "Lock de instalação não pôde ser removido: $INSTALL_LOCK" >&2
  elif [ "$INSTALL_LOCK_ACQUIRED" = true ]; then
    echo "Lock de instalação preservado para inspeção: $INSTALL_LOCK" >&2
  fi
  if [ "$rollback_ok" = true ]; then rosto_publish_lock_release || true; fi
}
trap cleanup EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM
rosto_publish_lock_acquire

TARGET_PARENTS="$HOME/Library
$HOME/Library/Application Support
$HOME/Library/Application Support/Adobe
$HOME/Library/Application Support/Adobe/Common
$HOME/Library/Application Support/Adobe/Common/Plug-ins
$HOME/Library/Application Support/Adobe/Common/Plug-ins/7.0
$TARGET_ROOT"
old_ifs=$IFS
IFS='
'
for directory in $TARGET_PARENTS; do
  if [ -L "$directory" ] ||
     { [ -e "$directory" ] && [ ! -d "$directory" ]; }; then
    echo "Diretório Adobe inseguro; desinstalação recusada: $directory" >&2
    exit 2
  fi
done
IFS=$old_ifs
if [ ! -d "$TARGET_ROOT" ] ||
   { [ ! -e "$TARGET" ] && [ ! -L "$TARGET" ]; }; then
  echo "Rosto Natural não está instalado em MediaCore."
  exit 0
fi
if ! /bin/mkdir "$INSTALL_LOCK" 2>/dev/null; then
  echo "Outra instalação/desinstalação está ativa ou deixou lock:" >&2
  echo "$INSTALL_LOCK" >&2
  exit 2
fi
INSTALL_LOCK_ACQUIRED=true
printf '%s\n' "$$" > "$INSTALL_LOCK/pid"

validate_product_identity "$TARGET" || {
  echo "Identidade instalada não confere; nada foi movido." >&2
  exit 2
}
BACKUP_ROOT="$DIST_DIR/uninstall-backups"
if [ -L "$BACKUP_ROOT" ] ||
   { [ -e "$BACKUP_ROOT" ] && [ ! -d "$BACKUP_ROOT" ]; }; then
  echo "Diretório de backup de desinstalação inseguro." >&2
  exit 2
fi
mkdir -p "$BACKUP_ROOT"
if [ -L "$BACKUP_ROOT" ] || [ ! -d "$BACKUP_ROOT" ]; then
  echo "Falha ao criar backup seguro da desinstalação." >&2
  exit 2
fi
BACKUP_RUN_DIR=$(/usr/bin/mktemp -d "$BACKUP_ROOT/run.XXXXXX")
BACKUP="$BACKUP_RUN_DIR/Rosto Natural.plugin"

check_hosts_closed
PUBLICATION_PENDING=true
trap '' HUP INT TERM
if ! /bin/mv "$TARGET" "$BACKUP"; then
  trap 'exit 129' HUP
  trap 'exit 130' INT
  trap 'exit 143' TERM
  echo "Falha ao mover a instalação para o backup." >&2
  exit 2
fi
OLD_MOVED=true
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM
if ! validate_product_identity "$BACKUP" ||
   [ -e "$TARGET" ] || [ -L "$TARGET" ]; then
  echo "Backup de desinstalação inválido; rollback será executado." >&2
  exit 2
fi
PUBLICATION_PENDING=false
echo "Efeito removido de MediaCore e preservado em: $BACKUP"
echo "Remova o painel pelo UXP Developer Tool ou Creative Cloud Desktop."
