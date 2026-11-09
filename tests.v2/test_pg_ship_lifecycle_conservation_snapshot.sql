\set ON_ERROR_STOP on

-- REQUIREMENT_ID: V3-SHIP-02
-- Isolated before/after evidence for an active-ship handoff.  The outer
-- transaction leaves no schema or data behind.
BEGIN;
CREATE SCHEMA ship_lifecycle_conservation_155_test;
SET LOCAL search_path TO ship_lifecycle_conservation_155_test, pg_catalog;

CREATE TABLE ships (
  ship_id integer PRIMARY KEY,
  holds integer NOT NULL,
  colonists bigint NOT NULL DEFAULT 0, ore bigint NOT NULL DEFAULT 0,
  organics bigint NOT NULL DEFAULT 0, equipment bigint NOT NULL DEFAULT 0,
  slaves bigint NOT NULL DEFAULT 0, weapons bigint NOT NULL DEFAULT 0,
  drugs bigint NOT NULL DEFAULT 0
);
CREATE TABLE players (player_id integer PRIMARY KEY, ship_id integer);
CREATE TABLE ship_ownership (
  ship_id integer NOT NULL, player_id integer NOT NULL, is_primary boolean
);
CREATE TABLE ship_cargo (
  ship_id integer NOT NULL, commodity_code text NOT NULL, quantity bigint NOT NULL
);

\ir ../sql/pg/155_ship_lifecycle_conservation_snapshot.sql
SET search_path TO ship_lifecycle_conservation_155_test, pg_catalog;

INSERT INTO ships (ship_id, holds, ore) VALUES (1, 5, 2), (2, 7, 0);
INSERT INTO players VALUES (10, 1);
INSERT INTO ship_ownership VALUES (1, 10, TRUE);
INSERT INTO ship_cargo VALUES (1, 'ORE', 2), (2, 'ORG', 3);

DO $test$
DECLARE before_state record; after_state record;
BEGIN
  SELECT * INTO before_state FROM ship_lifecycle_conservation_snapshot;
  IF before_state.active_ship_count <> 1
     OR before_state.active_primary_owner_count <> 1
     OR before_state.active_legacy_cargo_units <> 2
     OR before_state.active_dynamic_cargo_units <> 2
     OR before_state.violation_count <> 0 THEN
    RAISE EXCEPTION 'unexpected clean conservation snapshot: %', before_state;
  END IF;

  -- Move the active reference and primary ownership together.  Cargo on the
  -- inactive ship is intentionally outside the active-fleet conservation sum.
  INSERT INTO players VALUES (11, 2);
  INSERT INTO ship_ownership VALUES (2, 11, TRUE);
  DELETE FROM ship_ownership WHERE ship_id = 1 AND player_id = 10;
  DELETE FROM players WHERE player_id = 10;

  SELECT * INTO after_state FROM ship_lifecycle_conservation_snapshot;
  IF after_state.active_ship_count <> before_state.active_ship_count
     OR after_state.active_primary_owner_count <> before_state.active_primary_owner_count
     OR after_state.active_legacy_cargo_units <> 0
     OR after_state.active_dynamic_cargo_units <> 3
     OR after_state.violation_count <> 0 THEN
    RAISE EXCEPTION 'handoff changed active conservation unexpectedly: before %, after %',
      before_state, after_state;
  END IF;

  INSERT INTO ship_cargo VALUES (2, 'ORE', 5);
  SELECT * INTO after_state FROM ship_lifecycle_conservation_snapshot;
  IF after_state.active_over_capacity_ships <> 1
     OR after_state.violation_count <> 1 THEN
    RAISE EXCEPTION 'over-capacity active state was not observable: %', after_state;
  END IF;
END
$test$;

ROLLBACK;
\echo 'PostgreSQL ship lifecycle conservation snapshot checks passed.'
