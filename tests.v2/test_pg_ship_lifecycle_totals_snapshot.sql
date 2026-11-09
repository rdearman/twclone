\set ON_ERROR_STOP on

-- REQUIREMENT_ID: V3-SHIP-02
-- The totals view is an isolated persistence contract.  It deliberately does
-- not install production migrations or touch the shared database catalog.
BEGIN;
CREATE SCHEMA ship_lifecycle_totals_151_test;
SET LOCAL search_path TO ship_lifecycle_totals_151_test, pg_catalog;

CREATE TABLE ships (
  ship_id integer PRIMARY KEY,
  holds integer NOT NULL,
  colonists bigint NOT NULL DEFAULT 0,
  ore bigint NOT NULL DEFAULT 0,
  organics bigint NOT NULL DEFAULT 0,
  equipment bigint NOT NULL DEFAULT 0,
  slaves bigint NOT NULL DEFAULT 0,
  weapons bigint NOT NULL DEFAULT 0,
  drugs bigint NOT NULL DEFAULT 0
);
CREATE TABLE players (
  player_id integer PRIMARY KEY,
  ship_id integer REFERENCES ships(ship_id)
);
CREATE TABLE ship_ownership (
  ship_id integer NOT NULL,
  player_id integer NOT NULL,
  is_primary boolean NOT NULL
);
CREATE TABLE ship_cargo (
  ship_id integer NOT NULL,
  commodity_code text NOT NULL,
  quantity bigint NOT NULL CHECK (quantity >= 0)
);

\ir ../sql/pg/151_ship_lifecycle_totals_snapshot.sql
SET search_path TO ship_lifecycle_totals_151_test, pg_catalog;

INSERT INTO ships (ship_id, holds, ore) VALUES (1, 10, 3), (2, 10, 0);
INSERT INTO players VALUES (10, 1), (20, NULL);
INSERT INTO ship_ownership VALUES (1, 10, TRUE), (2, 20, TRUE);
INSERT INTO ship_cargo VALUES (1, 'ORE', 3);

DO $test$
DECLARE r record;
BEGIN
  SELECT * INTO r FROM ship_lifecycle_totals_snapshot;
  IF r.active_ship_count <> 1
     OR r.primary_owned_ship_count <> 2
     OR r.dynamic_cargo_units <> 3
     OR r.legacy_cargo_units <> 3
     OR r.dynamic_minus_legacy_cargo_units <> 0
     OR r.active_over_capacity_ships <> 0
     OR r.active_owner_mismatches <> 0 THEN
    RAISE EXCEPTION 'clean lifecycle totals snapshot is incorrect: %', r;
  END IF;

  -- A failed cargo settlement must be rolled back before the next snapshot;
  -- this models the persistence assertion required around transfer/sale.
  BEGIN
    INSERT INTO ship_cargo VALUES (1, 'ORG', 8);
    RAISE EXCEPTION 'over-capacity settlement was accepted';
  EXCEPTION WHEN OTHERS THEN
    NULL;
  END;
  IF (SELECT quantity FROM ship_cargo
       WHERE ship_id = 1 AND commodity_code = 'ORE') <> 3
     OR EXISTS (SELECT 1 FROM ship_cargo
                 WHERE ship_id = 1 AND commodity_code = 'ORG') THEN
    RAISE EXCEPTION 'failed cargo settlement left persisted rows';
  END IF;
END
$test$;

COMMIT;

BEGIN;
SET LOCAL search_path TO ship_lifecycle_totals_151_test, pg_catalog;
DO $test$
DECLARE r record;
BEGIN
  SELECT * INTO r FROM ship_lifecycle_totals_snapshot;
  IF r.dynamic_minus_legacy_cargo_units <> 0
     OR r.active_over_capacity_ships <> 0
     OR r.active_owner_mismatches <> 0 THEN
    RAISE EXCEPTION 'post-commit lifecycle totals snapshot is dirty: %', r;
  END IF;
END
$test$;
ROLLBACK;
\echo 'PostgreSQL ship lifecycle totals snapshot checks passed.'
