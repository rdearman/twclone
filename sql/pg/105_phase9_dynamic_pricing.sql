-- Phase 9: Dynamic Pricing Engine
-- Adds market pressure tracking for deterministic price adjustments based on supply/demand
-- BACKWARD COMPATIBLE: seed data and disabled-by-default config ensure prices unchanged until explicitly enabled

-- Table: port_commodity_state
-- Tracks current supply/demand state (stock level, trading volume) for each (port, commodity) pair
-- Used to calculate dynamic_mul for price adjustments
CREATE TABLE IF NOT EXISTS port_commodity_state (
    port_commodity_state_id serial PRIMARY KEY,
    port_id integer NOT NULL,
    commodity_code text NOT NULL,
    stock_level integer NOT NULL DEFAULT 0,           -- Current inventory level at port
    rolling_volume integer NOT NULL DEFAULT 0,        -- Trading volume over period (used for demand signal)
    last_trade_at timestamp with time zone NULL,      -- Timestamp of most recent trade
    updated_at timestamp with time zone NOT NULL DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY (port_id) REFERENCES ports (port_id) ON DELETE CASCADE,
    FOREIGN KEY (commodity_code) REFERENCES commodities (code) ON DELETE CASCADE,
    CONSTRAINT unique_port_commodity UNIQUE (port_id, commodity_code)
);

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
INSERT INTO port_commodity_state (port_id, commodity_code, stock_level, rolling_volume, updated_at)
SELECT p.port_id, c.code, 5000, 0, CURRENT_TIMESTAMP
FROM ports p
CROSS JOIN commodities c
WHERE NOT EXISTS (
    SELECT 1 FROM port_commodity_state pcs
    WHERE pcs.port_id = p.port_id AND pcs.commodity_code = c.code
)
ON CONFLICT DO NOTHING;

-- Verify seed completed
DO $$
DECLARE
    total_rows INTEGER;
    total_expected INTEGER;
BEGIN
    SELECT COUNT(*) INTO total_rows FROM port_commodity_state;
    SELECT COUNT(*) * (SELECT COUNT(*) FROM commodities) INTO total_expected 
    FROM ports;
    
    IF total_rows > 0 THEN
        RAISE NOTICE 'Phase 9: port_commodity_state seeded with % rows (expected ~%)', total_rows, total_expected;
    END IF;
END $$;
