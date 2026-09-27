-- Isolated upgrade-path test for a legacy database without ship_cargo.
-- All application tables are temporary; no persistent schema or data is changed.
SET search_path TO pg_temp, public;

CREATE TEMP TABLE ships (
  ship_id integer PRIMARY KEY,
  ore integer,
  organics integer,
  equipment integer,
  colonists integer,
  slaves integer,
  weapons integer,
  drugs integer
);
CREATE TEMP TABLE commodities (code text PRIMARY KEY);
INSERT INTO commodities (code) VALUES
  ('ORE'), ('ORG'), ('EQU'), ('COL'), ('SLV'), ('WPN'), ('DRG');
INSERT INTO ships VALUES (7, 8, 7, 6, 5, 4, 3, 2);

\ir ../sql/pg/100_init_ship_cargo.sql
\ir ../sql/pg/100_init_ship_cargo.sql

DO $test$
BEGIN
  IF (SELECT quantity FROM ship_cargo WHERE ship_id = 7 AND commodity_code = 'ORE') <> 8
     OR (SELECT quantity FROM ship_cargo WHERE ship_id = 7 AND commodity_code = 'ORG') <> 7
     OR (SELECT quantity FROM ship_cargo WHERE ship_id = 7 AND commodity_code = 'EQU') <> 6
     OR (SELECT quantity FROM ship_cargo WHERE ship_id = 7 AND commodity_code = 'COL') <> 5
     OR (SELECT quantity FROM ship_cargo WHERE ship_id = 7 AND commodity_code = 'SLV') <> 4
     OR (SELECT quantity FROM ship_cargo WHERE ship_id = 7 AND commodity_code = 'WPN') <> 3
     OR (SELECT quantity FROM ship_cargo WHERE ship_id = 7 AND commodity_code = 'DRG') <> 2 THEN
    RAISE EXCEPTION 'legacy cargo copy or replay behavior is incorrect';
  END IF;

  IF (SELECT count(*) FROM ship_cargo WHERE ship_id = 7) <> 7 THEN
    RAISE EXCEPTION 'migration produced missing or duplicate cargo rows';
  END IF;

  IF (SELECT count(*) FROM pg_constraint
      WHERE conrelid = 'ship_cargo'::regclass AND contype = 'f') <> 2 THEN
    RAISE EXCEPTION 'ship_cargo foreign keys are missing';
  END IF;

  IF NOT EXISTS (SELECT 1 FROM pg_indexes
      WHERE schemaname = current_schema()
        AND tablename = 'ship_cargo'
        AND indexname = 'idx_ship_cargo_ship_id') THEN
    RAISE EXCEPTION 'ship_cargo ship lookup index is missing';
  END IF;
END
$test$;

\echo 'PostgreSQL migration 100 upgrade path passed: table, keys, index, copy, and replay.'
