\set ON_ERROR_STOP on

-- REQUIREMENT_ID: V3-SHIP-02
-- Isolated upgrade/replay proof for the explicit primary-owner flag contract.
BEGIN;
CREATE SCHEMA ship_primary_flag_153_test;
SET LOCAL search_path TO ship_primary_flag_153_test, pg_catalog;

CREATE TABLE ship_ownership (
  ship_id integer NOT NULL,
  player_id integer NOT NULL,
  role_id integer NOT NULL,
  is_primary boolean
);

-- Existing legacy NULL rows are intentionally present before the upgrade.
INSERT INTO ship_ownership VALUES (1, 10, 1, NULL), (1, 20, 2, FALSE);

\ir ../sql/pg/153_ship_primary_flag_invariant.sql
SET search_path TO ship_primary_flag_153_test, pg_catalog;

DO $test$
DECLARE
  rejected boolean := FALSE;
  snapshot record;
BEGIN
  SELECT * INTO snapshot FROM ship_primary_flag_invariant_snapshot;
  IF snapshot.null_primary_flags <> 1 THEN
    RAISE EXCEPTION 'legacy NULL primary flag was not observable: %', snapshot;
  END IF;

  BEGIN
    INSERT INTO ship_ownership VALUES (2, 30, 1, NULL);
  EXCEPTION WHEN check_violation THEN
    rejected := TRUE;
  END;
  IF NOT rejected THEN
    RAISE EXCEPTION 'new NULL primary flag was accepted';
  END IF;
  IF EXISTS (
    SELECT 1 FROM ship_ownership
     WHERE ship_id = 2 AND player_id = 30
  ) THEN
    RAISE EXCEPTION 'rejected NULL ownership write left a row';
  END IF;

  INSERT INTO ship_ownership VALUES (2, 30, 1, TRUE);
  INSERT INTO ship_ownership VALUES (2, 40, 2, FALSE);
  UPDATE ship_ownership
     SET is_primary = FALSE
   WHERE ship_id = 1 AND player_id = 10;

  SELECT * INTO snapshot FROM ship_primary_flag_invariant_snapshot;
  IF snapshot.null_primary_flags <> 0 THEN
    RAISE EXCEPTION 'repaired ownership state still has NULL flags: %', snapshot;
  END IF;
END
$test$;

COMMIT;

BEGIN;
SET LOCAL search_path TO ship_primary_flag_153_test, pg_catalog;
DO $test$
DECLARE snapshot record;
BEGIN
  SELECT * INTO snapshot FROM ship_primary_flag_invariant_snapshot;
  IF snapshot.null_primary_flags <> 0 THEN
    RAISE EXCEPTION 'post-commit ownership snapshot is dirty: %', snapshot;
  END IF;
END
$test$;
ROLLBACK;
\echo 'PostgreSQL ship primary-flag invariant checks passed.'
