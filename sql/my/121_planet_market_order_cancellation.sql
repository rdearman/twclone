SET @column_exists = (
  SELECT COUNT(*) FROM information_schema.COLUMNS
  WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'commodity_orders'
    AND COLUMN_NAME = 'cancellation_reason'
);
SET @column_ddl = IF(@column_exists = 0,
  'ALTER TABLE commodity_orders ADD COLUMN cancellation_reason VARCHAR(64) NULL',
  'SELECT 1');
PREPARE planet_market_cancel_reason_stmt FROM @column_ddl;
EXECUTE planet_market_cancel_reason_stmt;
DEALLOCATE PREPARE planet_market_cancel_reason_stmt;

INSERT INTO config (`key`, `value`, `type`)
VALUES ('planet_market.buy_order_expiry_seconds', '86400', 'int')
ON DUPLICATE KEY UPDATE `key` = `key`;
