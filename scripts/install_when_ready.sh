#!/bin/sh
set -eu

PROJECT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
DIST_DIR="$PROJECT_DIR/dist"
CONFIRMED=false
REPLACE_CONFIRMED=false
for argument in "$@"; do
  case "$argument" in
    --confirm) CONFIRMED=true ;;
    --replace) REPLACE_CONFIRMED=true ;;
    *)
      echo "Uso: $0 --confirm [--replace]" >&2
      exit 2
      ;;
  esac
done
if [ "$CONFIRMED" != true ]; then
  echo "Este script instala o bundle. Execute novamente com --confirm." >&2
  echo "Se já existir uma instalação, também será exigido --replace." >&2
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
    echo "Feche o $host antes de instalar o efeito nativo." >&2
    exit 2
  fi
  if [ "$result" -ne 1 ]; then
    echo "Não foi possível confirmar que o $host está fechado." >&2
    echo "Instalação recusada por segurança (pgrep retornou $result)." >&2
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
    echo "Bundle existente não é uma instalação regular do produto:" >&2
    echo "$bundle" >&2
    return 1
  fi
  plist="$bundle/Contents/Info.plist"
  if [ -L "$plist" ] || [ ! -f "$plist" ]; then return 1; fi
  identifier=$(/usr/libexec/PlistBuddy -c 'Print :CFBundleIdentifier' \
    "$plist" 2>/dev/null || true)
  executable=$(/usr/libexec/PlistBuddy -c 'Print :CFBundleExecutable' \
    "$plist" 2>/dev/null || true)
  if [ "$identifier" != "com.lucas.RostoNatural" ] ||
     [ "$executable" != "Rosto Natural" ]; then
    echo "Bundle existente pertence a outro produto; operação recusada." >&2
    return 1
  fi
}

check_hosts_closed
if [ -L "$DIST_DIR" ] || [ ! -d "$DIST_DIR" ]; then
  echo "Diretório dist ausente ou inseguro." >&2
  exit 2
fi
. "$PROJECT_DIR/scripts/publish_lock.sh"

PLUGIN="$DIST_DIR/native/Rosto Natural.plugin"
VERSION=$(/usr/bin/python3 -c \
  'import json,sys; print(json.load(open(sys.argv[1]))["version"])' \
  "$PROJECT_DIR/uxp/manifest.json")
TARGET_ROOT="$HOME/Library/Application Support/Adobe/Common/Plug-ins/7.0/MediaCore"
INSTALLED="$TARGET_ROOT/Rosto Natural.plugin"
STAGE_DIR=""
STAGED_PLUGIN=""
BACKUP_RUN_DIR=""
BACKUP=""
HAD_INSTALLED=false
PUBLICATION_PENDING=false
OLD_MOVED=false
NEW_PUBLISHED=false
INSTALL_LOCK="$TARGET_ROOT/.rosto-natural-install.lock"
INSTALL_LOCK_ACQUIRED=false

rollback_installation() {
  [ "$PUBLICATION_PENDING" = true ] || return 0
  rollback_failed=false
  if [ "$NEW_PUBLISHED" = true ] &&
     { [ -e "$INSTALLED" ] || [ -L "$INSTALLED" ]; }; then
    if ! /bin/mv "$INSTALLED" "$BACKUP_RUN_DIR/installed.failed"; then
      echo "ATENÇÃO: não foi possível quarentenar a instalação nova." >&2
      rollback_failed=true
    fi
  fi
  if [ "$rollback_failed" = true ]; then return 1; fi
  if [ "$OLD_MOVED" = true ]; then
    if [ -e "$INSTALLED" ] || [ -L "$INSTALLED" ] ||
       [ ! -d "$BACKUP" ] || [ -L "$BACKUP" ] ||
       ! /bin/mv "$BACKUP" "$INSTALLED"; then
      echo "ATENÇÃO: restauração da instalação anterior falhou." >&2
      rollback_failed=true
    elif ! validate_product_identity "$INSTALLED"; then
      echo "ATENÇÃO: instalação restaurada não preservou a identidade." >&2
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
  if ! rollback_installation; then
    rollback_ok=false
    rosto_publish_lock_preserve "rollback da instalação falhou" || true
  fi
  case "$STAGE_DIR" in
    "$TARGET_ROOT"/.rosto-install-stage.*)
      [ ! -e "$STAGE_DIR" ] || /bin/rm -rf -- "$STAGE_DIR"
      ;;
  esac
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

if [ -L "$PLUGIN" ] || [ ! -d "$PLUGIN" ]; then
  echo "Bundle nativo não encontrado ou inseguro. Rode scripts/build_adobe.sh." >&2
  exit 2
fi
"$PROJECT_DIR/scripts/validate_native_payload.sh" "$PLUGIN" "$VERSION"

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
    echo "Diretório Adobe inseguro; instalação recusada: $directory" >&2
    exit 2
  fi
done
IFS=$old_ifs
mkdir -p "$TARGET_ROOT"
old_ifs=$IFS
IFS='
'
for directory in $TARGET_PARENTS; do
  if [ -L "$directory" ] || [ ! -d "$directory" ]; then
    echo "Não foi possível preparar MediaCore com segurança: $directory" >&2
    exit 2
  fi
done
IFS=$old_ifs

if ! /bin/mkdir "$INSTALL_LOCK" 2>/dev/null; then
  echo "Outra instalação/desinstalação está ativa ou deixou lock:" >&2
  echo "$INSTALL_LOCK" >&2
  exit 2
fi
INSTALL_LOCK_ACQUIRED=true
printf '%s\n' "$$" > "$INSTALL_LOCK/pid"

if [ -L "$INSTALLED" ] ||
   { [ -e "$INSTALLED" ] && [ ! -d "$INSTALLED" ]; }; then
  echo "Destino instalado não é um bundle regular: $INSTALLED" >&2
  exit 2
fi
if [ -d "$INSTALLED" ]; then
  validate_product_identity "$INSTALLED"
  if [ "$REPLACE_CONFIRMED" != true ]; then
    echo "Já existe Rosto Natural instalado em: $INSTALLED" >&2
    echo "Após confirmar a substituição recuperável, use --confirm --replace." >&2
    exit 2
  fi
  HAD_INSTALLED=true
fi

STAGE_DIR=$(/usr/bin/mktemp -d "$TARGET_ROOT/.rosto-install-stage.XXXXXX")
STAGED_PLUGIN="$STAGE_DIR/Rosto Natural.plugin"
/usr/bin/ditto --norsrc --noextattr --noqtn --noacl \
  "$PLUGIN" "$STAGED_PLUGIN"
"$PROJECT_DIR/scripts/validate_native_bundle.sh" "$STAGED_PLUGIN" "$VERSION"

BACKUPS_DIR="$DIST_DIR/backups"
BACKUP_ROOT="$BACKUPS_DIR/install"
for directory in "$BACKUPS_DIR" "$BACKUP_ROOT"; do
  if [ -L "$directory" ] ||
     { [ -e "$directory" ] && [ ! -d "$directory" ]; }; then
    echo "Diretório de backup da instalação inseguro: $directory" >&2
    exit 2
  fi
  if [ ! -d "$directory" ]; then /bin/mkdir "$directory"; fi
  if [ -L "$directory" ] || [ ! -d "$directory" ]; then
    echo "Falha ao criar backup seguro da instalação: $directory" >&2
    exit 2
  fi
done
BACKUP_RUN_DIR=$(/usr/bin/mktemp -d "$BACKUP_ROOT/run.XXXXXX")
BACKUP="$BACKUP_RUN_DIR/Rosto Natural.plugin.previous"

# Minimize the unavoidable race between process inspection and the swap.
check_hosts_closed
PUBLICATION_PENDING=true
trap '' HUP INT TERM
if [ "$HAD_INSTALLED" = true ]; then
  if ! /bin/mv "$INSTALLED" "$BACKUP"; then
    trap 'exit 129' HUP
    trap 'exit 130' INT
    trap 'exit 143' TERM
    echo "Falha ao preservar a instalação anterior." >&2
    exit 2
  fi
  OLD_MOVED=true
  if ! validate_product_identity "$BACKUP" ||
     [ -e "$INSTALLED" ] || [ -L "$INSTALLED" ]; then
    trap 'exit 129' HUP
    trap 'exit 130' INT
    trap 'exit 143' TERM
    echo "Backup anterior falhou na validação; rollback será executado." >&2
    exit 2
  fi
fi
if ! /bin/mv "$STAGED_PLUGIN" "$INSTALLED"; then
  trap 'exit 129' HUP
  trap 'exit 130' INT
  trap 'exit 143' TERM
  echo "Falha ao ativar o bundle novo; rollback será executado." >&2
  exit 2
fi
NEW_PUBLISHED=true
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM
if ! "$PROJECT_DIR/scripts/validate_native_bundle.sh" \
     "$INSTALLED" "$VERSION"; then
  echo "Instalação final inválida; rollback será executado." >&2
  exit 2
fi
PUBLICATION_PENDING=false
if [ "$HAD_INSTALLED" = false ]; then
  /bin/rmdir "$BACKUP_RUN_DIR" 2>/dev/null || true
  /bin/rmdir "$BACKUP_ROOT" 2>/dev/null || true
fi

echo "Efeito instalado e validado em: $INSTALLED"
if [ "$HAD_INSTALLED" = true ]; then echo "Backup anterior: $BACKUP"; fi
echo "O painel UXP deve ser carregado pelo UXP Developer Tool 2.2+."
