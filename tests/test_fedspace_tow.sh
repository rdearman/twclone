#!/usr/bin/env bash
# test_fedspace_tow.sh
# Insert 7 ships into sector 1 and 2 ships into sector 2 with 1000 fighters for fedspace towing tests.
# Usage:
#   DB=./twclone.db ./test_fedspace_tow.sh
#   ./test_fedspace_tow.sh --cleanup   # remove test data
set -euo pipefail

DB="${DB:-/home/rick/twclone/bin/twconfig.db}"
TMP_SQL=$(mktemp /tmp/twtest.XXXX.sql)
CLEANUP=false

for arg in "$@"; do
  case "$arg" in
    --cleanup) CLEANUP=true ;;
    *) echo "Unknown arg: $arg"; exit 1 ;;
  esac
done

if [ "$CLEANUP" = true ]; then
  cat > "$TMP_SQL" <<'SQL'
PRAGMA foreign_keys=OFF;
BEGIN;

-- Delete ships we created (marked by name if present or by owner test players)
DELETE FROM ships WHERE name LIKE 'test_cron_ship_%';

-- Remove the test players if present
DELETE FROM players WHERE name IN ('test_cron_p1','test_cron_p2');

COMMIT;
PRAGMA foreign_keys=ON;
SQL

  echo "Running cleanup on DB: $DB"
  sqlite3 "$DB" < "$TMP_SQL"
  rm -f "$TMP_SQL"
  echo "Cleanup finished."
  exit 0
fi

# Safety: ensure DB file exists
if [ ! -f "$DB" ]; then
  echo "ERROR: DB file not found: $DB"
  exit 1
fi

echo "Using DB: $DB"

# 1) Inspect ships table columns
cols=$(sqlite3 "$DB" "PRAGMA table_info('ships');" | awk -F'|' '{print $2}')
echo "ships columns: $cols"

# Helpers: detect existence of a column name
has_col() {
  local c="$1"
  echo "$cols" | tr ' ' '\n' | grep -qx "$c"
}

# 2) Create two test players if players table exists (and capture their IDs)
players_exist=$(sqlite3 "$DB" "SELECT count(name) FROM sqlite_master WHERE type='table' AND name='players';")
owner1_id=""
owner2_id=""

if [ "$players_exist" -gt 0 ]; then
  # Insert players (IF NOT EXISTS pattern)
  sqlite3 "$DB" <<'SQL'
BEGIN;
INSERT OR IGNORE INTO players(name) VALUES('test_cron_p1');
INSERT OR IGNORE INTO players(name) VALUES('test_cron_p2');
COMMIT;
SQL
  owner1_id=$(sqlite3 "$DB" "SELECT id FROM players WHERE name='test_cron_p1' LIMIT 1;")
  owner2_id=$(sqlite3 "$DB" "SELECT id FROM players WHERE name='test_cron_p2' LIMIT 1;")
  echo "Test players created/located: p1=$owner1_id p2=$owner2_id"
fi

# 3) Build an INSERT statement that only uses columns that exist
# We'll always try to set: sector, fighters, owner (owner_player_id or owner_id), name (for cleanup)
insert_cols=()
insert_vals=()

# sector?
if has_col "sector"; then
  insert_cols+=("sector")
  insert_vals+=("?sector")
else
  echo "Warning: ships.sector column not found - aborting."
  exit 1
fi

# fighters?
if has_col "fighters"; then
  insert_cols+=("fighters")
  insert_vals+=("?fighters")
else
  echo "Warning: ships.fighters column not found - aborting."
  exit 1
fi

# owner column - try owner_player_id then owner_id
owner_col=""
if has_col "owner_player_id"; then
  owner_col="owner_player_id"
elif has_col "owner_id"; then
  owner_col="owner_id"
fi
if [ -n "$owner_col" ]; then
  insert_cols+=("$owner_col")
  insert_vals+=("?owner")
fi

# name (useful to identify and cleanup)
if has_col "name"; then
  insert_cols+=("name")
  insert_vals+=("?name")
fi

# wanted/cloaked optional flags we won't set here

# Compose SQL
cols_csv=$(IFS=, ; echo "${insert_cols[*]}")
vals_csv=$(IFS=, ; echo "${insert_vals[*]}")

insert_sql="INSERT INTO ships ($cols_csv) VALUES ($vals_csv);"
echo "Using insert SQL: $insert_sql"

# 4) Function to insert N ships into a sector for a given owner
run_insert() {
  local sector="$1"; local count="$2"; local ownerid="$3"; local prefix="$4"
  local i=1
  while [ $i -le $count ]; do
    name="${prefix}_${sector}_${i}"
    # Build sqlite3 parameterised exec via a temporary SQL with bindings substituted
    # We substitute values directly but escape single quotes.
    esc_name=$(printf "%s" "$name" | sed "s/'/''/g")
    esc_owner="${ownerid:-NULL}"
    esc_fighters="1000"
    esc_sector="$sector"

    # Build VALUES list in proper order to match placeholders ?sector ?fighters ?owner ?name
    # We'll map positionally according to insert_cols array
    vals=()
    for col in ${insert_cols[@]}; do
      case "$col" in
        sector) vals+=("$esc_sector") ;;
        fighters) vals+=("$esc_fighters") ;;
        owner* ) vals+=("$esc_owner") ;;
        name) vals+=("'$esc_name'") ;;
        *) vals+=("NULL") ;;
      esac
    done

    # Build final SQL
    # Note: using direct TEXT concatenation to avoid sqlite3 parameter complexity in shell
    final_sql="INSERT INTO ships ($cols_csv) VALUES ("
    first=1
    for v in "${vals[@]}"; do
      if [ $first -eq 1 ]; then first=0; else final_sql+=", "; fi
      # if numeric (ownerid or sector or fighters), don't quote; ownerid may be NULL
      if [[ "$v" =~ ^[0-9]+$ ]]; then
        final_sql+="$v"
      else
        final_sql+="$v"
      fi
    done
    final_sql+=");"

    sqlite3 "$DB" "$final_sql"
    inserted_id=$(sqlite3 "$DB" "SELECT last_insert_rowid();")
    echo "Inserted ship id=$inserted_id sector=$sector name=$name"
    i=$((i+1))
  done
}

# 5) Insert ships:
# Choose owners: if players exist, alternate p1/p2; otherwise owner omitted (NULL)
if [ -n "$owner1_id" ] && [ -n "$owner2_id" ]; then
  # 7 ships in sector 1 owned by player1
  run_insert 1 7 "$owner1_id" "test_cron_ship"
  # 2 ships in sector 2 owned by player2, with 1000 fighters
  run_insert 2 2 "$owner2_id" "test_cron_ship"
else
  echo "No players table or players not found; inserting ships without owners."
  # Insert without owner (owner fields, if present, will receive NULL)
  run_insert 1 7 "" "test_cron_ship"
  run_insert 2 2 "" "test_cron_ship"
fi

echo "Done. Verify with:"
echo "  sqlite3 $DB \"SELECT id,sector,fighters,owner_player_id,name FROM ships WHERE name LIKE 'test_cron_ship_%';\""

# cleanup temp
rm -f "$TMP_SQL"
