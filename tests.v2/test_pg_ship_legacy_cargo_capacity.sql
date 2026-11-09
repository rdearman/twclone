\set ON_ERROR_STOP on

-- V3-SHIP-02: isolated proof that legacy cargo cannot exceed holds and a
-- rejected lifecycle write leaves the invariant snapshot unchanged.
BEGIN;
CREATE SCHEMA ship_legacy_cargo_capacity_test;
SET LOCAL search_path TO ship_legacy_cargo_capacity_test, pg_catalog;

CREATE TABLE ships (
  ship_id integer PRIMARY KEY,
  holds integer,
  mines bigint, limpets bigint, fighters integer, genesis bigint,
  detonators bigint, probes bigint, photons bigint, shields integer,
  installed_shields integer, beacons bigint, colonists bigint,
  equipment bigint, organics bigint, ore bigint, slaves bigint,
  weapons bigint, drugs bigint, cloaking_devices bigint, hull bigint
);
CREATE TABLE players (player_id integer PRIMARY KEY, ship_id integer);
CREATE TABLE ship_ownership (
  ship_id integer, player_id integer, is_primary boolean
);
CREATE TABLE ship_cargo (
  ship_id integer, commodity_code text, quantity bigint
);

\ir ../sql/pg/127_ship_lifecycle_invariants.sql
\ir ../sql/pg/142_ship_legacy_cargo_capacity.sql
SET search_path TO ship_legacy_cargo_capacity_test, pg_catalog;

INSERT INTO ships (
  ship_id, holds, mines, limpets, fighters, genesis, detonators, probes,
  photons, shields, installed_shields, beacons, colonists, equipment,
  organics, ore, slaves, weapons, drugs, cloaking_devices, hull
) VALUES (1, 5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4, 0, 0, 0, 0, 100);

DO $test$
DECLARE
  rejected boolean := FALSE;
  load_count bigint;
BEGIN
  SELECT over_capacity_ships INTO load_count
    FROM ship_legacy_cargo_capacity_snapshot;
  IF load_count <> 0 THEN
    RAISE EXCEPTION 'valid legacy cargo snapshot is not clean: %', load_count;
  END IF;

  BEGIN
    UPDATE ships SET ore = 6 WHERE ship_id = 1;
    SET CONSTRAINTS ALL IMMEDIATE;
  EXCEPTION WHEN OTHERS THEN
    rejected := SQLERRM = 'SHIP_LEGACY_CARGO_EXCEEDS_HOLDS:1';
  END;

  IF NOT rejected OR (SELECT ore FROM ships WHERE ship_id = 1) <> 4 THEN
    RAISE EXCEPTION 'over-capacity legacy cargo update was not rolled back';
  END IF;

  SELECT over_capacity_ships INTO load_count
    FROM ship_legacy_cargo_capacity_snapshot;
  IF load_count <> 0 THEN
    RAISE EXCEPTION 'failed legacy cargo update left invariant drift: %', load_count;
  END IF;
END
$test$;

ROLLBACK;
\echo 'PostgreSQL legacy ship cargo capacity checks passed.'
