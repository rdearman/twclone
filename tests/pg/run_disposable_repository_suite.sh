#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
QA_DB="twclone_qa_pg_${$}"
CREATED=0

cleanup()
{
  if [ "$CREATED" -eq 1 ]; then
    PGDATABASE=postgres dropdb -w "$QA_DB"
  fi
}
trap cleanup EXIT HUP INT TERM

if psql -X -w -d postgres -Atqc \
  "SELECT 1 FROM pg_database WHERE datname = '$QA_DB'" | grep -q 1; then
  echo "Refusing to reuse existing database $QA_DB" >&2
  exit 1
fi

createdb -w -T template0 "$QA_DB"
CREATED=1
export PGDATABASE=$QA_DB

for migration in "$ROOT"/sql/pg/[0-9][0-9][0-9]_*.sql; do
  echo "Apply ${migration#"$ROOT"/}"
  psql -X -w -v ON_ERROR_STOP=1 -f "$migration"
done

for migration in "$ROOT"/sql/pg/1[0-9][0-9]_*.sql; do
  echo "Replay ${migration#"$ROOT"/}"
  psql -X -w -v ON_ERROR_STOP=1 -f "$migration"
done

for suite in \
  run_db_pg_conformance.sh \
  run_repo_planetary_accounting.sh \
  run_repo_planet_fighter_transfer.sh \
  run_repo_podded_status.sh \
  run_server_combat_fedspace_atomic.sh \
  run_repo_stardock_atomic.sh; do
  echo "Run $suite"
  sh "$ROOT/tests/pg/$suite"
done

echo "PostgreSQL disposable repository suite passed ($QA_DB)"
