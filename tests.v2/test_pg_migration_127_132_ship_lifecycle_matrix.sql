\set ON_ERROR_STOP on

-- V3-SHIP-02: exercise the lifecycle guards and invariant snapshots together.
-- The temporary schema keeps this test isolated from the production catalog;
-- the outer transaction makes every fixture and migration reversible.
BEGIN;
CREATE SCHEMA ship_lifecycle_matrix_test;
SET LOCAL search_path TO ship_lifecycle_matrix_test, pg_catalog;

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
  destroyed boolean NOT NULL DEFAULT FALSE,
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

\ir ../sql/pg/126_ship_primary_ownership.sql
\ir ../sql/pg/127_ship_lifecycle_invariants.sql
\ir ../sql/pg/128_ship_relationship_invariants.sql
\ir ../sql/pg/131_ship_cargo_invariant_snapshot.sql
\ir ../sql/pg/132_ship_active_reference_invariants.sql
\ir ../sql/pg/134_ship_lifecycle_state_snapshot.sql
SET search_path TO ship_lifecycle_matrix_test, pg_catalog;

INSERT INTO ships VALUES
  (1, 4, 0, 0, 1, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 100, FALSE, 7),
  (2, 6, 0, 0, 1, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 100, FALSE, 8);
INSERT INTO players VALUES (10, 1, 7);
INSERT INTO ship_ownership VALUES (1, 10, TRUE);
INSERT INTO ship_cargo VALUES (1, 'ORE', 3);

DO $test$
DECLARE
  lifecycle record;
  relationship record;
  cargo record;
  active_ref record;
  state record;
  rejected boolean;
BEGIN
  SELECT * INTO lifecycle FROM ship_lifecycle_invariant_snapshot;
  SELECT * INTO relationship FROM ship_relationship_invariant_snapshot;
  SELECT * INTO cargo FROM ship_cargo_invariant_snapshot;
  SELECT * INTO active_ref FROM ship_active_reference_invariant_snapshot;
  SELECT * INTO state FROM ship_lifecycle_state_snapshot;
  IF lifecycle.resource_violations <> 0
     OR lifecycle.dynamic_cargo_violations <> 0
     OR lifecycle.active_without_primary_owner <> 0
     OR lifecycle.active_sector_mismatches <> 0
     OR relationship.active_without_primary_owner <> 0
     OR relationship.active_sector_mismatches <> 0
     OR cargo.negative_cargo_quantities <> 0
     OR cargo.over_capacity_ships <> 0
     OR cargo.legacy_mirror_mismatches <> 0
     OR active_ref.orphan_active_ship_references <> 0
     OR state.duplicate_active_ship_references <> 0
     OR state.orphan_active_ship_references <> 0
     OR state.destroyed_active_ship_references <> 0
     OR state.active_sector_mismatches <> 0
     OR state.active_without_primary_owner <> 0
     OR state.duplicate_primary_owners <> 0
     OR state.orphan_primary_owners <> 0 THEN
    RAISE EXCEPTION 'clean lifecycle matrix snapshot reported violations';
  END IF;

  -- A direct active-ship handoff must be coordinated with primary ownership
  -- and the player's sector; the deferred relationship guard accepts it only
  -- once all three tables agree.
  INSERT INTO ship_ownership VALUES (2, 10, TRUE);
  UPDATE players SET ship_id = 2, sector_id = 8 WHERE player_id = 10;
  DELETE FROM ship_ownership WHERE ship_id = 1 AND player_id = 10;
  SET CONSTRAINTS ALL IMMEDIATE;

  -- A capacity violation is rejected and leaves the committed snapshot clean.
  rejected := FALSE;
  BEGIN
    INSERT INTO ship_cargo VALUES (2, 'ORE', 7);
    SET CONSTRAINTS ALL IMMEDIATE;
  EXCEPTION WHEN OTHERS THEN
    rejected := SQLERRM = 'SHIP_CARGO_EXCEEDS_HOLDS';
  END;
  IF NOT rejected THEN
    RAISE EXCEPTION 'over-capacity cargo was accepted';
  END IF;

  rejected := FALSE;
  BEGIN
    INSERT INTO ship_ownership VALUES (2, 11, TRUE);
  EXCEPTION WHEN unique_violation THEN
    rejected := TRUE;
  END;
  IF NOT rejected THEN
    RAISE EXCEPTION 'duplicate primary ownership was accepted';
  END IF;

  -- Active references must be protected in both directions: a lifecycle
  -- operation cannot create an orphan player row, and it cannot delete the
  -- ship that an active player still points at.
  rejected := FALSE;
  BEGIN
    INSERT INTO players VALUES (11, 404, 8);
    RAISE EXCEPTION 'orphan active ship reference was accepted';
  EXCEPTION WHEN foreign_key_violation THEN
    rejected := TRUE;
  END;
  IF NOT rejected THEN
    RAISE EXCEPTION 'orphan active ship reference was accepted';
  END IF;

  rejected := FALSE;
  BEGIN
    DELETE FROM ships WHERE ship_id = 2;
    RAISE EXCEPTION 'referenced active ship deletion was accepted';
  EXCEPTION WHEN foreign_key_violation THEN
    rejected := TRUE;
  END;
  IF NOT rejected THEN
    RAISE EXCEPTION 'referenced active ship deletion was accepted';
  END IF;

  -- A sector mismatch must likewise roll back rather than leave an active
  -- player pointing at a ship in another sector.
  rejected := FALSE;
  BEGIN
    UPDATE ships SET sector_id = 7 WHERE ship_id = 2;
    SET CONSTRAINTS ALL IMMEDIATE;
  EXCEPTION WHEN OTHERS THEN
    rejected := SQLERRM = 'SHIP_ACTIVE_SECTOR_MISMATCH:2';
  END;
  IF NOT rejected THEN
    RAISE EXCEPTION 'active ship sector mismatch was accepted';
  END IF;

  SELECT * INTO lifecycle FROM ship_lifecycle_invariant_snapshot;
  SELECT * INTO relationship FROM ship_relationship_invariant_snapshot;
  SELECT * INTO cargo FROM ship_cargo_invariant_snapshot;
  SELECT * INTO active_ref FROM ship_active_reference_invariant_snapshot;
  SELECT * INTO state FROM ship_lifecycle_state_snapshot;
  IF lifecycle.resource_violations <> 0
     OR lifecycle.dynamic_cargo_violations <> 0
     OR lifecycle.active_without_primary_owner <> 0
     OR lifecycle.active_sector_mismatches <> 0
     OR relationship.active_without_primary_owner <> 0
     OR relationship.active_sector_mismatches <> 0
     OR cargo.negative_cargo_quantities <> 0
     OR cargo.over_capacity_ships <> 0
     OR cargo.legacy_mirror_mismatches <> 0
     OR active_ref.orphan_active_ship_references <> 0
     OR state.duplicate_active_ship_references <> 0
     OR state.orphan_active_ship_references <> 0
     OR state.destroyed_active_ship_references <> 0
     OR state.active_sector_mismatches <> 0
     OR state.active_without_primary_owner <> 0
     OR state.duplicate_primary_owners <> 0
     OR state.orphan_primary_owners <> 0
     OR (SELECT ship_id FROM players WHERE player_id = 10) <> 2
     OR (SELECT sector_id FROM players WHERE player_id = 10) <> 8 THEN
    RAISE EXCEPTION 'rejected lifecycle transition left invariant drift';
  END IF;
END
$test$;

ROLLBACK;
\echo 'PostgreSQL ship lifecycle migration matrix checks passed.'
