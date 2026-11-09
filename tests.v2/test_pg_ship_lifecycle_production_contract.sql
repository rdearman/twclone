\set ON_ERROR_STOP on

-- V3-SHIP-02: exercise the complete PostgreSQL ship-lifecycle invariant
-- bundle in an isolated schema.  This deliberately uses only the tables
-- touched by the migrations, so it can run without BigBang or live services.
BEGIN;
CREATE SCHEMA ship_lifecycle_production_contract_test;
SET LOCAL search_path TO ship_lifecycle_production_contract_test, pg_catalog;

CREATE TABLE ships (
  ship_id integer PRIMARY KEY,
  holds integer,
  mines bigint, limpets bigint, fighters integer, genesis bigint,
  detonators bigint, probes bigint, photons bigint, shields integer,
  installed_shields integer, beacons bigint, colonists bigint,
  equipment bigint, organics bigint, ore bigint, slaves bigint,
  weapons bigint, drugs bigint, cloaking_devices bigint, hull bigint,
  sector_id integer, destroyed boolean NOT NULL DEFAULT FALSE,
  towing_ship_id integer NOT NULL DEFAULT 0,
  is_being_towed_by integer NOT NULL DEFAULT 0
);
CREATE TABLE players (
  player_id integer PRIMARY KEY, ship_id integer, sector_id integer
);
CREATE TABLE ship_ownership (
  ship_id integer, player_id integer, role_id bigint NOT NULL DEFAULT 1,
  is_primary boolean
);
CREATE TABLE ship_cargo (
  ship_id integer, commodity_code text, quantity bigint
);

\ir ../sql/pg/126_ship_primary_ownership.sql
\ir ../sql/pg/127_ship_lifecycle_invariants.sql
\ir ../sql/pg/128_ship_relationship_invariants.sql
\ir ../sql/pg/131_ship_cargo_invariant_snapshot.sql
\ir ../sql/pg/132_ship_active_reference_invariants.sql
\ir ../sql/pg/134_ship_lifecycle_state_snapshot.sql
\ir ../sql/pg/135_ship_cargo_quantity_invariant.sql
\ir ../sql/pg/136_ship_destroyed_active_invariant.sql
\ir ../sql/pg/137_ship_active_owner_invariant.sql
\ir ../sql/pg/138_ship_ownership_invariant_snapshot.sql
\ir ../sql/pg/139_ship_lifecycle_acceptance_snapshot.sql
\ir ../sql/pg/140_ship_lifecycle_legacy_race_guards.sql
\ir ../sql/pg/142_ship_legacy_cargo_capacity.sql
\ir ../sql/pg/143_ship_tow_invariants.sql
\ir ../sql/pg/144_ship_lifecycle_composite_snapshot.sql
\ir ../sql/pg/145_ship_active_sector_invariant.sql

SET search_path TO ship_lifecycle_production_contract_test, pg_catalog;

INSERT INTO ships (ship_id, holds, mines, limpets, fighters, genesis,
  detonators, probes, photons, shields, installed_shields, beacons,
  colonists, equipment, organics, ore, slaves, weapons, drugs,
  cloaking_devices, hull, sector_id)
VALUES
  (1, 4, 0, 0, 1, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 100, 7),
  (2, 6, 0, 0, 1, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 100, 8),
  (3, 6, 0, 0, 1, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 100, 8);
INSERT INTO players VALUES (10, 1, 7);
INSERT INTO ship_ownership VALUES (1, 10, 1, TRUE);
UPDATE ships SET ore = 3 WHERE ship_id = 1;
INSERT INTO ship_cargo VALUES (1, 'ORE', 3);

DO $test$
DECLARE r record; rejected boolean;
BEGIN
  SELECT * INTO r FROM ship_lifecycle_composite_snapshot;
  IF r.violation_count <> 0 THEN
    RAISE EXCEPTION 'initial lifecycle snapshot is not clean: %', r;
  END IF;

  -- A coordinated active-ship handoff is valid when all deferred guards agree.
  INSERT INTO ship_ownership VALUES (2, 10, 1, TRUE);
  UPDATE players SET ship_id = 2, sector_id = 8 WHERE player_id = 10;
  DELETE FROM ship_ownership WHERE ship_id = 1 AND player_id = 10;
  SET CONSTRAINTS ALL IMMEDIATE;

  rejected := FALSE;
  BEGIN
    INSERT INTO ship_ownership VALUES (2, 11, 1, TRUE);
    SET CONSTRAINTS ALL IMMEDIATE;
  EXCEPTION WHEN OTHERS THEN
    rejected := SQLSTATE IN ('23505', '23514');
  END;
  IF NOT rejected THEN RAISE EXCEPTION 'duplicate primary owner accepted'; END IF;

  rejected := FALSE;
  BEGIN
    INSERT INTO ship_cargo VALUES (2, 'ORE', 7);
    SET CONSTRAINTS ALL IMMEDIATE;
  EXCEPTION WHEN OTHERS THEN
    rejected := SQLERRM = 'SHIP_CARGO_EXCEEDS_HOLDS';
  END;
  IF NOT rejected THEN RAISE EXCEPTION 'over-capacity cargo accepted'; END IF;

  rejected := FALSE;
  BEGIN
    UPDATE ships SET sector_id = 7 WHERE ship_id = 2;
    SET CONSTRAINTS ALL IMMEDIATE;
  EXCEPTION WHEN OTHERS THEN
    rejected := SQLERRM = 'SHIP_ACTIVE_SECTOR_MISMATCH:2';
  END;
  IF NOT rejected THEN RAISE EXCEPTION 'active sector mismatch accepted'; END IF;

  rejected := FALSE;
  BEGIN
    UPDATE ships SET destroyed = TRUE WHERE ship_id = 2;
    SET CONSTRAINTS ALL IMMEDIATE;
  EXCEPTION WHEN OTHERS THEN
    rejected := SQLERRM LIKE 'SHIP_ACTIVE_DESTROYED:%';
  END;
  IF NOT rejected THEN RAISE EXCEPTION 'destroyed active ship accepted'; END IF;

  rejected := FALSE;
  BEGIN
    UPDATE ships SET towing_ship_id = 3 WHERE ship_id = 2;
    SET CONSTRAINTS ALL IMMEDIATE;
  EXCEPTION WHEN OTHERS THEN
    rejected := SQLERRM LIKE 'SHIP_TOW_NOT_RECIPROCAL:%';
  END;
  IF NOT rejected THEN RAISE EXCEPTION 'one-sided tow link accepted'; END IF;

  SELECT * INTO r FROM ship_lifecycle_composite_snapshot;
  IF r.violation_count <> 0
     OR (SELECT sector_id FROM ships WHERE ship_id = 2) <> 8
     OR (SELECT destroyed FROM ships WHERE ship_id = 2) IS TRUE
     OR (SELECT towing_ship_id FROM ships WHERE ship_id = 2) <> 0 THEN
    RAISE EXCEPTION 'rejected lifecycle transition left drift: %', r;
  END IF;
END
$test$;

ROLLBACK;
\echo 'PostgreSQL ship lifecycle production contract checks passed.'
