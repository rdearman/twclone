#!/bin/sh
set -eu
umask 077
cp /run/secrets/admin_pgpass /tmp/admin.pgpass
chmod 0600 /tmp/admin.pgpass
cd /app/bin
./bigbang --include-v3-migrations
psql "$TWCLONE_DB_MIGRATION_CONNINFO" -X -w -v ON_ERROR_STOP=1 \
  -f /app/grant-runtime-privileges.sql
