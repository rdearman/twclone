\set ON_ERROR_STOP on

-- V3-SHIP-02: ownership drift is observable without mutating a production
-- catalog.  The fixture omits foreign keys deliberately so legacy violations
-- can be represented and checked by the snapshot.
BEGIN;
CREATE SCHEMA ship_ownership_snapshot_138_test;
SET LOCAL search_path TO ship_ownership_snapshot_138_test, pg_catalog;

CREATE TABLE ships (ship_id integer PRIMARY KEY);
CREATE TABLE players (player_id integer PRIMARY KEY);
CREATE TABLE ship_ownership (
  ship_id integer NOT NULL,
  player_id integer NOT NULL,
  is_primary boolean NOT NULL
);

\ir ../sql/pg/138_ship_ownership_invariant_snapshot.sql
SET search_path TO ship_ownership_snapshot_138_test, pg_catalog;

INSERT INTO ships VALUES (1), (2);
INSERT INTO players VALUES (10), (20);
INSERT INTO ship_ownership VALUES (1, 10, TRUE), (2, 20, TRUE);

DO $test$
DECLARE r record;
BEGIN
  SELECT * INTO r FROM ship_ownership_invariant_snapshot;
  IF r.orphan_ship_ownership <> 0
     OR r.orphan_player_ownership <> 0
     OR r.duplicate_primary_owners <> 0 THEN
    RAISE EXCEPTION 'clean ownership snapshot reported violations: %', r;
  END IF;

  INSERT INTO ship_ownership VALUES (404, 10, FALSE), (1, 404, FALSE),
                                    (1, 20, TRUE);
  SELECT * INTO r FROM ship_ownership_invariant_snapshot;
  IF r.orphan_ship_ownership <> 1
     OR r.orphan_player_ownership <> 1
     OR r.duplicate_primary_owners <> 1 THEN
    RAISE EXCEPTION 'ownership drift was not fully reported: %', r;
  END IF;

  DELETE FROM ship_ownership
   WHERE (ship_id, player_id, is_primary) IN
         ((404, 10, FALSE), (1, 404, FALSE), (1, 20, TRUE));
  SELECT * INTO r FROM ship_ownership_invariant_snapshot;
  IF r.orphan_ship_ownership <> 0
     OR r.orphan_player_ownership <> 0
     OR r.duplicate_primary_owners <> 0 THEN
    RAISE EXCEPTION 'restored ownership snapshot reported violations: %', r;
  END IF;
END
$test$;

ROLLBACK;
\echo 'PostgreSQL ship ownership invariant snapshot checks passed.'
