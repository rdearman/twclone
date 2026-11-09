\set ON_ERROR_STOP on

-- V3-SHIP-02: a failed lifecycle handoff must leave every persistence surface
-- at its pre-operation snapshot.  The temporary schema and outer transaction
-- keep this test isolated from the production catalog.
BEGIN;
CREATE SCHEMA ship_lifecycle_rollback_test;
SET LOCAL search_path TO ship_lifecycle_rollback_test, pg_catalog;

CREATE TABLE ships (
  ship_id integer PRIMARY KEY,
  holds integer,
  mines bigint, limpets bigint, fighters integer, genesis bigint,
  detonators bigint, probes bigint, photons bigint,
  shields integer, installed_shields integer, beacons bigint,
  colonists bigint, equipment bigint, organics bigint, ore bigint,
  slaves bigint, weapons bigint, drugs bigint, cloaking_devices bigint,
  hull bigint, sector_id integer
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
  quantity bigint CHECK (quantity >= 0)
);

\ir ../sql/pg/127_ship_lifecycle_invariants.sql
\ir ../sql/pg/128_ship_relationship_invariants.sql
\ir ../sql/pg/131_ship_cargo_invariant_snapshot.sql
\ir ../sql/pg/132_ship_active_reference_invariants.sql
SET search_path TO ship_lifecycle_rollback_test, pg_catalog;

INSERT INTO ships VALUES
  (1, 4, 0, 0, 1, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 100, 7),
  (2, 6, 0, 0, 1, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 100, 8);
INSERT INTO players VALUES (10, 1, 7);
INSERT INTO ship_ownership VALUES (1, 10, TRUE);
INSERT INTO ship_cargo VALUES (1, 'ORE', 2);

DO $test$
DECLARE
  lifecycle record;
  cargo record;
  active_ref record;
  rejected boolean := FALSE;
BEGIN
  BEGIN
    -- Model a transfer that updates ownership/reference and then fails while
    -- settling cargo.  All preceding writes must roll back as one unit.
    INSERT INTO ship_ownership VALUES (2, 10, TRUE);
    UPDATE players SET ship_id = 2, sector_id = 8 WHERE player_id = 10;
    INSERT INTO ship_cargo VALUES (2, 'ORE', 7);
    SET CONSTRAINTS ALL IMMEDIATE;
  EXCEPTION WHEN OTHERS THEN
    rejected := SQLERRM = 'SHIP_CARGO_EXCEEDS_HOLDS';
  END;

  IF NOT rejected THEN
    RAISE EXCEPTION 'over-capacity lifecycle handoff was accepted';
  END IF;

  SELECT * INTO lifecycle FROM ship_lifecycle_invariant_snapshot;
  SELECT * INTO cargo FROM ship_cargo_invariant_snapshot;
  SELECT * INTO active_ref FROM ship_active_reference_invariant_snapshot;

  IF (SELECT ship_id FROM players WHERE player_id = 10) <> 1
     OR (SELECT sector_id FROM players WHERE player_id = 10) <> 7
     OR NOT EXISTS (
       SELECT 1 FROM ship_ownership
        WHERE ship_id = 1 AND player_id = 10 AND is_primary
     )
     OR EXISTS (SELECT 1 FROM ship_ownership WHERE ship_id = 2)
     OR EXISTS (SELECT 1 FROM ship_cargo WHERE ship_id = 2)
     OR lifecycle.resource_violations <> 0
     OR lifecycle.dynamic_cargo_violations <> 0
     OR lifecycle.active_without_primary_owner <> 0
     OR lifecycle.active_sector_mismatches <> 0
     OR cargo.negative_cargo_quantities <> 0
     OR cargo.over_capacity_ships <> 0
     OR cargo.legacy_mirror_mismatches <> 0
     OR active_ref.orphan_active_ship_references <> 0 THEN
    RAISE EXCEPTION 'failed lifecycle handoff left persistence drift';
  END IF;
END
$test$;

ROLLBACK;
\echo 'PostgreSQL ship lifecycle rollback snapshot checks passed.'
