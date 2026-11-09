\set ON_ERROR_STOP on
BEGIN;
CREATE SCHEMA ship_cargo_quantity_135_test;
SET LOCAL search_path TO ship_cargo_quantity_135_test, pg_catalog;

CREATE TABLE ships (
  ship_id integer PRIMARY KEY,
  holds bigint NOT NULL,
  ore bigint NOT NULL DEFAULT 0,
  organics bigint NOT NULL DEFAULT 0,
  equipment bigint NOT NULL DEFAULT 0,
  colonists bigint NOT NULL DEFAULT 0,
  slaves bigint NOT NULL DEFAULT 0,
  weapons bigint NOT NULL DEFAULT 0,
  drugs bigint NOT NULL DEFAULT 0
);
CREATE TABLE ship_cargo (
  ship_id integer NOT NULL REFERENCES ships(ship_id),
  commodity_code text NOT NULL,
  quantity bigint,
  PRIMARY KEY (ship_id, commodity_code)
);

\ir ../sql/pg/135_ship_cargo_quantity_invariant.sql
\ir ../sql/pg/131_ship_cargo_invariant_snapshot.sql
SET search_path TO ship_cargo_quantity_135_test, pg_catalog;

INSERT INTO ships VALUES (1, 10);
INSERT INTO ship_cargo VALUES (1, 'ORE', 4);

DO $test$
DECLARE r record;
BEGIN
  SELECT * INTO r FROM ship_cargo_invariant_snapshot;
  IF r.negative_cargo_quantities <> 0 OR r.over_capacity_ships <> 0 THEN
    RAISE EXCEPTION 'clean cargo snapshot reported violations: %', r;
  END IF;

  BEGIN
    INSERT INTO ship_cargo VALUES (1, 'ORG', -1);
    RAISE EXCEPTION 'negative cargo quantity accepted';
  EXCEPTION WHEN check_violation THEN NULL;
  END;

  BEGIN
    INSERT INTO ship_cargo VALUES (1, 'EQU', NULL);
    RAISE EXCEPTION 'NULL cargo quantity accepted';
  EXCEPTION WHEN check_violation THEN NULL;
  END;

  UPDATE ship_cargo SET quantity = 5
   WHERE ship_id = 1 AND commodity_code = 'ORE';
  SELECT * INTO r FROM ship_cargo_invariant_snapshot;
  IF r.negative_cargo_quantities <> 0 OR r.over_capacity_ships <> 0 THEN
    RAISE EXCEPTION 'valid cargo update changed the invariant snapshot: %', r;
  END IF;
END
$test$;

ROLLBACK;
\echo 'PostgreSQL ship cargo quantity invariant checks passed.'
