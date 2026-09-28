-- Run with psql after applying the documented fresh install seed and migration
-- sequence through 114. This is an assertion-only check; it changes no rows.
\set ON_ERROR_STOP on

DO $test$
DECLARE
  v_missing text;
BEGIN
  SELECT string_agg(required.name, ', ' ORDER BY required.name) INTO v_missing
    FROM (VALUES
      ('sector_hazards'),
      ('ships.personality'),
      ('corporations.ship_personality'),
      ('shiptypes.default_personality'),
      ('ferengi_trader_definitions'),
      ('planettypes.fighterproduction'),
      ('planets.colonists_weapons'),
      ('planets.fighter_production_remainder'),
      ('planets.fighter_production_last_tick')
    ) AS required(name)
   WHERE CASE
     WHEN position('.' IN required.name) = 0 THEN to_regclass(required.name) IS NULL
     ELSE NOT EXISTS (
       SELECT 1 FROM information_schema.columns c
        WHERE c.table_schema = current_schema()
          AND c.table_name = split_part(required.name, '.', 1)
          AND c.column_name = split_part(required.name, '.', 2))
   END;

  IF v_missing IS NOT NULL THEN
    RAISE EXCEPTION 'fresh v2 schema is missing: %', v_missing;
  END IF;

  IF (SELECT COUNT(*) FROM planettypes WHERE code IN ('M','K','O','L','C','H','U')) <> 7 THEN
    RAISE EXCEPTION 'fresh v2 lookup seed is missing planet classes';
  END IF;

  IF (SELECT COUNT(*) FROM planettypes WHERE code IN ('M','K','O','L','C','H','U')
       AND fighterProduction IS NOT NULL) <> 7 THEN
    RAISE EXCEPTION 'fresh v2 schema did not seed fighter production factors';
  END IF;
END
$test$;

\echo 'Fresh PostgreSQL v2 migration schema checks passed.'
