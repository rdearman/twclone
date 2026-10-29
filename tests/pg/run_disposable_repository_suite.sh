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
  case "$migration" in
    */119_password_hash_enforcement.sql)
      # This legacy event-journal fixture creates a transaction-local player
      # with a non-login password. Run it before hash-only enforcement.
      echo "Check tests.v2/test_pg_migration_116_player_event_journal.sql"
      psql -X -w -v ON_ERROR_STOP=1 \
        -f "$ROOT/tests.v2/test_pg_migration_116_player_event_journal.sql"
      ;;
  esac
  echo "Apply ${migration#"$ROOT"/}"
  psql -X -w -v ON_ERROR_STOP=1 -f "$migration"
done

for migration in "$ROOT"/sql/pg/1[0-9][0-9]_*.sql; do
  echo "Replay ${migration#"$ROOT"/}"
  psql -X -w -v ON_ERROR_STOP=1 -f "$migration"
done

for migration_test in \
  "$ROOT"/tests.v2/test_migration_100_ship_cargo.sql \
  "$ROOT"/tests.v2/test_migration_102_porttypes.sql \
  "$ROOT"/tests.v2/test_migration_107_illegal_stock.sql \
  "$ROOT"/tests.v2/test_migration_108_planet_entity_stock.sql \
  "$ROOT"/tests.v2/test_pg_migration_*.sql; do
  case "$migration_test" in
    */test_pg_migration_116_player_event_journal.sql) continue ;;
  esac
  echo "Check ${migration_test#"$ROOT"/}"
  psql -X -w -v ON_ERROR_STOP=1 -f "$migration_test"
done

for suite in \
  run_db_pg_conformance.sh \
  run_repo_planetary_accounting.sh \
  run_repo_planet_fighter_transfer.sh \
  run_repo_podded_status.sh \
  run_repo_information_boundary.sh \
  run_server_combat_fedspace_atomic.sh \
  run_repo_stardock_atomic.sh; do
  echo "Run $suite"
  sh "$ROOT/tests/pg/$suite"
done

echo "PostgreSQL disposable repository suite passed ($QA_DB)"
