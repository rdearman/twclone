\set ON_ERROR_STOP on

-- REQUIREMENT_ID: V3-SHIP-02
-- The settlement snapshot is an isolated persistence contract for a valid
-- active-ship handoff.  It proves the handoff changes ownership/active
-- cardinality deliberately while preserving cargo and tow totals.
BEGIN;
CREATE SCHEMA ship_lifecycle_settlement_154_test;
SET LOCAL search_path TO ship_lifecycle_settlement_154_test, pg_catalog;

CREATE TABLE ships (
  ship_id integer PRIMARY KEY,
  colonists bigint NOT NULL DEFAULT 0, ore bigint NOT NULL DEFAULT 0,
  organics bigint NOT NULL DEFAULT 0, equipment bigint NOT NULL DEFAULT 0,
  slaves bigint NOT NULL DEFAULT 0, weapons bigint NOT NULL DEFAULT 0,
  drugs bigint NOT NULL DEFAULT 0,
  towing_ship_id integer NOT NULL DEFAULT 0,
  is_being_towed_by integer NOT NULL DEFAULT 0
);
CREATE TABLE players (player_id integer PRIMARY KEY, ship_id integer);
CREATE TABLE ship_ownership (
  ship_id integer NOT NULL, player_id integer NOT NULL,
  is_primary boolean NOT NULL
);
CREATE TABLE ship_cargo (
  ship_id integer NOT NULL, commodity_code text NOT NULL,
  quantity bigint NOT NULL CHECK (quantity >= 0)
);

\ir ../sql/pg/154_ship_lifecycle_settlement_snapshot.sql
SET search_path TO ship_lifecycle_settlement_154_test, pg_catalog;

INSERT INTO ships (ship_id, ore, towing_ship_id) VALUES (1, 2, 2), (2, 1);
UPDATE ships SET is_being_towed_by = 1 WHERE ship_id = 2;
INSERT INTO players VALUES (10, 1);
INSERT INTO ship_ownership VALUES (1, 10, TRUE);
INSERT INTO ship_cargo VALUES (1, 'ORE', 3), (2, 'ORG', 2);

DO $test$
DECLARE before_state record; after_state record;
BEGIN
  SELECT * INTO before_state FROM ship_lifecycle_settlement_snapshot;
  IF before_state.active_ship_count <> 1
     OR before_state.active_primary_owner_count <> 1
     OR before_state.fleet_legacy_cargo_units <> 3
     OR before_state.fleet_dynamic_cargo_units <> 5
     OR before_state.active_tow_link_count <> 1 THEN
    RAISE EXCEPTION 'unexpected pre-handoff settlement snapshot: %', before_state;
  END IF;

  -- The handoff moves the active reference and primary ownership together;
  -- Fleet cargo and tow participation are independent conserved totals.
  INSERT INTO players VALUES (11, 2);
  INSERT INTO ship_ownership VALUES (2, 11, TRUE);
  DELETE FROM ship_ownership WHERE ship_id = 1 AND player_id = 10;
  DELETE FROM players WHERE player_id = 10;

  SELECT * INTO after_state FROM ship_lifecycle_settlement_snapshot;
  IF after_state.active_ship_count <> before_state.active_ship_count
     OR after_state.active_primary_owner_count <> before_state.active_primary_owner_count
     OR after_state.fleet_legacy_cargo_units <> before_state.fleet_legacy_cargo_units
     OR after_state.fleet_dynamic_cargo_units <> before_state.fleet_dynamic_cargo_units
     OR after_state.active_tow_link_count <> before_state.active_tow_link_count THEN
    RAISE EXCEPTION 'handoff did not conserve settlement totals: before %, after %',
      before_state, after_state;
  END IF;
END
$test$;

ROLLBACK;
\echo 'PostgreSQL ship lifecycle settlement snapshot checks passed.'
