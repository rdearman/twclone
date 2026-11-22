#!/bin/sh
set -eu
umask 077

ENV_FILE=/etc/twclone-beta/.env.beta
COMPOSE_FILE=/opt/twclone-beta/current/compose.beta.yaml
BACKUP_DIR=/var/backups/twclone-beta

install -d -o root -g root -m 0700 "$BACKUP_DIR"
DB_ID=$(/usr/bin/docker compose -p twclone-beta -f "$COMPOSE_FILE" \
  --env-file "$ENV_FILE" ps -q db)
if [ -z "$DB_ID" ]; then
  echo "TWClone beta database container is not running" >&2
  exit 1
fi

STAMP=$(date -u +%Y%m%dT%H%M%SZ)
TMP="$BACKUP_DIR/.twclone-beta-$STAMP.dump.tmp"
FINAL="$BACKUP_DIR/twclone-beta-$STAMP.dump"
trap 'rm -f "$TMP" "$TMP.sha256"' EXIT HUP INT TERM

/usr/bin/docker exec --user root "$DB_ID" sh -ceu \
  'PGPASSWORD="$(cat /run/secrets/postgres_admin_password)" pg_dump -Fc -U twclone_admin -d twclone' \
  > "$TMP"
test -s "$TMP"
/usr/bin/docker exec -i "$DB_ID" pg_restore --list < "$TMP" >/dev/null
chmod 0600 "$TMP"
mv "$TMP" "$FINAL"
(cd "$BACKUP_DIR" && sha256sum "${FINAL##*/}" > "${FINAL##*/}.sha256")
find "$BACKUP_DIR" -maxdepth 1 -type f \
  \( -name 'twclone-beta-*.dump' -o -name 'twclone-beta-*.dump.sha256' \) \
  -mtime +14 -delete
trap - EXIT HUP INT TERM
printf 'Verified PostgreSQL backup: %s\n' "$FINAL"
