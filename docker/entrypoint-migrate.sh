#!/bin/sh
set -eu
umask 077
cp /run/secrets/admin_pgpass /tmp/admin.pgpass
chmod 0600 /tmp/admin.pgpass
cd /app/bin
./bigbang --include-v3-migrations
# These built-in NPC identities have SysOp type for internal game actions.
# Replace their known development seed passwords with an unguessable one-time
# Argon2id value so no published/test password can authenticate as SysOp.
locked_hash=$(python3 /app/bin/lock_seed_sysop_password.py)
{
  printf '\\set locked_hash %s\n' "$locked_hash"
  printf "UPDATE players SET passwd = :'locked_hash' WHERE name IN ('System', 'Federation Administrator');\\n"
  printf '%s\n' "DO \$\$ BEGIN IF (SELECT COUNT(*) FROM players WHERE name IN ('System', 'Federation Administrator')) <> 2 THEN RAISE EXCEPTION 'expected two internal SysOp actors'; END IF; END \$\$;"
} | psql "$TWCLONE_DB_MIGRATION_CONNINFO" -X -w -v ON_ERROR_STOP=1
unset locked_hash
psql "$TWCLONE_DB_MIGRATION_CONNINFO" -X -w -v ON_ERROR_STOP=1 \
  -f /app/grant-runtime-privileges.sql
