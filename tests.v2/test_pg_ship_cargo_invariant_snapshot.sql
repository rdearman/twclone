\set ON_ERROR_STOP on
BEGIN;
CREATE SCHEMA ship_cargo_131_test;
SET LOCAL search_path TO ship_cargo_131_test, pg_catalog;

CREATE TABLE ships (
  ship_id integer PRIMARY KEY,
  holds integer NOT NULL,
  colonists bigint NOT NULL DEFAULT 0,
  equipment bigint NOT NULL DEFAULT 0,
  organics bigint NOT NULL DEFAULT 0,
  ore bigint NOT NULL DEFAULT 0,
  slaves bigint NOT NULL DEFAULT 0,
  weapons bigint NOT NULL DEFAULT 0,
  drugs bigint NOT NULL DEFAULT 0
);
CREATE TABLE ship_cargo (
  ship_id integer NOT NULL REFERENCES ships(ship_id),
  commodity_code text NOT NULL,
  quantity bigint NOT NULL CHECK (quantity >= 0),
  PRIMARY KEY (ship_id, commodity_code)
);

\ir ../sql/pg/131_ship_cargo_invariant_snapshot.sql
SET search_path TO ship_cargo_131_test, pg_catalog;

INSERT INTO ships (ship_id, holds, ore, organics) VALUES (1, 5, 2, 1);
INSERT INTO ship_cargo (ship_id, commodity_code, quantity)
VALUES (1, 'ORE', 2), (1, 'ORG', 1);

DO $test$
DECLARE r record;
BEGIN
  SELECT * INTO r FROM ship_cargo_invariant_snapshot;
  IF r.negative_cargo_quantities <> 0 OR r.over_capacity_ships <> 0
     OR r.legacy_mirror_mismatches <> 0 THEN
    RAISE EXCEPTION 'clean cargo snapshot reported violations: %', r;
  END IF;

  INSERT INTO ship_cargo VALUES (1, 'EQU', 3);
  SELECT * INTO r FROM ship_cargo_invariant_snapshot;
  IF r.over_capacity_ships <> 1 OR r.legacy_mirror_mismatches <> 1 THEN
    RAISE EXCEPTION 'over-capacity cargo was not fully visible: %', r;
  END IF;
  DELETE FROM ship_cargo WHERE ship_id = 1 AND commodity_code = 'EQU';

  UPDATE ships SET ore = 4 WHERE ship_id = 1;
  SELECT * INTO r FROM ship_cargo_invariant_snapshot;
  IF r.legacy_mirror_mismatches <> 1 THEN
    RAISE EXCEPTION 'legacy mirror drift was not visible: %', r;
  END IF;

  BEGIN
    INSERT INTO ship_cargo VALUES (1, 'SLV', -1);
    RAISE EXCEPTION 'negative cargo accepted';
  EXCEPTION WHEN check_violation THEN NULL;
  END;
END
$test$;

ROLLBACK;
\echo 'PostgreSQL ship cargo invariant snapshot checks passed.'
