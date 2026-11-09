\set ON_ERROR_STOP on

-- REQUIREMENT_ID: V3-SHIP-02
-- Isolated proof for the persistence shape consumed by ship.list.  The test
-- deliberately omits production foreign keys so legacy ownership drift can be
-- represented without mutating a shared catalog.
BEGIN;
CREATE SCHEMA ship_list_snapshot_157_test;
SET LOCAL search_path TO ship_list_snapshot_157_test, pg_catalog;

CREATE TABLE ships (
  ship_id integer PRIMARY KEY,
  sector_id integer NOT NULL
);
CREATE TABLE players (
  player_id integer PRIMARY KEY,
  ship_id integer
);
CREATE TABLE ship_ownership (
  ship_id integer NOT NULL,
  player_id integer NOT NULL,
  is_primary boolean
);

\ir ../sql/pg/157_ship_list_invariant_snapshot.sql
SET search_path TO ship_list_snapshot_157_test, pg_catalog;

INSERT INTO ships VALUES (1, 10), (2, 20), (3, 30);
INSERT INTO players VALUES (11, 1), (22, NULL), (33, 3);
INSERT INTO ship_ownership VALUES
  (1, 11, TRUE),
  (2, 22, TRUE),
  (3, 33, TRUE),
  (3, 33, FALSE);

DO $test$
DECLARE snapshot record;
BEGIN
  SELECT * INTO snapshot FROM ship_list_invariant_snapshot;
  IF snapshot.primary_ownership_rows <> 3
     OR snapshot.distinct_primary_ships <> 3
     OR snapshot.distinct_list_ships <> 3
     OR snapshot.active_list_rows <> 2
     OR snapshot.inactive_list_rows <> 1
     OR snapshot.missing_list_rows <> 0
     OR snapshot.duplicate_list_rows <> 0
     OR snapshot.active_flag_row_drift <> 0
     OR snapshot.distinct_ship_drift <> 0 THEN
    RAISE EXCEPTION 'clean ship.list snapshot is incorrect: %', snapshot;
  END IF;

  -- A duplicate primary row is legacy drift: the view must expose that the
  -- list has an extra row even though it still represents three ship IDs.
  INSERT INTO ship_ownership VALUES (1, 11, TRUE);
  SELECT * INTO snapshot FROM ship_list_invariant_snapshot;
  IF snapshot.primary_ownership_rows <> 4
     OR snapshot.distinct_primary_ships <> 3
     OR snapshot.missing_list_rows <> 0
     OR snapshot.duplicate_list_rows <> 1
     OR snapshot.active_flag_row_drift <> 0 THEN
    RAISE EXCEPTION 'duplicate primary ownership was not exposed: %', snapshot;
  END IF;

  -- Restore the duplicate and prove restart-style readback returns to the
  -- same clean population/active-flag contract.
  DELETE FROM ship_ownership
   WHERE ship_id = 1 AND player_id = 11 AND is_primary IS TRUE
     AND ctid IN (SELECT ctid FROM ship_ownership
                   WHERE ship_id = 1 AND player_id = 11 AND is_primary IS TRUE
                   OFFSET 1);
  SELECT * INTO snapshot FROM ship_list_invariant_snapshot;
  IF snapshot.primary_ownership_rows <> 3
     OR snapshot.missing_list_rows <> 0
     OR snapshot.duplicate_list_rows <> 0
     OR snapshot.active_flag_row_drift <> 0
     OR snapshot.distinct_ship_drift <> 0 THEN
    RAISE EXCEPTION 'ship.list snapshot did not recover after repair: %', snapshot;
  END IF;
END
$test$;

ROLLBACK;
\echo 'PostgreSQL ship.list invariant snapshot checks passed.'
