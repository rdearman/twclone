#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
if [ -z "${PGDATABASE:-}" ]; then
  echo "Set PGDATABASE to an isolated twclone_rc2_* or twclone_qa_* database." >&2
  exit 77
fi

TMPDIR_TEST=$(mktemp -d "${TMPDIR:-/tmp}/twclone-podded-status.XXXXXX")
trap 'rm -rf "$TMPDIR_TEST"' EXIT HUP INT TERM

${CC:-cc} -std=c11 -D_GNU_SOURCE -DTW_DB_INTERNAL \
  -ffunction-sections -fdata-sections \
  -I"$ROOT" -I"$ROOT/src" -I"$ROOT/src/db" -I"$ROOT/src/db/repo" \
  $(pkg-config --cflags libpq jansson) \
  "$ROOT/tests/pg/test_repo_podded_status.c" \
  "$ROOT/src/db/db_api.c" "$ROOT/src/db/sql_driver.c" \
  "$ROOT/src/db/pg/db_pg.c" "$ROOT/src/db/repo/repo_cmd.c" \
  -Wl,--gc-sections -lpthread $(pkg-config --libs libpq jansson) \
  -o "$TMPDIR_TEST/test_repo_podded_status"

"$TMPDIR_TEST/test_repo_podded_status"
