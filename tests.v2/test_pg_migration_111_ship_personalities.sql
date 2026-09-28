-- Run with psql against a disposable PostgreSQL DB with v2 baseline tables.
-- All DDL/data changes are rolled back when this validation finishes.
BEGIN;
\ir ../sql/pg/111_ship_personalities.sql
\ir ../sql/pg/111_ship_personalities.sql

DO $test$
DECLARE
  v_count integer;
BEGIN
  SELECT COUNT(*) INTO v_count
    FROM information_schema.columns
   WHERE table_schema = current_schema()
     AND (table_name, column_name) IN
         (('ships', 'personality'),
          ('corporations', 'ship_personality'),
          ('shiptypes', 'default_personality'));
  IF v_count <> 3 THEN
    RAISE EXCEPTION 'expected three personality columns, found %', v_count;
  END IF;

  IF NOT EXISTS (SELECT 1 FROM shiptypes
                  WHERE name = 'Orion Heavy Fighter Patrol'
                    AND default_personality IS NOT DISTINCT FROM 'offensive')
     OR NOT EXISTS (SELECT 1 FROM shiptypes
                     WHERE name = 'Orion Scout/Looter'
                       AND default_personality IS NOT DISTINCT FROM 'looter')
     OR NOT EXISTS (SELECT 1 FROM shiptypes
                     WHERE name = 'Orion Black Market Guard'
                       AND default_personality IS NOT DISTINCT FROM 'blockader') THEN
    RAISE EXCEPTION 'Orion shiptype defaults were not assigned';
  END IF;

  BEGIN
    UPDATE ships SET personality = 'invalid' WHERE ship_id =
      (SELECT MIN(ship_id) FROM ships);
    RAISE EXCEPTION 'invalid ship personality was accepted';
  EXCEPTION WHEN check_violation THEN
    NULL;
  END;
END
$test$;

ROLLBACK;
\echo 'Ship personality migration persistence/replay checks passed.'
