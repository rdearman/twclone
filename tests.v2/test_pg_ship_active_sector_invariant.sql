\set ON_ERROR_STOP on

-- V3-SHIP-02: active player/ship sector changes must be atomic.
BEGIN;
CREATE SCHEMA ship_active_sector_145_test;
SET LOCAL search_path TO ship_active_sector_145_test, pg_catalog;

CREATE TABLE ships (
  ship_id integer PRIMARY KEY,
  sector_id integer
);
CREATE TABLE players (
  player_id integer PRIMARY KEY,
  ship_id integer,
  sector_id integer
);

INSERT INTO ships VALUES (1, 7), (2, 7);
INSERT INTO players VALUES (10, 1, 7), (20, 2, 7);

\ir ../sql/pg/145_ship_active_sector_invariant.sql
SET search_path TO ship_active_sector_145_test, pg_catalog;

DO $test$
DECLARE
  r record;
  rejected boolean;
BEGIN
  SELECT * INTO r FROM ship_active_sector_invariant_snapshot;
  IF r.active_sector_mismatches <> 0 THEN
    RAISE EXCEPTION 'clean sector snapshot reported violations: %', r;
  END IF;

  rejected := FALSE;
  BEGIN
    UPDATE players SET sector_id = 8 WHERE player_id = 10;
    SET CONSTRAINTS ALL IMMEDIATE;
  EXCEPTION WHEN check_violation THEN
    rejected := SQLERRM = 'SHIP_ACTIVE_SECTOR_MISMATCH:1';
  END;
  IF NOT rejected THEN
    RAISE EXCEPTION 'player-only sector move was accepted';
  END IF;

  -- A valid movement transaction can update both sides in either order.
  UPDATE players SET sector_id = 8 WHERE player_id = 10;
  UPDATE ships SET sector_id = 8 WHERE ship_id = 1;
  SET CONSTRAINTS ALL IMMEDIATE;

  UPDATE ships SET sector_id = 9 WHERE ship_id = 2;
  UPDATE players SET sector_id = 9 WHERE player_id = 20;
  SET CONSTRAINTS ALL IMMEDIATE;

  SELECT * INTO r FROM ship_active_sector_invariant_snapshot;
  IF r.active_sector_mismatches <> 0 THEN
    RAISE EXCEPTION 'coordinated sector movement left drift: %', r;
  END IF;
END
$test$;

ROLLBACK;
\echo 'PostgreSQL ship active-sector invariant checks passed.'
