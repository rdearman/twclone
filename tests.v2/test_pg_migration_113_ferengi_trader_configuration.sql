-- Run after the core lookup seed and migration 112 on a disposable database.
\set ON_ERROR_STOP on

\ir ../sql/pg/113_ferengi_trader_configuration.sql
\ir ../sql/pg/113_ferengi_trader_configuration.sql

DO $test$
DECLARE
  v_count integer;
  v_lifetime integer;
  v_rotation text[];
BEGIN
  SELECT COUNT(*) INTO v_count FROM ferengi_trader_definitions WHERE active = TRUE;
  IF v_count <> 3 THEN
    RAISE EXCEPTION 'expected three default data-driven Ferengi traders, got %', v_count;
  END IF;

  SELECT array_agg(commodity_code ORDER BY position) INTO v_rotation
    FROM ferengi_trader_definition_rotation WHERE trader_code = 'gaila';
  IF v_rotation IS DISTINCT FROM ARRAY['ORE', 'ORG', 'EQU']::text[]
     OR NOT EXISTS (SELECT 1 FROM ferengi_trader_definition_cargo
                    WHERE trader_code = 'gaila' AND commodity_code = 'ORE'
                      AND quantity = 30) THEN
    RAISE EXCEPTION 'Ferengi roster or starting cargo seed is incomplete';
  END IF;

  SELECT value::integer INTO v_lifetime FROM config
    WHERE key = 'ferengi.offer_lifetime_seconds' AND type = 'int';
  IF v_lifetime <> 21600 THEN
    RAISE EXCEPTION 'default Ferengi offer lifetime must remain six hours';
  END IF;
END
$test$;

\echo 'PostgreSQL migration 113 passed: Ferengi roster configuration is data-driven.'
