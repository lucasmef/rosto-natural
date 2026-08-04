#!/bin/sh

# Shared, cooperative publication lock for every script that can mutate dist/.
# This file is sourced by the publisher scripts; it is not meant to be run.

rosto_publish_lock_acquire() {
  ROSTO_PUBLISH_LOCK_DIR="$DIST_DIR/.publish.lock"
  ROSTO_PUBLISH_LOCK_OWNED=false

  if [ -n "${ROSTO_PUBLISH_LOCK_TOKEN:-}" ]; then
    if [ -L "$ROSTO_PUBLISH_LOCK_DIR" ] ||
       [ ! -d "$ROSTO_PUBLISH_LOCK_DIR" ] ||
       [ -L "$ROSTO_PUBLISH_LOCK_DIR/token" ] ||
       [ ! -f "$ROSTO_PUBLISH_LOCK_DIR/token" ]; then
      echo "Lock global herdado é inválido: $ROSTO_PUBLISH_LOCK_DIR" >&2
      return 2
    fi
    ROSTO_PUBLISH_LOCK_ON_DISK=$(/bin/cat \
      "$ROSTO_PUBLISH_LOCK_DIR/token")
    if [ "$ROSTO_PUBLISH_LOCK_ON_DISK" != "$ROSTO_PUBLISH_LOCK_TOKEN" ]; then
      echo "Token do lock global herdado não confere." >&2
      return 2
    fi
    return 0
  fi

  if ! /bin/mkdir "$ROSTO_PUBLISH_LOCK_DIR" 2>/dev/null; then
    echo "Outra publicação está ativa ou deixou lock global:" >&2
    echo "$ROSTO_PUBLISH_LOCK_DIR" >&2
    return 2
  fi
  ROSTO_PUBLISH_LOCK_OWNED=true
  ROSTO_PUBLISH_LOCK_TOKEN=$(/usr/bin/uuidgen)
  export ROSTO_PUBLISH_LOCK_TOKEN
  (umask 077
   printf '%s\n' "$ROSTO_PUBLISH_LOCK_TOKEN" \
     > "$ROSTO_PUBLISH_LOCK_DIR/token"
   printf '%s\n' "$$" > "$ROSTO_PUBLISH_LOCK_DIR/pid") || {
    /bin/rm -f "$ROSTO_PUBLISH_LOCK_DIR/token" \
      "$ROSTO_PUBLISH_LOCK_DIR/pid"
    /bin/rmdir "$ROSTO_PUBLISH_LOCK_DIR" 2>/dev/null || true
    ROSTO_PUBLISH_LOCK_OWNED=false
    return 2
  }
}

rosto_publish_lock_release() {
  [ "${ROSTO_PUBLISH_LOCK_OWNED:-false}" = true ] || return 0
  if [ -L "$ROSTO_PUBLISH_LOCK_DIR" ] ||
     [ ! -d "$ROSTO_PUBLISH_LOCK_DIR" ] ||
     [ -L "$ROSTO_PUBLISH_LOCK_DIR/token" ] ||
     [ ! -f "$ROSTO_PUBLISH_LOCK_DIR/token" ] ||
     [ "$(/bin/cat "$ROSTO_PUBLISH_LOCK_DIR/token")" != \
       "$ROSTO_PUBLISH_LOCK_TOKEN" ]; then
    echo "Lock global mudou durante a publicação; preservado para inspeção." >&2
    return 1
  fi
  if [ -e "$ROSTO_PUBLISH_LOCK_DIR/preserve" ] ||
     [ -L "$ROSTO_PUBLISH_LOCK_DIR/preserve" ]; then
    echo "Lock global preservado após falha de rollback:" >&2
    echo "$ROSTO_PUBLISH_LOCK_DIR" >&2
    return 1
  fi
  /bin/rm -f "$ROSTO_PUBLISH_LOCK_DIR/token" \
    "$ROSTO_PUBLISH_LOCK_DIR/pid"
  if ! /bin/rmdir "$ROSTO_PUBLISH_LOCK_DIR" 2>/dev/null; then
    echo "Lock global não pôde ser removido: $ROSTO_PUBLISH_LOCK_DIR" >&2
    return 1
  fi
  ROSTO_PUBLISH_LOCK_OWNED=false
  return 0
}

rosto_publish_lock_preserve() {
  if [ -L "$ROSTO_PUBLISH_LOCK_DIR" ] ||
     [ ! -d "$ROSTO_PUBLISH_LOCK_DIR" ] ||
     [ -L "$ROSTO_PUBLISH_LOCK_DIR/token" ] ||
     [ ! -f "$ROSTO_PUBLISH_LOCK_DIR/token" ] ||
     [ "$(/bin/cat "$ROSTO_PUBLISH_LOCK_DIR/token")" != \
       "$ROSTO_PUBLISH_LOCK_TOKEN" ]; then
    echo "Não foi possível marcar o lock global para preservação." >&2
    return 1
  fi
  if [ -L "$ROSTO_PUBLISH_LOCK_DIR/preserve" ] ||
     { [ -e "$ROSTO_PUBLISH_LOCK_DIR/preserve" ] &&
       [ ! -f "$ROSTO_PUBLISH_LOCK_DIR/preserve" ]; }; then
    echo "Marcador de preservação do lock global é inseguro." >&2
    return 1
  fi
  (umask 077
   printf '%s\n' "${1:-rollback failed}" \
     > "$ROSTO_PUBLISH_LOCK_DIR/preserve")
}
