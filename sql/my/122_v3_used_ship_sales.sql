-- V3-SHIP-01: persistent sale evidence and identifiable used hull inventory.
CREATE TABLE IF NOT EXISTS ship_sale_audit (
    sale_id BIGINT NOT NULL AUTO_INCREMENT PRIMARY KEY,
    ship_id INT NOT NULL,
    seller_player_id INT NOT NULL,
    replacement_ship_id INT NOT NULL,
    port_id INT NOT NULL,
    hull_valuation BIGINT NOT NULL,
    equipment_valuation BIGINT NOT NULL,
    sale_factor_bp INT NOT NULL,
    proceeds BIGINT NOT NULL,
    cargo_forfeited BOOLEAN NOT NULL DEFAULT TRUE,
    insurance_terminated BOOLEAN NOT NULL DEFAULT TRUE,
    sold_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    CONSTRAINT chk_ship_sale_hull_value CHECK (hull_valuation >= 0),
    CONSTRAINT chk_ship_sale_equipment_value CHECK (equipment_valuation >= 0),
    CONSTRAINT chk_ship_sale_factor CHECK (sale_factor_bp BETWEEN 0 AND 10000),
    CONSTRAINT chk_ship_sale_proceeds CHECK (proceeds >= 0),
    CONSTRAINT fk_ship_sale_ship FOREIGN KEY (ship_id) REFERENCES ships (ship_id),
    CONSTRAINT fk_ship_sale_seller FOREIGN KEY (seller_player_id) REFERENCES players (player_id),
    CONSTRAINT fk_ship_sale_replacement FOREIGN KEY (replacement_ship_id) REFERENCES ships (ship_id),
    CONSTRAINT fk_ship_sale_port FOREIGN KEY (port_id) REFERENCES ports (port_id)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS shipyard_used_inventory (
    used_inventory_id BIGINT NOT NULL AUTO_INCREMENT PRIMARY KEY,
    sale_id BIGINT NOT NULL UNIQUE,
    ship_id INT NOT NULL UNIQUE,
    port_id INT NOT NULL,
    listed_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    CONSTRAINT fk_used_inventory_sale FOREIGN KEY (sale_id) REFERENCES ship_sale_audit (sale_id),
    CONSTRAINT fk_used_inventory_ship FOREIGN KEY (ship_id) REFERENCES ships (ship_id),
    CONSTRAINT fk_used_inventory_port FOREIGN KEY (port_id) REFERENCES ports (port_id),
    INDEX idx_shipyard_used_inventory_port (port_id, listed_at, used_inventory_id)
) ENGINE=InnoDB;
