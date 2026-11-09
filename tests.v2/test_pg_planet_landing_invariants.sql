\set ON_ERROR_STOP on
BEGIN;
CREATE SCHEMA planet_landing_133_test;
SET LOCAL search_path TO planet_landing_133_test, pg_catalog;

CREATE TABLE players (
  player_id integer PRIMARY KEY,
  sector_id integer NOT NULL,
  lastplanet integer
);
CREATE TABLE planets (
  planet_id integer PRIMARY KEY,
  sector_id integer NOT NULL
);
CREATE TABLE turns (
  player_id integer PRIMARY KEY,
  turns_remaining integer NOT NULL,
  last_update timestamp NOT NULL
);

\ir ../sql/pg/133_planet_landing_invariants.sql
SET search_path TO planet_landing_133_test, pg_catalog;

INSERT INTO players (player_id, sector_id, lastplanet) VALUES (7, 42, NULL);
INSERT INTO planets (planet_id, sector_id) VALUES (9001, 42), (9002, 99);
INSERT INTO turns (player_id, turns_remaining, last_update)
VALUES (7, 10, CURRENT_TIMESTAMP);

-- Baseline is clean before any landing attempt.
DO $test$
DECLARE r record;
BEGIN
  SELECT * INTO r FROM planet_landing_invariant_snapshot;
  IF r.orphan_landed_planets <> 0 OR r.remote_landed_planets <> 0
     OR r.negative_turn_balances <> 0 OR r.players_without_turn_rows <> 0 THEN
    RAISE EXCEPTION 'baseline landing snapshot is not clean: %', r;
  END IF;
END
$test$;

-- A refused remote landing must not consume a turn or set lastplanet.
DO $test$
DECLARE before_turns integer;
DECLARE before_planet integer;
BEGIN
  SELECT turns_remaining, lastplanet INTO before_turns, before_planet
    FROM players p JOIN turns t USING (player_id) WHERE p.player_id = 7;

  IF NOT EXISTS (SELECT 1 FROM planets WHERE planet_id = 9002 AND sector_id <> 42) THEN
    RAISE EXCEPTION 'remote fixture missing';
  END IF;

  -- This models the handler's precondition branch: no mutation is allowed.
  IF EXISTS (SELECT 1 FROM planets WHERE planet_id = 9002 AND sector_id = 42) THEN
    UPDATE turns SET turns_remaining = turns_remaining - 1 WHERE player_id = 7;
    UPDATE players SET lastplanet = 9002 WHERE player_id = 7;
  END IF;

  IF (SELECT turns_remaining FROM turns WHERE player_id = 7) <> before_turns
     OR (SELECT lastplanet FROM players WHERE player_id = 7) IS DISTINCT FROM before_planet THEN
    RAISE EXCEPTION 'remote refusal mutated landing state';
  END IF;
END
$test$;

-- A database failure after the tentative mutation must roll back both fields.
DO $test$
DECLARE before_turns integer;
BEGIN
  SELECT turns_remaining INTO before_turns FROM turns WHERE player_id = 7;
  BEGIN
    UPDATE turns SET turns_remaining = turns_remaining - 1 WHERE player_id = 7;
    UPDATE players SET lastplanet = 9001 WHERE player_id = 7;
    RAISE EXCEPTION 'injected landing failure';
  EXCEPTION WHEN OTHERS THEN
    NULL;
  END;

  IF (SELECT turns_remaining FROM turns WHERE player_id = 7) <> before_turns
     OR (SELECT lastplanet FROM players WHERE player_id = 7) IS NOT NULL THEN
    RAISE EXCEPTION 'failed landing was not rolled back';
  END IF;
END
$test$;

-- A valid landing consumes exactly one turn and records the planet once.
UPDATE turns SET turns_remaining = turns_remaining - 1 WHERE player_id = 7;
UPDATE players SET lastplanet = 9001 WHERE player_id = 7;

DO $test$
DECLARE r record;
BEGIN
  IF (SELECT turns_remaining FROM turns WHERE player_id = 7) <> 9
     OR (SELECT lastplanet FROM players WHERE player_id = 7) <> 9001 THEN
    RAISE EXCEPTION 'valid landing did not settle exactly once';
  END IF;

  SELECT * INTO r FROM planet_landing_invariant_snapshot;
  IF r.orphan_landed_planets <> 0 OR r.remote_landed_planets <> 0
     OR r.negative_turn_balances <> 0 OR r.players_without_turn_rows <> 0 THEN
    RAISE EXCEPTION 'successful landing left invariant violations: %', r;
  END IF;
END
$test$;

ROLLBACK;
\echo 'PostgreSQL planet landing invariant checks passed.'
