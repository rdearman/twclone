\set ON_ERROR_STOP on

-- V3-SHIP-02: migration 126 must not fail on legacy duplicate ownership rows.
-- The duplicate is retained for diagnosis, while new duplicate writes are
-- rejected by the deferred advisory-lock guard until data repair is possible.
BEGIN;
CREATE SCHEMA ship_primary_ownership_upgrade_test;
SET LOCAL search_path TO ship_primary_ownership_upgrade_test, pg_catalog;

CREATE TABLE ships (ship_id integer PRIMARY KEY);
CREATE TABLE players (player_id integer PRIMARY KEY, ship_id integer);
CREATE TABLE ship_ownership (
  ship_id integer,
  player_id integer,
  is_primary boolean
);

INSERT INTO ships VALUES (1), (2);
INSERT INTO ship_ownership VALUES (1, 10, TRUE), (1, 11, TRUE);

\ir ../sql/pg/126_ship_primary_ownership.sql
SET search_path TO ship_primary_ownership_upgrade_test, pg_catalog;

DO $test$
DECLARE
  rejected boolean := FALSE;
BEGIN
  IF (SELECT count(*) FROM ship_ownership WHERE ship_id = 1 AND is_primary) <> 2 THEN
    RAISE EXCEPTION 'legacy duplicate primary ownership was rewritten';
  END IF;

  BEGIN
    INSERT INTO ship_ownership VALUES (1, 12, TRUE);
    SET CONSTRAINTS ALL IMMEDIATE;
  EXCEPTION WHEN SQLSTATE '23505' THEN
    rejected := SQLERRM = 'SHIP_PRIMARY_OWNER_DUPLICATE:1';
  END;
  IF NOT rejected THEN
    RAISE EXCEPTION 'new duplicate primary ownership was accepted';
  END IF;

  -- A distinct ship remains writable, proving the guard is scoped by ship.
  INSERT INTO ship_ownership VALUES (2, 10, TRUE);
  SET CONSTRAINTS ALL IMMEDIATE;
END
$test$;

ROLLBACK;
\echo 'PostgreSQL ship primary-ownership upgrade checks passed.'
