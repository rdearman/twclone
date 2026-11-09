-- Add the nullable order-expiry column used by the shared commodity-order
-- repository. Existing orders remain unexpired (NULL), matching PostgreSQL.
SET @column_exists = (
  SELECT COUNT(*) FROM information_schema.COLUMNS
  WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'commodity_orders'
    AND COLUMN_NAME = 'expires_at'
);
SET @column_ddl = IF(@column_exists = 0,
  'ALTER TABLE commodity_orders ADD COLUMN expires_at TIMESTAMP NULL',
  'SELECT 1');
PREPARE commodity_order_expiry_stmt FROM @column_ddl;
EXECUTE commodity_order_expiry_stmt;
DEALLOCATE PREPARE commodity_order_expiry_stmt;
