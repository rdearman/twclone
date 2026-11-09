\set ON_ERROR_STOP on

-- V3-SHIP-02: the final lifecycle evidence surface must compose the active
-- sector and tow guards, and a failed linked-ship delete must be atomic.
BEGIN;
CREATE SCHEMA ship_lifecycle_final_snapshot_test;
SET LOCAL search_path TO ship_lifecycle_final_snapshot_test, pg_catalog;

CREATE TABLE ships (
  ship_id integer PRIMARY KEY,
  holds integer, mines bigint, limpets bigint, fighters integer,
  genesis bigint, detonators bigint, probes bigint, photons bigint,
  shields integer, installed_shields integer, beacons bigint,
  colonists bigint, equipment bigint, organics bigint, ore bigint,
  slaves bigint, weapons bigint, drugs bigint, cloaking_devices bigint,
  hull bigint, sector_id integer, destroyed boolean NOT NULL DEFAULT FALSE,
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
\ir ../sql/pg/148_ship_primary_owner_semantics_snapshot.sql
\ir ../sql/pg/150_ship_lifecycle_release_snapshot.sql
\ir ../sql/pg/153_ship_primary_flag_invariant.sql

SET search_path TO ship_lifecycle_final_snapshot_test, pg_catalog;
INSERT INTO ships (
  ship_id, holds, mines, limpets, fighters, genesis, detonators, probes,
  photons, shields, installed_shields, beacons, colonists, equipment,
  organics, ore, slaves, weapons, drugs, cloaking_devices, hull, sector_id,
  towing_ship_id, is_being_towed_by
) VALUES
  (1, 8, 0, 0, 1, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0,
   100, 7, 2, 0),
  (2, 8, 0, 0, 1, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0,
   100, 7, 0, 1),
  (3, 8, 0, 0, 1, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0,
   100, 8, 0, 0);
INSERT INTO players VALUES (10, 1, 7);
INSERT INTO ship_ownership VALUES (1, 10, 1, TRUE);
SET CONSTRAINTS ALL IMMEDIATE;

DO $test$
DECLARE
  snapshot record;
  rejected boolean := FALSE;
BEGIN
  SELECT * INTO snapshot FROM ship_lifecycle_composite_snapshot;
  IF snapshot.violation_count <> 0
     OR EXISTS (SELECT 1 FROM jsonb_each_text(snapshot.invariants)
               WHERE value <> '0') THEN
    RAISE EXCEPTION 'clean final lifecycle snapshot reported violations: %', snapshot;
  END IF;

  SELECT * INTO snapshot FROM ship_lifecycle_release_snapshot;
  IF snapshot.violation_count <> 0
     OR (snapshot.invariants ->> 'owned_ships_without_primary') <> '0'
     OR (snapshot.invariants ->> 'null_primary_flags') <> '0'
     OR (snapshot.invariants ->> 'duplicate_primary_owners_latest') <> '0' THEN
    RAISE EXCEPTION 'clean release lifecycle snapshot reported violations: %', snapshot;
  END IF;

  BEGIN
    UPDATE ships SET sector_id = 8 WHERE ship_id = 1;
    SET CONSTRAINTS ALL IMMEDIATE;
  EXCEPTION WHEN check_violation THEN
    rejected := SQLERRM = 'SHIP_ACTIVE_SECTOR_MISMATCH:1';
  END;
  IF NOT rejected OR (SELECT sector_id FROM ships WHERE ship_id = 1) <> 7 THEN
    RAISE EXCEPTION 'active-sector mismatch was not rolled back';
  END IF;

  rejected := FALSE;
  BEGIN
    DELETE FROM ships WHERE ship_id = 1;
  EXCEPTION WHEN check_violation THEN
    rejected := SQLERRM = 'SHIP_TOW_DELETE_REQUIRES_DETACH:1';
  END;
  IF NOT rejected OR NOT EXISTS (SELECT 1 FROM ships WHERE ship_id = 1) THEN
    RAISE EXCEPTION 'linked ship deletion was not refused atomically';
  END IF;

  UPDATE ships SET towing_ship_id = 0 WHERE ship_id = 1;
  UPDATE ships SET is_being_towed_by = 0 WHERE ship_id = 2;
  DELETE FROM players WHERE player_id = 10;
  DELETE FROM ship_ownership WHERE ship_id = 1 AND player_id = 10;
  SET CONSTRAINTS ALL IMMEDIATE;
  DELETE FROM ships WHERE ship_id = 1;

  SELECT * INTO snapshot FROM ship_lifecycle_composite_snapshot;
  IF snapshot.violation_count <> 0
     OR EXISTS (SELECT 1 FROM jsonb_each_text(snapshot.invariants)
               WHERE value <> '0')
     OR (SELECT linked_ships_pending_delete
           FROM ship_tow_delete_invariant_snapshot) <> 0 THEN
    RAISE EXCEPTION 'detached delete left lifecycle drift: %', snapshot;
  END IF;
END
$test$;

ROLLBACK;
\echo 'PostgreSQL final ship lifecycle snapshot checks passed.'
