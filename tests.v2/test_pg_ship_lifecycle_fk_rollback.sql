\set ON_ERROR_STOP on

-- V3-SHIP-02: lifecycle invariants must compose with the production foreign
-- keys, not only with the deferred cross-table guards.  This disposable
-- catalog proves orphan ownership/cargo writes fail without leaving a partial
-- lifecycle transition or changing the invariant snapshot.
BEGIN;
CREATE SCHEMA ship_lifecycle_fk_rollback_test;
SET LOCAL search_path TO ship_lifecycle_fk_rollback_test, pg_catalog;

CREATE TABLE ships (
  ship_id integer PRIMARY KEY,
  holds integer,
  mines bigint, limpets bigint, fighters integer, genesis bigint,
  detonators bigint, probes bigint, photons bigint, shields integer,
  installed_shields integer, beacons bigint, colonists bigint,
  equipment bigint, organics bigint, ore bigint, slaves bigint,
  weapons bigint, drugs bigint, cloaking_devices bigint, hull bigint,
  sector_id integer,
  destroyed boolean NOT NULL DEFAULT FALSE
);
CREATE TABLE players (
  player_id integer PRIMARY KEY,
  ship_id integer REFERENCES ships (ship_id),
  sector_id integer
);
CREATE TABLE ship_ownership (
  ship_id integer NOT NULL REFERENCES ships (ship_id) ON DELETE CASCADE,
  player_id integer NOT NULL REFERENCES players (player_id),
  role_id bigint NOT NULL DEFAULT 1,
  is_primary boolean NOT NULL DEFAULT TRUE,
  PRIMARY KEY (ship_id, player_id, role_id)
);
CREATE TABLE ship_cargo (
  ship_id integer NOT NULL REFERENCES ships (ship_id) ON DELETE CASCADE,
  commodity_code text NOT NULL,
  quantity bigint NOT NULL DEFAULT 0,
  PRIMARY KEY (ship_id, commodity_code)
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

SET search_path TO ship_lifecycle_fk_rollback_test, pg_catalog;
INSERT INTO ships (
  ship_id, holds, mines, limpets, fighters, genesis, detonators, probes,
  photons, shields, installed_shields, beacons, colonists, equipment,
  organics, ore, slaves, weapons, drugs, cloaking_devices, hull, sector_id
) VALUES (1, 8, 0, 0, 1, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 2, 0, 0, 0, 0, 100, 7);
INSERT INTO players VALUES (10, 1, 7);
INSERT INTO ship_ownership (ship_id, player_id, is_primary) VALUES (1, 10, TRUE);
INSERT INTO ship_cargo (ship_id, commodity_code, quantity) VALUES (1, 'ORE', 2);
SET CONSTRAINTS ALL IMMEDIATE;

DO $test$
DECLARE
  snapshot record;
  rejected boolean;
BEGIN
  SELECT * INTO snapshot FROM ship_lifecycle_acceptance_snapshot;
  IF EXISTS (SELECT 1 FROM jsonb_each_text(to_jsonb(snapshot)) WHERE value <> '0') THEN
    RAISE EXCEPTION 'valid FK-backed lifecycle state is not clean: %', snapshot;
  END IF;

  rejected := FALSE;
  BEGIN
    INSERT INTO ship_ownership (ship_id, player_id, is_primary)
    VALUES (1, 404, TRUE);
  EXCEPTION WHEN foreign_key_violation THEN
    rejected := TRUE;
  END;
  IF NOT rejected OR EXISTS (
    SELECT 1 FROM ship_ownership WHERE ship_id = 1 AND player_id = 404
  ) THEN
    RAISE EXCEPTION 'orphan ownership write was not rolled back';
  END IF;

  rejected := FALSE;
  BEGIN
    INSERT INTO ship_cargo (ship_id, commodity_code, quantity)
    VALUES (404, 'ORE', 1);
  EXCEPTION WHEN foreign_key_violation THEN
    rejected := TRUE;
  END;
  IF NOT rejected OR EXISTS (
    SELECT 1 FROM ship_cargo WHERE ship_id = 404
  ) THEN
    RAISE EXCEPTION 'orphan cargo write was not rolled back';
  END IF;

  SELECT * INTO snapshot FROM ship_lifecycle_acceptance_snapshot;
  IF EXISTS (SELECT 1 FROM jsonb_each_text(to_jsonb(snapshot)) WHERE value <> '0') THEN
    RAISE EXCEPTION 'rejected FK writes left lifecycle drift: %', snapshot;
  END IF;
END
$test$;

ROLLBACK;
\echo 'PostgreSQL ship lifecycle FK rollback checks passed.'
