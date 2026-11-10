-- Persistent Exchange inventory is a virtual entity_stock location. The system
-- account is a clearing account, not a player or physical ship.
ALTER TABLE entity_stock DROP CONSTRAINT IF EXISTS entity_stock_entity_type_check;
ALTER TABLE entity_stock ADD CONSTRAINT entity_stock_entity_type_check
  CHECK (entity_type IN ('port', 'planet', 'exchange'));

ALTER TABLE commodity_orders DROP CONSTRAINT IF EXISTS commodity_orders_actor_type_check;
ALTER TABLE commodity_orders ADD CONSTRAINT commodity_orders_actor_type_check
  CHECK (actor_type IN ('player', 'corp', 'npc_planet', 'port', 'exchange'));
ALTER TABLE commodity_orders DROP CONSTRAINT IF EXISTS commodity_orders_location_type_check;
ALTER TABLE commodity_orders ADD CONSTRAINT commodity_orders_location_type_check
  CHECK (location_type IN ('planet', 'port', 'exchange'));

ALTER TABLE commodity_trades DROP CONSTRAINT IF EXISTS commodity_trades_buyer_actor_type_check;
ALTER TABLE commodity_trades ADD CONSTRAINT commodity_trades_buyer_actor_type_check
  CHECK (buyer_actor_type IN ('player', 'corp', 'npc_planet', 'port', 'exchange'));
ALTER TABLE commodity_trades DROP CONSTRAINT IF EXISTS commodity_trades_seller_actor_type_check;
ALTER TABLE commodity_trades ADD CONSTRAINT commodity_trades_seller_actor_type_check
  CHECK (seller_actor_type IN ('player', 'corp', 'npc_planet', 'port', 'exchange'));
ALTER TABLE commodity_trades DROP CONSTRAINT IF EXISTS commodity_trades_buyer_location_type_check;
ALTER TABLE commodity_trades ADD CONSTRAINT commodity_trades_buyer_location_type_check
  CHECK (buyer_location_type IN ('planet', 'port', 'exchange'));
ALTER TABLE commodity_trades DROP CONSTRAINT IF EXISTS commodity_trades_seller_location_type_check;
ALTER TABLE commodity_trades ADD CONSTRAINT commodity_trades_seller_location_type_check
  CHECK (seller_location_type IN ('planet', 'port', 'exchange'));

ALTER TABLE bank_transactions DROP CONSTRAINT IF EXISTS bank_transactions_tx_type_check;
ALTER TABLE bank_transactions ADD CONSTRAINT bank_transactions_tx_type_check
  CHECK (tx_type IN ('DEPOSIT', 'WITHDRAWAL', 'TRANSFER', 'INTEREST', 'FEE',
    'WIRE', 'TAX', 'TRADE_BUY', 'TRADE_SELL', 'TRADE_BUY_FEE',
    'TRADE_SELL_FEE', 'WITHDRAWAL_FEE', 'ADJUSTMENT', 'SYSTEM_LIQUIDITY',
    'INSURANCE_GUARANTEE'));

CREATE TABLE IF NOT EXISTS galactic_exchange_transactions (
  exchange_tx_id bigserial PRIMARY KEY,
  idempotency_key text NOT NULL UNIQUE,
  planet_id integer NOT NULL,
  player_id integer NOT NULL,
  commodity_code text NOT NULL REFERENCES commodities(code),
  quantity integer NOT NULL CHECK (quantity > 0),
  unit_price bigint NOT NULL CHECK (unit_price >= 0),
  credits_paid bigint NOT NULL CHECK (credits_paid >= 0),
  bank_transaction_id integer,
  created_at timestamptz NOT NULL DEFAULT CURRENT_TIMESTAMP
);

INSERT INTO config(key, value, type)
VALUES ('galactic_exchange_liquidity_credits', '0', 'int')
ON CONFLICT (key) DO NOTHING;
