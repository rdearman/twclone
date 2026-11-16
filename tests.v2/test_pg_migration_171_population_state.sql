\set ON_ERROR_STOP on

-- Run only in the disposable PostgreSQL QA cluster. The isolated schema lets
-- the migration create its foreign-keyed event table while keeping game data
-- and public-schema objects untouched.
DROP SCHEMA IF EXISTS twclone_population_migration_test CASCADE;
CREATE SCHEMA twclone_population_migration_test;
SET search_path = twclone_population_migration_test, public;

CREATE TABLE planets (
  planet_id integer PRIMARY KEY,
  population bigint NOT NULL DEFAULT 0,
  colonists_unassigned bigint NOT NULL DEFAULT 0,
  colonists_ore bigint NOT NULL DEFAULT 0,
  colonists_org bigint NOT NULL DEFAULT 0,
  colonists_eq bigint NOT NULL DEFAULT 0,
  colonists_weapons bigint NOT NULL DEFAULT 0
);

INSERT INTO planets (planet_id, population, colonists_unassigned,
                     colonists_ore, colonists_org, colonists_eq,
                     colonists_weapons)
VALUES (1, 100, 20, 10, 20, 30, 20),
       (2, 0, 0, 0, 0, 0, 0);

\ir ../sql/pg/171_v3_planet_population_state.sql

DO $migration$
BEGIN
  IF (SELECT population_type FROM planets WHERE planet_id=1) <> 'COLONIST'
     OR (SELECT population FROM planets WHERE planet_id=1) <> 100
     OR (SELECT colonists_unassigned + colonists_ore + colonists_org
                + colonists_eq + colonists_weapons
           FROM planets WHERE planet_id=1) <> 100 THEN
    RAISE EXCEPTION 'migration failed to preserve populated-planet count/allocation';
  END IF;
  IF (SELECT population_type FROM planets WHERE planet_id=2) <> 'UNPOPULATED'
     OR (SELECT population FROM planets WHERE planet_id=2) <> 0 THEN
    RAISE EXCEPTION 'migration changed an empty planet';
  END IF;
END
$migration$;

-- Legacy planet-generation writes that omit the new enum must be classified
-- without changing their population or worker counts.
INSERT INTO planets (planet_id, population, colonists_unassigned)
VALUES (3, 0, 7);
DO $new_planet$
BEGIN
  IF (SELECT population_type FROM planets WHERE planet_id=3) <> 'COLONIST'
     OR (SELECT population FROM planets WHERE planet_id=3) <> 0
     OR (SELECT colonists_unassigned FROM planets WHERE planet_id=3) <> 7 THEN
    RAISE EXCEPTION 'new populated legacy-style planet was not conserved/classified';
  END IF;
END
$new_planet$;

UPDATE planets SET population=45, population_type='SLAVE',
                   colonists_unassigned=20, colonists_ore=10,
                   colonists_org=5, colonists_eq=5, colonists_weapons=5
WHERE planet_id=3;
UPDATE planets SET colonists_org=colonists_org WHERE planet_id=3;
DO $slave$
BEGIN
  IF (SELECT population_type FROM planets WHERE planet_id=3) <> 'SLAVE'
     OR (SELECT population FROM planets WHERE planet_id=3) <> 45
     OR (SELECT colonists_unassigned + colonists_ore + colonists_org
                + colonists_eq + colonists_weapons
           FROM planets WHERE planet_id=3) <> 45 THEN
    RAISE EXCEPTION 'ordinary update changed slave classification or population accounting';
  END IF;
END
$slave$;

\echo '171 population migration conservation: PASS'

DROP SCHEMA twclone_population_migration_test CASCADE;
