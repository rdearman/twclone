-- Run against a disposable initialized PostgreSQL database. The migration
-- commits by design, so this test leaves the intended v2 factor defaults set.
\set ON_ERROR_STOP on

UPDATE planettypes SET fighterProduction = 77 WHERE code = 'M';
DELETE FROM planets WHERE name = 'migration-114-preservation-fixture';
INSERT INTO planets (sector_id, name, owner_id, owner_type, class, population,
                    created_by, colonists_unassigned, colonists_mil)
VALUES (1, 'migration-114-preservation-fixture', 1, 'player', 'M', 123,
        1, 100, 23);
UPDATE planets
   SET population = 123, colonists_unassigned = 100,
       colonists_weapons = 0, colonists_mil = 23
 WHERE name = 'migration-114-preservation-fixture';
\ir ../sql/pg/114_planet_fighter_production.sql

DO $test$
DECLARE
  v_nullable text;
  v_default text;
BEGIN
  SELECT is_nullable, column_default INTO v_nullable, v_default
    FROM information_schema.columns
   WHERE table_schema = current_schema()
     AND table_name = 'planets'
     AND column_name = 'fighter_production_remainder';
  IF v_nullable IS DISTINCT FROM 'NO' OR v_default IS NULL THEN
    RAISE EXCEPTION 'fighter production remainder must be non-null with a default';
  END IF;

  SELECT is_nullable, column_default INTO v_nullable, v_default
    FROM information_schema.columns
   WHERE table_schema = current_schema()
     AND table_name = 'planets'
     AND column_name = 'fighter_production_last_tick';
  IF v_nullable IS DISTINCT FROM 'NO' OR v_default IS NULL THEN
    RAISE EXCEPTION 'fighter production tick marker must be non-null with a default';
  END IF;

  IF (SELECT fighterProduction FROM planettypes WHERE code = 'M') <> 77 THEN
    RAISE EXCEPTION 'migration overwrote a nonzero custom class factor';
  END IF;
  IF NOT EXISTS (SELECT 1 FROM planets
                  WHERE name = 'migration-114-preservation-fixture' AND population = 123
                    AND colonists_unassigned = 100 AND colonists_weapons = 23
                    AND colonists_mil = 0) THEN
    RAISE EXCEPTION 'legacy military assignment was not preserved as weapons workers';
  END IF;
END
$test$;

-- Clear the test override, then replay to verify defaults and idempotency.
UPDATE planettypes SET fighterProduction = 0 WHERE code = 'M';
\ir ../sql/pg/114_planet_fighter_production.sql

DO $test$
BEGIN
  IF (SELECT fighterProduction FROM planettypes WHERE code = 'K') <> 15
     OR (SELECT fighterProduction FROM planettypes WHERE code = 'M') <> 10
     OR (SELECT fighterProduction FROM planettypes WHERE code = 'O') <> 15
     OR (SELECT fighterProduction FROM planettypes WHERE code = 'L') <> 12
     OR (SELECT fighterProduction FROM planettypes WHERE code = 'C') <> 25
     OR (SELECT fighterProduction FROM planettypes WHERE code = 'H') <> 50
     OR (SELECT fighterProduction FROM planettypes WHERE code = 'U') <> 0 THEN
    RAISE EXCEPTION 'built-in class fighter factors were not initialized';
  END IF;
END
$test$;

DELETE FROM planets WHERE name = 'migration-114-preservation-fixture';

\echo 'Planet fighter production migration replay/configuration checks passed.'
