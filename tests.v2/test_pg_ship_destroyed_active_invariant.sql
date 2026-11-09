\set ON_ERROR_STOP on

-- V3-SHIP-02: isolated proof that destroyed-active transitions roll back and
-- the lifecycle snapshot remains clean after the rejected write.
BEGIN;
CREATE SCHEMA ship_destroyed_active_136_test;
SET LOCAL search_path TO ship_destroyed_active_136_test, pg_catalog;

CREATE TABLE ships (
  ship_id integer PRIMARY KEY,
  sector_id integer,
  destroyed boolean NOT NULL DEFAULT FALSE
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

\ir ../sql/pg/136_ship_destroyed_active_invariant.sql
\ir ../sql/pg/134_ship_lifecycle_state_snapshot.sql
SET search_path TO ship_destroyed_active_136_test, pg_catalog;

INSERT INTO ships VALUES (1, 7, FALSE), (2, 8, FALSE);
INSERT INTO players VALUES (10, 1, 7);

DO $test$
DECLARE
  r record;
  rejected boolean;
BEGIN
  SELECT * INTO r FROM ship_lifecycle_state_snapshot;
  IF r.destroyed_active_ship_references <> 0 THEN
    RAISE EXCEPTION 'clean destroyed-active snapshot reported violations';
  END IF;

  rejected := FALSE;
  BEGIN
    UPDATE ships SET destroyed = TRUE WHERE ship_id = 1;
    SET CONSTRAINTS ALL IMMEDIATE;
  EXCEPTION WHEN OTHERS THEN
    rejected := SQLERRM = 'SHIP_ACTIVE_DESTROYED:1';
  END;
  IF NOT rejected THEN
    RAISE EXCEPTION 'destroying an active ship was accepted';
  END IF;

  SELECT * INTO r FROM ship_lifecycle_state_snapshot;
  IF r.destroyed_active_ship_references <> 0
     OR (SELECT destroyed FROM ships WHERE ship_id = 1) THEN
    RAISE EXCEPTION 'rejected destruction left lifecycle drift';
  END IF;

  -- A coordinated handoff permits destruction of the former active ship.
  UPDATE players SET ship_id = 2 WHERE player_id = 10;
  UPDATE ships SET destroyed = TRUE WHERE ship_id = 1;
  SET CONSTRAINTS ALL IMMEDIATE;

  SELECT * INTO r FROM ship_lifecycle_state_snapshot;
  IF r.destroyed_active_ship_references <> 0
     OR (SELECT ship_id FROM players WHERE player_id = 10) <> 2 THEN
    RAISE EXCEPTION 'valid active-ship handoff left lifecycle drift';
  END IF;
END
$test$;

ROLLBACK;
\echo 'PostgreSQL destroyed-active ship invariant checks passed.'
