#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
TMPDIR_TEST=$(mktemp -d "${TMPDIR:-/tmp}/twclone-engine-regressions.XXXXXX")
trap 'rm -rf "$TMPDIR_TEST"' EXIT HUP INT TERM

${CC:-cc} -std=c11 -Wall -Wextra -Werror \
  -I"$ROOT/src" \
  "$ROOT/tests.v2/test_ship_personality_rules.c" \
  -o "$TMPDIR_TEST/ship-personality"
"$TMPDIR_TEST/ship-personality"

${CC:-cc} -std=c11 -Wall -Wextra -Werror \
  -I"$ROOT/src" \
  "$ROOT/tests.v2/test_planet_fighter_production.c" \
  -o "$TMPDIR_TEST/planet-fighter-production"
"$TMPDIR_TEST/planet-fighter-production"

echo "Engine gameplay regression unit tests passed."
