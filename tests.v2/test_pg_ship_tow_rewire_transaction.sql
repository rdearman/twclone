\set ON_ERROR_STOP on

-- V3-SHIP-02: a deferred tow guard must validate the final transaction state,
-- not stale endpoint values captured by an earlier queued trigger event.
BEGIN;
CREATE SCHEMA ship_tow_rewire_transaction_test;
SET LOCAL search_path TO ship_tow_rewire_transaction_test, pg_catalog;

CREATE TABLE ships (
  ship_id integer PRIMARY KEY,
  towing_ship_id integer NOT NULL DEFAULT 0,
  is_being_towed_by integer NOT NULL DEFAULT 0
);

\ir ../sql/pg/143_ship_tow_invariants.sql
SET search_path TO ship_tow_rewire_transaction_test, pg_catalog;

INSERT INTO ships (ship_id, towing_ship_id, is_being_towed_by)
VALUES (1, 2, 0), (2, 0, 1), (3, 0, 0);
SET CONSTRAINTS ALL IMMEDIATE;

DO $test$
DECLARE
  r record;
BEGIN
  -- Rewire ship 1 from ship 2 to ship 3 in one transaction.  The old
  -- reciprocal edge is detached before the new edge is attached, so queued
  -- trigger events contain transient endpoint IDs.
  UPDATE ships SET towing_ship_id = 0 WHERE ship_id = 1;
  UPDATE ships SET is_being_towed_by = 0 WHERE ship_id = 2;
  UPDATE ships SET towing_ship_id = 3 WHERE ship_id = 1;
  UPDATE ships SET is_being_towed_by = 1 WHERE ship_id = 3;
  SET CONSTRAINTS ALL IMMEDIATE;

  SELECT * INTO r FROM ship_tow_invariant_snapshot;
  IF r.self_tow_links <> 0 OR r.orphan_tow_targets <> 0
     OR r.orphan_tow_sources <> 0 OR r.nonreciprocal_tow_links <> 0
     OR (SELECT towing_ship_id FROM ships WHERE ship_id = 1) <> 3
     OR (SELECT is_being_towed_by FROM ships WHERE ship_id = 3) <> 1
     OR (SELECT is_being_towed_by FROM ships WHERE ship_id = 2) <> 0 THEN
    RAISE EXCEPTION 'valid tow rewire left lifecycle drift: %', r;
  END IF;
END
$test$;

ROLLBACK;
\echo 'PostgreSQL ship tow rewire transaction checks passed.'
