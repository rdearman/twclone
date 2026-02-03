-- 200_gameplay_procs.sql
-- Gameplay mutation stored procedures (FIXED with full explicit casts).

BEGIN;

-- -----------------------------------------------------------------------------
-- Helpers (shared)
-- -----------------------------------------------------------------------------
CREATE OR REPLACE FUNCTION assert_table_exists(p_table regclass)
RETURNS void LANGUAGE plpgsql AS $$
BEGIN
  PERFORM 1;
END;
$$;

CREATE OR REPLACE FUNCTION assert_column_exists(p_table regclass, p_col text)
RETURNS void
LANGUAGE plpgsql
AS $$
DECLARE
  v_exists boolean;
BEGIN
  SELECT EXISTS (
    SELECT 1
    FROM pg_attribute a
    WHERE a.attrelid = p_table
      AND a.attname  = p_col
      AND a.attnum   > 0
      AND NOT a.attisdropped
  ) INTO v_exists;

  IF NOT v_exists THEN
    RAISE EXCEPTION 'Required column %.% does not exist.', p_table::text, p_col
      USING ERRCODE = '42703';
  END IF;
END;
$$;

CREATE OR REPLACE FUNCTION bigbang_lock(p_key bigint)
RETURNS void LANGUAGE plpgsql AS $$
BEGIN
  PERFORM pg_advisory_lock(p_key);
END;
$$;

CREATE OR REPLACE FUNCTION bigbang_unlock(p_key bigint)
RETURNS void LANGUAGE plpgsql AS $$
BEGIN
  PERFORM pg_advisory_unlock(p_key);
END;
$$;

-- Deterministic result envelope
DO $$ 
BEGIN
    IF NOT EXISTS (SELECT 1 FROM pg_type WHERE typname = 'db_result') THEN
        CREATE TYPE db_result AS (
          code    int,
          message text,
          id      bigint
        );
    END IF;
END $$;

-- -----------------------------------------------------------------------------
-- 1) Ship claim / destroy / create_initial
-- -----------------------------------------------------------------------------

-- ship_claim(player_id, ship_id)
CREATE OR REPLACE FUNCTION ship_claim(p_player_id bigint, p_ship_id bigint)
RETURNS db_result
LANGUAGE plpgsql
AS $$
BEGIN
  PERFORM assert_column_exists('ships'::regclass, 'ship_id');
  PERFORM assert_column_exists('players'::regclass, 'player_id');
  PERFORM assert_column_exists('players'::regclass, 'ship_id');

  -- Lock rows
  PERFORM 1 FROM ships   WHERE ship_id = p_ship_id   FOR UPDATE;
  IF NOT FOUND THEN
    RETURN (404, 'ship not found'::text, NULL::bigint);
  END IF;

  PERFORM 1 FROM players WHERE player_id = p_player_id FOR UPDATE;
  IF NOT FOUND THEN
    RETURN (404, 'player not found'::text, NULL::bigint);
  END IF;

  -- Verify ship is not already assigned to another player
  IF EXISTS (SELECT 1 FROM players WHERE ship_id = p_ship_id AND player_id <> p_player_id) THEN
     RETURN (409, 'ship already active for another player'::text, NULL::bigint);
  END IF;

  UPDATE players SET ship_id = p_ship_id WHERE player_id = p_player_id;

  RETURN (0, 'ok'::text, NULL::bigint);
END;
$$;

-- ship_destroy(ship_id)
CREATE OR REPLACE FUNCTION ship_destroy(p_ship_id bigint)
RETURNS db_result
LANGUAGE plpgsql
AS $$
BEGIN
  PERFORM assert_column_exists('ships'::regclass, 'ship_id');
  PERFORM assert_column_exists('ships'::regclass, 'destroyed');
  PERFORM assert_column_exists('players'::regclass, 'ship_id');

  PERFORM 1 FROM ships WHERE ship_id = p_ship_id FOR UPDATE;
  IF NOT FOUND THEN
    RETURN (404, 'ship not found'::text, NULL::bigint);
  END IF;

  UPDATE ships SET destroyed = TRUE WHERE ship_id = p_ship_id;
  UPDATE players SET ship_id = NULL WHERE ship_id = p_ship_id;

  RETURN (0, 'ok'::text, NULL::bigint);
END;
$$;

-- ship_create_initial(player_id, sector_id) -> ship_id
CREATE OR REPLACE FUNCTION ship_create_initial(p_player_id bigint, p_sector_id bigint)
RETURNS db_result
LANGUAGE plpgsql
AS $$
DECLARE
  v_ship_id bigint;
BEGIN
  PERFORM assert_column_exists('players'::regclass, 'player_id');
  PERFORM assert_column_exists('players'::regclass, 'ship_id');
  PERFORM assert_column_exists('ships'::regclass, 'ship_id');
  PERFORM assert_column_exists('ships'::regclass, 'sector_id');

  PERFORM 1 FROM players WHERE player_id = p_player_id FOR UPDATE;
  IF NOT FOUND THEN
    RETURN (404, 'player not found'::text, NULL::bigint);
  END IF;

  -- Create starter ship (Scout Marauder, type_id = 2)
  INSERT INTO ships (name, type_id, sector_id, holds, fighters, shields)
  VALUES ('Starter Ship', 2, p_sector_id, 10, 1, 1)
  RETURNING ship_id INTO v_ship_id;

  UPDATE players SET ship_id = v_ship_id WHERE player_id = p_player_id;
  
  IF EXISTS (SELECT 1 FROM pg_class WHERE relname = 'ship_ownership') THEN
      INSERT INTO ship_ownership (ship_id, player_id, role_id, is_primary)
      VALUES (v_ship_id, p_player_id, 1, TRUE)
      ON CONFLICT DO NOTHING;
  END IF;

  RETURN (0, 'ok'::text, v_ship_id::bigint);
END;
$$;

-- -----------------------------------------------------------------------------
-- 2) Player move / land / launch
-- -----------------------------------------------------------------------------

-- player_move(player_id, to_sector)
CREATE OR REPLACE FUNCTION player_move(p_player_id bigint, p_to_sector bigint)
RETURNS db_result
LANGUAGE plpgsql
AS $$
BEGIN
  PERFORM assert_column_exists('players'::regclass, 'player_id');
  PERFORM assert_column_exists('players'::regclass, 'sector_id');

  PERFORM 1 FROM players WHERE player_id = p_player_id FOR UPDATE;
  IF NOT FOUND THEN
    RETURN (404, 'player not found'::text, NULL::bigint);
  END IF;

  UPDATE players SET sector_id = p_to_sector WHERE player_id = p_player_id;

  IF EXISTS (SELECT 1 FROM pg_attribute WHERE attrelid='players'::regclass AND attname='lastplanet' AND NOT attisdropped) THEN
    EXECUTE 'UPDATE players SET lastplanet = NULL WHERE player_id = $1' USING p_player_id;
  END IF;

  RETURN (0, 'ok'::text, NULL::bigint);
END;
$$;

-- player_land(player_id, planet_id)
CREATE OR REPLACE FUNCTION player_land(p_player_id bigint, p_planet_id bigint)
RETURNS db_result
LANGUAGE plpgsql
AS $$
DECLARE
  v_sector bigint;
BEGIN
  PERFORM assert_column_exists('players'::regclass, 'player_id');
  PERFORM assert_column_exists('players'::regclass, 'sector_id');
  PERFORM assert_column_exists('planets'::regclass, 'planet_id');
  PERFORM assert_column_exists('planets'::regclass, 'sector_id');

  PERFORM 1 FROM players WHERE player_id = p_player_id FOR UPDATE;
  IF NOT FOUND THEN RETURN (404, 'player not found'::text, NULL::bigint); END IF;

  SELECT sector_id INTO v_sector FROM players WHERE player_id = p_player_id;

  PERFORM 1 FROM planets WHERE planet_id = p_planet_id AND sector_id = v_sector FOR UPDATE;
  IF NOT FOUND THEN RETURN (412, 'planet not in current sector'::text, NULL::bigint); END IF;

  IF EXISTS (SELECT 1 FROM pg_attribute WHERE attrelid='players'::regclass AND attname='lastplanet' AND NOT attisdropped) THEN
    EXECUTE 'UPDATE players SET lastplanet = $1 WHERE player_id = $2' USING p_planet_id, p_player_id;
  END IF;

  UPDATE ships SET onplanet = TRUE WHERE ship_id = (SELECT ship_id FROM players WHERE player_id = p_player_id);

  RETURN (0, 'ok'::text, NULL::bigint);
END;
$$;

-- player_launch(player_id) clears lastplanet
CREATE OR REPLACE FUNCTION player_launch(p_player_id bigint)
RETURNS db_result
LANGUAGE plpgsql
AS $$
BEGIN
  PERFORM assert_column_exists('players'::regclass, 'player_id');

  PERFORM 1 FROM players WHERE player_id = p_player_id FOR UPDATE;
  IF NOT FOUND THEN RETURN (404, 'player not found'::text, NULL::bigint); END IF;

  IF EXISTS (SELECT 1 FROM pg_attribute WHERE attrelid='players'::regclass AND attname='lastplanet' AND NOT attisdropped) THEN
    EXECUTE 'UPDATE players SET lastplanet = NULL WHERE player_id = $1' USING p_player_id;
  END IF;

  UPDATE ships SET onplanet = FALSE WHERE ship_id = (SELECT ship_id FROM players WHERE player_id = p_player_id);

  RETURN (0, 'ok'::text, NULL::bigint);
END;
$$;

-- -----------------------------------------------------------------------------
-- 3) Market: fill order atomically
-- -----------------------------------------------------------------------------

-- market_fill_order(order_id, fill_qty, actor_id)
CREATE OR REPLACE FUNCTION market_fill_order(p_order_id bigint, p_fill_qty bigint, p_actor_id bigint)
RETURNS db_result
LANGUAGE plpgsql
AS $$
DECLARE
  v_remaining bigint;
  v_qty bigint;
  v_filled bigint;
BEGIN
  PERFORM assert_column_exists('commodity_orders'::regclass, 'commodity_orders_id');
  PERFORM assert_column_exists('commodity_orders'::regclass, 'quantity');
  PERFORM assert_column_exists('commodity_orders'::regclass, 'filled_quantity');
  PERFORM assert_column_exists('commodity_orders'::regclass, 'status');

  SELECT quantity, filled_quantity
  INTO v_qty, v_filled
  FROM commodity_orders WHERE commodity_orders_id = p_order_id FOR UPDATE;
  
  IF NOT FOUND THEN RETURN (404, 'order not found'::text, NULL::bigint); END IF;

  v_remaining := v_qty - v_filled;

  IF p_fill_qty <= 0 THEN RETURN (412, 'fill_qty must be > 0'::text, NULL::bigint); END IF;
  IF v_remaining < p_fill_qty THEN RETURN (409, 'insufficient remaining qty'::text, NULL::bigint); END IF;

  UPDATE commodity_orders
  SET filled_quantity = filled_quantity + p_fill_qty,
      status = CASE WHEN (filled_quantity + p_fill_qty) >= quantity THEN 'filled'::text ELSE status END
  WHERE commodity_orders_id = p_order_id;

  RETURN (0, 'ok'::text, NULL::bigint);
END;
$$;

-- -----------------------------------------------------------------------------
-- 4) Ship Cargo Management
-- -----------------------------------------------------------------------------

-- ship_update_cargo(ship_id, commodity_col, delta)
CREATE OR REPLACE FUNCTION ship_update_cargo(p_ship_id bigint, p_commodity text, p_delta bigint)
RETURNS db_result
LANGUAGE plpgsql
AS $$
DECLARE
  v_current_load bigint;
  v_holds        bigint;
  v_new_qty      bigint;
  v_total_new    bigint;
BEGIN
  -- Validate commodity column name
  IF p_commodity NOT IN ('ore','organics','equipment','colonists','slaves','weapons','drugs') THEN
    RETURN (400, 'invalid commodity'::text, NULL::bigint);
  END IF;

  -- Lock and fetch
  SELECT (ore + organics + equipment + colonists + slaves + weapons + drugs), holds
  INTO v_current_load, v_holds
  FROM ships
  WHERE ship_id = p_ship_id
  FOR UPDATE;

  IF NOT FOUND THEN
    RETURN (404, 'ship not found'::text, NULL::bigint);
  END IF;

  -- Get current qty
  EXECUTE format('SELECT %I FROM ships WHERE ship_id = $1', p_commodity)
  INTO v_new_qty
  USING p_ship_id;

  v_new_qty := v_new_qty + p_delta;
  v_total_new := v_current_load + p_delta;

  IF v_new_qty < 0 THEN
    RETURN (412, 'negative quantity'::text, NULL::bigint);
  END IF;

  IF v_total_new > v_holds THEN
    RETURN (409, 'holds full'::text, NULL::bigint);
  END IF;

  EXECUTE format('UPDATE ships SET %I = $1 WHERE ship_id = $2', p_commodity)
  USING v_new_qty, p_ship_id;

  RETURN (0, 'ok'::text, v_new_qty::bigint);
END;
$$;

-- -----------------------------------------------------------------------------
-- 5) Bank batch jobs (interest / order processing)
-- -----------------------------------------------------------------------------

-- bank_apply_interest()
CREATE OR REPLACE FUNCTION bank_apply_interest()
RETURNS db_result
LANGUAGE plpgsql
AS $$
BEGIN
  RETURN (0, 'template: implement bank_apply_interest'::text, NULL::bigint);
END;
$$;

-- bank_process_orders(batch_limit)
CREATE OR REPLACE FUNCTION bank_process_orders(batch_limit int DEFAULT 1000)
RETURNS db_result
LANGUAGE plpgsql
AS $$
BEGIN
  RETURN (0, 'template: implement bank_process_orders'::text, NULL::bigint);
END;
$$;

COMMIT;