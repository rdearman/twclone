\set ON_ERROR_STOP on

-- REQUIREMENT_ID: V3-SHIP-02
-- Release-boundary proof for the complete PostgreSQL lifecycle evidence set.
-- The schema is disposable and the outer transaction is rolled back; this
-- test does not run or alter a shared database.
BEGIN;
CREATE SCHEMA ship_lifecycle_release_contract_test;
SET LOCAL search_path TO ship_lifecycle_release_contract_test, pg_catalog;

CREATE TABLE ships (
  ship_id integer PRIMARY KEY,
  holds integer,
  mines bigint, limpets bigint, fighters integer, genesis bigint,
  detonators bigint, probes bigint, photons bigint, shields integer,
  installed_shields integer, beacons bigint, colonists bigint,
  equipment bigint, organics bigint, ore bigint, slaves bigint,
  weapons bigint, drugs bigint, cloaking_devices bigint, hull bigint,
  sector_id integer,
  destroyed boolean NOT NULL DEFAULT FALSE,
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
\ir ../sql/pg/146_ship_tow_delete_invariant.sql
\ir ../sql/pg/147_ship_lifecycle_current_snapshot.sql
\ir ../sql/pg/148_ship_primary_owner_semantics_snapshot.sql
\ir ../sql/pg/150_ship_lifecycle_release_snapshot.sql
\ir ../sql/pg/151_ship_lifecycle_totals_snapshot.sql
\ir ../sql/pg/154_ship_lifecycle_settlement_snapshot.sql
\ir ../sql/pg/155_ship_lifecycle_conservation_snapshot.sql
\ir ../sql/pg/156_ship_lifecycle_settlement_release_snapshot.sql
\ir ../sql/pg/157_ship_list_invariant_snapshot.sql

SET search_path TO ship_lifecycle_release_contract_test, pg_catalog;
INSERT INTO ships (
  ship_id, holds, mines, limpets, fighters, genesis, detonators, probes,
  photons, shields, installed_shields, beacons, colonists, equipment,
  organics, ore, slaves, weapons, drugs, cloaking_devices, hull, sector_id
) VALUES
  (1, 8, 0, 0, 1, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 3, 0, 0, 0, 0,
   100, 7),
  (2, 8, 0, 0, 1, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0,
   100, 7);
INSERT INTO players VALUES (10, 1, 7), (20, NULL, 7);
INSERT INTO ship_ownership VALUES (1, 10, 1, TRUE), (2, 20, 1, TRUE);
INSERT INTO ship_cargo VALUES (1, 'ORE', 3);
SET CONSTRAINTS ALL IMMEDIATE;

DO $test$
DECLARE
  release_snapshot record;
  settlement_snapshot record;
  list_snapshot record;
  rejected boolean := FALSE;
BEGIN
  SELECT * INTO release_snapshot FROM ship_lifecycle_release_snapshot;
  IF release_snapshot.violation_count <> 0
     OR (SELECT count(*) FROM jsonb_each_text(release_snapshot.invariants)
         WHERE value <> '0') <> 0 THEN
    RAISE EXCEPTION 'clean release snapshot reported violations: %', release_snapshot;
  END IF;

  SELECT * INTO settlement_snapshot
    FROM ship_lifecycle_settlement_release_snapshot;
  IF settlement_snapshot.violation_count <> 0
     OR (settlement_snapshot.invariants ->> 'active_ship_count') <> '1'
     OR (settlement_snapshot.invariants ->> 'active_primary_owner_count') <> '1'
     OR (settlement_snapshot.invariants ->> 'active_dynamic_cargo_units') <> '3' THEN
    RAISE EXCEPTION 'clean settlement snapshot reported drift: %', settlement_snapshot;
  END IF;

  SELECT * INTO list_snapshot FROM ship_list_invariant_snapshot;
  IF list_snapshot.primary_ownership_rows <> 2
     OR list_snapshot.distinct_list_ships <> 2
     OR list_snapshot.active_list_rows <> 1
     OR list_snapshot.inactive_list_rows <> 1
     OR list_snapshot.missing_list_rows <> 0
     OR list_snapshot.duplicate_list_rows <> 0
     OR list_snapshot.active_flag_row_drift <> 0
     OR list_snapshot.distinct_ship_drift <> 0 THEN
    RAISE EXCEPTION 'clean ship.list snapshot reported drift: %', list_snapshot;
  END IF;

  -- A linked ship cannot disappear without first detaching both endpoints.
  UPDATE ships SET towing_ship_id = 2 WHERE ship_id = 1;
  UPDATE ships SET is_being_towed_by = 1 WHERE ship_id = 2;
  SET CONSTRAINTS ALL IMMEDIATE;
  BEGIN
    DELETE FROM ships WHERE ship_id = 1;
    RAISE EXCEPTION 'linked ship deletion was accepted';
  EXCEPTION WHEN SQLSTATE '23514' THEN
    rejected := SQLERRM = 'SHIP_TOW_DELETE_REQUIRES_DETACH:1';
  END;
  IF NOT rejected THEN
    RAISE EXCEPTION 'linked ship deletion produced the wrong refusal';
  END IF;

  -- A real handoff may clear the tow pair and transfer the active reference.
  UPDATE ships SET towing_ship_id = 0 WHERE ship_id = 1;
  UPDATE ships SET is_being_towed_by = 0 WHERE ship_id = 2;
  -- Release the target's inactive primary owner before assigning it to the
  -- active player; the one-primary-owner guard must remain satisfied.
  DELETE FROM ship_ownership WHERE ship_id = 2 AND player_id = 20;
  INSERT INTO ship_ownership VALUES (2, 10, 1, TRUE);
  UPDATE players SET ship_id = 2 WHERE player_id = 10;
  DELETE FROM ship_ownership WHERE ship_id = 1 AND player_id = 10;
  DELETE FROM ship_cargo WHERE ship_id = 1;
  UPDATE ships SET ore = 0 WHERE ship_id = 1;
  INSERT INTO ship_cargo VALUES (2, 'ORE', 3);
  UPDATE ships SET ore = 3 WHERE ship_id = 2;
  SET CONSTRAINTS ALL IMMEDIATE;

  SELECT * INTO settlement_snapshot
    FROM ship_lifecycle_settlement_release_snapshot;
  IF settlement_snapshot.violation_count <> 0
     OR (settlement_snapshot.invariants ->> 'active_ship_count') <> '1'
     OR (settlement_snapshot.invariants ->> 'active_primary_owner_count') <> '1'
     OR (settlement_snapshot.invariants ->> 'active_dynamic_cargo_units') <> '3' THEN
    RAISE EXCEPTION 'handoff failed release conservation: %', settlement_snapshot;
  END IF;
END
$test$;

ROLLBACK;
\echo 'PostgreSQL ship lifecycle release contract checks passed.'
