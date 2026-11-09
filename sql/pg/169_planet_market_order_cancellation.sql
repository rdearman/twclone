BEGIN;

ALTER TABLE commodity_orders
  ADD COLUMN IF NOT EXISTS cancellation_reason text;

INSERT INTO config (key, value, type)
VALUES ('planet_market.buy_order_expiry_seconds', '86400', 'int')
ON CONFLICT (key) DO NOTHING;

COMMIT;
