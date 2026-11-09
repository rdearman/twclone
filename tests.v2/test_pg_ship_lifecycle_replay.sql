\set ON_ERROR_STOP on

-- V3-SHIP-02: lifecycle migrations must be safely replayable during restart
-- and upgrade verification.  This test is isolated in a transaction and a
-- temporary schema; it does not exercise or mutate a production catalog.
BEGIN;
CREATE SCHEMA ship_lifecycle_replay_test;
SET LOCAL search_path TO ship_lifecycle_replay_test, pg_catalog;

CREATE TABLE ships (
  ship_id integer PRIMARY KEY,
  holds integer,
  mines bigint,
  limpets bigint,
  fighters integer,
  genesis bigint,
  detonators bigint,
  probes bigint,
  photons bigint,
  shields integer,
  installed_shields integer,
  beacons bigint,
  colonists bigint,
  equipment bigint,
  organics bigint,
  ore bigint,
  slaves bigint,
  weapons bigint,
  drugs bigint,
  cloaking_devices bigint,
  hull bigint,
  sector_id integer
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

\ir ../sql/pg/126_ship_primary_ownership.sql
\ir ../sql/pg/127_ship_lifecycle_invariants.sql
\ir ../sql/pg/128_ship_relationship_invariants.sql
\ir ../sql/pg/131_ship_cargo_invariant_snapshot.sql
\ir ../sql/pg/132_ship_active_reference_invariants.sql

-- A restart/upgrade can replay the supported migration set.  The second pass
-- must not create duplicate indexes, triggers, constraints, or views.
\ir ../sql/pg/126_ship_primary_ownership.sql
\ir ../sql/pg/127_ship_lifecycle_invariants.sql
\ir ../sql/pg/128_ship_relationship_invariants.sql
\ir ../sql/pg/131_ship_cargo_invariant_snapshot.sql
\ir ../sql/pg/132_ship_active_reference_invariants.sql

SET search_path TO ship_lifecycle_replay_test, pg_catalog;
INSERT INTO ships VALUES
  (1, 8, 0, 0, 1, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 100, 7);
INSERT INTO players VALUES (10, 1, 7);
INSERT INTO ship_ownership VALUES (1, 10, TRUE);
INSERT INTO ship_cargo VALUES (1, 'ORE', 3);
SET CONSTRAINTS ALL IMMEDIATE;

DO $test$
DECLARE
  lifecycle record;
  relationship record;
  cargo record;
  active_ref record;
  trigger_count integer;
BEGIN
  SELECT * INTO lifecycle FROM ship_lifecycle_invariant_snapshot;
  SELECT * INTO relationship FROM ship_relationship_invariant_snapshot;
  SELECT * INTO cargo FROM ship_cargo_invariant_snapshot;
  SELECT * INTO active_ref FROM ship_active_reference_invariant_snapshot;

  IF lifecycle.resource_violations <> 0
     OR lifecycle.dynamic_cargo_violations <> 0
     OR lifecycle.active_without_primary_owner <> 0
     OR lifecycle.active_sector_mismatches <> 0
     OR relationship.active_without_primary_owner <> 0
     OR relationship.active_sector_mismatches <> 0
     OR cargo.negative_cargo_quantities <> 0
     OR cargo.over_capacity_ships <> 0
     OR active_ref.orphan_active_ship_references <> 0 THEN
    RAISE EXCEPTION 'replayed lifecycle migrations reported drift';
  END IF;

  SELECT count(*) INTO trigger_count
    FROM pg_trigger t
    JOIN pg_class c ON c.oid = t.tgrelid
   WHERE c.relnamespace = 'ship_lifecycle_replay_test'::regnamespace
     AND NOT t.tgisinternal;
  IF trigger_count <> 6 THEN
    RAISE EXCEPTION 'replayed lifecycle migrations created duplicate triggers: %',
      trigger_count;
  END IF;
END
$test$;

ROLLBACK;
\echo 'PostgreSQL ship lifecycle replay checks passed.'
