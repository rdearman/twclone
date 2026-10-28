#!/bin/sh
set -eu
repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
binary=$(mktemp "${TMPDIR:-/tmp}/twclone-password-hash.XXXXXX")
trap 'rm -f "$binary"' EXIT HUP INT TERM
${CC:-cc} -D_GNU_SOURCE -I"$repo_root/src" \
  "$repo_root/tests.v2/test_password_hash.c" \
  "$repo_root/src/password_hash.c" -ldl -lpthread -o "$binary"
"$binary"
