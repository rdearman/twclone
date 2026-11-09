\set ON_ERROR_STOP on

-- V3-SHIP-02: the composite snapshot must include every lifecycle evidence
-- surface, while preserving pre-existing drift for upgrade diagnostics.
BEGIN;
CREATE SCHEMA ship_lifecycle_composite_144_test;
SET LOCAL search_path TO ship_lifecycle_composite_144_test, pg_catalog;

CREATE TABLE ships (
  ship_id integer PRIMARY KEY,
  holds integer, mines bigint, limpets bigint, fighters integer,
  genesis bigint, detonators bigint, probes bigint, photons bigint,
  shields integer, installed_shields integer, beacons bigint,
  colonists bigint, equipment bigint, organics bigint, ore bigint,
  slaves bigint, weapons bigint, drugs bigint, cloaking_devices bigint,
  hull bigint, sector_id integer, destroyed boolean DEFAULT FALSE,
  towing_ship_id integer DEFAULT 0, is_being_towed_by integer DEFAULT 0
);
CREATE TABLE players (
  player_id integer PRIMARY KEY, ship_id integer, sector_id integer
);
CREATE TABLE ship_ownership (
  ship_id integer, player_id integer, is_primary boolean
);
CREATE TABLE ship_cargo (
  ship_id integer, commodity_code text, quantity bigint
);

-- Seed legacy tow drift before installing the deferred write guard.  The
-- snapshot must report it; the migration must not rewrite populated state.
INSERT INTO ships VALUES
  (1, 4, 0, 0, 1, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 3, 0, 0, 0, 0,
   100, 7, FALSE, 2, 0),
  (2, 4, 0, 0, 1, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0,
   100, 7, FALSE, 0, 0);
INSERT INTO players VALUES (10, 1, 7);
INSERT INTO ship_ownership VALUES (1, 10, TRUE);
INSERT INTO ship_cargo VALUES (1, 'ORE', 3);

\ir ../sql/pg/127_ship_lifecycle_invariants.sql
\ir ../sql/pg/128_ship_relationship_invariants.sql
\ir ../sql/pg/131_ship_cargo_invariant_snapshot.sql
\ir ../sql/pg/132_ship_active_reference_invariants.sql
\ir ../sql/pg/134_ship_lifecycle_state_snapshot.sql
\ir ../sql/pg/135_ship_cargo_quantity_invariant.sql
\ir ../sql/pg/136_ship_destroyed_active_invariant.sql
\ir ../sql/pg/137_ship_active_owner_invariant.sql
\ir ../sql/pg/138_ship_ownership_invariant_snapshot.sql
\ir ../sql/pg/139_ship_lifecycle_acceptance_snapshot.sql
\ir ../sql/pg/142_ship_legacy_cargo_capacity.sql
\ir ../sql/pg/143_ship_tow_invariants.sql
\ir ../sql/pg/144_ship_lifecycle_composite_snapshot.sql
SET search_path TO ship_lifecycle_composite_144_test, pg_catalog;

DO $test$
DECLARE r record;
BEGIN
  SELECT * INTO r FROM ship_lifecycle_composite_snapshot;
  IF r.violation_count <> 1
     OR (r.invariants ->> 'nonreciprocal_tow_links')::bigint <> 1 THEN
    RAISE EXCEPTION 'composite snapshot lost legacy tow drift: %', r;
  END IF;

  -- A coordinated tow repair must leave the same aggregate contract clean.
  UPDATE ships SET is_being_towed_by = 1 WHERE ship_id = 2;
  SET CONSTRAINTS ALL IMMEDIATE;
  SELECT * INTO r FROM ship_lifecycle_composite_snapshot;
  IF r.violation_count <> 0
     OR EXISTS (
       SELECT 1 FROM jsonb_each_text(r.invariants) WHERE value <> '0'
     ) THEN
    RAISE EXCEPTION 'clean composite snapshot reported violations: %', r;
  END IF;
END
$test$;

ROLLBACK;
\echo 'PostgreSQL composite ship lifecycle snapshot checks passed.'
