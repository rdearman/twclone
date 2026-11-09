\set ON_ERROR_STOP on

-- V3-SHIP-02: the release snapshot must include primary-owner semantics as
-- well as the existing resource, cargo, active-reference, sector and tow
-- invariants.  The temporary schema and outer transaction keep this isolated.
BEGIN;
CREATE SCHEMA ship_lifecycle_release_snapshot_test;
SET LOCAL search_path TO ship_lifecycle_release_snapshot_test, pg_catalog;

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
  destroyed boolean NOT NULL DEFAULT FALSE,
  sector_id integer,
  towing_ship_id integer NOT NULL DEFAULT 0,
  is_being_towed_by integer NOT NULL DEFAULT 0
);
CREATE TABLE players (
  player_id integer PRIMARY KEY,
  ship_id integer,
  sector_id integer
);
CREATE TABLE ship_ownership (
  ship_id integer NOT NULL,
  player_id integer NOT NULL,
  role_id bigint NOT NULL DEFAULT 1,
  is_primary boolean,
  PRIMARY KEY (ship_id, player_id, role_id)
);
CREATE TABLE ship_cargo (
  ship_id integer NOT NULL,
  commodity_code text NOT NULL,
  quantity bigint NOT NULL
);

\ir ../sql/pg/127_ship_lifecycle_invariants.sql
\ir ../sql/pg/128_ship_relationship_invariants.sql
\ir ../sql/pg/131_ship_cargo_invariant_snapshot.sql
\ir ../sql/pg/132_ship_active_reference_invariants.sql
\ir ../sql/pg/134_ship_lifecycle_state_snapshot.sql
\ir ../sql/pg/137_ship_active_owner_invariant.sql
\ir ../sql/pg/138_ship_ownership_invariant_snapshot.sql
\ir ../sql/pg/143_ship_tow_invariants.sql
\ir ../sql/pg/146_ship_tow_delete_invariant.sql
\ir ../sql/pg/144_ship_lifecycle_composite_snapshot.sql
\ir ../sql/pg/145_ship_active_sector_invariant.sql
\ir ../sql/pg/147_ship_lifecycle_current_snapshot.sql
\ir ../sql/pg/148_ship_primary_owner_semantics_snapshot.sql
\ir ../sql/pg/150_ship_lifecycle_release_snapshot.sql
SET search_path TO ship_lifecycle_release_snapshot_test, pg_catalog;

INSERT INTO ships (
  ship_id, holds, mines, limpets, fighters, genesis, detonators, probes,
  photons, shields, installed_shields, beacons, colonists, equipment,
  organics, ore, slaves, weapons, drugs, cloaking_devices, hull, sector_id
) VALUES (1, 10, 0, 0, 1, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 100, 7);
INSERT INTO players VALUES (10, 1, 7);
INSERT INTO ship_ownership VALUES (1, 10, 1, TRUE);

DO $test$
DECLARE
  snapshot record;
BEGIN
  SELECT * INTO snapshot FROM ship_lifecycle_release_snapshot;
  IF snapshot.violation_count <> 0
     OR (snapshot.invariants ->> 'owned_ships_without_primary')::bigint <> 0
     OR (snapshot.invariants ->> 'null_primary_flags')::bigint <> 0
     OR (snapshot.invariants ->> 'duplicate_primary_owners_latest')::bigint <> 0 THEN
    RAISE EXCEPTION 'clean release snapshot reported violations: %', snapshot;
  END IF;

  -- A secondary-only membership is valid membership but not a valid primary
  -- owner state for an owned ship; the release snapshot must surface it.
  UPDATE ship_ownership SET is_primary = FALSE
   WHERE ship_id = 1 AND player_id = 10;
  SELECT * INTO snapshot FROM ship_lifecycle_release_snapshot;
  IF snapshot.violation_count <> 1
     OR (snapshot.invariants ->> 'owned_ships_without_primary')::bigint <> 1 THEN
    RAISE EXCEPTION 'primary-owner drift was not reported: %', snapshot;
  END IF;

  UPDATE ship_ownership SET is_primary = TRUE
   WHERE ship_id = 1 AND player_id = 10;
  SELECT * INTO snapshot FROM ship_lifecycle_release_snapshot;
  IF snapshot.violation_count <> 0 THEN
    RAISE EXCEPTION 'repaired release snapshot remains dirty: %', snapshot;
  END IF;
END
$test$;

ROLLBACK;
\echo 'PostgreSQL ship lifecycle release snapshot checks passed.'
