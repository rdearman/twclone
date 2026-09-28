#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
TEST_BIN=$(mktemp "${TMPDIR:-/tmp}/twclone-s2s-dispatch.XXXXXX")
trap 'rm -f "$TEST_BIN"' EXIT HUP INT TERM

${CC:-cc} -std=c11 -D_GNU_SOURCE -ffunction-sections -fdata-sections \
  -I"$ROOT" -I"$ROOT/src" -I"$ROOT/src/db" -I"$ROOT/src/db/repo" \
  "$ROOT/tests.v2/test_server_s2s_dispatch.c" \
  "$ROOT/src/server_s2s.c" "$ROOT/src/schemas.c" \
  -Wl,--gc-sections -ljansson -lpthread -o "$TEST_BIN"

"$TEST_BIN"
