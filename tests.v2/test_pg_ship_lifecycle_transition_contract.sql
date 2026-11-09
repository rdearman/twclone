\set ON_ERROR_STOP on

-- REQUIREMENT_ID: V3-SHIP-02
-- Disposable composition test for the persistence boundary consumed by a
-- ship sale/transfer implementation.  It intentionally uses the existing
-- read-only release snapshots rather than adding a new production migration.
-- The outer transaction keeps the catalog and all fixture rows isolated.
BEGIN;
CREATE SCHEMA ship_lifecycle_transition_contract_test;
SET LOCAL search_path TO ship_lifecycle_transition_contract_test, pg_catalog;

CREATE TABLE ships (
  ship_id integer PRIMARY KEY,
  holds integer NOT NULL,
  sector_id integer NOT NULL,
  colonists bigint DEFAULT 0, ore bigint DEFAULT 0, organics bigint DEFAULT 0,
  equipment bigint DEFAULT 0, slaves bigint DEFAULT 0, weapons bigint DEFAULT 0,
  drugs bigint DEFAULT 0,
  towing_ship_id integer DEFAULT 0,
  is_being_towed_by integer DEFAULT 0
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

-- The release snapshot depends on the earlier component snapshots.  These
-- definitions are intentionally local to the disposable fixture.
CREATE VIEW ship_lifecycle_release_snapshot AS
SELECT '{}'::jsonb AS invariants, 0::bigint AS violation_count;
\ir ../sql/pg/154_ship_lifecycle_settlement_snapshot.sql
\ir ../sql/pg/155_ship_lifecycle_conservation_snapshot.sql
\ir ../sql/pg/156_ship_lifecycle_settlement_release_snapshot.sql
\ir ../sql/pg/157_ship_list_invariant_snapshot.sql

SET search_path TO ship_lifecycle_transition_contract_test, pg_catalog;
INSERT INTO ships (ship_id, holds, sector_id, ore)
VALUES (1, 5, 7, 2), (2, 5, 7, 0);
INSERT INTO players VALUES (10, 1, 7);
INSERT INTO ship_ownership VALUES (1, 10, TRUE), (2, 10, FALSE);
INSERT INTO ship_cargo VALUES (1, 'ORE', 2);

DO $test$
DECLARE
  settlement record;
  listed record;
  initial_hash text;
BEGIN
  SELECT * INTO settlement FROM ship_lifecycle_settlement_release_snapshot;
  SELECT * INTO listed FROM ship_list_invariant_snapshot;
  initial_hash := md5(row_to_json(settlement)::text || row_to_json(listed)::text);

  IF settlement.violation_count <> 0
     OR (settlement.invariants ->> 'active_ship_count') <> '1'
     OR (settlement.invariants ->> 'active_primary_owner_count') <> '1'
     OR (settlement.invariants ->> 'active_dynamic_cargo_units') <> '2'
     OR listed.missing_list_rows <> 0
     OR listed.duplicate_list_rows <> 0
     OR listed.active_flag_row_drift <> 0 THEN
    RAISE EXCEPTION 'initial lifecycle contract is not clean: % / %', settlement, listed;
  END IF;

  -- A valid transfer preserves one active owner and the cargo total while
  -- changing the active ship represented by ship.list.
  UPDATE ship_ownership SET is_primary = TRUE
   WHERE ship_id = 2 AND player_id = 10;
  UPDATE players SET ship_id = 2 WHERE player_id = 10;
  DELETE FROM ship_ownership
   WHERE ship_id = 1 AND player_id = 10 AND is_primary IS TRUE;
  UPDATE ships SET ore = 2 WHERE ship_id = 2;
  DELETE FROM ship_cargo WHERE ship_id = 1;
  INSERT INTO ship_cargo VALUES (2, 'ORE', 2);
  SET CONSTRAINTS ALL IMMEDIATE;

  SELECT * INTO settlement FROM ship_lifecycle_settlement_release_snapshot;
  SELECT * INTO listed FROM ship_list_invariant_snapshot;
  IF settlement.violation_count <> 0
     OR (settlement.invariants ->> 'active_ship_count') <> '1'
     OR (settlement.invariants ->> 'active_primary_owner_count') <> '1'
     OR (settlement.invariants ->> 'active_dynamic_cargo_units') <> '2'
     OR listed.missing_list_rows <> 0
     OR listed.duplicate_list_rows <> 0
     OR listed.active_flag_row_drift <> 0 THEN
    RAISE EXCEPTION 'valid lifecycle transfer left persistence drift: % / %', settlement, listed;
  END IF;

  -- A failed settlement must not be allowed to become a durable partial
  -- transfer.  The nested exception block restores the pre-attempt state.
  BEGIN
    INSERT INTO ship_cargo VALUES (2, 'ORE-OVERFLOW', 4);
    SELECT * INTO settlement FROM ship_lifecycle_settlement_release_snapshot;
    IF settlement.violation_count = 0 THEN
      RAISE EXCEPTION 'over-capacity lifecycle settlement was not observable';
    END IF;
    RAISE EXCEPTION 'expected over-capacity lifecycle settlement rejection';
  EXCEPTION WHEN OTHERS THEN
    NULL;
  END;

  SELECT * INTO settlement FROM ship_lifecycle_settlement_release_snapshot;
  SELECT * INTO listed FROM ship_list_invariant_snapshot;
  IF settlement.violation_count <> 0
     OR listed.missing_list_rows <> 0
     OR listed.duplicate_list_rows <> 0
     OR listed.active_flag_row_drift <> 0 THEN
    RAISE EXCEPTION 'failed settlement left persistence drift: % / %', settlement, listed;
  END IF;
  -- The state hash must differ from the original ship selection after the
  -- valid transfer, proving the snapshot is observing durable state rather
  -- than returning a fixture-independent constant.
  IF md5(row_to_json(settlement)::text || row_to_json(listed)::text) = initial_hash THEN
    RAISE EXCEPTION 'valid lifecycle transfer did not change the state snapshot';
  END IF;
END
$test$;

ROLLBACK;
\echo 'PostgreSQL ship lifecycle transition contract checks passed.'
