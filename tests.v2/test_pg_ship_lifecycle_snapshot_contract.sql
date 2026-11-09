\set ON_ERROR_STOP on

-- V3-SHIP-02: the aggregate lifecycle snapshot is a persistence contract,
-- not merely diagnostic SQL.  Keep its column surface stable for restart and
-- acceptance checks, and prove a valid active-ship state is all-zero.
BEGIN;
CREATE SCHEMA ship_lifecycle_snapshot_contract_test;
SET LOCAL search_path TO ship_lifecycle_snapshot_contract_test, pg_catalog;

CREATE TABLE ships (
  ship_id integer PRIMARY KEY,
  holds integer,
  mines bigint, limpets bigint, fighters integer, genesis bigint,
  detonators bigint, probes bigint, photons bigint, shields integer,
  installed_shields integer, beacons bigint, colonists bigint,
  equipment bigint, organics bigint, ore bigint, slaves bigint,
  weapons bigint, drugs bigint, cloaking_devices bigint, hull bigint,
  destroyed boolean NOT NULL DEFAULT FALSE, sector_id integer
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

\ir ../sql/pg/126_ship_primary_ownership.sql
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
SET search_path TO ship_lifecycle_snapshot_contract_test, pg_catalog;

DO $test$
DECLARE
  expected text[] := ARRAY[
    'resource_violations', 'legacy_cargo_violations',
    'dynamic_cargo_violations', 'dynamic_cargo_quantity_violations',
    'active_ship_duplicates', 'active_without_primary_owner',
    'primary_owner_duplicates', 'orphan_primary_owners',
    'active_sector_mismatches', 'negative_cargo_quantities',
    'over_capacity_ships', 'legacy_mirror_mismatches',
    'relationship_owner_violations', 'relationship_sector_violations',
    'orphan_active_ship_references', 'duplicate_active_ship_references',
    'state_orphan_active_ship_references', 'destroyed_active_ship_references',
    'state_sector_violations', 'state_owner_violations',
    'state_duplicate_primary_owners', 'state_orphan_primary_owners',
    'active_owner_mismatches', 'orphan_ship_ownership',
    'orphan_player_ownership', 'ownership_duplicate_primary_owners'
  ];
  actual text[];
  snapshot record;
BEGIN
  SELECT array_agg(column_name ORDER BY ordinal_position)
    INTO actual
    FROM information_schema.columns
   WHERE table_schema = current_schema()
     AND table_name = 'ship_lifecycle_acceptance_snapshot';

  IF actual IS DISTINCT FROM expected THEN
    RAISE EXCEPTION 'lifecycle snapshot columns changed: expected %, got %',
      expected, actual;
  END IF;

  INSERT INTO ships (
    ship_id, holds, mines, limpets, fighters, genesis, detonators, probes,
    photons, shields, installed_shields, beacons, colonists, equipment,
    organics, ore, slaves, weapons, drugs, cloaking_devices, hull, sector_id
  ) VALUES
    (1, 5, 0, 0, 1, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 2, 0, 0, 0, 0, 100, 7);
  INSERT INTO players VALUES (10, 1, 7);
  INSERT INTO ship_ownership VALUES (1, 10, TRUE);
  INSERT INTO ship_cargo VALUES (1, 'ORE', 2);
  SET CONSTRAINTS ALL IMMEDIATE;

  SELECT * INTO snapshot FROM ship_lifecycle_acceptance_snapshot;
  IF EXISTS (SELECT 1 FROM jsonb_each_text(to_jsonb(snapshot)) WHERE value <> '0') THEN
    RAISE EXCEPTION 'valid lifecycle state is not all-zero: %', snapshot;
  END IF;
END
$test$;

ROLLBACK;
\echo 'PostgreSQL ship lifecycle snapshot contract checks passed.'
