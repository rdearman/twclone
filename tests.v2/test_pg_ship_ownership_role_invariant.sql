\set ON_ERROR_STOP on

-- REQUIREMENT_ID: V3-SHIP-02
-- Isolated proof for the ownership-role FK, upgrade idempotence, and the
-- diagnostic snapshot for pre-existing orphan role rows.
BEGIN;
CREATE SCHEMA ship_ownership_role_invariant_test;
SET LOCAL search_path TO ship_ownership_role_invariant_test, pg_catalog;

CREATE TABLE ship_roles (
  role_id bigint PRIMARY KEY,
  role text NOT NULL
);
CREATE TABLE ships (
  ship_id integer PRIMARY KEY
);
CREATE TABLE players (
  player_id integer PRIMARY KEY
);
CREATE TABLE ship_ownership (
  ship_id integer NOT NULL REFERENCES ships(ship_id),
  player_id integer NOT NULL REFERENCES players(player_id),
  role_id bigint NOT NULL,
  is_primary boolean NOT NULL DEFAULT TRUE,
  PRIMARY KEY (ship_id, player_id, role_id)
);

INSERT INTO ship_roles VALUES (1, 'owner');
INSERT INTO ships VALUES (10);
INSERT INTO players VALUES (20);

-- A NOT VALID upgrade must preserve legacy drift for diagnosis.
INSERT INTO ship_ownership VALUES (10, 20, 999, TRUE);

\ir ../sql/pg/159_ship_ownership_role_invariant.sql
\ir ../sql/pg/159_ship_ownership_role_invariant.sql

DO $test$
DECLARE
  rejected boolean := FALSE;
  orphan_count bigint;
BEGIN
  SELECT orphan_role_references INTO orphan_count
    FROM ship_ownership_role_invariant_snapshot;
  IF orphan_count <> 1 THEN
    RAISE EXCEPTION 'legacy orphan role was not exposed: %', orphan_count;
  END IF;

  INSERT INTO ship_ownership VALUES (10, 20, 1, TRUE);

  BEGIN
    INSERT INTO ship_ownership VALUES (10, 20, 998, FALSE);
  EXCEPTION WHEN foreign_key_violation THEN
    rejected := TRUE;
  END;

  IF NOT rejected THEN
    RAISE EXCEPTION 'new orphan ownership role was accepted';
  END IF;

  SELECT orphan_role_references INTO orphan_count
    FROM ship_ownership_role_invariant_snapshot;
  IF orphan_count <> 1 THEN
    RAISE EXCEPTION 'rejected write changed orphan snapshot: %', orphan_count;
  END IF;
END
$test$;

ROLLBACK;
\echo 'PostgreSQL ship ownership role invariant checks passed.'
