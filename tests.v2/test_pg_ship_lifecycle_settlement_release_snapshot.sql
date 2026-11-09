\set ON_ERROR_STOP on

-- REQUIREMENT_ID: V3-SHIP-02
-- Disposable proof for the release-facing settlement invariant.  The outer
-- transaction and temporary schema guarantee that no installed database is
-- changed by this test.
BEGIN;
CREATE SCHEMA ship_lifecycle_settlement_release_test;
SET LOCAL search_path TO ship_lifecycle_settlement_release_test, pg_catalog;

CREATE TABLE ships (
  ship_id integer PRIMARY KEY,
  holds integer,
  colonists bigint DEFAULT 0, equipment bigint DEFAULT 0,
  organics bigint DEFAULT 0, ore bigint DEFAULT 0,
  slaves bigint DEFAULT 0, weapons bigint DEFAULT 0, drugs bigint DEFAULT 0,
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

\ir ../sql/pg/154_ship_lifecycle_settlement_snapshot.sql
\ir ../sql/pg/155_ship_lifecycle_conservation_snapshot.sql
-- Keep this test focused on the new aggregate.  The production release view
-- contributes the earlier invariant count; this disposable fixture supplies
-- its zero-valued equivalent without reproducing the whole schema bundle.
CREATE VIEW ship_lifecycle_release_snapshot AS
SELECT '{}'::jsonb AS invariants, 0::bigint AS violation_count;
\ir ../sql/pg/156_ship_lifecycle_settlement_release_snapshot.sql

SET search_path TO ship_lifecycle_settlement_release_test, pg_catalog;
INSERT INTO ships (ship_id, holds, ore) VALUES (1, 8, 3), (2, 8, 0);
INSERT INTO players VALUES (10, 1, 7);
INSERT INTO ship_ownership VALUES (1, 10, TRUE);
INSERT INTO ship_cargo VALUES (1, 'ORE', 3);

DO $test$
DECLARE
  snapshot record;
BEGIN
  SELECT * INTO snapshot FROM ship_lifecycle_settlement_release_snapshot;
  IF snapshot.violation_count <> 0
     OR (snapshot.invariants ->> 'active_ship_count') <> '1'
     OR (snapshot.invariants ->> 'active_primary_owner_count') <> '1'
     OR (snapshot.invariants ->> 'active_dynamic_cargo_units') <> '3'
     OR (snapshot.invariants ->> 'active_legacy_cargo_units') <> '3' THEN
    RAISE EXCEPTION 'clean settlement snapshot reported drift: %', snapshot;
  END IF;

  -- A valid active-ship handoff preserves the settlement totals.
  INSERT INTO ship_ownership VALUES (2, 10, TRUE);
  UPDATE players SET ship_id = 2 WHERE player_id = 10;
  DELETE FROM ship_ownership WHERE ship_id = 1 AND player_id = 10;
  UPDATE ships SET ore = 3 WHERE ship_id = 2;
  DELETE FROM ship_cargo WHERE ship_id = 1;
  INSERT INTO ship_cargo VALUES (2, 'ORE', 3);
  SELECT * INTO snapshot FROM ship_lifecycle_settlement_release_snapshot;
  IF snapshot.violation_count <> 0
     OR (snapshot.invariants ->> 'active_ship_count') <> '1'
     OR (snapshot.invariants ->> 'active_primary_owner_count') <> '1'
     OR (snapshot.invariants ->> 'active_dynamic_cargo_units') <> '3' THEN
    RAISE EXCEPTION 'handoff failed conservation/ownership invariant: %', snapshot;
  END IF;

  -- The snapshot must expose a persistence defect rather than silently pass.
  INSERT INTO ship_cargo VALUES (2, 'ORE', 9);
  SELECT * INTO snapshot FROM ship_lifecycle_settlement_release_snapshot;
  IF snapshot.violation_count <= 0
     OR (snapshot.invariants ->> 'active_over_capacity_ships') <> '1' THEN
    RAISE EXCEPTION 'over-capacity drift was not exposed: %', snapshot;
  END IF;
END
$test$;

ROLLBACK;
\echo 'PostgreSQL ship lifecycle settlement release snapshot checks passed.'
