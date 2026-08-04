#!/bin/sh
set -eu

PROJECT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
PLUGIN=${1:-}
EXPECTED_VERSION=${2:-}

if [ -z "$PLUGIN" ] || [ -L "$PLUGIN" ] || [ ! -d "$PLUGIN" ]; then
  echo "Uso: $0 '/caminho/Rosto Natural.plugin' [versão]" >&2
  exit 2
fi
if /usr/bin/find "$PLUGIN" ! -type d ! -type f -print |
   /usr/bin/grep -q .; then
  echo "Payload nativo recusado: somente diretórios e arquivos regulares são permitidos." >&2
  exit 2
fi

VERIFY_ROOT=$(/usr/bin/mktemp -d /private/tmp/rosto-native-payload-verify.XXXXXX)
VERIFY_PLUGIN="$VERIFY_ROOT/Rosto Natural.plugin"
SOURCE_FILES="$VERIFY_ROOT/source-files"
COPIED_FILES="$VERIFY_ROOT/copied-files"
cleanup() {
  case "$VERIFY_ROOT" in
    /private/tmp/rosto-native-payload-verify.*)
      [ ! -e "$VERIFY_ROOT" ] || /bin/rm -rf -- "$VERIFY_ROOT"
      ;;
  esac
}
trap cleanup EXIT HUP INT TERM

# Documents may be backed by Apple's File Provider, which adds FinderInfo to
# package directories after they are signed. Those attributes are not payload.
# Recreate exactly the data-fork contents in a non-File-Provider location.
/usr/bin/ditto --norsrc --noextattr --noqtn --noacl \
  "$PLUGIN" "$VERIFY_PLUGIN"

/usr/bin/find "$PLUGIN" -type f -print |
  /usr/bin/sed "s#^$PLUGIN/##" | LC_ALL=C /usr/bin/sort > "$SOURCE_FILES"
/usr/bin/find "$VERIFY_PLUGIN" -type f -print |
  /usr/bin/sed "s#^$VERIFY_PLUGIN/##" | LC_ALL=C /usr/bin/sort > "$COPIED_FILES"
if ! /usr/bin/cmp -s "$SOURCE_FILES" "$COPIED_FILES"; then
  echo "Payload nativo recusado: inventário mudou durante a cópia limpa." >&2
  exit 2
fi
while IFS= read -r relative; do
  if ! /usr/bin/cmp -s "$PLUGIN/$relative" "$VERIFY_PLUGIN/$relative"; then
    echo "Payload nativo recusado: bytes divergentes em $relative" >&2
    exit 2
  fi
done < "$SOURCE_FILES"

"$PROJECT_DIR/scripts/validate_native_bundle.sh" \
  "$VERIFY_PLUGIN" "$EXPECTED_VERSION"
printf 'Payload nativo portátil validado: %s\n' "$PLUGIN"
