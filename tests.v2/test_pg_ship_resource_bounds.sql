\set ON_ERROR_STOP on

-- V3-SHIP-02: direct writes cannot create negative ship resources or an
-- out-of-range hull.  The temporary schema and outer transaction make this
-- an isolated migration test; no production catalog or data is changed.
BEGIN;
CREATE SCHEMA ship_resource_bounds_test;
SET LOCAL search_path TO ship_resource_bounds_test, pg_catalog;

CREATE TABLE ships (
  ship_id integer PRIMARY KEY,
  holds integer,
  mines bigint,
  limpets bigint,
  fighters integer,
  genesis bigint,
  detonators bigint,
  probes bigint,
  photons bigint,
  shields integer,
  installed_shields integer,
  beacons bigint,
  colonists bigint,
  equipment bigint,
  organics bigint,
  ore bigint,
  slaves bigint,
  weapons bigint,
  drugs bigint,
  cloaking_devices bigint,
  hull bigint
);
CREATE TABLE players (
  player_id integer PRIMARY KEY,
  ship_id integer,
  sector_id integer
);
CREATE TABLE ship_ownership (
  ship_id integer,
  player_id integer,
  is_primary boolean
);
CREATE TABLE ship_cargo (
  ship_id integer,
  commodity_code text,
  quantity bigint
);

\ir ../sql/pg/127_ship_lifecycle_invariants.sql
SET search_path TO ship_resource_bounds_test, pg_catalog;

INSERT INTO ships VALUES
  (1, 8, 0, 0, 10, 0, 0, 0, 0, 10, 10, 0, 0, 0, 0, 0, 0, 0, 0, 100);

DO $test$
DECLARE
  rejected boolean;
BEGIN
  rejected := FALSE;
  BEGIN
    UPDATE ships SET fighters = -1 WHERE ship_id = 1;
  EXCEPTION WHEN check_violation THEN
    rejected := TRUE;
  END;
  IF NOT rejected THEN
    RAISE EXCEPTION 'negative fighters were accepted';
  END IF;

  rejected := FALSE;
  BEGIN
    UPDATE ships SET ore = -1 WHERE ship_id = 1;
  EXCEPTION WHEN check_violation THEN
    rejected := TRUE;
  END;
  IF NOT rejected THEN
    RAISE EXCEPTION 'negative legacy cargo was accepted';
  END IF;

  rejected := FALSE;
  BEGIN
    UPDATE ships SET hull = 101 WHERE ship_id = 1;
  EXCEPTION WHEN check_violation THEN
    rejected := TRUE;
  END;
  IF NOT rejected THEN
    RAISE EXCEPTION 'hull above 100 was accepted';
  END IF;

  IF (SELECT fighters FROM ships WHERE ship_id = 1) <> 10
     OR (SELECT ore FROM ships WHERE ship_id = 1) <> 0
     OR (SELECT hull FROM ships WHERE ship_id = 1) <> 100 THEN
    RAISE EXCEPTION 'rejected resource writes changed persisted state';
  END IF;
END
$test$;

ROLLBACK;
\echo 'PostgreSQL ship resource-bound checks passed.'
