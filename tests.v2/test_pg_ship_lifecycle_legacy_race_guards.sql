\set ON_ERROR_STOP on

-- V3-SHIP-02: upgrade-safe fallback guards must still reject new duplicates
-- when legacy rows prevent creation of the native unique ownership index.
BEGIN;
CREATE SCHEMA ship_lifecycle_legacy_race_140_test;
SET LOCAL search_path TO ship_lifecycle_legacy_race_140_test, pg_catalog;

CREATE TABLE ships (
  ship_id integer PRIMARY KEY, holds integer, mines bigint, limpets bigint,
  fighters integer, genesis bigint, detonators bigint, probes bigint,
  photons bigint, shields integer, installed_shields integer, beacons bigint,
  colonists bigint, equipment bigint, organics bigint, ore bigint, slaves bigint,
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
\ir ../sql/pg/140_ship_lifecycle_legacy_race_guards.sql
SET search_path TO ship_lifecycle_legacy_race_140_test, pg_catalog;

INSERT INTO ships VALUES
  (1, 4, 0, 0, 1, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 100, 7);
INSERT INTO players VALUES (10, 1, 7);
INSERT INTO ship_ownership VALUES (1, 10, TRUE);
SET CONSTRAINTS ALL IMMEDIATE;

DO $test$
DECLARE
  rejected boolean;
BEGIN
  rejected := FALSE;
  BEGIN
    INSERT INTO ship_ownership VALUES (1, 11, TRUE);
    SET CONSTRAINTS ALL IMMEDIATE;
  EXCEPTION WHEN SQLSTATE '23505' THEN
    rejected := SQLERRM = 'SHIP_PRIMARY_OWNER_DUPLICATE:1';
  END;
  IF NOT rejected THEN
    RAISE EXCEPTION 'fallback primary-owner guard accepted duplicate';
  END IF;

  rejected := FALSE;
  BEGIN
    INSERT INTO players VALUES (11, 1, 7);
    SET CONSTRAINTS ALL IMMEDIATE;
  EXCEPTION WHEN SQLSTATE '23514' THEN
    rejected := SQLERRM = 'SHIP_ACTIVE_DUPLICATE:1';
  END;
  IF NOT rejected THEN
    RAISE EXCEPTION 'active-ship fallback guard accepted duplicate';
  END IF;

  -- A normal lifecycle handoff remains valid under the serialized guards.
  INSERT INTO ships VALUES
    (2, 4, 0, 0, 1, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 100, 7);
  INSERT INTO ship_ownership VALUES (2, 10, TRUE);
  UPDATE players SET ship_id = 2 WHERE player_id = 10;
  DELETE FROM ship_ownership WHERE ship_id = 1 AND player_id = 10;
  SET CONSTRAINTS ALL IMMEDIATE;
END
$test$;

ROLLBACK;
\echo 'PostgreSQL ship lifecycle legacy race guard checks passed.'
