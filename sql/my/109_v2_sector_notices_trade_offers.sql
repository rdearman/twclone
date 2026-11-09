-- MySQL equivalent of PostgreSQL migration 109.
-- Add notice targeting metadata, then create durable player trade offers.

SET @ddl = IF((SELECT COUNT(*) FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='system_notice' AND column_name='scope')=0,
 'ALTER TABLE system_notice ADD COLUMN scope VARCHAR(16) CHARACTER SET ascii COLLATE ascii_bin NOT NULL DEFAULT ''global''', 'SELECT 1');
PREPARE stmt FROM @ddl; EXECUTE stmt; DEALLOCATE PREPARE stmt;
SET @ddl = IF((SELECT COUNT(*) FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='system_notice' AND column_name='sector_id')=0,
 'ALTER TABLE system_notice ADD COLUMN sector_id BIGINT NULL', 'SELECT 1');
PREPARE stmt FROM @ddl; EXECUTE stmt; DEALLOCATE PREPARE stmt;
SET @ddl = IF((SELECT COUNT(*) FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='system_notice' AND column_name='player_id')=0,
 'ALTER TABLE system_notice ADD COLUMN player_id BIGINT NULL', 'SELECT 1');
PREPARE stmt FROM @ddl; EXECUTE stmt; DEALLOCATE PREPARE stmt;
SET @ddl = IF((SELECT COUNT(*) FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='system_notice' AND column_name='meta')=0,
 'ALTER TABLE system_notice ADD COLUMN meta JSON NULL', 'SELECT 1');
PREPARE stmt FROM @ddl; EXECUTE stmt; DEALLOCATE PREPARE stmt;
SET @ddl = IF((SELECT COUNT(*) FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='system_notice' AND column_name='ephemeral')=0,
 'ALTER TABLE system_notice ADD COLUMN ephemeral BOOLEAN NOT NULL DEFAULT FALSE', 'SELECT 1');
PREPARE stmt FROM @ddl; EXECUTE stmt; DEALLOCATE PREPARE stmt;
SET @ddl = IF((SELECT COUNT(*) FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='system_notice' AND column_name='notice_key')=0,
 'ALTER TABLE system_notice ADD COLUMN notice_key VARCHAR(191) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NULL', 'SELECT 1');
PREPARE stmt FROM @ddl; EXECUTE stmt; DEALLOCATE PREPARE stmt;

SET @ddl = IF((SELECT COUNT(*) FROM information_schema.statistics WHERE table_schema=DATABASE() AND table_name='system_notice' AND index_name='idx_system_notice_sector_expiry')=0,
 'CREATE INDEX idx_system_notice_sector_expiry ON system_notice (sector_id, expires_at, created_at)', 'SELECT 1');
PREPARE stmt FROM @ddl; EXECUTE stmt; DEALLOCATE PREPARE stmt;
SET @ddl = IF((SELECT COUNT(*) FROM information_schema.statistics WHERE table_schema=DATABASE() AND table_name='system_notice' AND index_name='idx_system_notice_player_expiry')=0,
 'CREATE INDEX idx_system_notice_player_expiry ON system_notice (player_id, expires_at, created_at)', 'SELECT 1');
PREPARE stmt FROM @ddl; EXECUTE stmt; DEALLOCATE PREPARE stmt;
SET @ddl = IF((SELECT COUNT(*) FROM information_schema.statistics WHERE table_schema=DATABASE() AND table_name='system_notice' AND index_name='ux_system_notice_notice_key')=0,
 'CREATE UNIQUE INDEX ux_system_notice_notice_key ON system_notice (notice_key)', 'SELECT 1');
PREPARE stmt FROM @ddl; EXECUTE stmt; DEALLOCATE PREPARE stmt;
SET @ddl = IF((SELECT COUNT(*) FROM information_schema.table_constraints WHERE constraint_schema=DATABASE() AND table_name='system_notice' AND constraint_name='system_notice_scope_check')=0,
 'ALTER TABLE system_notice ADD CONSTRAINT system_notice_scope_check CHECK (scope IN (''global'',''sector'',''corp'',''player''))', 'SELECT 1');
PREPARE stmt FROM @ddl; EXECUTE stmt; DEALLOCATE PREPARE stmt;
SET @ddl = IF((SELECT COUNT(*) FROM information_schema.table_constraints WHERE constraint_schema=DATABASE() AND table_name='system_notice' AND constraint_name='system_notice_sector_scope_check')=0,
 'ALTER TABLE system_notice ADD CONSTRAINT system_notice_sector_scope_check CHECK ((scope=''sector'' AND sector_id IS NOT NULL) OR (scope<>''sector'' AND sector_id IS NULL))', 'SELECT 1');
PREPARE stmt FROM @ddl; EXECUTE stmt; DEALLOCATE PREPARE stmt;
SET @ddl = IF((SELECT COUNT(*) FROM information_schema.referential_constraints WHERE constraint_schema=DATABASE() AND constraint_name='system_notice_sector_fk')=0,
 'ALTER TABLE system_notice ADD CONSTRAINT system_notice_sector_fk FOREIGN KEY (sector_id) REFERENCES sectors(sector_id) ON DELETE RESTRICT', 'SELECT 1');
PREPARE stmt FROM @ddl; EXECUTE stmt; DEALLOCATE PREPARE stmt;
SET @ddl = IF((SELECT COUNT(*) FROM information_schema.referential_constraints WHERE constraint_schema=DATABASE() AND constraint_name='system_notice_player_fk')=0,
 'ALTER TABLE system_notice ADD CONSTRAINT system_notice_player_fk FOREIGN KEY (player_id) REFERENCES players(player_id) ON DELETE SET NULL', 'SELECT 1');
PREPARE stmt FROM @ddl; EXECUTE stmt; DEALLOCATE PREPARE stmt;

CREATE TABLE IF NOT EXISTS trade_offers (
    trade_offer_id BIGINT NOT NULL AUTO_INCREMENT,
    sender_player_id BIGINT NOT NULL,
    recipient_player_id BIGINT NOT NULL,
    commodity_code VARCHAR(64) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NOT NULL,
    mode VARCHAR(8) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    quantity INT NOT NULL,
    unit_price BIGINT NOT NULL,
    status VARCHAR(16) CHARACTER SET ascii COLLATE ascii_bin NOT NULL DEFAULT 'pending',
    created_at DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
    expires_at DATETIME(6) NOT NULL,
    accepted_at DATETIME(6) NULL,
    cancelled_at DATETIME(6) NULL,
    expired_at DATETIME(6) NULL,
    idempotency_key VARCHAR(191) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NULL,
    PRIMARY KEY (trade_offer_id),
    UNIQUE KEY ux_trade_offers_sender_idempotency (sender_player_id, idempotency_key),
    KEY idx_trade_offers_recipient_pending_expiry (recipient_player_id, status, expires_at),
    KEY idx_trade_offers_sender_status_created (sender_player_id, status, created_at),
    KEY idx_trade_offers_pending_expiry (status, expires_at),
    CONSTRAINT trade_offers_sender_fk FOREIGN KEY (sender_player_id) REFERENCES players(player_id) ON DELETE CASCADE,
    CONSTRAINT trade_offers_recipient_fk FOREIGN KEY (recipient_player_id) REFERENCES players(player_id) ON DELETE CASCADE,
    CONSTRAINT trade_offers_commodity_fk FOREIGN KEY (commodity_code) REFERENCES commodities(code) ON DELETE RESTRICT,
    CONSTRAINT trade_offers_mode_check CHECK (mode IN ('buy','sell')),
    CONSTRAINT trade_offers_quantity_check CHECK (quantity > 0),
    CONSTRAINT trade_offers_price_check CHECK (unit_price >= 0),
    CONSTRAINT trade_offers_status_check CHECK (status IN ('pending','accepted','cancelled','expired')),
    CONSTRAINT trade_offers_distinct_players_check CHECK (sender_player_id <> recipient_player_id),
    CONSTRAINT trade_offers_expiry_check CHECK (expires_at > created_at)
) ENGINE=InnoDB;
