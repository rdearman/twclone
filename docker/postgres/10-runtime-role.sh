#!/bin/sh
set -eu
runtime_password=$(cat /run/secrets/app_db_password)
export TWCLONE_RUNTIME_PASSWORD="$runtime_password"
unset runtime_password
psql -v ON_ERROR_STOP=1 --username "$POSTGRES_USER" --dbname "$POSTGRES_DB" <<'SQL'
\getenv runtime_password TWCLONE_RUNTIME_PASSWORD
CREATE ROLE twclone_runtime LOGIN PASSWORD :'runtime_password';
SQL
unset TWCLONE_RUNTIME_PASSWORD
