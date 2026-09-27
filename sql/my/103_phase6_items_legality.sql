-- Phase 6: Data-Driven Devices/Items + Legality Gates (MySQL)
-- Extend hardware_items with legality and alignment gates
-- Add porttype_items mapping for availability configuration

-- MySQL DDL commits independently. Guard column/index creation so reruns can
-- resume after an interrupted upgrade.
SET @column_exists = (
  SELECT COUNT(*) FROM information_schema.COLUMNS
  WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'hardware_items'
    AND COLUMN_NAME = 'is_illegal'
);
SET @column_ddl = IF(@column_exists = 0,
  'ALTER TABLE hardware_items ADD COLUMN is_illegal BOOLEAN NOT NULL DEFAULT FALSE', 'SELECT 1');
PREPARE hardware_column_stmt FROM @column_ddl;
EXECUTE hardware_column_stmt;
DEALLOCATE PREPARE hardware_column_stmt;

SET @column_exists = (
  SELECT COUNT(*) FROM information_schema.COLUMNS
  WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'hardware_items'
    AND COLUMN_NAME = 'min_alignment'
);
SET @column_ddl = IF(@column_exists = 0,
  'ALTER TABLE hardware_items ADD COLUMN min_alignment INTEGER DEFAULT NULL', 'SELECT 1');
PREPARE hardware_column_stmt FROM @column_ddl;
EXECUTE hardware_column_stmt;
DEALLOCATE PREPARE hardware_column_stmt;

SET @column_exists = (
  SELECT COUNT(*) FROM information_schema.COLUMNS
  WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'hardware_items'
    AND COLUMN_NAME = 'max_alignment'
);
SET @column_ddl = IF(@column_exists = 0,
  'ALTER TABLE hardware_items ADD COLUMN max_alignment INTEGER DEFAULT NULL', 'SELECT 1');
PREPARE hardware_column_stmt FROM @column_ddl;
EXECUTE hardware_column_stmt;
DEALLOCATE PREPARE hardware_column_stmt;

-- Create porttype_items mapping table
CREATE TABLE IF NOT EXISTS porttype_items (
    porttype_item_id BIGINT AUTO_INCREMENT PRIMARY KEY,
    porttype_id bigint NOT NULL,
    hardware_items_id bigint NOT NULL,
    can_buy boolean NOT NULL DEFAULT true,
    can_sell boolean NOT NULL DEFAULT true,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY (porttype_id) REFERENCES porttypes (porttype_id) ON DELETE CASCADE,
    FOREIGN KEY (hardware_items_id) REFERENCES hardware_items (hardware_items_id) ON DELETE CASCADE,
    UNIQUE KEY unique_porttype_item (porttype_id, hardware_items_id)
);

-- Add indexes only when they are missing.
SET @index_exists = (
  SELECT COUNT(*) FROM information_schema.STATISTICS
  WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'porttype_items'
    AND INDEX_NAME = 'idx_porttype_items_porttype'
);
SET @index_ddl = IF(@index_exists = 0,
  'CREATE INDEX idx_porttype_items_porttype ON porttype_items (porttype_id)', 'SELECT 1');
PREPARE migration_index_stmt FROM @index_ddl;
EXECUTE migration_index_stmt;
DEALLOCATE PREPARE migration_index_stmt;

SET @index_exists = (
  SELECT COUNT(*) FROM information_schema.STATISTICS
  WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'porttype_items'
    AND INDEX_NAME = 'idx_porttype_items_hardware'
);
SET @index_ddl = IF(@index_exists = 0,
  'CREATE INDEX idx_porttype_items_hardware ON porttype_items (hardware_items_id)', 'SELECT 1');
PREPARE migration_index_stmt FROM @index_ddl;
EXECUTE migration_index_stmt;
DEALLOCATE PREPARE migration_index_stmt;

SET @index_exists = (
  SELECT COUNT(*) FROM information_schema.STATISTICS
  WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'hardware_items'
    AND INDEX_NAME = 'idx_hardware_items_code'
);
SET @index_ddl = IF(@index_exists = 0,
  'CREATE INDEX idx_hardware_items_code ON hardware_items (code)', 'SELECT 1');
PREPARE migration_index_stmt FROM @index_ddl;
EXECUTE migration_index_stmt;
DEALLOCATE PREPARE migration_index_stmt;

SET @index_exists = (
  SELECT COUNT(*) FROM information_schema.STATISTICS
  WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'hardware_items'
    AND INDEX_NAME = 'idx_hardware_items_is_illegal'
);
SET @index_ddl = IF(@index_exists = 0,
  'CREATE INDEX idx_hardware_items_is_illegal ON hardware_items (is_illegal)', 'SELECT 1');
PREPARE migration_index_stmt FROM @index_ddl;
EXECUTE migration_index_stmt;
DEALLOCATE PREPARE migration_index_stmt;

-- Seed porttype_items for existing hardware items
-- Map all existing hardware items to all porttypes (maintains backward compat)
INSERT IGNORE INTO porttype_items (porttype_id, hardware_items_id, can_buy, can_sell)
SELECT pt.porttype_id, hi.hardware_items_id, true, true
FROM porttypes pt
CROSS JOIN hardware_items hi;
