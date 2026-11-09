\set ON_ERROR_STOP on

-- V3-SHIP-02: moving authoritative cargo between ships is one lifecycle
-- transition.  Destination capacity failure must roll back the row move and
-- leave both ships' invariant snapshot unchanged.
BEGIN;
CREATE SCHEMA ship_lifecycle_cargo_relocation_test;
SET LOCAL search_path TO ship_lifecycle_cargo_relocation_test, pg_catalog;

CREATE TABLE ships (
  ship_id integer PRIMARY KEY,
  holds integer,
  mines bigint, limpets bigint, fighters integer, genesis bigint,
  detonators bigint, probes bigint, photons bigint, shields integer,
  installed_shields integer, beacons bigint, colonists bigint,
  equipment bigint, organics bigint, ore bigint, slaves bigint,
  weapons bigint, drugs bigint, cloaking_devices bigint, hull bigint,
  sector_id integer
);
CREATE TABLE players (
  player_id integer PRIMARY KEY, ship_id integer, sector_id integer
);
CREATE TABLE ship_ownership (
  ship_id integer, player_id integer, is_primary boolean
);
CREATE TABLE ship_cargo (
  ship_id integer, commodity_code text, quantity bigint
);

\ir ../sql/pg/127_ship_lifecycle_invariants.sql
\ir ../sql/pg/131_ship_cargo_invariant_snapshot.sql
\ir ../sql/pg/135_ship_cargo_quantity_invariant.sql

SET search_path TO ship_lifecycle_cargo_relocation_test, pg_catalog;
INSERT INTO ships VALUES
  (1, 8, 0, 0, 1, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 100, 7),
  (2, 4, 0, 0, 1, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 100, 7);
INSERT INTO ship_cargo VALUES (1, 'ORE', 3), (2, 'ORG', 3);
SET CONSTRAINTS ALL IMMEDIATE;

DO $test$
DECLARE
  rejected boolean := FALSE;
  snapshot record;
BEGIN
  SELECT * INTO snapshot FROM ship_cargo_invariant_snapshot;
  IF snapshot.over_capacity_ships <> 0
     OR snapshot.negative_cargo_quantities <> 0 THEN
    RAISE EXCEPTION 'initial cargo snapshot is not clean: %', snapshot;
  END IF;

  -- The destination has one free hold; moving two units must fail as one
  -- statement and must not leave the cargo row at either destination.
  BEGIN
    UPDATE ship_cargo
       SET ship_id = 2, quantity = 2
     WHERE ship_id = 1 AND commodity_code = 'ORE';
    SET CONSTRAINTS ALL IMMEDIATE;
  EXCEPTION WHEN OTHERS THEN
    rejected := SQLERRM = 'SHIP_CARGO_EXCEEDS_HOLDS';
  END;

  IF NOT rejected THEN
    RAISE EXCEPTION 'over-capacity cargo relocation was accepted';
  END IF;
  IF NOT EXISTS (
       SELECT 1 FROM ship_cargo
        WHERE ship_id = 1 AND commodity_code = 'ORE' AND quantity = 3
     ) OR EXISTS (
       SELECT 1 FROM ship_cargo
        WHERE ship_id = 2 AND commodity_code = 'ORE'
     ) THEN
    RAISE EXCEPTION 'rejected cargo relocation left row drift';
  END IF;

  SELECT * INTO snapshot FROM ship_cargo_invariant_snapshot;
  IF snapshot.over_capacity_ships <> 0
     OR snapshot.negative_cargo_quantities <> 0 THEN
    RAISE EXCEPTION 'rejected cargo relocation left invariant drift: %', snapshot;
  END IF;

  -- A move that fits is valid and is visible in the same snapshot contract.
  UPDATE ship_cargo
     SET ship_id = 2, quantity = 1
   WHERE ship_id = 1 AND commodity_code = 'ORE';
  SET CONSTRAINTS ALL IMMEDIATE;
  IF (SELECT COALESCE(SUM(quantity), 0) FROM ship_cargo WHERE ship_id = 2) <> 4
     OR (SELECT count(*) FROM ship_cargo WHERE ship_id = 1) <> 0 THEN
    RAISE EXCEPTION 'valid cargo relocation was not persisted';
  END IF;
END
$test$;

ROLLBACK;
\echo 'PostgreSQL ship cargo relocation lifecycle checks passed.'
