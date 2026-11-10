-- Extend the existing virtual inventory/order model for the Galactic
-- Exchange. Check names are the MySQL-generated names for the inline checks
-- in sql/my/000_tables.sql.
ALTER TABLE entity_stock
  DROP CHECK entity_stock_chk_1,
  ADD CONSTRAINT entity_stock_chk_1
    CHECK (entity_type IN ('port','planet','exchange'));

ALTER TABLE commodity_orders
  DROP CHECK commodity_orders_chk_1,
  DROP CHECK commodity_orders_chk_2,
  ADD CONSTRAINT commodity_orders_chk_1
    CHECK (actor_type IN ('player','corp','npc_planet','port','exchange')),
  ADD CONSTRAINT commodity_orders_chk_2
    CHECK (location_type IN ('planet','port','exchange'));

ALTER TABLE commodity_trades
  DROP CHECK commodity_trades_chk_1,
  DROP CHECK commodity_trades_chk_2,
  DROP CHECK commodity_trades_chk_3,
  DROP CHECK commodity_trades_chk_4,
  ADD CONSTRAINT commodity_trades_chk_1
    CHECK (buyer_actor_type IN ('player','corp','npc_planet','port','exchange')),
  ADD CONSTRAINT commodity_trades_chk_2
    CHECK (buyer_location_type IN ('planet','port','exchange')),
  ADD CONSTRAINT commodity_trades_chk_3
    CHECK (seller_actor_type IN ('player','corp','npc_planet','port','exchange')),
  ADD CONSTRAINT commodity_trades_chk_4
    CHECK (seller_location_type IN ('planet','port','exchange'));

ALTER TABLE bank_transactions
  DROP CHECK bank_transactions_chk_1,
  ADD CONSTRAINT bank_transactions_chk_1 CHECK (tx_type IN
    ('DEPOSIT','WITHDRAWAL','TRANSFER','INTEREST','FEE','WIRE','TAX',
     'TRADE_BUY','TRADE_SELL','TRADE_BUY_FEE','TRADE_SELL_FEE',
     'WITHDRAWAL_FEE','ADJUSTMENT','SYSTEM_LIQUIDITY',
     'INSURANCE_GUARANTEE'));

CREATE TABLE IF NOT EXISTS galactic_exchange_transactions (
  exchange_tx_id BIGINT AUTO_INCREMENT PRIMARY KEY,
  idempotency_key VARCHAR(191) NOT NULL UNIQUE,
  planet_id BIGINT NOT NULL,
  player_id BIGINT NOT NULL,
  commodity_code VARCHAR(64) NOT NULL,
  quantity BIGINT NOT NULL CHECK (quantity > 0),
  unit_price BIGINT NOT NULL CHECK (unit_price >= 0),
  credits_paid BIGINT NOT NULL CHECK (credits_paid >= 0),
  bank_transaction_id BIGINT NULL,
  created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  FOREIGN KEY (commodity_code) REFERENCES commodities(code)
);

INSERT IGNORE INTO config (`key`, `value`, `type`)
VALUES ('galactic_exchange_liquidity_credits','0','int');
