-- 200_gameplay_procs.sql
-- Gameplay mutation stored procedures (TEMPLATES).
--
-- IMPORTANT:
-- The schema file you provided (fullschema.sql) contains literal "..." truncations
-- inside CREATE TABLE statements (e.g., players/ports/planets). That means the
-- exact column sets are not available here. These procedures therefore:
--   * Assert required columns exist
--   * Use minimal, clearly-marked INSERT/UPDATE statements
--   * Must be finalised once you dump a non-truncated SQLite schema
--
-- Recommended dump command:
--   sqlite3 your.db '.output sqlite_schema.sql' '.schema' '.output stdout'
-- Then regenerate PG DDL and update these procs to match real columns.

BEGIN;

-- -----------------------------------------------------------------------------
-- Helpers (shared)
-- -----------------------------------------------------------------------------
CREATE OR REPLACE FUNCTION assert_table_exists(p_table regclass)
RETURNS void LANGUAGE plpgsql AS $$
BEGIN
  -- regclass cast will fail if table missing
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

-- Deterministic result envelope for server mapping
CREATE TYPE db_result AS (
  code    int,
  message text,
  id      bigint
);

-- -----------------------------------------------------------------------------
-- 1) Ship claim / destroy / create_initial
-- -----------------------------------------------------------------------------

-- ship_claim(player_id, ship_id)
-- Locks ship row and player row, verifies ship is unowned, assigns ownership,
-- and sets player's active ship.
CREATE OR REPLACE FUNCTION ship_claim(p_player_id bigint, p_ship_id bigint)
RETURNS db_result
LANGUAGE plpgsql
AS $$
DECLARE
  r db_result;
BEGIN
  r := (0, 'ok', NULL);

  PERFORM assert_column_exists('ships'::regclass, 'id');
  PERFORM assert_column_exists('players'::regclass, 'id');

  -- TODO (schema): required columns
  -- ships.owner_id, ships.destroyed (or status), players.active_ship_id
  PERFORM assert_column_exists('ships'::regclass, 'owner_id');
  PERFORM assert_column_exists('players'::regclass, 'active_ship_id');

  -- Lock rows
  PERFORM 1 FROM ships   WHERE id = p_ship_id   FOR UPDATE;
  IF NOT FOUND THEN
    RETURN (404, 'ship not found', NULL);
  END IF;

  PERFORM 1 FROM players WHERE id = p_player_id FOR UPDATE;
  IF NOT FOUND THEN
    RETURN (404, 'player not found', NULL);
  END IF;

  -- Ensure unowned
  IF EXISTS (SELECT 1 FROM ships WHERE id=p_ship_id AND owner_id IS NOT NULL AND owner_id <> 0) THEN
    RETURN (409, 'ship already owned', NULL);
  END IF;

  UPDATE ships SET owner_id = p_player_id WHERE id = p_ship_id;
  UPDATE players SET active_ship_id = p_ship_id WHERE id = p_player_id;

  RETURN r;
END;
$$;

-- ship_destroy(ship_id)
-- Marks ship destroyed and clears any player.active_ship_id referencing it.
CREATE OR REPLACE FUNCTION ship_destroy(p_ship_id bigint)
RETURNS db_result
LANGUAGE plpgsql
AS $$
BEGIN
  PERFORM assert_column_exists('ships'::regclass, 'id');
  PERFORM assert_column_exists('ships'::regclass, 'destroyed');
  PERFORM assert_column_exists('players'::regclass, 'active_ship_id');

  PERFORM 1 FROM ships WHERE id = p_ship_id FOR UPDATE;
  IF NOT FOUND THEN
    RETURN (404, 'ship not found', NULL);
  END IF;

  UPDATE ships SET destroyed = 1 WHERE id = p_ship_id;
  UPDATE players SET active_ship_id = NULL WHERE active_ship_id = p_ship_id;

  RETURN (0, 'ok', NULL);
END;
$$;

-- ship_create_initial(player_id, sector_id) -> ship_id
-- Inserts a new ship and sets it as the player's active ship.
CREATE OR REPLACE FUNCTION ship_create_initial(p_player_id bigint, p_sector_id bigint)
RETURNS db_result
LANGUAGE plpgsql
AS $$
DECLARE
  v_ship_id bigint;
BEGIN
  PERFORM assert_column_exists('players'::regclass, 'id');
  PERFORM assert_column_exists('players'::regclass, 'active_ship_id');
  PERFORM assert_column_exists('ships'::regclass, 'id');
  PERFORM assert_column_exists('ships'::regclass, 'owner_id');
  PERFORM assert_column_exists('ships'::regclass, 'sector');

  PERFORM 1 FROM players WHERE id = p_player_id FOR UPDATE;
  IF NOT FOUND THEN
    RETURN (404, 'player not found', NULL);
  END IF;

  INSERT INTO ships (owner_id, sector)
  VALUES (p_player_id, p_sector_id)
  RETURNING id INTO v_ship_id;

  UPDATE players SET active_ship_id = v_ship_id WHERE id = p_player_id;

  RETURN (0, 'ok', v_ship_id);
END;
$$;

-- -----------------------------------------------------------------------------
-- 2) Player move / land / launch
-- -----------------------------------------------------------------------------

-- player_move(player_id, to_sector)
-- Locks player row; updates sector; clears lastplanet/docked state if present.
CREATE OR REPLACE FUNCTION player_move(p_player_id bigint, p_to_sector bigint)
RETURNS db_result
LANGUAGE plpgsql
AS $$
BEGIN
  PERFORM assert_column_exists('players'::regclass, 'id');
  PERFORM assert_column_exists('players'::regclass, 'sector');

  PERFORM 1 FROM players WHERE id = p_player_id FOR UPDATE;
  IF NOT FOUND THEN
    RETURN (404, 'player not found', NULL);
  END IF;

  UPDATE players SET sector = p_to_sector WHERE id = p_player_id;

  -- Optional clears if columns exist (best-effort)
  IF EXISTS (SELECT 1 FROM pg_attribute WHERE attrelid='players'::regclass AND attname='lastplanet' AND NOT attisdropped) THEN
    EXECUTE 'UPDATE players SET lastplanet = NULL WHERE id = $1' USING p_player_id;
  END IF;

  RETURN (0, 'ok', NULL);
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
  PERFORM assert_column_exists('players'::regclass, 'id');
  PERFORM assert_column_exists('players'::regclass, 'sector');
  PERFORM assert_column_exists('planets'::regclass, 'id');
  PERFORM assert_column_exists('planets'::regclass, 'sector');

  PERFORM 1 FROM players WHERE id = p_player_id FOR UPDATE;
  IF NOT FOUND THEN RETURN (404,'player not found',NULL); END IF;

  SELECT sector INTO v_sector FROM players WHERE id = p_player_id;

  PERFORM 1 FROM planets WHERE id = p_planet_id AND sector = v_sector FOR UPDATE;
  IF NOT FOUND THEN RETURN (412,'planet not in current sector',NULL); END IF;

  IF EXISTS (SELECT 1 FROM pg_attribute WHERE attrelid='players'::regclass AND attname='lastplanet' AND NOT attisdropped) THEN
    EXECUTE 'UPDATE players SET lastplanet = $1 WHERE id = $2' USING p_planet_id, p_player_id;
  END IF;

  RETURN (0,'ok',NULL);
END;
$$;

-- player_launch(player_id) clears lastplanet
CREATE OR REPLACE FUNCTION player_launch(p_player_id bigint)
RETURNS db_result
LANGUAGE plpgsql
AS $$
BEGIN
  PERFORM assert_column_exists('players'::regclass, 'id');

  PERFORM 1 FROM players WHERE id = p_player_id FOR UPDATE;
  IF NOT FOUND THEN RETURN (404,'player not found',NULL); END IF;

  IF EXISTS (SELECT 1 FROM pg_attribute WHERE attrelid='players'::regclass AND attname='lastplanet' AND NOT attisdropped) THEN
    EXECUTE 'UPDATE players SET lastplanet = NULL WHERE id = $1' USING p_player_id;
  END IF;

  RETURN (0,'ok',NULL);
END;
$$;

-- -----------------------------------------------------------------------------
-- 3) Market: fill order atomically
-- -----------------------------------------------------------------------------

-- market_fill_order(order_id, fill_qty, actor_id)
-- Locks the order row; validates; inserts trade; updates order remaining/status.
CREATE OR REPLACE FUNCTION market_fill_order(p_order_id bigint, p_fill_qty bigint, p_actor_id bigint)
RETURNS db_result
LANGUAGE plpgsql
AS $$
DECLARE
  v_remaining bigint;
BEGIN
  PERFORM assert_column_exists('commodity_orders'::regclass, 'id');
  -- TODO schema: remaining_qty/status/filled_qty/quantity fields
  PERFORM assert_column_exists('commodity_orders'::regclass, 'remaining_qty');
  PERFORM assert_column_exists('commodity_orders'::regclass, 'status');

  PERFORM 1 FROM commodity_orders WHERE id = p_order_id FOR UPDATE;
  IF NOT FOUND THEN RETURN (404,'order not found',NULL); END IF;

  SELECT remaining_qty INTO v_remaining FROM commodity_orders WHERE id = p_order_id;

  IF p_fill_qty <= 0 THEN RETURN (412,'fill_qty must be > 0',NULL); END IF;
  IF v_remaining < p_fill_qty THEN RETURN (409,'insufficient remaining qty',NULL); END IF;

  -- Insert trade row (template: adjust columns)
  PERFORM assert_column_exists('commodity_trades'::regclass, 'order_id');
  PERFORM assert_column_exists('commodity_trades'::regclass, 'qty');
  PERFORM assert_column_exists('commodity_trades'::regclass, 'actor_id');

  INSERT INTO commodity_trades (order_id, qty, actor_id)
  VALUES (p_order_id, p_fill_qty, p_actor_id);

  UPDATE commodity_orders
  SET remaining_qty = remaining_qty - p_fill_qty,
      status = CASE WHEN remaining_qty - p_fill_qty = 0 THEN 'filled' ELSE status END
  WHERE id = p_order_id;

  RETURN (0,'ok',NULL);
END;
$$;

-- -----------------------------------------------------------------------------
-- 4) Ship Cargo Management
-- -----------------------------------------------------------------------------

-- ship_update_cargo(ship_id, commodity_col, delta)
-- Atomically updates a cargo column and ensures (total cargo <= holds).
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
  -- Validate commodity column name to prevent injection
  IF p_commodity NOT IN ('ore','organics','equipment','colonists','slaves','weapons','drugs') THEN
    RETURN (400, 'invalid commodity', NULL);
  END IF;

  -- Lock and fetch
  SELECT (ore + organics + equipment + colonists + slaves + weapons + drugs), holds
  INTO v_current_load, v_holds
  FROM ships
  WHERE id = p_ship_id
  FOR UPDATE;

  IF NOT FOUND THEN
    RETURN (404, 'ship not found', NULL);
  END IF;

  -- Get current qty of target
  EXECUTE format('SELECT %I FROM ships WHERE id = $1', p_commodity)
  INTO v_new_qty
  USING p_ship_id;

  v_new_qty := v_new_qty + p_delta;
  v_total_new := v_current_load + p_delta;

  IF v_new_qty < 0 THEN
    RETURN (412, 'negative quantity', NULL);
  END IF;

  IF v_total_new > v_holds THEN
    RETURN (409, 'holds full', NULL);
  END IF;

  EXECUTE format('UPDATE ships SET %I = $1 WHERE id = $2', p_commodity)
  USING v_new_qty, p_ship_id;

  RETURN (0, 'ok', v_new_qty);
END;
$$;

-- -----------------------------------------------------------------------------
-- 5) Bank batch jobs (interest / order processing)
-- -----------------------------------------------------------------------------

-- bank_apply_interest()
-- Template: implement as set-based UPDATE/INSERT over bank_accounts/bank_tx.
CREATE OR REPLACE FUNCTION bank_apply_interest()
RETURNS db_result
LANGUAGE plpgsql
AS $$
BEGIN
  -- TODO: tie to your schema (bank_accounts interest_rate, balance, last_accrued, etc)
  RETURN (0, 'template: implement bank_apply_interest using real columns', NULL);
END;
$$;

-- bank_process_orders(batch_limit)
CREATE OR REPLACE FUNCTION bank_process_orders(batch_limit int DEFAULT 1000)
RETURNS db_result
LANGUAGE plpgsql
AS $$
BEGIN
  -- TODO: tie to your schema (orders table, tx table, idempotency marker)
  RETURN (0, 'template: implement bank_process_orders using real columns', NULL);
END;
$$;

COMMIT;
