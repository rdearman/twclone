\set ON_ERROR_STOP on

-- V3-SHIP-02: isolated evidence checks for active lifecycle state.  The
-- temporary schema and outer transaction ensure no production objects/data are
-- changed by this test.
BEGIN;
CREATE SCHEMA ship_lifecycle_state_134_test;
SET LOCAL search_path TO ship_lifecycle_state_134_test, pg_catalog;

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

\ir ../sql/pg/134_ship_lifecycle_state_snapshot.sql
SET search_path TO ship_lifecycle_state_134_test, pg_catalog;

INSERT INTO ships VALUES (1, 7, FALSE), (2, 8, FALSE), (3, 9, FALSE);
INSERT INTO players VALUES (10, 1, 7), (11, 2, 8);
INSERT INTO ship_ownership VALUES (1, 10, TRUE), (2, 11, TRUE);

DO $test$
DECLARE r record;
BEGIN
  SELECT * INTO r FROM ship_lifecycle_state_snapshot;
  IF r.duplicate_active_ship_references <> 0
     OR r.orphan_active_ship_references <> 0
     OR r.destroyed_active_ship_references <> 0
     OR r.active_sector_mismatches <> 0
     OR r.active_without_primary_owner <> 0
     OR r.duplicate_primary_owners <> 0
     OR r.orphan_primary_owners <> 0 THEN
    RAISE EXCEPTION 'clean lifecycle state snapshot reported violations: %', r;
  END IF;

  -- Inject one independently observable violation in each snapshot column.
  INSERT INTO players VALUES (12, 1, 7);
  INSERT INTO players VALUES (13, 404, 7);
  UPDATE ships SET destroyed = TRUE WHERE ship_id = 2;
  UPDATE players SET sector_id = 9 WHERE player_id = 10;
  DELETE FROM ship_ownership WHERE ship_id = 1 AND player_id = 10;
  INSERT INTO ship_ownership VALUES (2, 10, TRUE), (3, 404, TRUE);

  SELECT * INTO r FROM ship_lifecycle_state_snapshot;
  IF r.duplicate_active_ship_references <> 1
     OR r.orphan_active_ship_references <> 1
     OR r.destroyed_active_ship_references <> 1
     OR r.active_sector_mismatches <> 1
     OR r.active_without_primary_owner <> 1
     OR r.duplicate_primary_owners <> 1
     OR r.orphan_primary_owners <> 1 THEN
    RAISE EXCEPTION 'injected lifecycle state drift was not fully reported: %', r;
  END IF;

  -- Remove the injected drift and prove the same snapshot is restart-safe and
  -- returns to the zero-violation state expected by lifecycle acceptance.
  DELETE FROM players WHERE player_id IN (12, 13);
  UPDATE ships SET destroyed = FALSE WHERE ship_id = 2;
  UPDATE players SET sector_id = 7 WHERE player_id = 10;
  INSERT INTO ship_ownership VALUES (1, 10, TRUE);
  DELETE FROM ship_ownership WHERE ship_id = 2 AND player_id = 10;
  DELETE FROM ship_ownership WHERE ship_id = 3 AND player_id = 404;

  SELECT * INTO r FROM ship_lifecycle_state_snapshot;
  IF r.duplicate_active_ship_references <> 0
     OR r.orphan_active_ship_references <> 0
     OR r.destroyed_active_ship_references <> 0
     OR r.active_sector_mismatches <> 0
     OR r.active_without_primary_owner <> 0
     OR r.duplicate_primary_owners <> 0
     OR r.orphan_primary_owners <> 0 THEN
    RAISE EXCEPTION 'restored lifecycle state snapshot reported violations: %', r;
  END IF;
END
$test$;

ROLLBACK;
\echo 'PostgreSQL ship lifecycle state snapshot checks passed.'
