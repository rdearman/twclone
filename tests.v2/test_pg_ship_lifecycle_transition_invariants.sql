\set ON_ERROR_STOP on
BEGIN;
CREATE SCHEMA ship_lifecycle_transition_test;
SET LOCAL search_path TO ship_lifecycle_transition_test, pg_catalog;

CREATE TABLE ships (
  ship_id integer PRIMARY KEY,
  holds integer, mines bigint, limpets bigint, fighters integer,
  genesis bigint, detonators bigint, probes bigint, photons bigint,
  shields integer, installed_shields integer, beacons bigint,
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
\ir ../sql/pg/128_ship_relationship_invariants.sql
SET search_path TO ship_lifecycle_transition_test, pg_catalog;

INSERT INTO ships VALUES
  (1, 4, 0, 0, 1, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 100, 7),
  (2, 6, 0, 0, 1, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 100, 8);
INSERT INTO players VALUES (10, 1, 7);
INSERT INTO ship_ownership VALUES (1, 10, TRUE);
INSERT INTO ship_cargo VALUES (1, 'ORE', 3);

DO $test$
DECLARE r record;
BEGIN
  SELECT * INTO r FROM ship_lifecycle_invariant_snapshot;
  IF r.resource_violations <> 0 OR r.dynamic_cargo_violations <> 0
     OR r.active_ship_duplicates <> 0 OR r.active_without_primary_owner <> 0
     OR r.primary_owner_duplicates <> 0 OR r.orphan_primary_owners <> 0
     OR r.active_sector_mismatches <> 0 THEN
    RAISE EXCEPTION 'initial lifecycle snapshot is not clean: %', r;
  END IF;

  -- A coordinated ship transfer is valid once all deferred checks settle.
  INSERT INTO ship_ownership VALUES (2, 10, TRUE);
  UPDATE players SET ship_id = 2, sector_id = 8 WHERE player_id = 10;
  DELETE FROM ship_ownership WHERE ship_id = 1 AND player_id = 10;
  SET CONSTRAINTS ALL IMMEDIATE;

  SELECT * INTO r FROM ship_relationship_invariant_snapshot;
  IF r.active_without_primary_owner <> 0 OR r.active_sector_mismatches <> 0 THEN
    RAISE EXCEPTION 'valid transfer left relationship violations: %', r;
  END IF;

  -- A rejected transition must roll back to a clean durable state.
  BEGIN
    UPDATE ships SET sector_id = 7 WHERE ship_id = 2;
    SET CONSTRAINTS ALL IMMEDIATE;
    RAISE EXCEPTION 'sector mismatch accepted';
  EXCEPTION WHEN OTHERS THEN
    IF SQLERRM NOT LIKE 'SHIP_ACTIVE_SECTOR_MISMATCH:%' THEN
      RAISE;
    END IF;
  END;
  SET CONSTRAINTS ALL IMMEDIATE;

  SELECT * INTO r FROM ship_lifecycle_invariant_snapshot;
  IF r.active_ship_duplicates <> 0 OR r.active_without_primary_owner <> 0
     OR r.primary_owner_duplicates <> 0 OR r.orphan_primary_owners <> 0
     OR r.active_sector_mismatches <> 0 THEN
    RAISE EXCEPTION 'rollback left lifecycle violations: %', r;
  END IF;
END
$test$;

ROLLBACK;
\echo 'PostgreSQL ship lifecycle transition invariant checks passed.'
