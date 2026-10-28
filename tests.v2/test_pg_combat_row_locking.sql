-- Run on a disposable PostgreSQL database with at least one primary-owned
-- ship. Demonstrate the old nullable-side lock failure, then assert that the
-- repository's targeted locking query can retrieve and lock the same combat
-- row.
\set ON_ERROR_STOP on

DO $test$
DECLARE
  v_ship_id bigint;
  v_locked_ship_id bigint;
  v_corporation_id integer;
  v_original_query_failed boolean := false;
BEGIN
  SELECT s.ship_id
    INTO v_ship_id
    FROM ships s
    JOIN shiptypes st ON st.shiptypes_id = s.type_id
    JOIN ship_ownership op ON op.ship_id = s.ship_id AND op.is_primary = TRUE
    LEFT JOIN corp_members cm ON cm.player_id = op.player_id
   ORDER BY s.ship_id
   LIMIT 1;

  IF v_ship_id IS NULL THEN
    RAISE EXCEPTION 'combat row-lock regression requires a primary-owned ship';
  END IF;

  BEGIN
    SELECT s.ship_id
      INTO v_locked_ship_id
      FROM ships s
      JOIN shiptypes st ON st.shiptypes_id = s.type_id
      JOIN ship_ownership op ON op.ship_id = s.ship_id AND op.is_primary = TRUE
      LEFT JOIN corp_members cm ON cm.player_id = op.player_id
     WHERE s.ship_id = v_ship_id
     FOR UPDATE SKIP LOCKED;
  EXCEPTION WHEN feature_not_supported THEN
    v_original_query_failed := true;
  END;

  IF NOT v_original_query_failed THEN
    RAISE EXCEPTION 'original FOR UPDATE SKIP LOCKED query did not reproduce SQLSTATE 0A000';
  END IF;

  SELECT s.ship_id, cm.corporation_id
    INTO v_locked_ship_id, v_corporation_id
    FROM ships s
    JOIN shiptypes st ON st.shiptypes_id = s.type_id
    JOIN ship_ownership op ON op.ship_id = s.ship_id AND op.is_primary = TRUE
    LEFT JOIN corp_members cm ON cm.player_id = op.player_id
    WHERE s.ship_id = v_ship_id
   FOR UPDATE OF s, st, op SKIP LOCKED;

  IF v_locked_ship_id IS DISTINCT FROM v_ship_id THEN
    RAISE EXCEPTION 'targeted lock did not retrieve combat ship %', v_ship_id;
  END IF;
END
$test$;

\echo 'Combat row-lock regression passed: original query raises 0A000; targeted locks retrieve the ship.'
