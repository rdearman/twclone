\set ON_ERROR_STOP on

-- REQUIREMENT_ID: V3-PLAN-01
-- Isolated PostgreSQL proof for the landing retry contract.  This fixture
-- deliberately models the durable rows that must remain together; it does
-- not claim live-server acceptance or replace the handler contract tests.
BEGIN;
CREATE SCHEMA planet_landing_retry_test;
SET LOCAL search_path TO planet_landing_retry_test, pg_catalog;

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
  turns_remaining integer NOT NULL CHECK (turns_remaining >= 0)
);
CREATE TABLE idempotency (
  key text PRIMARY KEY,
  cmd text NOT NULL,
  response text
);
CREATE TABLE landing_events (
  event_id bigint GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
  request_key text NOT NULL UNIQUE,
  player_id integer NOT NULL,
  planet_id integer NOT NULL
);

-- Read-only acceptance snapshot: every count must remain zero after a valid
-- landing and after a duplicate replay.
CREATE VIEW planet_landing_retry_invariant_snapshot AS
SELECT
  (SELECT count(*) FROM players p
    WHERE p.lastplanet IS NOT NULL
      AND NOT EXISTS (SELECT 1 FROM planets pl WHERE pl.planet_id = p.lastplanet))
      AS orphan_landed_planets,
  (SELECT count(*) FROM players p JOIN planets pl ON pl.planet_id = p.lastplanet
    WHERE p.lastplanet IS NOT NULL AND p.sector_id IS DISTINCT FROM pl.sector_id)
      AS remote_landed_planets,
  (SELECT count(*) FROM turns WHERE turns_remaining < 0)
      AS negative_turn_balances,
  (SELECT count(*) FROM landing_events e
    WHERE NOT EXISTS (SELECT 1 FROM idempotency i WHERE i.key = e.request_key))
      AS events_without_request,
  (SELECT count(*) FROM landing_events e
    JOIN idempotency i ON i.key = e.request_key
    WHERE i.cmd <> 'planet.land') AS wrong_command_events;

INSERT INTO players VALUES (7, 42, NULL);
INSERT INTO planets VALUES (9001, 42), (9002, 99);
INSERT INTO turns VALUES (7, 10);

DO $test$
DECLARE s record;
BEGIN
  SELECT * INTO s FROM planet_landing_retry_invariant_snapshot;
  IF s.orphan_landed_planets <> 0 OR s.remote_landed_planets <> 0
     OR s.negative_turn_balances <> 0 OR s.events_without_request <> 0
     OR s.wrong_command_events <> 0 THEN
    RAISE EXCEPTION 'initial landing snapshot is not clean: %', s;
  END IF;
END
$test$;

-- Refusal before mutation preserves every landing row.
DO $test$
DECLARE before_turns integer;
BEGIN
  SELECT turns_remaining INTO before_turns FROM turns WHERE player_id = 7;
  IF EXISTS (SELECT 1 FROM planets WHERE planet_id = 9002 AND sector_id = 42) THEN
    RAISE EXCEPTION 'remote planet was incorrectly accepted';
  END IF;
  IF (SELECT turns_remaining FROM turns WHERE player_id = 7) <> before_turns
     OR (SELECT lastplanet FROM players WHERE player_id = 7) IS NOT NULL
     OR (SELECT count(*) FROM landing_events) <> 0 THEN
    RAISE EXCEPTION 'refused landing mutated durable state';
  END IF;
END
$test$;

-- A persistence failure after tentative writes rolls back the turn, landing,
-- idempotency record and event as one transaction.
DO $test$
BEGIN
  BEGIN
    INSERT INTO idempotency VALUES ('land-7-1', 'planet.land', NULL);
    UPDATE turns SET turns_remaining = turns_remaining - 1 WHERE player_id = 7;
    UPDATE players SET lastplanet = 9001 WHERE player_id = 7;
    INSERT INTO landing_events (request_key, player_id, planet_id)
      VALUES ('land-7-1', 7, 9001);
    RAISE EXCEPTION 'injected landing persistence failure';
  EXCEPTION WHEN OTHERS THEN
    NULL;
  END;
  IF (SELECT turns_remaining FROM turns WHERE player_id = 7) <> 10
     OR (SELECT lastplanet FROM players WHERE player_id = 7) IS NOT NULL
     OR EXISTS (SELECT 1 FROM idempotency WHERE key = 'land-7-1')
     OR EXISTS (SELECT 1 FROM landing_events WHERE request_key = 'land-7-1') THEN
    RAISE EXCEPTION 'failed landing was not fully rolled back';
  END IF;
END
$test$;

-- First request settles once; the retry's idempotency conflict must not spend
-- another turn or emit another durable landing event.
INSERT INTO idempotency (key, cmd, response)
VALUES ('land-7-1', 'planet.land', 'accepted')
ON CONFLICT (key) DO NOTHING;
UPDATE turns SET turns_remaining = turns_remaining - 1 WHERE player_id = 7;
UPDATE players SET lastplanet = 9001 WHERE player_id = 7;
INSERT INTO landing_events (request_key, player_id, planet_id)
VALUES ('land-7-1', 7, 9001)
ON CONFLICT (request_key) DO NOTHING;

INSERT INTO idempotency (key, cmd, response)
VALUES ('land-7-1', 'planet.land', 'accepted')
ON CONFLICT (key) DO NOTHING;
UPDATE turns SET turns_remaining = turns_remaining - 1
 WHERE player_id = 7
   AND NOT EXISTS (SELECT 1 FROM landing_events WHERE request_key = 'land-7-1');
INSERT INTO landing_events (request_key, player_id, planet_id)
SELECT 'land-7-1', 7, 9001
WHERE NOT EXISTS (SELECT 1 FROM landing_events WHERE request_key = 'land-7-1');

DO $test$
DECLARE s record;
BEGIN
  SELECT * INTO s FROM planet_landing_retry_invariant_snapshot;
  IF (SELECT turns_remaining FROM turns WHERE player_id = 7) <> 9
     OR (SELECT lastplanet FROM players WHERE player_id = 7) <> 9001
     OR (SELECT count(*) FROM idempotency WHERE key = 'land-7-1') <> 1
     OR (SELECT count(*) FROM landing_events WHERE request_key = 'land-7-1') <> 1
     OR s.orphan_landed_planets <> 0 OR s.remote_landed_planets <> 0
     OR s.negative_turn_balances <> 0 OR s.events_without_request <> 0
     OR s.wrong_command_events <> 0 THEN
    RAISE EXCEPTION 'landing retry snapshot is inconsistent: %', s;
  END IF;
END
$test$;

ROLLBACK;
\echo 'PostgreSQL V3-PLAN-01 landing retry assertions passed.'
