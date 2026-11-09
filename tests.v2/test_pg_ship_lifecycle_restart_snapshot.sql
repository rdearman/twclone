\set ON_ERROR_STOP on

-- V3-SHIP-02: the lifecycle invariant snapshot is the persistence contract
-- used after a restart.  This test deliberately uses a production-shaped
-- isolated schema and commits once before reading the snapshot again.
BEGIN;
CREATE SCHEMA ship_lifecycle_restart_snapshot_test;
SET LOCAL search_path TO ship_lifecycle_restart_snapshot_test, pg_catalog;

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

SET search_path TO ship_lifecycle_restart_snapshot_test, pg_catalog;
INSERT INTO ships (
  ship_id, holds, mines, limpets, fighters, genesis, detonators, probes,
  photons, shields, installed_shields, beacons, colonists, equipment,
  organics, ore, slaves, weapons, drugs, cloaking_devices, hull, sector_id
) VALUES (1, 4, 0, 0, 1, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 2, 0, 0,
          0, 0, 100, 7),
         (2, 4, 0, 0, 1, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0,
          0, 0, 100, 8);
INSERT INTO players VALUES (10, 1, 7);
INSERT INTO ship_ownership VALUES (1, 10, 1, TRUE);
INSERT INTO ship_cargo VALUES (1, 'ORE', 2);
SET CONSTRAINTS ALL IMMEDIATE;

DO $test$
DECLARE r record;
BEGIN
  SELECT * INTO r FROM ship_lifecycle_composite_snapshot;
  IF r.violation_count <> 0
     OR (SELECT count(*) FROM jsonb_each_text(r.invariants) WHERE value <> '0') <> 0 THEN
    RAISE EXCEPTION 'clean lifecycle snapshot reported violations: %', r;
  END IF;

  -- A failed active-ship handoff must not leave ownership, location, or
  -- cargo state behind when the target ship cannot accept its load.
  BEGIN
    UPDATE players SET ship_id = 2, sector_id = 8 WHERE player_id = 10;
    UPDATE ships SET sector_id = 8 WHERE ship_id = 2;
    INSERT INTO ship_ownership VALUES (2, 10, 1, TRUE);
    INSERT INTO ship_cargo VALUES (2, 'ORE', 5);
    SET CONSTRAINTS ALL IMMEDIATE;
    RAISE EXCEPTION 'over-capacity handoff was accepted';
  EXCEPTION WHEN OTHERS THEN
    IF SQLERRM NOT LIKE 'SHIP_CARGO_EXCEEDS_HOLDS%' THEN
      RAISE;
    END IF;
  END;

  IF (SELECT ship_id FROM players WHERE player_id = 10) <> 1
     OR (SELECT sector_id FROM players WHERE player_id = 10) <> 7
     OR EXISTS (SELECT 1 FROM ship_ownership WHERE ship_id = 2)
     OR EXISTS (SELECT 1 FROM ship_cargo WHERE ship_id = 2) THEN
    RAISE EXCEPTION 'failed handoff left lifecycle state behind';
  END IF;
END
$test$;

COMMIT;

-- A new transaction stands in for a fresh persistence read after restart.
BEGIN;
SET LOCAL search_path TO ship_lifecycle_restart_snapshot_test, pg_catalog;
DO $test$
DECLARE r record;
BEGIN
  SELECT * INTO r FROM ship_lifecycle_composite_snapshot;
  IF r.violation_count <> 0
     OR (SELECT count(*) FROM jsonb_each_text(r.invariants) WHERE value <> '0') <> 0 THEN
    RAISE EXCEPTION 'post-commit lifecycle snapshot reported violations: %', r;
  END IF;
END
$test$;
ROLLBACK;
\echo 'PostgreSQL ship lifecycle restart snapshot checks passed.'
