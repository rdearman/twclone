\set ON_ERROR_STOP on
BEGIN;
CREATE SCHEMA ship_active_reference_132_test;
SET LOCAL search_path TO ship_active_reference_132_test, pg_catalog;

CREATE TABLE ships (
  ship_id integer PRIMARY KEY
);
CREATE TABLE players (
  player_id integer PRIMARY KEY,
  ship_id integer,
  sector_id integer
);

-- Seed a legacy orphan before installing the NOT VALID production guard.
INSERT INTO players VALUES (1, 99, 7);
INSERT INTO ships VALUES (7);

\ir ../sql/pg/132_ship_active_reference_invariants.sql
SET search_path TO ship_active_reference_132_test, pg_catalog;

DO $test$
DECLARE r record;
BEGIN
  SELECT * INTO r FROM ship_active_reference_invariant_snapshot;
  IF r.orphan_active_ship_references <> 1 THEN
    RAISE EXCEPTION 'legacy orphan reference was not visible: %', r;
  END IF;

  BEGIN
    INSERT INTO players VALUES (2, 404, 7);
    RAISE EXCEPTION 'new orphan ship reference accepted';
  EXCEPTION WHEN foreign_key_violation THEN
    NULL;
  END;

  INSERT INTO players VALUES (3, 7, 7);
  UPDATE players SET ship_id = NULL WHERE player_id = 1;

  SELECT * INTO r FROM ship_active_reference_invariant_snapshot;
  IF r.orphan_active_ship_references <> 0 THEN
    RAISE EXCEPTION 'clean active reference snapshot reported violations: %', r;
  END IF;
END
$test$;

ROLLBACK;
\echo 'PostgreSQL ship active-reference invariant checks passed.'
