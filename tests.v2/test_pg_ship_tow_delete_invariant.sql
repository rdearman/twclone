\set ON_ERROR_STOP on

-- V3-SHIP-02: deleting a linked ship must fail atomically until both tow
-- endpoints are detached.  The temporary schema and outer transaction keep
-- this isolated from the production catalog.
BEGIN;
CREATE SCHEMA ship_tow_delete_146_test;
SET LOCAL search_path TO ship_tow_delete_146_test, pg_catalog;

CREATE TABLE ships (
  ship_id integer PRIMARY KEY,
  towing_ship_id integer NOT NULL DEFAULT 0,
  is_being_towed_by integer NOT NULL DEFAULT 0
);

\ir ../sql/pg/143_ship_tow_invariants.sql
\ir ../sql/pg/146_ship_tow_delete_invariant.sql
SET search_path TO ship_tow_delete_146_test, pg_catalog;

INSERT INTO ships (ship_id) VALUES (1), (2), (3);
UPDATE ships SET towing_ship_id = 2 WHERE ship_id = 1;
UPDATE ships SET is_being_towed_by = 1 WHERE ship_id = 2;
SET CONSTRAINTS ALL IMMEDIATE;

DO $test$
DECLARE
  snapshot record;
  rejected boolean := FALSE;
BEGIN
  SELECT * INTO snapshot FROM ship_tow_delete_invariant_snapshot;
  IF snapshot.linked_ships_pending_delete <> 2 THEN
    RAISE EXCEPTION 'linked tow snapshot is incorrect: %', snapshot;
  END IF;

  BEGIN
    DELETE FROM ships WHERE ship_id = 1;
  EXCEPTION WHEN check_violation THEN
    rejected := SQLERRM = 'SHIP_TOW_DELETE_REQUIRES_DETACH:1';
  END;
  IF NOT rejected OR NOT EXISTS (SELECT 1 FROM ships WHERE ship_id = 1) THEN
    RAISE EXCEPTION 'linked ship deletion was accepted or was not rolled back';
  END IF;

  -- Detach both endpoints before deleting the former towing ship.
  UPDATE ships SET towing_ship_id = 0 WHERE ship_id = 1;
  UPDATE ships SET is_being_towed_by = 0 WHERE ship_id = 2;
  SET CONSTRAINTS ALL IMMEDIATE;
  DELETE FROM ships WHERE ship_id = 1;

  SELECT * INTO snapshot FROM ship_tow_delete_invariant_snapshot;
  IF snapshot.linked_ships_pending_delete <> 0
     OR EXISTS (SELECT 1 FROM ships WHERE ship_id = 1) THEN
    RAISE EXCEPTION 'detached ship delete left tow drift: %', snapshot;
  END IF;
END
$test$;

ROLLBACK;
\echo 'PostgreSQL ship tow delete invariant checks passed.'
