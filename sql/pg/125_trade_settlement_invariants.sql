-- Settlement invariants that do not prescribe market matching semantics.
BEGIN;

DO $$
BEGIN
  IF NOT EXISTS (SELECT 1 FROM pg_constraint WHERE conname = 'commodity_orders_filled_le_quantity') THEN
    ALTER TABLE commodity_orders ADD CONSTRAINT commodity_orders_filled_le_quantity
      CHECK (filled_quantity <= quantity) NOT VALID;
  END IF;
  IF NOT EXISTS (SELECT 1 FROM pg_constraint WHERE conname = 'commodity_trades_settlement_buy_fk') THEN
    ALTER TABLE commodity_trades ADD CONSTRAINT commodity_trades_settlement_buy_fk
      FOREIGN KEY (settlement_tx_buy) REFERENCES bank_transactions(bank_transactions_id)
      DEFERRABLE INITIALLY DEFERRED NOT VALID;
  END IF;
  IF NOT EXISTS (SELECT 1 FROM pg_constraint WHERE conname = 'commodity_trades_settlement_sell_fk') THEN
    ALTER TABLE commodity_trades ADD CONSTRAINT commodity_trades_settlement_sell_fk
      FOREIGN KEY (settlement_tx_sell) REFERENCES bank_transactions(bank_transactions_id)
      DEFERRABLE INITIALLY DEFERRED NOT VALID;
  END IF;
END $$;

CREATE UNIQUE INDEX IF NOT EXISTS ux_commodity_trades_settlement_buy
  ON commodity_trades (settlement_tx_buy) WHERE settlement_tx_buy IS NOT NULL;
CREATE UNIQUE INDEX IF NOT EXISTS ux_commodity_trades_settlement_sell
  ON commodity_trades (settlement_tx_sell) WHERE settlement_tx_sell IS NOT NULL;

COMMIT;
