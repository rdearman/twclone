#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
case "${PGDATABASE:-}" in
  twclone_rc2_*|twclone_qa_*) ;;
  *) echo "Set PGDATABASE to an isolated twclone_rc2_* or twclone_qa_* database." >&2; exit 77 ;;
esac

TMPDIR_TEST=$(mktemp -d "${TMPDIR:-/tmp}/twclone-db-pg-conformance.XXXXXX")
trap 'rm -rf "$TMPDIR_TEST"' EXIT HUP INT TERM

${CC:-cc} -std=c11 -D_GNU_SOURCE -DTW_DB_INTERNAL \
  -I"$ROOT" -I"$ROOT/src" -I"$ROOT/src/db" \
  $(pkg-config --cflags libpq) \
  "$ROOT/tests/pg/test_db_pg_conformance.c" \
  "$ROOT/src/db/db_api.c" "$ROOT/src/db/sql_driver.c" \
  "$ROOT/src/db/pg/db_pg.c" \
  -lpthread $(pkg-config --libs libpq) \
  -o "$TMPDIR_TEST/test_db_pg_conformance"

"$TMPDIR_TEST/test_db_pg_conformance"
