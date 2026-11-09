-- MySQL equivalent of PostgreSQL migration 112.
-- Native DATETIME(6) columns retain microsecond precision and avoid TIMESTAMP's
-- 2038 range limit. Partial PostgreSQL indexes become full composite indexes.

CREATE TABLE IF NOT EXISTS port_hardware_stock (
    port_id BIGINT NOT NULL,
    hardware_items_id BIGINT NOT NULL,
    stock_quantity INT NOT NULL DEFAULT 0,
    max_stock INT NOT NULL,
    restock_quantity INT NOT NULL DEFAULT 0,
    restock_interval_seconds INT NOT NULL DEFAULT 86400,
    next_restock_at DATETIME(6) NOT NULL,
    updated_at DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
    PRIMARY KEY (port_id, hardware_items_id),
    KEY idx_port_hardware_stock_restock_due (restock_quantity, next_restock_at),
    CONSTRAINT port_hardware_stock_port_fk FOREIGN KEY (port_id) REFERENCES ports(port_id) ON DELETE CASCADE,
    CONSTRAINT port_hardware_stock_item_fk FOREIGN KEY (hardware_items_id) REFERENCES hardware_items(hardware_items_id) ON DELETE CASCADE,
    CONSTRAINT port_hardware_stock_quantity_check CHECK (stock_quantity >= 0 AND stock_quantity <= max_stock),
    CONSTRAINT port_hardware_stock_max_check CHECK (max_stock BETWEEN 1 AND 100000000),
    CONSTRAINT port_hardware_stock_restock_check CHECK (restock_quantity >= 0 AND restock_interval_seconds > 0)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS ferengi_traders (
    ferengi_trader_id BIGINT NOT NULL AUTO_INCREMENT,
    trader_code VARCHAR(64) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NOT NULL,
    display_name VARCHAR(191) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NOT NULL,
    corporation_id BIGINT NOT NULL,
    ship_id BIGINT NOT NULL,
    reputation INT NOT NULL DEFAULT 0,
    visit_number BIGINT NOT NULL DEFAULT 0,
    active BOOLEAN NOT NULL DEFAULT TRUE,
    last_interaction_at DATETIME(6) NULL,
    created_at DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
    updated_at DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
    PRIMARY KEY (ferengi_trader_id),
    UNIQUE KEY ux_ferengi_traders_code (trader_code),
    UNIQUE KEY ux_ferengi_traders_ship (ship_id),
    KEY idx_ferengi_traders_active (active, ferengi_trader_id),
    CONSTRAINT ferengi_traders_corporation_fk FOREIGN KEY (corporation_id) REFERENCES corporations(corporation_id) ON DELETE CASCADE,
    CONSTRAINT ferengi_traders_ship_fk FOREIGN KEY (ship_id) REFERENCES ships(ship_id) ON DELETE CASCADE,
    CONSTRAINT ferengi_traders_reputation_check CHECK (reputation BETWEEN -10000 AND 10000),
    CONSTRAINT ferengi_traders_visits_check CHECK (visit_number >= 0)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS ferengi_trader_deals (
    ferengi_trader_deal_id BIGINT NOT NULL AUTO_INCREMENT,
    ferengi_trader_id BIGINT NOT NULL,
    player_id BIGINT NOT NULL,
    commodity_code VARCHAR(64) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NOT NULL,
    side VARCHAR(16) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    quantity INT NOT NULL,
    unit_price BIGINT NOT NULL,
    status VARCHAR(16) CHARACTER SET ascii COLLATE ascii_bin NOT NULL DEFAULT 'open',
    idempotency_key VARCHAR(191) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NOT NULL,
    created_at DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
    expires_at DATETIME(6) NOT NULL,
    settled_at DATETIME(6) NULL,
    PRIMARY KEY (ferengi_trader_deal_id),
    UNIQUE KEY ux_ferengi_deal_idempotency (ferengi_trader_id, idempotency_key),
    KEY idx_ferengi_deals_status_expiry (status, expires_at),
    KEY idx_ferengi_deals_player (player_id, created_at),
    CONSTRAINT ferengi_deals_trader_fk FOREIGN KEY (ferengi_trader_id) REFERENCES ferengi_traders(ferengi_trader_id) ON DELETE CASCADE,
    CONSTRAINT ferengi_deals_player_fk FOREIGN KEY (player_id) REFERENCES players(player_id) ON DELETE CASCADE,
    CONSTRAINT ferengi_deals_commodity_fk FOREIGN KEY (commodity_code) REFERENCES commodities(code),
    CONSTRAINT ferengi_deals_side_check CHECK (side IN ('trader_buys','trader_sells')),
    CONSTRAINT ferengi_deals_quantity_check CHECK (quantity > 0),
    CONSTRAINT ferengi_deals_price_check CHECK (unit_price >= 0),
    CONSTRAINT ferengi_deals_status_check CHECK (status IN ('open','settled','declined','expired','cancelled')),
    CONSTRAINT ferengi_deals_expiry_check CHECK (expires_at > created_at)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS ferengi_trader_interactions (
    ferengi_trader_interaction_id BIGINT NOT NULL AUTO_INCREMENT,
    ferengi_trader_id BIGINT NOT NULL,
    player_id BIGINT NULL,
    deal_id BIGINT NULL,
    interaction_type VARCHAR(16) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    reputation_delta INT NOT NULL DEFAULT 0,
    idempotency_key VARCHAR(191) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NOT NULL,
    occurred_at DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
    PRIMARY KEY (ferengi_trader_interaction_id),
    UNIQUE KEY ux_ferengi_interaction_idempotency (idempotency_key),
    KEY idx_ferengi_interactions_trader_time (ferengi_trader_id, occurred_at),
    CONSTRAINT ferengi_interactions_trader_fk FOREIGN KEY (ferengi_trader_id) REFERENCES ferengi_traders(ferengi_trader_id) ON DELETE CASCADE,
    CONSTRAINT ferengi_interactions_player_fk FOREIGN KEY (player_id) REFERENCES players(player_id) ON DELETE SET NULL,
    CONSTRAINT ferengi_interactions_deal_fk FOREIGN KEY (deal_id) REFERENCES ferengi_trader_deals(ferengi_trader_deal_id) ON DELETE SET NULL,
    CONSTRAINT ferengi_interactions_type_check CHECK (interaction_type IN ('encounter','offer','accepted','declined','expired')),
    CONSTRAINT ferengi_interactions_reputation_check CHECK (reputation_delta BETWEEN -10000 AND 10000)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS ferengi_player_relationships (
    ferengi_trader_id BIGINT NOT NULL,
    player_id BIGINT NOT NULL,
    reputation INT NOT NULL DEFAULT 0,
    encounters INT NOT NULL DEFAULT 0,
    last_interaction_at DATETIME(6) NULL,
    PRIMARY KEY (ferengi_trader_id, player_id),
    KEY idx_ferengi_relationship_player (player_id, reputation),
    CONSTRAINT ferengi_relationship_trader_fk FOREIGN KEY (ferengi_trader_id) REFERENCES ferengi_traders(ferengi_trader_id) ON DELETE CASCADE,
    CONSTRAINT ferengi_relationship_player_fk FOREIGN KEY (player_id) REFERENCES players(player_id) ON DELETE CASCADE,
    CONSTRAINT ferengi_relationship_reputation_check CHECK (reputation BETWEEN -10000 AND 10000),
    CONSTRAINT ferengi_relationship_encounters_check CHECK (encounters >= 0)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS market_shocks (
    market_shock_id BIGINT NOT NULL AUTO_INCREMENT,
    trigger_source VARCHAR(16) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    player_id BIGINT NULL,
    ferengi_trader_id BIGINT NULL,
    npc_code VARCHAR(64) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NULL,
    scope_type VARCHAR(16) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    sector_id BIGINT NULL,
    port_id BIGINT NULL,
    commodity_code VARCHAR(64) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NULL,
    multiplier_bps INT NOT NULL,
    starts_at DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
    expires_at DATETIME(6) NOT NULL,
    idempotency_key VARCHAR(191) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NOT NULL,
    created_at DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
    PRIMARY KEY (market_shock_id),
    UNIQUE KEY ux_market_shocks_idempotency (idempotency_key),
    KEY idx_market_shocks_active_scope (expires_at, starts_at, scope_type, sector_id, port_id),
    KEY idx_market_shocks_commodity (commodity_code, expires_at),
    -- A player-triggered shock cannot outlive its player: PostgreSQL's source
    -- CHECK would reject SET NULL, so RESTRICT preserves the same outcome.
    CONSTRAINT market_shocks_player_fk FOREIGN KEY (player_id) REFERENCES players(player_id) ON DELETE RESTRICT,
    CONSTRAINT market_shocks_trader_fk FOREIGN KEY (ferengi_trader_id) REFERENCES ferengi_traders(ferengi_trader_id) ON DELETE SET NULL,
    CONSTRAINT market_shocks_sector_fk FOREIGN KEY (sector_id) REFERENCES sectors(sector_id) ON DELETE CASCADE,
    CONSTRAINT market_shocks_port_fk FOREIGN KEY (port_id) REFERENCES ports(port_id) ON DELETE CASCADE,
    CONSTRAINT market_shocks_commodity_fk FOREIGN KEY (commodity_code) REFERENCES commodities(code) ON DELETE CASCADE,
    CONSTRAINT market_shocks_source_check CHECK (trigger_source IN ('random','player','npc')),
    CONSTRAINT market_shocks_scope_check CHECK (scope_type IN ('universe','sector','port')),
    CONSTRAINT market_shocks_multiplier_check CHECK (multiplier_bps BETWEEN 2500 AND 40000),
    CONSTRAINT market_shocks_expiry_check CHECK (expires_at > starts_at),
    CONSTRAINT market_shocks_scope_fields_check CHECK ((scope_type='universe' AND sector_id IS NULL AND port_id IS NULL) OR (scope_type='sector' AND sector_id IS NOT NULL AND port_id IS NULL) OR (scope_type='port' AND port_id IS NOT NULL AND sector_id IS NULL))
) ENGINE=InnoDB;

-- MySQL disallows a CHECK that references columns with FK SET NULL actions.
-- Triggers enforce the equivalent source-field rule; the player FK uses
-- RESTRICT because PostgreSQL's CHECK also prevents deleting its referenced
-- player while a player-source shock exists.
DROP TRIGGER IF EXISTS market_shocks_validate_insert;
DROP TRIGGER IF EXISTS market_shocks_validate_update;
DELIMITER //
CREATE TRIGGER market_shocks_validate_insert BEFORE INSERT ON market_shocks
FOR EACH ROW
BEGIN
  IF NOT ((NEW.trigger_source='random' AND NEW.player_id IS NULL AND NEW.ferengi_trader_id IS NULL AND NEW.npc_code IS NULL)
       OR (NEW.trigger_source='player' AND NEW.player_id IS NOT NULL AND NEW.ferengi_trader_id IS NULL AND NEW.npc_code IS NULL)
       OR (NEW.trigger_source='npc' AND NEW.player_id IS NULL AND NEW.npc_code IS NOT NULL AND CHAR_LENGTH(NEW.npc_code) BETWEEN 1 AND 64)) THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='invalid market shock source fields';
  END IF;
END//
CREATE TRIGGER market_shocks_validate_update BEFORE UPDATE ON market_shocks
FOR EACH ROW
BEGIN
  IF NOT ((NEW.trigger_source='random' AND NEW.player_id IS NULL AND NEW.ferengi_trader_id IS NULL AND NEW.npc_code IS NULL)
       OR (NEW.trigger_source='player' AND NEW.player_id IS NOT NULL AND NEW.ferengi_trader_id IS NULL AND NEW.npc_code IS NULL)
       OR (NEW.trigger_source='npc' AND NEW.player_id IS NULL AND NEW.npc_code IS NOT NULL AND CHAR_LENGTH(NEW.npc_code) BETWEEN 1 AND 64)) THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='invalid market shock source fields';
  END IF;
END//
DELIMITER ;

CREATE TABLE IF NOT EXISTS planet_tax_policies (
    planet_id BIGINT NOT NULL,
    rate_bps INT NOT NULL DEFAULT 0,
    enabled BOOLEAN NOT NULL DEFAULT FALSE,
    updated_at DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
    PRIMARY KEY (planet_id),
    CONSTRAINT planet_tax_policies_planet_fk FOREIGN KEY (planet_id) REFERENCES planets(planet_id) ON DELETE CASCADE,
    CONSTRAINT planet_tax_policies_rate_check CHECK (rate_bps BETWEEN 0 AND 10000)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS planet_economic_activity (
    planet_economic_activity_id BIGINT NOT NULL AUTO_INCREMENT,
    planet_id BIGINT NOT NULL,
    activity_type VARCHAR(16) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    commodity_code VARCHAR(64) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NOT NULL,
    quantity BIGINT NOT NULL,
    unit_value BIGINT NOT NULL,
    taxable_value BIGINT NOT NULL,
    idempotency_key VARCHAR(191) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NOT NULL,
    occurred_at DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
    assessed_at DATETIME(6) NULL,
    PRIMARY KEY (planet_economic_activity_id),
    UNIQUE KEY ux_planet_activity_idempotency (idempotency_key),
    KEY idx_planet_activity_unassessed (planet_id, assessed_at, occurred_at),
    CONSTRAINT planet_activity_planet_fk FOREIGN KEY (planet_id) REFERENCES planets(planet_id) ON DELETE CASCADE,
    CONSTRAINT planet_activity_commodity_fk FOREIGN KEY (commodity_code) REFERENCES commodities(code),
    CONSTRAINT planet_activity_type_check CHECK (activity_type IN ('production','market_trade')),
    CONSTRAINT planet_activity_quantity_check CHECK (quantity > 0),
    CONSTRAINT planet_activity_values_check CHECK (unit_value >= 0 AND taxable_value >= 0)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS planet_tax_assessments (
    planet_id BIGINT NOT NULL,
    tax_date DATE NOT NULL,
    owner_type VARCHAR(16) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    owner_id BIGINT NOT NULL,
    taxable_value BIGINT NOT NULL,
    rate_bps INT NOT NULL,
    tax_amount BIGINT NOT NULL,
    assessed_at DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
    PRIMARY KEY (planet_id, tax_date),
    KEY idx_planet_tax_owner_date (owner_type, owner_id, tax_date),
    CONSTRAINT planet_tax_assessments_planet_fk FOREIGN KEY (planet_id) REFERENCES planets(planet_id) ON DELETE CASCADE,
    CONSTRAINT planet_tax_owner_type_check CHECK (owner_type IN ('player','corp')),
    CONSTRAINT planet_tax_values_check CHECK (taxable_value >= 0 AND rate_bps BETWEEN 0 AND 10000 AND tax_amount >= 0)
) ENGINE=InnoDB;

INSERT INTO cron_tasks (name, schedule, last_run_at, next_due_at, enabled, payload)
SELECT v.name, v.schedule, NULL, CURRENT_TIMESTAMP, TRUE, NULL
FROM (SELECT 'port_hardware_restock' name, 'daily@04:15Z' schedule UNION ALL
      SELECT 'market_shock_tick', 'every:5m' UNION ALL
      SELECT 'daily_planet_tax', 'daily@05:15Z') v
WHERE NOT EXISTS (SELECT 1 FROM cron_tasks c WHERE c.name=v.name);
