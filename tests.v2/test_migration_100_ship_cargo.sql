-- Isolated migration test: temporary relations shadow any public tables.
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
CREATE TEMP TABLE ship_cargo (
  ship_id integer NOT NULL,
  commodity_code text NOT NULL,
  quantity integer NOT NULL CHECK (quantity >= 0),
  PRIMARY KEY (ship_id, commodity_code)
);

INSERT INTO ships VALUES (7, 8, 7, 6, 5, 4, 3, 2);
-- Existing canonical data wins if an upgrade has already populated a row.
INSERT INTO ship_cargo VALUES (7, 'WPN', 1);

\ir ../sql/pg/100_init_ship_cargo.sql
\ir ../sql/pg/100_init_ship_cargo.sql

DO $test$
BEGIN
  IF (SELECT quantity FROM ship_cargo WHERE ship_id = 7 AND commodity_code = 'ORE') <> 8
     OR (SELECT quantity FROM ship_cargo WHERE ship_id = 7 AND commodity_code = 'ORG') <> 7
     OR (SELECT quantity FROM ship_cargo WHERE ship_id = 7 AND commodity_code = 'EQU') <> 6
     OR (SELECT quantity FROM ship_cargo WHERE ship_id = 7 AND commodity_code = 'COL') <> 5
     OR (SELECT quantity FROM ship_cargo WHERE ship_id = 7 AND commodity_code = 'SLV') <> 4
     OR (SELECT quantity FROM ship_cargo WHERE ship_id = 7 AND commodity_code = 'WPN') <> 1
     OR (SELECT quantity FROM ship_cargo WHERE ship_id = 7 AND commodity_code = 'DRG') <> 2 THEN
    RAISE EXCEPTION 'legacy cargo copy or canonical conflict behavior is incorrect';
  END IF;

  IF (SELECT count(*) FROM ship_cargo WHERE ship_id = 7) <> 7 THEN
    RAISE EXCEPTION 'migration produced missing or duplicate cargo rows';
  END IF;
END
$test$;

\echo 'Migration 100 passed: legacy cargo copied once and existing canonical data preserved.'
