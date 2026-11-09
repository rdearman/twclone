\set ON_ERROR_STOP on

-- V3-SHIP-02: tow links must be reciprocal and transaction-safe.
BEGIN;
CREATE SCHEMA ship_tow_143_test;
SET LOCAL search_path TO ship_tow_143_test, pg_catalog;

CREATE TABLE ships (
  ship_id integer PRIMARY KEY,
  towing_ship_id integer NOT NULL DEFAULT 0,
  is_being_towed_by integer NOT NULL DEFAULT 0
);

\ir ../sql/pg/143_ship_tow_invariants.sql
SET search_path TO ship_tow_143_test, pg_catalog;

INSERT INTO ships (ship_id) VALUES (1), (2), (3);
SET CONSTRAINTS ALL IMMEDIATE;

DO $test$
DECLARE
  r record;
  rejected boolean;
BEGIN
  SELECT * INTO r FROM ship_tow_invariant_snapshot;
  IF r.self_tow_links <> 0 OR r.orphan_tow_targets <> 0
     OR r.orphan_tow_sources <> 0 OR r.nonreciprocal_tow_links <> 0 THEN
    RAISE EXCEPTION 'clean tow snapshot reported violations: %', r;
  END IF;

  -- Both ends may be established in one lifecycle transaction.
  UPDATE ships SET towing_ship_id = 2 WHERE ship_id = 1;
  UPDATE ships SET is_being_towed_by = 1 WHERE ship_id = 2;
  SET CONSTRAINTS ALL IMMEDIATE;

  SELECT * INTO r FROM ship_tow_invariant_snapshot;
  IF r.self_tow_links <> 0 OR r.orphan_tow_targets <> 0
     OR r.orphan_tow_sources <> 0 OR r.nonreciprocal_tow_links <> 0 THEN
    RAISE EXCEPTION 'reciprocal tow snapshot reported violations: %', r;
  END IF;

  rejected := FALSE;
  BEGIN
    UPDATE ships SET towing_ship_id = 1 WHERE ship_id = 1;
    SET CONSTRAINTS ALL IMMEDIATE;
  EXCEPTION WHEN check_violation THEN
    rejected := SQLERRM = 'SHIP_TOW_SELF:1';
  END;
  IF NOT rejected OR (SELECT towing_ship_id FROM ships WHERE ship_id = 1) <> 2 THEN
    RAISE EXCEPTION 'self tow was accepted or left state behind';
  END IF;

  rejected := FALSE;
  BEGIN
    UPDATE ships SET towing_ship_id = 404 WHERE ship_id = 1;
    SET CONSTRAINTS ALL IMMEDIATE;
  EXCEPTION WHEN foreign_key_violation THEN
    rejected := SQLERRM = 'SHIP_TOW_TARGET_NOT_FOUND:404';
  END;
  IF NOT rejected OR (SELECT towing_ship_id FROM ships WHERE ship_id = 1) <> 2 THEN
    RAISE EXCEPTION 'orphan tow was accepted or left state behind';
  END IF;

  -- Rewiring only the towed ship must not strand the old towing endpoint.
  UPDATE ships SET towing_ship_id = 2 WHERE ship_id = 3;
  UPDATE ships SET is_being_towed_by = 3 WHERE ship_id = 2;
  rejected := FALSE;
  BEGIN
    SET CONSTRAINTS ALL IMMEDIATE;
  EXCEPTION WHEN check_violation THEN
    rejected := SQLERRM = 'SHIP_TOW_NOT_RECIPROCAL:1';
  END;
  IF NOT rejected
     OR (SELECT is_being_towed_by FROM ships WHERE ship_id = 2) <> 1
     OR (SELECT towing_ship_id FROM ships WHERE ship_id = 3) <> 0 THEN
    RAISE EXCEPTION 'one-sided tow rewire was accepted or left state behind';
  END IF;

  -- A coordinated detach is valid and returns the snapshot to zero drift.
  UPDATE ships SET towing_ship_id = 0 WHERE ship_id = 1;
  UPDATE ships SET is_being_towed_by = 0 WHERE ship_id = 2;
  SET CONSTRAINTS ALL IMMEDIATE;
  SELECT * INTO r FROM ship_tow_invariant_snapshot;
  IF r.self_tow_links <> 0 OR r.orphan_tow_targets <> 0
     OR r.orphan_tow_sources <> 0 OR r.nonreciprocal_tow_links <> 0 THEN
    RAISE EXCEPTION 'detached tow snapshot reported violations: %', r;
  END IF;
END
$test$;

ROLLBACK;
\echo 'PostgreSQL ship tow invariant checks passed.'
