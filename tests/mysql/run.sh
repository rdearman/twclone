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

set +e
"$TMPDIR_TEST/test_db_mysql"
status=$?
set -e
exit "$status"
