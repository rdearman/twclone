-- MySQL equivalent of PostgreSQL migration 115. Existing 44 commission rows
-- and players.experience remain the rank source of truth.

CREATE TABLE IF NOT EXISTS player_progress_adjustments (
    player_progress_adjustment_id BIGINT NOT NULL AUTO_INCREMENT,
    player_id BIGINT NOT NULL,
    source_type VARCHAR(64) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NOT NULL,
    source_key VARCHAR(191) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NOT NULL,
    xp_delta BIGINT NOT NULL,
    alignment_delta INT NOT NULL DEFAULT 0,
    formula_version INT NOT NULL DEFAULT 1,
    created_at DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
    PRIMARY KEY (player_progress_adjustment_id),
    UNIQUE KEY ux_player_progress_source_key (source_key),
    KEY idx_player_progress_player_time (player_id, created_at),
    CONSTRAINT player_progress_player_fk FOREIGN KEY (player_id) REFERENCES players(player_id) ON DELETE CASCADE,
    CONSTRAINT player_progress_source_type_check CHECK (CHAR_LENGTH(source_type) > 0),
    CONSTRAINT player_progress_source_key_check CHECK (CHAR_LENGTH(source_key) > 0),
    CONSTRAINT player_progress_formula_check CHECK (formula_version > 0)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS planet_tax_global_config (
    config_id SMALLINT NOT NULL,
    rate_bps INT NOT NULL DEFAULT 0,
    enabled BOOLEAN NOT NULL DEFAULT FALSE,
    updated_at DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
    PRIMARY KEY (config_id),
    CONSTRAINT planet_tax_global_singleton_check CHECK (config_id=1),
    CONSTRAINT planet_tax_global_rate_check CHECK (rate_bps BETWEEN 0 AND 10000)
) ENGINE=InnoDB;

INSERT INTO planet_tax_global_config (config_id, rate_bps, enabled)
VALUES (1,0,FALSE)
ON DUPLICATE KEY UPDATE config_id=VALUES(config_id);
