\set ON_ERROR_STOP on
BEGIN;
CREATE SCHEMA ship_owner_126_test;
SET LOCAL search_path TO ship_owner_126_test, pg_catalog;
CREATE TABLE ships (ship_id integer PRIMARY KEY);
CREATE TABLE players (player_id integer PRIMARY KEY);
CREATE TABLE ship_ownership (
  ship_id integer NOT NULL REFERENCES ships(ship_id),
  player_id integer NOT NULL REFERENCES players(player_id),
  role_id bigint NOT NULL, is_primary boolean DEFAULT TRUE,
  PRIMARY KEY (ship_id, player_id, role_id)
);
\ir ../sql/pg/126_ship_primary_ownership.sql
SET search_path TO ship_owner_126_test, pg_catalog;
INSERT INTO ships VALUES (1), (2);
INSERT INTO players VALUES (10), (11);
INSERT INTO ship_ownership VALUES (1,10,1,TRUE), (1,11,2,FALSE);
DO $test$
BEGIN
  BEGIN
    INSERT INTO ship_ownership VALUES (1,11,3,TRUE);
    RAISE EXCEPTION 'multiple primary owners accepted';
  EXCEPTION WHEN unique_violation THEN NULL;
  END;
  INSERT INTO ship_ownership VALUES (1,11,4,FALSE);
  IF (SELECT count(*) FROM ship_ownership WHERE ship_id=1 AND is_primary) <> 1 THEN
    RAISE EXCEPTION 'primary ownership invariant changed';
  END IF;
END
$test$;
ROLLBACK;
\echo 'PostgreSQL primary ship ownership checks passed.'
