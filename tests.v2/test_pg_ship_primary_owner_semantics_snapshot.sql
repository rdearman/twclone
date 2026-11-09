\set ON_ERROR_STOP on

-- V3-SHIP-02: primary ownership is a lifecycle invariant, not just a
-- uniqueness constraint. Keep this isolated from the production schema so
-- legacy drift can be asserted without modifying shared data.
BEGIN;
CREATE SCHEMA ship_primary_owner_semantics_test;
SET LOCAL search_path TO ship_primary_owner_semantics_test, pg_catalog;

CREATE TABLE ships (ship_id integer PRIMARY KEY);
CREATE TABLE players (player_id integer PRIMARY KEY);
CREATE TABLE ship_ownership (
  ship_id integer NOT NULL REFERENCES ships(ship_id),
  player_id integer NOT NULL REFERENCES players(player_id),
  role_id bigint NOT NULL DEFAULT 1,
  is_primary boolean
);

\ir ../sql/pg/148_ship_primary_owner_semantics_snapshot.sql
SET search_path TO ship_primary_owner_semantics_test, pg_catalog;

INSERT INTO ships VALUES (1), (2), (3), (4);
INSERT INTO players VALUES (10), (20), (30), (40);
INSERT INTO ship_ownership VALUES
  (1, 10, 1, TRUE),
  (2, 20, 1, FALSE),
  (3, 30, 1, NULL),
  (4, 40, 1, TRUE),
  (4, 20, 2, TRUE);

DO $test$
DECLARE snapshot record;
BEGIN
  SELECT * INTO snapshot FROM ship_primary_owner_semantics_snapshot;
  IF snapshot.owned_ships_without_primary <> 2
     OR snapshot.null_primary_flags <> 1
     OR snapshot.duplicate_primary_owners <> 1 THEN
    RAISE EXCEPTION 'unexpected primary-owner semantic snapshot: %', snapshot;
  END IF;

  DELETE FROM ship_ownership WHERE ship_id IN (2, 3, 4);
  INSERT INTO ship_ownership VALUES (2, 20, 1, TRUE);
  SELECT * INTO snapshot FROM ship_primary_owner_semantics_snapshot;
  IF snapshot.owned_ships_without_primary <> 0
     OR snapshot.null_primary_flags <> 0
     OR snapshot.duplicate_primary_owners <> 0 THEN
    RAISE EXCEPTION 'repaired primary-owner snapshot is not clean: %', snapshot;
  END IF;
END
$test$;

ROLLBACK;
\echo 'PostgreSQL primary-owner semantic snapshot checks passed.'
