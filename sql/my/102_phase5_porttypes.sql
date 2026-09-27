-- Phase 5: Data-Driven Port Types (MySQL)
-- Create porttypes table and migrate ports to use it

-- Create porttypes table
CREATE TABLE IF NOT EXISTS porttypes (
    porttype_id BIGINT AUTO_INCREMENT PRIMARY KEY,
    code VARCHAR(64) NOT NULL UNIQUE,
    description TEXT,
    can_buy boolean NOT NULL DEFAULT TRUE,
    can_sell boolean NOT NULL DEFAULT TRUE,
    is_stardock boolean NOT NULL DEFAULT FALSE,
    is_black_market boolean NOT NULL DEFAULT FALSE,
    special_rules TEXT,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

-- Seed base port types
INSERT IGNORE INTO porttypes (code, description, can_buy, can_sell, is_stardock, is_black_market)
VALUES 
    ('CLASS0', 'Standard Trading Port', TRUE, TRUE, FALSE, FALSE),
    ('STARDOCK', 'Shipyard and Hardware Station', TRUE, TRUE, TRUE, FALSE),
    ('BLACKMARKET', 'Black Market Port', TRUE, TRUE, FALSE, TRUE);

-- MySQL DDL commits independently. Guard each DDL operation so an interrupted
-- upgrade can safely be resumed by rerunning this file.
SET @fk_exists = (
  SELECT COUNT(*) FROM information_schema.TABLE_CONSTRAINTS
  WHERE CONSTRAINT_SCHEMA = DATABASE()
    AND TABLE_NAME = 'ports'
    AND CONSTRAINT_NAME = 'fk_ports_porttype'
    AND CONSTRAINT_TYPE = 'FOREIGN KEY'
);
SET @fk_ddl = IF(@fk_exists = 0,
  'ALTER TABLE ports ADD CONSTRAINT fk_ports_porttype FOREIGN KEY (porttype_id) REFERENCES porttypes (porttype_id) ON DELETE SET NULL',
  'SELECT 1');
PREPARE porttype_fk_stmt FROM @fk_ddl;
EXECUTE porttype_fk_stmt;
DEALLOCATE PREPARE porttype_fk_stmt;

-- Backfill porttype_id from existing type values
UPDATE ports p
INNER JOIN porttypes pt ON (
    (p.`type` = 0 AND pt.code = 'CLASS0') OR
    (p.`type` = 9 AND pt.code = 'STARDOCK') OR
    (p.`type` = 10 AND pt.code = 'BLACKMARKET')
)
SET p.porttype_id = pt.porttype_id
WHERE p.porttype_id IS NULL;

-- Set default for any unmapped types to CLASS0
UPDATE ports p
INNER JOIN porttypes pt ON pt.code = 'CLASS0'
SET p.porttype_id = pt.porttype_id
WHERE p.porttype_id IS NULL;

-- Add indexes for frequent lookups only when they are missing.
SET @index_exists = (
  SELECT COUNT(*) FROM information_schema.STATISTICS
  WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'ports'
    AND INDEX_NAME = 'idx_ports_porttype_id'
);
SET @index_ddl = IF(@index_exists = 0,
  'CREATE INDEX idx_ports_porttype_id ON ports (porttype_id)', 'SELECT 1');
PREPARE porttype_index_stmt FROM @index_ddl;
EXECUTE porttype_index_stmt;
DEALLOCATE PREPARE porttype_index_stmt;

SET @index_exists = (
  SELECT COUNT(*) FROM information_schema.STATISTICS
  WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'porttypes'
    AND INDEX_NAME = 'idx_porttypes_code'
);
SET @index_ddl = IF(@index_exists = 0,
  'CREATE INDEX idx_porttypes_code ON porttypes (code)', 'SELECT 1');
PREPARE porttype_index_stmt FROM @index_ddl;
EXECUTE porttype_index_stmt;
DEALLOCATE PREPARE porttype_index_stmt;
