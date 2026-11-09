\set ON_ERROR_STOP on

-- V3-SHIP-02: active references and primary ownership must transition
-- atomically.  The temporary schema and outer transaction isolate the test.
BEGIN;
CREATE SCHEMA ship_active_owner_137_test;
SET LOCAL search_path TO ship_active_owner_137_test, pg_catalog;

CREATE TABLE players (
  player_id integer PRIMARY KEY,
  ship_id integer
);
CREATE TABLE ship_ownership (
  ship_id integer,
  player_id integer,
  is_primary boolean
);

\ir ../sql/pg/137_ship_active_owner_invariant.sql
SET search_path TO ship_active_owner_137_test, pg_catalog;

INSERT INTO players VALUES (10, 1), (20, 2);
INSERT INTO ship_ownership VALUES (1, 10, TRUE), (2, 20, TRUE);
SET CONSTRAINTS ALL IMMEDIATE;

DO $test$
DECLARE
  r record;
  rejected boolean;
BEGIN
  SELECT * INTO r FROM ship_active_owner_invariant_snapshot;
  IF r.active_owner_mismatches <> 0 THEN
    RAISE EXCEPTION 'clean active-owner snapshot reported violations: %', r;
  END IF;

  rejected := FALSE;
  BEGIN
    -- A reference to another player's primary ship is not a valid lifecycle
    -- state and must be rejected at the deferred constraint boundary.
    UPDATE players SET ship_id = 2 WHERE player_id = 10;
    SET CONSTRAINTS ALL IMMEDIATE;
  EXCEPTION WHEN OTHERS THEN
    rejected := SQLERRM = 'SHIP_ACTIVE_OWNER_MISMATCH:2';
  END;
  IF NOT rejected THEN
    RAISE EXCEPTION 'active-owner mismatch was accepted';
  END IF;

  SELECT * INTO r FROM ship_active_owner_invariant_snapshot;
  IF r.active_owner_mismatches <> 0
     OR (SELECT ship_id FROM players WHERE player_id = 10) <> 1 THEN
    RAISE EXCEPTION 'rejected ownership transition left lifecycle drift';
  END IF;

  -- A coordinated handoff is valid when the primary ownership and active
  -- reference change in the same transaction.
  INSERT INTO ship_ownership VALUES (1, 20, TRUE);
  UPDATE players SET ship_id = 1 WHERE player_id = 20;
  DELETE FROM ship_ownership
   WHERE ship_id = 2 AND player_id = 20 AND is_primary = TRUE;
  UPDATE players SET ship_id = NULL WHERE player_id = 10;
  DELETE FROM ship_ownership
   WHERE ship_id = 1 AND player_id = 10 AND is_primary = TRUE;
  SET CONSTRAINTS ALL IMMEDIATE;

  SELECT * INTO r FROM ship_active_owner_invariant_snapshot;
  IF r.active_owner_mismatches <> 0
     OR (SELECT ship_id FROM players WHERE player_id = 20) <> 1
     OR EXISTS (
       SELECT 1 FROM ship_ownership
        WHERE ship_id = 1 AND player_id = 10 AND is_primary
     ) THEN
    RAISE EXCEPTION 'valid ownership handoff left lifecycle drift';
  END IF;
END
$test$;

ROLLBACK;
\echo 'PostgreSQL active-owner lifecycle invariant checks passed.'
