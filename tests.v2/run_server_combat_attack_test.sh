#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
TEST_BIN=$(mktemp "${TMPDIR:-/tmp}/twclone-combat-attack.XXXXXX")
trap 'rm -f "$TEST_BIN"' EXIT HUP INT TERM

${CC:-cc} -std=c11 -D_GNU_SOURCE -Wall -Wextra -Werror \
  -ffunction-sections -fdata-sections \
  -I"$ROOT" -I"$ROOT/src" -I"$ROOT/src/db" -I"$ROOT/src/db/repo" \
  "$ROOT/tests.v2/test_server_combat_attack_failure.c" \
  "$ROOT/src/server_combat.c" \
  -Wl,--gc-sections -ljansson -lm -o "$TEST_BIN"

"$TEST_BIN"
