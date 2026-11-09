\set ON_ERROR_STOP on

-- V3-SHIP-02: a failed tow/lifecycle transition must roll back both the
-- relationship write and the invariant evidence.  This deliberately joins
-- the deferred tow guard to the composite lifecycle snapshot rather than
-- testing either surface in isolation.
BEGIN;
CREATE SCHEMA ship_lifecycle_tow_rollback_test;
SET LOCAL search_path TO ship_lifecycle_tow_rollback_test, pg_catalog;

CREATE TABLE ships (
  ship_id integer PRIMARY KEY,
  holds integer, mines bigint, limpets bigint, fighters integer,
  genesis bigint, detonators bigint, probes bigint, photons bigint,
  shields integer, installed_shields integer, beacons bigint,
  colonists bigint, equipment bigint, organics bigint, ore bigint,
  slaves bigint, weapons bigint, drugs bigint, cloaking_devices bigint,
  hull integer, sector_id integer, destroyed boolean NOT NULL DEFAULT FALSE,
  towing_ship_id integer NOT NULL DEFAULT 0,
  is_being_towed_by integer NOT NULL DEFAULT 0
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
\ir ../sql/pg/143_ship_tow_invariants.sql
\ir ../sql/pg/144_ship_lifecycle_composite_snapshot.sql

SET search_path TO ship_lifecycle_tow_rollback_test, pg_catalog;
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
   100, 7, 0, 0);
INSERT INTO players VALUES (10, 1, 7);
INSERT INTO ship_ownership VALUES (1, 10, TRUE);
SET CONSTRAINTS ALL IMMEDIATE;

DO $test$
DECLARE
  rejected boolean := FALSE;
  snapshot record;
BEGIN
  -- The new edge is persisted without its reciprocal endpoint.  Immediate
  -- validation must reject it and restore the original 1<->2 relationship.
  BEGIN
    UPDATE ships SET towing_ship_id = 3 WHERE ship_id = 1;
    SET CONSTRAINTS ALL IMMEDIATE;
  EXCEPTION WHEN check_violation THEN
    rejected := SQLERRM = 'SHIP_TOW_NOT_RECIPROCAL:1';
  END;
  IF NOT rejected
     OR (SELECT towing_ship_id FROM ships WHERE ship_id = 1) <> 2
     OR (SELECT is_being_towed_by FROM ships WHERE ship_id = 2) <> 1
     OR (SELECT towing_ship_id FROM ships WHERE ship_id = 3) <> 0 THEN
    RAISE EXCEPTION 'failed tow transition was not rolled back';
  END IF;

  -- A coordinated rewire is valid and must leave every lifecycle surface
  -- clean, including the tow fields folded into the composite snapshot.
  UPDATE ships SET towing_ship_id = 0 WHERE ship_id = 1;
  UPDATE ships SET is_being_towed_by = 0 WHERE ship_id = 2;
  UPDATE ships SET towing_ship_id = 3 WHERE ship_id = 1;
  UPDATE ships SET is_being_towed_by = 1 WHERE ship_id = 3;
  SET CONSTRAINTS ALL IMMEDIATE;

  SELECT * INTO snapshot FROM ship_lifecycle_composite_snapshot;
  IF snapshot.violation_count <> 0
     OR EXISTS (
       SELECT 1 FROM jsonb_each_text(snapshot.invariants) WHERE value <> '0'
     ) THEN
    RAISE EXCEPTION 'valid tow rewire left lifecycle violations: %', snapshot;
  END IF;
END
$test$;

ROLLBACK;
\echo 'PostgreSQL ship lifecycle tow rollback checks passed.'
