\set ON_ERROR_STOP on

-- V3-SHIP-02: exercise the complete persistence invariant bundle in one
-- isolated PostgreSQL catalog.  This covers the lifecycle transition that
-- sale/transfer handlers must compose, without touching a live database.
BEGIN;
CREATE SCHEMA ship_lifecycle_bundle_test;
SET LOCAL search_path TO ship_lifecycle_bundle_test, pg_catalog;

CREATE TABLE ships (
  ship_id integer PRIMARY KEY,
  holds integer, mines bigint, limpets bigint, fighters integer,
  genesis bigint, detonators bigint, probes bigint, photons bigint,
  shields integer, installed_shields integer, beacons bigint,
  colonists bigint, equipment bigint, organics bigint, ore bigint,
  slaves bigint, weapons bigint, drugs bigint, cloaking_devices bigint,
  hull bigint, sector_id integer, destroyed boolean NOT NULL DEFAULT FALSE
);
CREATE TABLE players (
  player_id integer PRIMARY KEY, ship_id integer, sector_id integer
);
CREATE TABLE ship_ownership (
  ship_id integer, player_id integer, role_id bigint,
  is_primary boolean, acquired_at timestamptz DEFAULT CURRENT_TIMESTAMP
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

SET search_path TO ship_lifecycle_bundle_test, pg_catalog;
INSERT INTO ships (
  ship_id, holds, mines, limpets, fighters, genesis, detonators, probes,
  photons, shields, installed_shields, beacons, colonists, equipment,
  organics, ore, slaves, weapons, drugs, cloaking_devices, hull, sector_id
) VALUES
  (1, 4, 0, 0, 1, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 2, 0, 0, 0, 0, 100, 7);
INSERT INTO players VALUES (10, 1, 7);
INSERT INTO ship_ownership VALUES (1, 10, 1, TRUE);
INSERT INTO ship_cargo VALUES (1, 'ORE', 2);
SET CONSTRAINTS ALL IMMEDIATE;

DO $test$
DECLARE
  snapshot record;
  rejected boolean;
BEGIN
  SELECT * INTO snapshot FROM ship_lifecycle_acceptance_snapshot;
  IF EXISTS (SELECT 1 FROM jsonb_each_text(to_jsonb(snapshot)) WHERE value <> '0') THEN
    RAISE EXCEPTION 'valid initial lifecycle state is not clean: %', snapshot;
  END IF;

  -- A failed transfer must roll back ownership, active reference and cargo as
  -- one unit when the destination ship would exceed its holds.
  rejected := FALSE;
  BEGIN
    INSERT INTO ships (
      ship_id, holds, mines, limpets, fighters, genesis, detonators, probes,
      photons, shields, installed_shields, beacons, colonists, equipment,
      organics, ore, slaves, weapons, drugs, cloaking_devices, hull, sector_id
    ) VALUES (2, 6, 0, 0, 1, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 100, 8);
    INSERT INTO ship_ownership VALUES (2, 10, 1, TRUE);
    UPDATE players SET ship_id = 2, sector_id = 8 WHERE player_id = 10;
    INSERT INTO ship_cargo VALUES (2, 'ORE', 7);
    SET CONSTRAINTS ALL IMMEDIATE;
  EXCEPTION WHEN OTHERS THEN
    rejected := SQLERRM = 'SHIP_CARGO_EXCEEDS_HOLDS';
  END;
  IF NOT rejected THEN
    RAISE EXCEPTION 'over-capacity lifecycle transfer was accepted';
  END IF;

  IF (SELECT ship_id FROM players WHERE player_id = 10) <> 1
     OR EXISTS (SELECT 1 FROM ship_ownership WHERE ship_id = 2)
     OR EXISTS (SELECT 1 FROM ship_cargo WHERE ship_id = 2) THEN
    RAISE EXCEPTION 'failed transfer left lifecycle writes behind';
  END IF;

  rejected := FALSE;
  BEGIN
    UPDATE ships SET destroyed = TRUE WHERE ship_id = 1;
    SET CONSTRAINTS ALL IMMEDIATE;
  EXCEPTION WHEN OTHERS THEN
    rejected := SQLERRM = 'SHIP_ACTIVE_DESTROYED:1';
  END;
  IF NOT rejected OR (SELECT destroyed FROM ships WHERE ship_id = 1) THEN
    RAISE EXCEPTION 'active ship destruction was not rejected atomically';
  END IF;

  -- A coordinated handoff permits destruction of the former active ship.
  INSERT INTO ships (
    ship_id, holds, mines, limpets, fighters, genesis, detonators, probes,
    photons, shields, installed_shields, beacons, colonists, equipment,
    organics, ore, slaves, weapons, drugs, cloaking_devices, hull, sector_id
  ) VALUES (2, 6, 0, 0, 1, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 100, 8);
  INSERT INTO ship_ownership VALUES (2, 10, 1, TRUE);
  UPDATE players SET ship_id = 2, sector_id = 8 WHERE player_id = 10;
  UPDATE ships SET destroyed = TRUE WHERE ship_id = 1;
  DELETE FROM ship_ownership WHERE ship_id = 1 AND player_id = 10;
  SET CONSTRAINTS ALL IMMEDIATE;

  SELECT * INTO snapshot FROM ship_lifecycle_acceptance_snapshot;
  IF EXISTS (SELECT 1 FROM jsonb_each_text(to_jsonb(snapshot)) WHERE value <> '0') THEN
    RAISE EXCEPTION 'valid handoff lifecycle state is not clean: %', snapshot;
  END IF;
END
$test$;

ROLLBACK;
\echo 'PostgreSQL ship lifecycle bundle checks passed.'
