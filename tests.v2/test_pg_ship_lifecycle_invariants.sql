\set ON_ERROR_STOP on
BEGIN;
CREATE SCHEMA ship_lifecycle_127_test;
SET LOCAL search_path TO ship_lifecycle_127_test, pg_catalog;

CREATE TABLE ships (
  ship_id integer PRIMARY KEY, holds integer, mines bigint, limpets bigint,
  fighters integer, genesis bigint, detonators bigint, probes bigint,
  photons bigint, shields integer, installed_shields integer, beacons bigint,
  colonists bigint, equipment bigint, organics bigint, ore bigint, slaves bigint,
  weapons bigint, drugs bigint, cloaking_devices bigint, hull bigint
);
CREATE TABLE players (player_id integer PRIMARY KEY, ship_id integer, sector_id integer);
CREATE TABLE ship_ownership (ship_id integer, player_id integer, is_primary boolean);
CREATE TABLE ship_cargo (
  ship_id integer,
  commodity_code text,
  quantity bigint CHECK (quantity >= 0)
);

\ir ../sql/pg/127_ship_lifecycle_invariants.sql
SET search_path TO ship_lifecycle_127_test, pg_catalog;

INSERT INTO ships VALUES (1, 3, 0, 0, 1, 0, 0, 0, 0, 1, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 100);
INSERT INTO players VALUES (1, 1, 7);
INSERT INTO ship_ownership VALUES (1, 1, TRUE);
INSERT INTO ship_cargo VALUES (1, 'ORE', 3);

DO $test$
DECLARE rejected boolean := FALSE;
BEGIN
  BEGIN
    INSERT INTO ship_cargo VALUES (1, 'ORG', 1);
    SET CONSTRAINTS ALL IMMEDIATE;
  EXCEPTION WHEN OTHERS THEN
    rejected := TRUE;
  END;
  IF NOT rejected THEN RAISE EXCEPTION 'over-capacity cargo accepted'; END IF;

  rejected := FALSE;
  BEGIN
    UPDATE ship_cargo SET quantity = 4
      WHERE ship_id = 1 AND commodity_code = 'ORE';
    SET CONSTRAINTS ALL IMMEDIATE;
  EXCEPTION WHEN OTHERS THEN
    rejected := SQLERRM = 'SHIP_CARGO_EXCEEDS_HOLDS';
  END;
  IF NOT rejected THEN RAISE EXCEPTION 'over-capacity cargo update accepted'; END IF;

  rejected := FALSE;
  BEGIN
    UPDATE ships SET holds = 2 WHERE ship_id = 1;
    SET CONSTRAINTS ALL IMMEDIATE;
  EXCEPTION WHEN OTHERS THEN
    rejected := SQLERRM = 'SHIP_CARGO_EXCEEDS_HOLDS';
  END;
  IF NOT rejected THEN RAISE EXCEPTION 'holds reduction below cargo accepted'; END IF;

  rejected := FALSE;
  BEGIN
    INSERT INTO players VALUES (2, 1, 7);
  EXCEPTION WHEN unique_violation THEN
    rejected := TRUE;
  END;
  IF NOT rejected THEN RAISE EXCEPTION 'duplicate active ship accepted'; END IF;

  rejected := FALSE;
  BEGIN
    UPDATE ships SET holds = NULL WHERE ship_id = 1;
  EXCEPTION WHEN check_violation THEN
    rejected := TRUE;
  END;
  IF NOT rejected THEN RAISE EXCEPTION 'nullable lifecycle resource accepted'; END IF;

  rejected := FALSE;
  BEGIN
    UPDATE ships SET hull = NULL WHERE ship_id = 1;
  EXCEPTION WHEN check_violation THEN
    rejected := TRUE;
  END;
  IF NOT rejected THEN RAISE EXCEPTION 'nullable hull accepted'; END IF;

  rejected := FALSE;
  BEGIN
    INSERT INTO ship_cargo VALUES (1, 'SLV', -1);
    RAISE EXCEPTION 'negative dynamic cargo accepted';
  EXCEPTION WHEN check_violation THEN
    rejected := TRUE;
  END;
  IF NOT rejected THEN RAISE EXCEPTION 'negative dynamic cargo was not rejected'; END IF;

  UPDATE ship_cargo SET quantity = 2
    WHERE ship_id = 1 AND commodity_code = 'ORE';
  UPDATE ships SET holds = 2 WHERE ship_id = 1;
  SET CONSTRAINTS ALL IMMEDIATE;
END
$test$;

-- Snapshot shape is stable and exposes sector/ownership evidence even when
-- historical rows predate the NOT VALID production check.
DO $test$
DECLARE r record;
BEGIN
  SELECT * INTO r FROM ship_lifecycle_invariant_snapshot;
  IF r.resource_violations <> 0 OR r.legacy_cargo_violations <> 0
     OR r.dynamic_cargo_violations <> 0
     OR r.dynamic_cargo_quantity_violations <> 0
     OR r.active_ship_duplicates <> 0
     OR r.active_without_primary_owner <> 0 OR r.active_sector_mismatches <> 0 THEN
    RAISE EXCEPTION 'clean lifecycle snapshot reported violations: %', r;
  END IF;
END
$test$;

ROLLBACK;
\echo 'PostgreSQL ship lifecycle invariant checks passed.'
