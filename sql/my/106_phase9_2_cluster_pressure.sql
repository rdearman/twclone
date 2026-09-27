-- Phase 9.2: Cluster Commodity Pressure (MySQL)
-- Add cluster-level market pressure tracking for dynamic pricing

-- Track pressure and rolling volume at the cluster level for each commodity
CREATE TABLE IF NOT EXISTS cluster_commodity_pressure (
    cluster_id BIGINT NOT NULL,
    commodity_code VARCHAR(64) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NOT NULL,
    pressure INT NOT NULL DEFAULT 0,            -- signed, can be positive or negative
    rolling_volume INT NOT NULL DEFAULT 0,      -- cumulative trading activity
    updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (cluster_id, commodity_code),
    FOREIGN KEY (cluster_id) REFERENCES clusters (clusters_id) ON DELETE CASCADE
);

-- MySQL does not support CREATE INDEX IF NOT EXISTS; guard each index lookup.
SET @index_exists = (
  SELECT COUNT(*) FROM information_schema.STATISTICS
  WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'cluster_commodity_pressure'
    AND INDEX_NAME = 'idx_cluster_pressure_by_cluster'
);
SET @index_ddl = IF(@index_exists = 0,
  'CREATE INDEX idx_cluster_pressure_by_cluster ON cluster_commodity_pressure (cluster_id)', 'SELECT 1');
PREPARE migration_index_stmt FROM @index_ddl;
EXECUTE migration_index_stmt;
DEALLOCATE PREPARE migration_index_stmt;

SET @index_exists = (
  SELECT COUNT(*) FROM information_schema.STATISTICS
  WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'cluster_commodity_pressure'
    AND INDEX_NAME = 'idx_cluster_pressure_by_commodity'
);
SET @index_ddl = IF(@index_exists = 0,
  'CREATE INDEX idx_cluster_pressure_by_commodity ON cluster_commodity_pressure (commodity_code)', 'SELECT 1');
PREPARE migration_index_stmt FROM @index_ddl;
EXECUTE migration_index_stmt;
DEALLOCATE PREPARE migration_index_stmt;

SET @index_exists = (
  SELECT COUNT(*) FROM information_schema.STATISTICS
  WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'cluster_commodity_pressure'
    AND INDEX_NAME = 'idx_cluster_pressure_updated'
);
SET @index_ddl = IF(@index_exists = 0,
  'CREATE INDEX idx_cluster_pressure_updated ON cluster_commodity_pressure (updated_at)', 'SELECT 1');
PREPARE migration_index_stmt FROM @index_ddl;
EXECUTE migration_index_stmt;
DEALLOCATE PREPARE migration_index_stmt;

-- Seed: Initialize rows lazily (via application logic), no pre-population needed
-- When a trade occurs in a cluster, application creates row on demand
