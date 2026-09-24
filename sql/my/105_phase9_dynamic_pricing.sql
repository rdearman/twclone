-- Phase 9: Dynamic Pricing Engine (MySQL equivalent)
-- Adds market pressure tracking for deterministic price adjustments based on supply/demand
-- BACKWARD COMPATIBLE: seed data and disabled-by-default config ensure prices unchanged until explicitly enabled

-- Table: port_commodity_state
-- Tracks current supply/demand state (stock level, trading volume) for each (port, commodity) pair
-- Used to calculate dynamic_mul for price adjustments
CREATE TABLE IF NOT EXISTS port_commodity_state (
    port_commodity_state_id int AUTO_INCREMENT PRIMARY KEY,
    port_id int NOT NULL,
    commodity_code varchar(64) NOT NULL,
    stock_level int NOT NULL DEFAULT 0,               -- Current inventory level at port
    rolling_volume int NOT NULL DEFAULT 0,            -- Trading volume over period (used for demand signal)
    last_trade_at timestamp NULL DEFAULT NULL,        -- Timestamp of most recent trade
    updated_at timestamp NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    FOREIGN KEY (port_id) REFERENCES ports (port_id) ON DELETE CASCADE,
    FOREIGN KEY (commodity_code) REFERENCES commodities (code) ON DELETE CASCADE,
    UNIQUE KEY unique_port_commodity (port_id, commodity_code)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- Indexes for fast lookups
CREATE INDEX IF NOT EXISTS idx_port_commodity_state_port_id 
  ON port_commodity_state (port_id);
CREATE INDEX IF NOT EXISTS idx_port_commodity_state_commodity_code 
  ON port_commodity_state (commodity_code);
CREATE INDEX IF NOT EXISTS idx_port_commodity_state_updated_at 
  ON port_commodity_state (updated_at);

-- Seed default state for all existing (port, commodity) combinations
-- Default: stock_level=5000 (ensures ports can calculate buy/sell prices)
-- rolling_volume=0 (neutral demand signal)
INSERT IGNORE INTO port_commodity_state (port_id, commodity_code, stock_level, rolling_volume, updated_at)
SELECT p.port_id, c.code, 5000, 0, CURRENT_TIMESTAMP
FROM ports p
CROSS JOIN commodities c
WHERE NOT EXISTS (
    SELECT 1 FROM port_commodity_state pcs
    WHERE pcs.port_id = p.port_id AND pcs.commodity_code = c.code
);

-- Log seed completion
-- MySQL doesn't support DO/DECLARE like PostgreSQL, so seed is implicit
