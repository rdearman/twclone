\set ON_ERROR_STOP on

-- V3-SHIP-02: isolated persistence contract for the complete lifecycle
-- snapshot.  The schema and outer transaction are disposable by design.
BEGIN;
CREATE SCHEMA ship_lifecycle_acceptance_139_test;
SET LOCAL search_path TO ship_lifecycle_acceptance_139_test, pg_catalog;

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
\ir ../sql/pg/135_ship_cargo_quantity_invariant.sql
\ir ../sql/pg/136_ship_destroyed_active_invariant.sql
\ir ../sql/pg/137_ship_active_owner_invariant.sql
\ir ../sql/pg/138_ship_ownership_invariant_snapshot.sql
\ir ../sql/pg/139_ship_lifecycle_acceptance_snapshot.sql
SET search_path TO ship_lifecycle_acceptance_139_test, pg_catalog;

INSERT INTO ships (
  ship_id, holds, mines, limpets, fighters, genesis, detonators, probes,
  photons, shields, installed_shields, beacons, colonists, equipment,
  organics, ore, slaves, weapons, drugs, cloaking_devices, hull, destroyed,
  sector_id
) VALUES
  (1, 4, 0, 0, 1, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 3, 0, 0, 0, 0, 100, FALSE, 7),
  (2, 6, 0, 0, 1, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 100, FALSE, 8);
INSERT INTO players VALUES (10, 1, 7);
INSERT INTO players (player_id, ship_id, sector_id) VALUES (11, NULL, 8);
INSERT INTO ship_ownership VALUES (1, 10, TRUE);
INSERT INTO ship_cargo VALUES (1, 'ORE', 3);

DO $test$
DECLARE
  r record;
  rejected boolean;
BEGIN
  SELECT * INTO r FROM ship_lifecycle_acceptance_snapshot;
  IF EXISTS (SELECT 1 FROM jsonb_each_text(to_jsonb(r)) WHERE value <> '0') THEN
    RAISE EXCEPTION 'clean lifecycle acceptance snapshot reported violations: %', r;
  END IF;

  -- A coordinated active-ship/owner/sector handoff is valid and must leave
  -- the same zero-violation snapshot used for restart readback.
  INSERT INTO ship_ownership VALUES (2, 10, TRUE);
  UPDATE players SET ship_id = 2, sector_id = 8 WHERE player_id = 10;
  DELETE FROM ship_ownership WHERE ship_id = 1 AND player_id = 10;
  SET CONSTRAINTS ALL IMMEDIATE;

  SELECT * INTO r FROM ship_lifecycle_acceptance_snapshot;
  IF EXISTS (SELECT 1 FROM jsonb_each_text(to_jsonb(r)) WHERE value <> '0') THEN
    RAISE EXCEPTION 'valid handoff left lifecycle violations: %', r;
  END IF;

  rejected := FALSE;
  BEGIN
    UPDATE ships SET destroyed = TRUE WHERE ship_id = 2;
    SET CONSTRAINTS ALL IMMEDIATE;
  EXCEPTION WHEN OTHERS THEN
    rejected := SQLERRM = 'SHIP_ACTIVE_DESTROYED:2';
  END;
  IF NOT rejected THEN
    RAISE EXCEPTION 'destroyed active ship was accepted';
  END IF;

  SELECT * INTO r FROM ship_lifecycle_acceptance_snapshot;
  IF EXISTS (SELECT 1 FROM jsonb_each_text(to_jsonb(r)) WHERE value <> '0')
     OR (SELECT destroyed FROM ships WHERE ship_id = 2) THEN
    RAISE EXCEPTION 'rejected lifecycle write left persisted drift: %', r;
  END IF;

  -- An active player may not keep a ship after its primary ownership is
  -- reassigned.  This is distinct from the weaker one-primary-owner index:
  -- the deferred relationship guard must reject the mismatch and preserve the
  -- previous owner row.
  rejected := FALSE;
  BEGIN
    UPDATE ship_ownership
       SET player_id = 11
     WHERE ship_id = 2 AND player_id = 10 AND is_primary;
    SET CONSTRAINTS ALL IMMEDIATE;
  EXCEPTION WHEN OTHERS THEN
    rejected := SQLERRM = 'SHIP_ACTIVE_OWNER_MISMATCH:2';
  END;
  IF NOT rejected THEN
    RAISE EXCEPTION 'active owner mismatch was accepted';
  END IF;

  SELECT * INTO r FROM ship_lifecycle_acceptance_snapshot;
  IF EXISTS (SELECT 1 FROM jsonb_each_text(to_jsonb(r)) WHERE value <> '0')
     OR NOT EXISTS (
       SELECT 1 FROM ship_ownership
        WHERE ship_id = 2 AND player_id = 10 AND is_primary
     ) THEN
    RAISE EXCEPTION 'rejected owner mismatch left lifecycle drift: %', r;
  END IF;

  -- Native resource bounds are part of the same aggregate contract, not only
  -- a diagnostic view.  A failed write must leave the snapshot unchanged.
  rejected := FALSE;
  BEGIN
    UPDATE ships SET fighters = -1 WHERE ship_id = 2;
  EXCEPTION WHEN check_violation THEN
    rejected := TRUE;
  END;
  IF NOT rejected THEN
    RAISE EXCEPTION 'negative ship resource was accepted';
  END IF;

  -- The aggregate snapshot must include ownership-table drift as well as
  -- active-reference drift; otherwise a later ship sale/transfer could look
  -- clean while retaining an orphaned ownership row.
  INSERT INTO ship_ownership VALUES (404, 10, FALSE), (2, 404, FALSE);
  SELECT * INTO r FROM ship_lifecycle_acceptance_snapshot;
  IF r.orphan_ship_ownership <> 1 OR r.orphan_player_ownership <> 1 THEN
    RAISE EXCEPTION 'aggregate snapshot omitted ownership drift: %', r;
  END IF;
END
$test$;

ROLLBACK;
\echo 'PostgreSQL ship lifecycle acceptance snapshot checks passed.'
