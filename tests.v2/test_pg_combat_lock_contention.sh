#!/usr/bin/env bash
set -euo pipefail

PSQL=(psql -X -qAt -v ON_ERROR_STOP=1)
ship_id=$("${PSQL[@]}" -c "SELECT s.ship_id FROM ships s JOIN shiptypes st ON st.shiptypes_id = s.type_id JOIN ship_ownership op ON op.ship_id = s.ship_id AND op.is_primary = TRUE LEFT JOIN corp_members cm ON cm.player_id = op.player_id ORDER BY s.ship_id LIMIT 1")
if [[ ! "$ship_id" =~ ^[0-9]+$ ]]; then
  echo "No primary-owned ship available for combat lock test" >&2
  exit 1
fi

tmpdir=$(mktemp -d "${TMPDIR:-/tmp}/twclone-pg-combat-lock.XXXXXX")
trap 'rm -rf "$tmpdir"' EXIT

"${PSQL[@]}" >"$tmpdir/holder.out" 2>"$tmpdir/holder.err" <<SQL &
BEGIN;
SELECT s.ship_id
  FROM ships s
 JOIN shiptypes st ON st.shiptypes_id = s.type_id
 JOIN ship_ownership op ON op.ship_id = s.ship_id AND op.is_primary = TRUE
 LEFT JOIN corp_members cm ON cm.player_id = op.player_id
 WHERE s.ship_id = $ship_id
 FOR UPDATE OF s, st, op;
\\! touch "$tmpdir/locked"
SELECT pg_sleep(2);
ROLLBACK;
SQL
holder_pid=$!

for _ in $(seq 1 50); do
  [[ -e "$tmpdir/locked" ]] && break
  sleep 0.1
done
if [[ ! -e "$tmpdir/locked" ]]; then
  cat "$tmpdir/holder.err" >&2
  wait "$holder_pid" || true
  echo "Lock holder did not acquire the combat rows" >&2
  exit 1
fi

skipped=$("${PSQL[@]}" -c "SELECT s.ship_id FROM ships s JOIN shiptypes st ON st.shiptypes_id = s.type_id JOIN ship_ownership op ON op.ship_id = s.ship_id AND op.is_primary = TRUE LEFT JOIN corp_members cm ON cm.player_id = op.player_id WHERE s.ship_id = $ship_id FOR UPDATE OF s, st, op SKIP LOCKED")
if [[ -n "$skipped" ]]; then
  wait "$holder_pid" || true
  echo "SKIP LOCKED returned a ship held by a concurrent combat transaction: $skipped" >&2
  exit 1
fi

wait "$holder_pid"
released=$("${PSQL[@]}" -c "SELECT s.ship_id FROM ships s JOIN shiptypes st ON st.shiptypes_id = s.type_id JOIN ship_ownership op ON op.ship_id = s.ship_id AND op.is_primary = TRUE LEFT JOIN corp_members cm ON cm.player_id = op.player_id WHERE s.ship_id = $ship_id FOR UPDATE OF s, st, op SKIP LOCKED")
if [[ "$released" != "$ship_id" ]]; then
  echo "Combat row remained locked after rollback; expected $ship_id, got '$released'" >&2
  exit 1
fi

echo "Combat lock contention passed: concurrent row skipped and rollback released the lock."
