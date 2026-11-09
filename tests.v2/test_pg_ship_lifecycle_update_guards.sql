\set ON_ERROR_STOP on

-- V3-SHIP-02: legacy duplicate rows must not reopen duplicate lifecycle state
-- through UPDATE paths.  This fixture intentionally starts with duplicate
-- primary ownership, so the upgrade-safe deferred guards are exercised instead
-- of the native partial unique indexes.
BEGIN;
CREATE SCHEMA ship_lifecycle_update_guard_test;
SET LOCAL search_path TO ship_lifecycle_update_guard_test, pg_catalog;

CREATE TABLE ships (
  ship_id integer PRIMARY KEY,
  holds integer, mines bigint, limpets bigint, fighters integer,
  genesis bigint, detonators bigint, probes bigint, photons bigint,
  shields integer, installed_shields integer, beacons bigint,
  colonists bigint, equipment bigint, organics bigint, ore bigint,
  slaves bigint, weapons bigint, drugs bigint, cloaking_devices bigint,
  hull bigint, sector_id integer, destroyed boolean NOT NULL DEFAULT FALSE
);
CREATE TABLE players (
  player_id integer PRIMARY KEY,
  ship_id integer,
  sector_id integer
);
CREATE TABLE ship_ownership (
  ship_id integer,
  player_id integer,
  role_id bigint,
  is_primary boolean,
  acquired_at timestamptz DEFAULT CURRENT_TIMESTAMP
);
CREATE TABLE ship_cargo (
  ship_id integer,
  commodity_code text,
  quantity bigint
);

-- The duplicate legacy rows exist before the guards are installed.
INSERT INTO ships VALUES
  (1, 5, 0, 0, 1, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 100, 7, FALSE),
  (2, 5, 0, 0, 1, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 100, 8, FALSE);
INSERT INTO players VALUES (10, 1, 7), (20, 2, 8);
INSERT INTO ship_ownership VALUES
  (1, 10, 1, TRUE), (1, 20, 1, TRUE), (2, 20, 1, TRUE),
  (2, 10, 2, FALSE);

\ir ../sql/pg/126_ship_primary_ownership.sql
\ir ../sql/pg/127_ship_lifecycle_invariants.sql
\ir ../sql/pg/128_ship_relationship_invariants.sql
\ir ../sql/pg/138_ship_ownership_invariant_snapshot.sql
\ir ../sql/pg/140_ship_lifecycle_legacy_race_guards.sql
SET search_path TO ship_lifecycle_update_guard_test, pg_catalog;

DO $test$
DECLARE
  rejected boolean;
  snapshot record;
BEGIN
  SELECT * INTO snapshot FROM ship_ownership_invariant_snapshot;
  IF snapshot.duplicate_primary_owners <> 1 THEN
    RAISE EXCEPTION 'legacy duplicate primary owner was not observable: %', snapshot;
  END IF;

  -- Changing a secondary row into a primary row must be rejected even when a
  -- pre-existing duplicate prevented creation of the native unique index.
  rejected := FALSE;
  BEGIN
    UPDATE ship_ownership
       SET is_primary = TRUE
     WHERE ship_id = 2 AND player_id = 10;
    SET CONSTRAINTS ALL IMMEDIATE;
  EXCEPTION WHEN SQLSTATE '23505' THEN
    rejected := SQLERRM = 'SHIP_PRIMARY_OWNER_DUPLICATE:2';
  END;
  IF NOT rejected THEN
    RAISE EXCEPTION 'primary-owner UPDATE bypassed legacy race guard';
  END IF;

  -- Updating an existing player onto an already-active ship is the analogous
  -- active-reference race.  The failed statement must not move the player.
  rejected := FALSE;
  BEGIN
    UPDATE players SET ship_id = 1, sector_id = 7 WHERE player_id = 20;
    SET CONSTRAINTS ALL IMMEDIATE;
  EXCEPTION WHEN SQLSTATE '23514' THEN
    rejected := SQLERRM = 'SHIP_ACTIVE_DUPLICATE:1';
  END;
  IF NOT rejected THEN
    RAISE EXCEPTION 'active-ship UPDATE bypassed legacy race guard';
  END IF;

  SELECT * INTO snapshot FROM ship_ownership_invariant_snapshot;
  IF snapshot.duplicate_primary_owners <> 1
     OR (SELECT ship_id FROM players WHERE player_id = 20) <> 2
     OR NOT EXISTS (
       SELECT 1 FROM ship_ownership
        WHERE ship_id = 2 AND player_id = 20 AND is_primary
     ) THEN
    RAISE EXCEPTION 'rejected updates changed lifecycle state: %', snapshot;
  END IF;
END
$test$;

ROLLBACK;
\echo 'PostgreSQL ship lifecycle UPDATE guard checks passed.'
