\set ON_ERROR_STOP on

-- V3-SHIP-02: a lifecycle operation that reduces holds below authoritative
-- dynamic cargo must fail atomically and leave the invariant snapshot clean.
-- The disposable schema and outer transaction keep this test isolated.
BEGIN;
CREATE SCHEMA ship_lifecycle_capacity_rollback_test;
SET LOCAL search_path TO ship_lifecycle_capacity_rollback_test, pg_catalog;

CREATE TABLE ships (
  ship_id integer PRIMARY KEY,
  holds integer,
  mines bigint, limpets bigint, fighters integer, genesis bigint,
  detonators bigint, probes bigint, photons bigint, shields integer,
  installed_shields integer, beacons bigint, colonists bigint,
  equipment bigint, organics bigint, ore bigint, slaves bigint,
  weapons bigint, drugs bigint, cloaking_devices bigint, hull bigint,
  sector_id integer
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

\ir ../sql/pg/127_ship_lifecycle_invariants.sql
\ir ../sql/pg/128_ship_relationship_invariants.sql
\ir ../sql/pg/131_ship_cargo_invariant_snapshot.sql
\ir ../sql/pg/132_ship_active_reference_invariants.sql
\ir ../sql/pg/134_ship_lifecycle_state_snapshot.sql
\ir ../sql/pg/135_ship_cargo_quantity_invariant.sql
\ir ../sql/pg/137_ship_active_owner_invariant.sql
\ir ../sql/pg/138_ship_ownership_invariant_snapshot.sql
\ir ../sql/pg/139_ship_lifecycle_acceptance_snapshot.sql

SET search_path TO ship_lifecycle_capacity_rollback_test, pg_catalog;

INSERT INTO ships VALUES
  (1, 5, 0, 0, 1, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 100, 7);
INSERT INTO players VALUES (10, 1, 7);
INSERT INTO ship_ownership VALUES (1, 10, TRUE);
INSERT INTO ship_cargo VALUES (1, 'ORE', 4);
SET CONSTRAINTS ALL IMMEDIATE;

DO $test$
DECLARE
  snapshot record;
  rejected boolean := FALSE;
BEGIN
  SELECT * INTO snapshot FROM ship_lifecycle_acceptance_snapshot;
  IF EXISTS (SELECT 1 FROM jsonb_each_text(to_jsonb(snapshot)) WHERE value <> '0') THEN
    RAISE EXCEPTION 'valid loaded ship is not all-zero: %', snapshot;
  END IF;

  BEGIN
    UPDATE ships SET holds = 3 WHERE ship_id = 1;
    SET CONSTRAINTS ALL IMMEDIATE;
  EXCEPTION WHEN OTHERS THEN
    rejected := SQLERRM = 'SHIP_CARGO_EXCEEDS_HOLDS';
  END;

  IF NOT rejected THEN
    RAISE EXCEPTION 'holds reduction below dynamic cargo was accepted';
  END IF;

  SELECT * INTO snapshot FROM ship_lifecycle_acceptance_snapshot;
  IF (SELECT holds FROM ships WHERE ship_id = 1) <> 5
     OR EXISTS (SELECT 1 FROM jsonb_each_text(to_jsonb(snapshot)) WHERE value <> '0') THEN
    RAISE EXCEPTION 'rejected holds reduction left lifecycle drift: %', snapshot;
  END IF;

  rejected := FALSE;
  BEGIN
    INSERT INTO ship_cargo VALUES (1, 'ORG', -1);
  EXCEPTION WHEN check_violation THEN
    rejected := TRUE;
  END;

  IF NOT rejected THEN
    RAISE EXCEPTION 'negative dynamic cargo was accepted';
  END IF;

  SELECT * INTO snapshot FROM ship_lifecycle_acceptance_snapshot;
  IF EXISTS (SELECT 1 FROM jsonb_each_text(to_jsonb(snapshot)) WHERE value <> '0') THEN
    RAISE EXCEPTION 'rejected negative cargo left lifecycle drift: %', snapshot;
  END IF;
END
$test$;

ROLLBACK;
\echo 'PostgreSQL ship lifecycle capacity rollback checks passed.'
