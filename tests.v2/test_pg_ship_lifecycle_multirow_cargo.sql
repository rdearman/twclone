\set ON_ERROR_STOP on

-- V3-SHIP-02: deferred cargo checks must validate the final aggregate of a
-- multi-row lifecycle mutation, and a refused mutation must leave the
-- persisted ship and its invariant snapshot unchanged.
BEGIN;
CREATE SCHEMA ship_lifecycle_multirow_cargo_test;
SET LOCAL search_path TO ship_lifecycle_multirow_cargo_test, pg_catalog;

CREATE TABLE ships (
  ship_id integer PRIMARY KEY, holds integer, mines bigint, limpets bigint,
  fighters integer, genesis bigint, detonators bigint, probes bigint,
  photons bigint, shields integer, installed_shields integer, beacons bigint,
  colonists bigint, equipment bigint, organics bigint, ore bigint,
  slaves bigint, weapons bigint, drugs bigint, cloaking_devices bigint,
  hull bigint, sector_id integer
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

SET search_path TO ship_lifecycle_multirow_cargo_test, pg_catalog;
INSERT INTO ships VALUES
  (1, 10, 0, 0, 1, 0, 0, 0, 0, 1, 1, 0, 0, 0, 2, 3,
   0, 0, 0, 0, 100, 7);
INSERT INTO ship_cargo VALUES
  (1, 'ORE', 3), (1, 'ORG', 2);
SET CONSTRAINTS ALL IMMEDIATE;

DO $test$
DECLARE
  snapshot record;
  rejected boolean := FALSE;
BEGIN
  SELECT * INTO snapshot FROM ship_cargo_invariant_snapshot;
  IF snapshot.negative_cargo_quantities <> 0
     OR snapshot.over_capacity_ships <> 0 THEN
    RAISE EXCEPTION 'clean multi-row cargo state is not clean: %', snapshot;
  END IF;

  -- A single statement changes two rows.  The aggregate remains within the
  -- ten-unit hold limit and must therefore commit as one valid transition.
  UPDATE ship_cargo
     SET quantity = quantity + 1
   WHERE ship_id = 1;
  SET CONSTRAINTS ALL IMMEDIATE;

  IF (SELECT COALESCE(SUM(quantity), 0) FROM ship_cargo WHERE ship_id = 1) <> 7 THEN
    RAISE EXCEPTION 'valid multi-row cargo transition was not persisted';
  END IF;

  -- Both rows are increased in one mutation, taking the final aggregate to
  -- eleven.  The deferred guard must reject the transaction-local change,
  -- rather than accepting either row independently.
  BEGIN
    UPDATE ship_cargo
       SET quantity = quantity + 2
     WHERE ship_id = 1;
    SET CONSTRAINTS ALL IMMEDIATE;
  EXCEPTION WHEN OTHERS THEN
    rejected := SQLERRM = 'SHIP_CARGO_EXCEEDS_HOLDS';
  END;

  IF NOT rejected THEN
    RAISE EXCEPTION 'over-capacity multi-row cargo transition was accepted';
  END IF;

  SELECT * INTO snapshot FROM ship_cargo_invariant_snapshot;
  IF (SELECT COALESCE(SUM(quantity), 0) FROM ship_cargo WHERE ship_id = 1) <> 7
     OR snapshot.negative_cargo_quantities <> 0
     OR snapshot.over_capacity_ships <> 0 THEN
    RAISE EXCEPTION 'rejected multi-row transition left cargo drift: %', snapshot;
  END IF;
END
$test$;

ROLLBACK;
\echo 'PostgreSQL multi-row ship cargo lifecycle checks passed.'
