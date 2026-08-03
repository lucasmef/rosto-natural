#!/bin/sh
set -eu

PROJECT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
if [ "${1:-}" != "--confirm" ]; then
  echo "Este script instala o bundle. Execute novamente com --confirm." >&2
  exit 2
fi
if /usr/bin/pgrep -x "Adobe Premiere Pro 2026" >/dev/null 2>&1; then
  echo "Feche o Premiere antes de instalar o efeito nativo." >&2
  exit 2
fi
if /usr/bin/pgrep -x "Adobe After Effects 2026" >/dev/null 2>&1; then
  echo "Feche o After Effects antes de instalar o efeito nativo." >&2
  exit 2
fi

PLUGIN="$PROJECT_DIR/dist/native/Rosto Natural.plugin"
TARGET="$HOME/Library/Application Support/Adobe/Common/Plug-ins/7.0/MediaCore"
if [ ! -d "$PLUGIN" ]; then
  echo "Bundle nativo não encontrado. Rode scripts/build_adobe.sh." >&2
  exit 2
fi
mkdir -p "$TARGET"
INSTALLED="$TARGET/Rosto Natural.plugin"
if [ -d "$INSTALLED" ]; then
  BACKUP_DIR="$PROJECT_DIR/dist/backups"
  BACKUP="$BACKUP_DIR/Rosto Natural.plugin"
  if [ -e "$BACKUP" ]; then
    echo "Backup anterior já existe em: $BACKUP" >&2
    echo "Mova-o antes de substituir novamente o plugin instalado." >&2
    exit 2
  fi
  mkdir -p "$BACKUP_DIR"
  /usr/bin/ditto "$INSTALLED" "$BACKUP"
fi
/usr/bin/ditto "$PLUGIN" "$INSTALLED"
/usr/bin/codesign --verify --deep --strict "$INSTALLED"
echo "Efeito instalado. O painel UXP deve ser carregado pelo UDT 2.2+."
