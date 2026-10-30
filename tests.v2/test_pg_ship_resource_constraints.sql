\set ON_ERROR_STOP on
BEGIN;
CREATE SCHEMA ship_constraints_123_test;
SET LOCAL search_path TO ship_constraints_123_test, pg_catalog;
CREATE TABLE ships (
  ship_id integer PRIMARY KEY, holds integer DEFAULT 1, mines bigint,
  limpets bigint, fighters integer DEFAULT 1, genesis bigint,
  detonators bigint NOT NULL DEFAULT 0, probes bigint NOT NULL DEFAULT 0,
  photons bigint, shields integer DEFAULT 1, installed_shields integer DEFAULT 1,
  beacons bigint, colonists bigint, equipment bigint, organics bigint, ore bigint,
  slaves bigint DEFAULT 0, weapons bigint DEFAULT 0, drugs bigint DEFAULT 0,
  cloaking_devices bigint, hull bigint NOT NULL DEFAULT 100,
  CONSTRAINT check_current_cargo_limit CHECK
    ((colonists + ore + organics + equipment + slaves + weapons + drugs) <= holds)
);
\ir ../sql/pg/123_ship_resource_constraints.sql
SET search_path TO ship_constraints_123_test, pg_catalog;
\ir ../sql/pg/123_ship_resource_constraints.sql
INSERT INTO ships (ship_id, holds, mines, limpets, fighters, hull)
VALUES (1, 10, 0, 0, 1, 100);
DO $test$
BEGIN
  BEGIN
    UPDATE ships SET mines = -1 WHERE ship_id = 1;
    RAISE EXCEPTION 'negative mines accepted';
  EXCEPTION WHEN check_violation THEN NULL;
  END;
  BEGIN
    UPDATE ships SET hull = 101 WHERE ship_id = 1;
    RAISE EXCEPTION 'out-of-range hull accepted';
  EXCEPTION WHEN check_violation THEN NULL;
  END;
  IF (SELECT hull FROM ships WHERE ship_id = 1) <> 100 THEN
    RAISE EXCEPTION 'valid ship state changed';
  END IF;
END
$test$;
ROLLBACK;
\echo 'PostgreSQL ship resource constraint checks passed.'
