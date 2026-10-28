#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
if [ -z "${PGDATABASE:-}" ]; then
  echo "Set PGDATABASE to an isolated twclone_rc2_* or twclone_qa_* database." >&2
  exit 77
fi
case "$PGDATABASE" in
  twclone_rc2_*|twclone_qa_*) ;;
  *) echo "Refusing non-isolated PGDATABASE: $PGDATABASE" >&2; exit 77 ;;
esac

TMPDIR_TEST=$(mktemp -d "${TMPDIR:-/tmp}/twclone-captainz-atomic.XXXXXX")
trap 'rm -rf "$TMPDIR_TEST"' EXIT HUP INT TERM
CFLAGS="-std=c11 -D_GNU_SOURCE -DTW_DB_INTERNAL -ffunction-sections -fdata-sections -I$ROOT -I$ROOT/src -I$ROOT/src/db -I$ROOT/src/db/repo $(pkg-config --cflags libpq jansson)"

${CC:-cc} $CFLAGS -c "$ROOT/tests/pg/test_server_combat_fedspace_atomic.c" -o "$TMPDIR_TEST/test.o"
${CC:-cc} $CFLAGS -Ddb_destroy_ship=db_destroy_ship_repo_unused -c "$ROOT/src/db/repo/repo_cmd.c" -o "$TMPDIR_TEST/repo_cmd.o"
${CC:-cc} $CFLAGS -c "$ROOT/src/db/db_api.c" -o "$TMPDIR_TEST/db_api.o"
${CC:-cc} $CFLAGS -c "$ROOT/src/db/sql_driver.c" -o "$TMPDIR_TEST/sql_driver.o"
${CC:-cc} $CFLAGS -c "$ROOT/src/db/pg/db_pg.c" -o "$TMPDIR_TEST/db_pg.o"
${CC:-cc} $CFLAGS -c "$ROOT/src/server_combat.c" -o "$TMPDIR_TEST/server_combat.o"
${CC:-cc} -Wl,--gc-sections -o "$TMPDIR_TEST/test" \
  "$TMPDIR_TEST/test.o" "$TMPDIR_TEST/repo_cmd.o" \
  "$TMPDIR_TEST/db_api.o" "$TMPDIR_TEST/sql_driver.o" \
  "$TMPDIR_TEST/db_pg.o" "$TMPDIR_TEST/server_combat.o" \
  -lpthread $(pkg-config --libs libpq jansson) -lm

"$TMPDIR_TEST/test"
