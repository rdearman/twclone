-- Run only against a disposable database; all fixture objects are rolled back.
\set ON_ERROR_STOP on
BEGIN;
CREATE SCHEMA ship_claim_120_test;
SET LOCAL search_path TO ship_claim_120_test, pg_catalog;
CREATE TYPE db_result AS (code integer, message text, value bigint);
CREATE TABLE ships (ship_id integer PRIMARY KEY, sector_id integer, destroyed boolean NOT NULL DEFAULT false);
CREATE TABLE players (player_id integer PRIMARY KEY, sector_id integer, ship_id integer REFERENCES ships(ship_id));
\ir ../sql/pg/120_ship_claim_location_guard.sql
\ir ../sql/pg/120_ship_claim_location_guard.sql
INSERT INTO ships VALUES (10, 1, false), (20, 1, false), (30, 2, false), (40, 1, true), (50, 1, false);
INSERT INTO players VALUES (1, 1, 10), (2, 1, 50), (3, NULL, NULL);
DO $test$
DECLARE r db_result;
BEGIN
  SELECT * INTO r FROM ship_claim(1, 30);
  IF r.code <> 409 OR (SELECT ship_id FROM players WHERE player_id=1) <> 10 THEN
    RAISE EXCEPTION 'remote claim changed active ship: %', r;
  END IF;
  SELECT * INTO r FROM ship_claim(1, 40);
  IF r.code <> 409 OR (SELECT ship_id FROM players WHERE player_id=1) <> 10 THEN
    RAISE EXCEPTION 'destroyed claim changed active ship: %', r;
  END IF;
  SELECT * INTO r FROM ship_claim(1, 50);
  IF r.code <> 409 OR (SELECT ship_id FROM players WHERE player_id=1) <> 10 THEN
    RAISE EXCEPTION 'occupied claim changed active ship: %', r;
  END IF;
  SELECT * INTO r FROM ship_claim(3, 20);
  IF r.code <> 409 OR (SELECT ship_id FROM players WHERE player_id=3) IS NOT NULL THEN
    RAISE EXCEPTION 'unknown player location allowed a claim: %', r;
  END IF;
  SELECT * INTO r FROM ship_claim(1, 999);
  IF r.code <> 404 THEN RAISE EXCEPTION 'missing ship was not refused: %', r; END IF;
  SELECT * INTO r FROM ship_claim(999, 20);
  IF r.code <> 404 THEN RAISE EXCEPTION 'missing player was not refused: %', r; END IF;
  SELECT * INTO r FROM ship_claim(1, 20);
  IF r.code <> 0 OR (SELECT ship_id FROM players WHERE player_id=1) <> 20 THEN
    RAISE EXCEPTION 'local unoccupied ship was not selected: %', r;
  END IF;
END
$test$;
ROLLBACK;
\echo 'PostgreSQL ship claim migration checks passed.'
