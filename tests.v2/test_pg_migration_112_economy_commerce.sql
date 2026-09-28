-- Run against a disposable, initialized PostgreSQL database.
\set ON_ERROR_STOP on

\ir ../sql/pg/112_v2_economy_commerce.sql
\ir ../sql/pg/112_v2_economy_commerce.sql

DO $test$
BEGIN
  IF to_regclass('port_hardware_stock') IS NULL
     OR to_regclass('ferengi_traders') IS NULL
     OR to_regclass('ferengi_trader_deals') IS NULL
     OR to_regclass('ferengi_trader_interactions') IS NULL
     OR to_regclass('ferengi_player_relationships') IS NULL
     OR to_regclass('market_shocks') IS NULL
     OR to_regclass('planet_tax_policies') IS NULL
     OR to_regclass('planet_economic_activity') IS NULL
     OR to_regclass('planet_tax_assessments') IS NULL THEN
  RAISE EXCEPTION 'one or more v2 economy/commerce tables are missing';
  END IF;

  IF NOT EXISTS (SELECT 1 FROM pg_indexes
                 WHERE schemaname = current_schema()
                   AND indexname = 'idx_market_shocks_active_scope') THEN
    RAISE EXCEPTION 'market shock active lookup index is missing';
  END IF;

  IF NOT EXISTS (SELECT 1 FROM pg_indexes
                 WHERE schemaname = current_schema()
                   AND indexname = 'idx_port_hardware_stock_restock_due') THEN
    RAISE EXCEPTION 'hardware restock due index is missing';
  END IF;
END
$test$;

-- Check key exactly-once and range constraints without retaining test rows.
BEGIN;
DO $constraints$
DECLARE
  v_port integer;
  v_item integer;
  v_planet integer;
BEGIN
  DELETE FROM market_shocks
   WHERE idempotency_key LIKE 'v2-migration-112-%';

  SELECT p.port_id, hi.hardware_items_id INTO v_port, v_item
    FROM ports p CROSS JOIN hardware_items hi
    WHERE hi.enabled = TRUE LIMIT 1;
  IF v_port IS NOT NULL THEN
    INSERT INTO port_hardware_stock
      (port_id, hardware_items_id, stock_quantity, max_stock,
       restock_quantity, restock_interval_seconds, next_restock_at)
    VALUES (v_port, v_item, 4, 10, 2, 86400, CURRENT_TIMESTAMP);
    BEGIN
      INSERT INTO port_hardware_stock
        (port_id, hardware_items_id, stock_quantity, max_stock,
         restock_quantity, restock_interval_seconds, next_restock_at)
      VALUES (v_port, v_item, 4, 10, 2, 86400, CURRENT_TIMESTAMP);
      RAISE EXCEPTION 'duplicate hardware inventory unexpectedly succeeded';
    EXCEPTION WHEN unique_violation THEN NULL;
    END;
  END IF;

  SELECT planet_id INTO v_planet FROM planets LIMIT 1;
  IF v_planet IS NOT NULL THEN
    INSERT INTO planet_tax_assessments
      (planet_id, tax_date, owner_type, owner_id, taxable_value, rate_bps,
       tax_amount)
    VALUES (v_planet, CURRENT_DATE, 'player', 1, 1000, 500, 50);
    BEGIN
      INSERT INTO planet_tax_assessments
        (planet_id, tax_date, owner_type, owner_id, taxable_value, rate_bps,
         tax_amount)
      VALUES (v_planet, CURRENT_DATE, 'player', 1, 1000, 500, 50);
      RAISE EXCEPTION 'duplicate daily tax assessment unexpectedly succeeded';
    EXCEPTION WHEN unique_violation THEN NULL;
    END;
  END IF;

  INSERT INTO market_shocks
    (trigger_source, scope_type, multiplier_bps, starts_at, expires_at,
     idempotency_key)
  VALUES ('random', 'universe', 10000, CURRENT_TIMESTAMP,
          CURRENT_TIMESTAMP + INTERVAL '1 hour',
          'v2-migration-112-random-trigger-contract');
  IF NOT EXISTS (SELECT 1 FROM market_shocks
                 WHERE idempotency_key = 'v2-migration-112-random-trigger-contract'
                   AND trigger_source = 'random'
                   AND scope_type = 'universe') THEN
    RAISE EXCEPTION 'valid random universe shock was not persisted';
  END IF;

  BEGIN
    INSERT INTO market_shocks
      (trigger_source, scope_type, multiplier_bps, starts_at, expires_at,
       idempotency_key)
    VALUES ('invalid', 'universe', 10000, CURRENT_TIMESTAMP,
            CURRENT_TIMESTAMP + INTERVAL '1 hour',
            'v2-migration-112-invalid-trigger');
    RAISE EXCEPTION 'invalid shock trigger source unexpectedly succeeded';
  EXCEPTION WHEN check_violation THEN NULL;
  END;

  BEGIN
    INSERT INTO market_shocks
      (trigger_source, scope_type, multiplier_bps, starts_at, expires_at,
       idempotency_key)
    VALUES ('random', 'sector', 10000, CURRENT_TIMESTAMP,
            CURRENT_TIMESTAMP + INTERVAL '1 hour',
            'v2-migration-112-invalid-scope');
    RAISE EXCEPTION 'incomplete sector shock scope unexpectedly succeeded';
  EXCEPTION WHEN check_violation THEN NULL;
  END;

  BEGIN
    INSERT INTO market_shocks
      (trigger_source, scope_type, multiplier_bps, starts_at, expires_at,
       idempotency_key)
    VALUES ('random', 'universe', 50000, CURRENT_TIMESTAMP,
            CURRENT_TIMESTAMP + INTERVAL '1 hour',
            'v2-migration-112-invalid-multiplier');
    RAISE EXCEPTION 'out-of-range shock multiplier unexpectedly succeeded';
  EXCEPTION WHEN check_violation THEN NULL;
  END;

  BEGIN
    INSERT INTO market_shocks
      (trigger_source, scope_type, multiplier_bps, starts_at, expires_at,
       idempotency_key)
    VALUES ('random', 'universe', 10000, CURRENT_TIMESTAMP,
            CURRENT_TIMESTAMP, 'v2-migration-112-invalid-expiry');
    RAISE EXCEPTION 'non-positive shock duration unexpectedly succeeded';
  EXCEPTION WHEN check_violation THEN NULL;
  END;

  IF v_planet IS NOT NULL THEN
    BEGIN
      INSERT INTO planet_tax_policies (planet_id, rate_bps, enabled)
      VALUES (v_planet, 10001, TRUE)
      ON CONFLICT (planet_id) DO UPDATE SET rate_bps = EXCLUDED.rate_bps;
      RAISE EXCEPTION 'out-of-range planetary tax rate unexpectedly succeeded';
    EXCEPTION WHEN check_violation THEN NULL;
    END;
  END IF;
END
$constraints$;
ROLLBACK;

\echo 'PostgreSQL migration 112 passed: economy/commerce schema and idempotency constraints.'
