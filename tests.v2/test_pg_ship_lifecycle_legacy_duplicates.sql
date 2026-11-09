\set ON_ERROR_STOP on

-- V3-SHIP-02: migration 127 must remain upgrade-safe when legacy active-ship
-- duplicates exist, while rejecting new duplicate writes.
BEGIN;
CREATE SCHEMA ship_lifecycle_legacy_duplicate_test;
SET LOCAL search_path TO ship_lifecycle_legacy_duplicate_test, pg_catalog;

CREATE TABLE ships (
  ship_id integer PRIMARY KEY,
  holds integer,
  mines bigint, limpets bigint, fighters integer, genesis bigint,
  detonators bigint, probes bigint, photons bigint, shields integer,
  installed_shields integer, beacons bigint, colonists bigint,
  equipment bigint, organics bigint, ore bigint, slaves bigint,
  weapons bigint, drugs bigint, cloaking_devices bigint, hull bigint
);
CREATE TABLE players (
  player_id integer PRIMARY KEY,
  ship_id integer,
  sector_id integer
);
CREATE TABLE ship_cargo (
  ship_id integer,
  commodity_code text,
  quantity bigint
);

INSERT INTO ships VALUES
  (1, 4, 0, 0, 1, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 100);
INSERT INTO players VALUES (10, 1, 7), (11, 1, 7);

\ir ../sql/pg/127_ship_lifecycle_invariants.sql
SET search_path TO ship_lifecycle_legacy_duplicate_test, pg_catalog;

DO $test$
DECLARE
  snapshot record;
  rejected boolean := FALSE;
BEGIN
  SELECT * INTO snapshot FROM ship_lifecycle_invariant_snapshot;
  IF snapshot.active_ship_duplicates <> 1 THEN
    RAISE EXCEPTION 'legacy duplicate was not preserved for reporting: %', snapshot;
  END IF;

  BEGIN
    INSERT INTO players VALUES (12, 1, 7);
    SET CONSTRAINTS ALL IMMEDIATE;
  EXCEPTION WHEN SQLSTATE '23514' THEN
    rejected := SQLERRM = 'SHIP_ACTIVE_DUPLICATE:1';
  END;
  IF NOT rejected THEN
    RAISE EXCEPTION 'new duplicate active-ship reference was accepted';
  END IF;
END
$test$;

ROLLBACK;
\echo 'PostgreSQL legacy duplicate ship lifecycle checks passed.'
