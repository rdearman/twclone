-- Phase 9.2: Cluster Commodity Pressure (MySQL)
-- Add cluster-level market pressure tracking for dynamic pricing

-- Track pressure and rolling volume at the cluster level for each commodity
CREATE TABLE IF NOT EXISTS cluster_commodity_pressure (
    cluster_id BIGINT NOT NULL,
    commodity_code VARCHAR(10) NOT NULL,
    pressure INT NOT NULL DEFAULT 0,            -- signed, can be positive or negative
    rolling_volume INT NOT NULL DEFAULT 0,      -- cumulative trading activity
    updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (cluster_id, commodity_code),
    FOREIGN KEY (cluster_id) REFERENCES clusters (clusters_id) ON DELETE CASCADE
);

-- Indexes for common lookups
CREATE INDEX IF NOT EXISTS idx_cluster_pressure_by_cluster ON cluster_commodity_pressure (cluster_id);
CREATE INDEX IF NOT EXISTS idx_cluster_pressure_by_commodity ON cluster_commodity_pressure (commodity_code);
CREATE INDEX IF NOT EXISTS idx_cluster_pressure_updated ON cluster_commodity_pressure (updated_at);

-- Seed: Initialize rows lazily (via application logic), no pre-population needed
-- When a trade occurs in a cluster, application creates row on demand
