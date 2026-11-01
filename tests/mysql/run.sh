#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
MYSQL_CONFIG=${MYSQL_CONFIG:-$(command -v mysql_config || command -v mariadb_config || true)}
if [ -z "$MYSQL_CONFIG" ]; then
  echo "SKIP: MySQL C client development package is missing; install libmariadb-dev or libmysqlclient-dev (mysql_config/mariadb_config not found)." >&2
  exit 77
fi

if [ -z "${MYSQL_TEST_HOST:-}" ] || [ -z "${MYSQL_TEST_USER:-}" ] || [ -z "${MYSQL_TEST_DATABASE:-}" ]; then
  echo "SKIP: set MYSQL_TEST_HOST, MYSQL_TEST_USER, and MYSQL_TEST_DATABASE for a disposable MySQL test database." >&2
  exit 77
fi

if ! "$MYSQL_CONFIG" --version >/dev/null 2>&1 ||
   ! "$MYSQL_CONFIG" --cflags >/dev/null 2>&1 ||
   ! "$MYSQL_CONFIG" --libs >/dev/null 2>&1; then
  echo "SKIP: MYSQL_CONFIG='$MYSQL_CONFIG' is not a working mysql_config/mariadb_config; install the client development package or set MYSQL_CONFIG explicitly." >&2
  exit 77
fi

TMPDIR_TEST=$(mktemp -d "${TMPDIR:-/tmp}/twclone-mysql-test.XXXXXX")
trap 'rm -rf "$TMPDIR_TEST"' EXIT HUP INT TERM

# mysql_config emits compiler/linker flags as shell words for the selected
# client library. shellcheck disable=SC2046
${CC:-cc} -std=c11 -D_GNU_SOURCE -DHAVE_MYSQL -DTW_DB_INTERNAL \
  -I"$ROOT/src" -I"$ROOT/src/db" $("$MYSQL_CONFIG" --cflags) \
  "$ROOT/tests/mysql/test_db_mysql.c" \
  "$ROOT/src/db/db_api.c" "$ROOT/src/db/sql_driver.c" "$ROOT/src/db/mysql/db_mysql.c" \
  -lpthread $("$MYSQL_CONFIG" --libs) -o "$TMPDIR_TEST/test_db_mysql"

# Compile the cron repository in isolation and discard unrelated repository
# entry points; this verifies the database-agnostic registration path.
${CC:-cc} -std=c11 -D_GNU_SOURCE -DHAVE_MYSQL -DTW_DB_INTERNAL \
  -ffunction-sections -fdata-sections \
  -I"$ROOT" -I"$ROOT/src" -I"$ROOT/src/db" -I"$ROOT/src/db/repo" $("$MYSQL_CONFIG" --cflags) \
  "$ROOT/tests/mysql/test_repo_engine_cron.c" \
  "$ROOT/src/db/db_api.c" "$ROOT/src/db/sql_driver.c" \
  "$ROOT/src/db/mysql/db_mysql.c" "$ROOT/src/db/repo/repo_engine.c" \
  "$ROOT/src/db/repo/repo_bank.c" \
  -Wl,--gc-sections -lpthread $("$MYSQL_CONFIG" --libs) \
  -o "$TMPDIR_TEST/test_repo_engine_cron"

# The event retention test uses migration 116 tables and a dedicated player
# fixture from a disposable database.
${CC:-cc} -std=c11 -D_GNU_SOURCE -DHAVE_MYSQL -DTW_DB_INTERNAL \
  -ffunction-sections -fdata-sections \
  -I"$ROOT/src" -I"$ROOT/src/db" -I"$ROOT/src/db/repo" $("$MYSQL_CONFIG" --cflags) \
  "$ROOT/tests/mysql/test_repo_event_prune.c" \
  "$ROOT/src/db/db_api.c" "$ROOT/src/db/sql_driver.c" \
  "$ROOT/src/db/mysql/db_mysql.c" "$ROOT/src/db/repo/repo_communication.c" \
  -Wl,--gc-sections -lpthread $("$MYSQL_CONFIG" --libs) \
  -o "$TMPDIR_TEST/test_repo_event_prune"

${CC:-cc} -std=c11 -D_GNU_SOURCE -DHAVE_MYSQL -DTW_DB_INTERNAL \
  -ffunction-sections -fdata-sections \
  -I"$ROOT" -I"$ROOT/src" -I"$ROOT/src/db" -I"$ROOT/src/db/repo" $("$MYSQL_CONFIG" --cflags) \
  "$ROOT/tests/mysql/test_repo_world_cron.c" \
  "$ROOT/src/db/db_api.c" "$ROOT/src/db/sql_driver.c" \
  "$ROOT/src/db/mysql/db_mysql.c" "$ROOT/src/db/repo/repo_cron.c" \
  -Wl,--gc-sections -lpthread $("$MYSQL_CONFIG" --libs) \
  -o "$TMPDIR_TEST/test_repo_world_cron"

set +e
"$TMPDIR_TEST/test_db_mysql"
status=$?
set -e
if [ "$status" -ne 0 ]; then exit "$status"; fi

"$TMPDIR_TEST/test_repo_engine_cron"

set +e
"$TMPDIR_TEST/test_repo_event_prune"
status=$?
set -e
if [ "$status" -ne 0 ] && [ "$status" -ne 77 ]; then exit "$status"; fi

"$TMPDIR_TEST/test_repo_world_cron"
