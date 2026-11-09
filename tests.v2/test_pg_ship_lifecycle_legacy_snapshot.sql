\set ON_ERROR_STOP on
BEGIN;
CREATE SCHEMA ship_lifecycle_legacy_snapshot_test;
SET LOCAL search_path TO ship_lifecycle_legacy_snapshot_test, pg_catalog;

-- Seed rows before the NOT VALID lifecycle checks are installed.  Production
-- data can contain these historical violations; the snapshot must keep them
-- visible while new writes are rejected by the candidate migration.
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
  hull bigint,
  sector_id integer
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

INSERT INTO ships VALUES
  (1, NULL, -1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 101, 8),
  (2, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4, 0, 0, 0, 0, 0, 0, 0, 100, 7);
INSERT INTO players VALUES (1, 1, 9);
INSERT INTO ship_cargo VALUES (2, 'ORE', 4);

\ir ../sql/pg/127_ship_lifecycle_invariants.sql
\ir ../sql/pg/128_ship_relationship_invariants.sql
SET search_path TO ship_lifecycle_legacy_snapshot_test, pg_catalog;

DO $test$
DECLARE r record;
BEGIN
  SELECT * INTO r FROM ship_lifecycle_invariant_snapshot;
  IF r.resource_violations <> 1
     OR r.legacy_cargo_violations <> 0
     OR r.dynamic_cargo_violations <> 1
     OR r.dynamic_cargo_quantity_violations <> 0
     OR r.active_ship_duplicates <> 0
     OR r.active_without_primary_owner <> 1
     OR r.active_sector_mismatches <> 1 THEN
    RAISE EXCEPTION 'legacy lifecycle snapshot mismatch: %', r;
  END IF;

  SELECT * INTO r FROM ship_relationship_invariant_snapshot;
  IF r.active_without_primary_owner <> 1
     OR r.active_sector_mismatches <> 1 THEN
    RAISE EXCEPTION 'legacy relationship snapshot mismatch: %', r;
  END IF;
END
$test$;

ROLLBACK;
\echo 'PostgreSQL legacy ship lifecycle snapshot checks passed.'
