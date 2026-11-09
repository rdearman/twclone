\set ON_ERROR_STOP on

-- REQUIREMENT_ID: V3-PLAN-01
-- Isolated persistence assertions for the handler's transaction contract.  The
-- fixture deliberately models only the rows that must move together; it does
-- not claim live-server acceptance or replace the command-handler tests.
BEGIN;
CREATE SCHEMA planet_landing_txn_test;
SET LOCAL search_path TO planet_landing_txn_test, pg_catalog;

CREATE TABLE players (
  player_id integer PRIMARY KEY,
  sector_id integer NOT NULL,
  lastplanet integer
);
CREATE TABLE planets (
  planet_id integer PRIMARY KEY,
  sector_id integer NOT NULL
);
CREATE TABLE ships (
  ship_id integer PRIMARY KEY,
  onplanet boolean NOT NULL DEFAULT false
);
CREATE TABLE turns (
  player_id integer PRIMARY KEY,
  turns_remaining integer NOT NULL CHECK (turns_remaining >= 0)
);
CREATE TABLE system_events (
  system_events_id integer GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
  event_type text NOT NULL,
  player_id integer NOT NULL,
  planet_id integer,
  created_at timestamptz NOT NULL DEFAULT current_timestamp
);

INSERT INTO players (player_id, sector_id, lastplanet) VALUES (7, 42, NULL);
INSERT INTO planets (planet_id, sector_id) VALUES (9001, 42), (9002, 99);
INSERT INTO ships (ship_id, onplanet) VALUES (70, false);
UPDATE players SET lastplanet = NULL WHERE player_id = 7;
INSERT INTO turns (player_id, turns_remaining) VALUES (7, 10);

-- Refusal is checked before any state mutation, including ship/event state.
DO $test$
DECLARE
  before_turns integer;
  before_planet integer;
  before_onplanet boolean;
  before_events bigint;
BEGIN
  SELECT turns_remaining INTO before_turns FROM turns WHERE player_id = 7;
  SELECT lastplanet INTO before_planet FROM players WHERE player_id = 7;
  SELECT onplanet INTO before_onplanet FROM ships WHERE ship_id = 70;
  SELECT count(*) INTO before_events FROM system_events WHERE player_id = 7;

  IF EXISTS (SELECT 1 FROM planets WHERE planet_id = 9002 AND sector_id = 42) THEN
    RAISE EXCEPTION 'remote planet was incorrectly accepted';
  END IF;

  IF (SELECT turns_remaining FROM turns WHERE player_id = 7) <> before_turns
     OR (SELECT lastplanet FROM players WHERE player_id = 7) IS DISTINCT FROM before_planet
     OR (SELECT onplanet FROM ships WHERE ship_id = 70) <> before_onplanet
     OR (SELECT count(*) FROM system_events WHERE player_id = 7) <> before_events THEN
    RAISE EXCEPTION 'refused landing changed persistent state';
  END IF;
END
$test$;

-- A failure after all tentative writes must roll back every landing side effect.
DO $test$
DECLARE
  before_turns integer;
  before_planet integer;
  before_onplanet boolean;
  before_events bigint;
BEGIN
  SELECT turns_remaining INTO before_turns FROM turns WHERE player_id = 7;
  SELECT lastplanet INTO before_planet FROM players WHERE player_id = 7;
  SELECT onplanet INTO before_onplanet FROM ships WHERE ship_id = 70;
  SELECT count(*) INTO before_events FROM system_events WHERE player_id = 7;

  BEGIN
    UPDATE turns SET turns_remaining = turns_remaining - 1 WHERE player_id = 7;
    UPDATE players SET lastplanet = 9001 WHERE player_id = 7;
    UPDATE ships SET onplanet = true WHERE ship_id = 70;
    INSERT INTO system_events (event_type, player_id, planet_id)
      VALUES ('planet.land', 7, 9001);
    RAISE EXCEPTION 'injected landing persistence failure';
  EXCEPTION WHEN OTHERS THEN
    NULL;
  END;

  IF (SELECT turns_remaining FROM turns WHERE player_id = 7) <> before_turns
     OR (SELECT lastplanet FROM players WHERE player_id = 7) IS DISTINCT FROM before_planet
     OR (SELECT onplanet FROM ships WHERE ship_id = 70) <> before_onplanet
     OR (SELECT count(*) FROM system_events WHERE player_id = 7) <> before_events THEN
    RAISE EXCEPTION 'failed landing did not roll back all persistent state';
  END IF;
END
$test$;

-- A valid landing settles exactly one turn and one event with consistent state.
BEGIN;
UPDATE turns SET turns_remaining = turns_remaining - 1 WHERE player_id = 7;
UPDATE players SET lastplanet = 9001 WHERE player_id = 7;
UPDATE ships SET onplanet = true WHERE ship_id = 70;
INSERT INTO system_events (event_type, player_id, planet_id)
  VALUES ('planet.land', 7, 9001);
COMMIT;

DO $test$
DECLARE
  snapshot record;
BEGIN
  SELECT
    (SELECT turns_remaining FROM turns WHERE player_id = 7) AS turns,
    (SELECT lastplanet FROM players WHERE player_id = 7) AS planet,
    (SELECT onplanet FROM ships WHERE ship_id = 70) AS onplanet,
    (SELECT count(*) FROM system_events
      WHERE player_id = 7 AND event_type = 'planet.land' AND planet_id = 9001) AS events
  INTO snapshot;

  IF snapshot.turns <> 9 OR snapshot.planet <> 9001
     OR snapshot.onplanet IS DISTINCT FROM true OR snapshot.events <> 1 THEN
    RAISE EXCEPTION 'successful landing snapshot is inconsistent: %', snapshot;
  END IF;

  IF (SELECT count(*) FROM turns WHERE player_id = 7) <> 1
     OR (SELECT count(*) FROM system_events WHERE player_id = 7) <> 1 THEN
    RAISE EXCEPTION 'successful landing created duplicate durable rows';
  END IF;
END
$test$;

ROLLBACK;
\echo 'PostgreSQL V3-PLAN-01 transaction assertions passed.'
