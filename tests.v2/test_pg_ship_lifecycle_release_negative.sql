\set ON_ERROR_STOP on

-- REQUIREMENT_ID: V3-SHIP-02
-- The release snapshot must fail closed when an active ship loses its
-- primary-owner relationship.  This is intentionally observational: the
-- lifecycle guards are tested separately, while this test proves that the
-- aggregate evidence cannot report a clean release state for drift already
-- present in a restored or legacy database.
BEGIN;
CREATE SCHEMA ship_lifecycle_release_negative_test;
SET LOCAL search_path TO ship_lifecycle_release_negative_test, pg_catalog;

CREATE TABLE ships (
  ship_id integer PRIMARY KEY,
  holds integer NOT NULL,
  colonists bigint NOT NULL DEFAULT 0,
  ore bigint NOT NULL DEFAULT 0,
  organics bigint NOT NULL DEFAULT 0,
  equipment bigint NOT NULL DEFAULT 0,
  slaves bigint NOT NULL DEFAULT 0,
  weapons bigint NOT NULL DEFAULT 0,
  drugs bigint NOT NULL DEFAULT 0,
  towing_ship_id integer NOT NULL DEFAULT 0,
  is_being_towed_by integer NOT NULL DEFAULT 0
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
-- Supply the earlier release aggregate with a clean base.  The test is about
-- the new combined count, not about reproducing every preceding migration.
CREATE VIEW ship_lifecycle_release_snapshot AS
SELECT '{}'::jsonb AS invariants, 0::bigint AS violation_count;
\ir ../sql/pg/156_ship_lifecycle_settlement_release_snapshot.sql

SET search_path TO ship_lifecycle_release_negative_test, pg_catalog;
INSERT INTO ships (ship_id, holds, ore) VALUES (1, 8, 3);
INSERT INTO players VALUES (10, 1, 7);
INSERT INTO ship_cargo VALUES (1, 'ORE', 3);

DO $test$
DECLARE
  snapshot record;
BEGIN
  SELECT * INTO snapshot FROM ship_lifecycle_settlement_release_snapshot;

  IF snapshot.violation_count <= 0
     OR (snapshot.invariants ->> 'active_ship_count') <> '1'
     OR (snapshot.invariants ->> 'active_primary_owner_count') <> '0'
     OR (snapshot.invariants ->> 'active_owner_mismatches') <> '1' THEN
    RAISE EXCEPTION
      'active ship without primary owner was not fail-closed: %', snapshot;
  END IF;

  -- Repairing the relationship must clear the aggregate without changing
  -- the tracked active cargo total.
  INSERT INTO ship_ownership VALUES (1, 10, TRUE);
  SELECT * INTO snapshot FROM ship_lifecycle_settlement_release_snapshot;
  IF snapshot.violation_count <> 0
     OR (snapshot.invariants ->> 'active_ship_count') <> '1'
     OR (snapshot.invariants ->> 'active_primary_owner_count') <> '1'
     OR (snapshot.invariants ->> 'active_dynamic_cargo_units') <> '3' THEN
    RAISE EXCEPTION 'repaired release snapshot remained dirty: %', snapshot;
  END IF;
END
$test$;

ROLLBACK;
\echo 'PostgreSQL ship lifecycle release negative checks passed.'
