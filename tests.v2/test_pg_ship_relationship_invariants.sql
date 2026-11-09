\set ON_ERROR_STOP on
BEGIN;
CREATE SCHEMA ship_relationship_128_test;
SET LOCAL search_path TO ship_relationship_128_test, pg_catalog;

CREATE TABLE ships (ship_id integer PRIMARY KEY, sector_id integer);
CREATE TABLE players (player_id integer PRIMARY KEY, ship_id integer, sector_id integer);
CREATE TABLE ship_ownership (ship_id integer, player_id integer, is_primary boolean);

\ir ../sql/pg/128_ship_relationship_invariants.sql
SET search_path TO ship_relationship_128_test, pg_catalog;

INSERT INTO ships VALUES (1, 7), (2, 8);
INSERT INTO players VALUES (1, 1, 7);
INSERT INTO ship_ownership VALUES (1, 1, TRUE);

DO $test$
DECLARE r record;
BEGIN
  SELECT * INTO r FROM ship_relationship_invariant_snapshot;
  IF r.active_without_primary_owner <> 0 OR r.active_sector_mismatches <> 0 THEN
    RAISE EXCEPTION 'clean relationship snapshot reported violations: %', r;
  END IF;

  BEGIN
    DELETE FROM ship_ownership WHERE ship_id = 1;
    SET CONSTRAINTS ALL IMMEDIATE;
    RAISE EXCEPTION 'active owner removal accepted';
  EXCEPTION WHEN OTHERS THEN
    IF SQLERRM NOT LIKE 'SHIP_ACTIVE_WITHOUT_PRIMARY_OWNER:%' THEN
      RAISE;
    END IF;
  END;
  SET CONSTRAINTS ALL DEFERRED;

  BEGIN
    UPDATE players SET sector_id = 8 WHERE player_id = 1;
    SET CONSTRAINTS ALL IMMEDIATE;
    RAISE EXCEPTION 'active sector mismatch accepted';
  EXCEPTION WHEN OTHERS THEN
    IF SQLERRM NOT LIKE 'SHIP_ACTIVE_SECTOR_MISMATCH:%' THEN
      RAISE;
    END IF;
  END;
  SET CONSTRAINTS ALL DEFERRED;

  -- A coordinated lifecycle transition is valid when all rows settle before
  -- deferred constraint checks run.
  INSERT INTO ship_ownership VALUES (2, 1, TRUE);
  UPDATE players SET ship_id = 2, sector_id = 8 WHERE player_id = 1;
  SET CONSTRAINTS ALL IMMEDIATE;

  SELECT * INTO r FROM ship_relationship_invariant_snapshot;
  IF r.active_without_primary_owner <> 0 OR r.active_sector_mismatches <> 0 THEN
    RAISE EXCEPTION 'valid transfer left relationship violations: %', r;
  END IF;
END
$test$;

ROLLBACK;
\echo 'PostgreSQL ship relationship invariant checks passed.'
