-- MySQL equivalent of PostgreSQL migration 113.

CREATE TABLE IF NOT EXISTS ferengi_trader_definitions (
    trader_code VARCHAR(31) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NOT NULL,
    display_name VARCHAR(95) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NOT NULL,
    ship_type_id BIGINT NOT NULL,
    active BOOLEAN NOT NULL DEFAULT TRUE,
    updated_at DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
    PRIMARY KEY (trader_code),
    CONSTRAINT ferengi_definitions_shiptype_fk FOREIGN KEY (ship_type_id) REFERENCES shiptypes(shiptypes_id),
    CONSTRAINT ferengi_definitions_code_check CHECK (CHAR_LENGTH(trader_code) BETWEEN 1 AND 31),
    CONSTRAINT ferengi_definitions_name_check CHECK (CHAR_LENGTH(display_name) BETWEEN 1 AND 95)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS ferengi_trader_definition_rotation (
    trader_code VARCHAR(31) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NOT NULL,
    position INT NOT NULL,
    commodity_code VARCHAR(64) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NOT NULL,
    PRIMARY KEY (trader_code, position),
    UNIQUE KEY ux_ferengi_rotation_commodity (trader_code, commodity_code),
    CONSTRAINT ferengi_rotation_definition_fk FOREIGN KEY (trader_code) REFERENCES ferengi_trader_definitions(trader_code) ON DELETE CASCADE,
    CONSTRAINT ferengi_rotation_commodity_fk FOREIGN KEY (commodity_code) REFERENCES commodities(code),
    CONSTRAINT ferengi_rotation_position_check CHECK (position > 0)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS ferengi_trader_definition_cargo (
    trader_code VARCHAR(31) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NOT NULL,
    commodity_code VARCHAR(64) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NOT NULL,
    quantity INT NOT NULL,
    PRIMARY KEY (trader_code, commodity_code),
    CONSTRAINT ferengi_cargo_definition_fk FOREIGN KEY (trader_code) REFERENCES ferengi_trader_definitions(trader_code) ON DELETE CASCADE,
    CONSTRAINT ferengi_cargo_commodity_fk FOREIGN KEY (commodity_code) REFERENCES commodities(code),
    CONSTRAINT ferengi_cargo_quantity_check CHECK (quantity >= 0)
) ENGINE=InnoDB;

INSERT INTO ferengi_trader_definitions (trader_code, display_name, ship_type_id)
SELECT defs.trader_code, defs.display_name, st.shiptypes_id
FROM (SELECT 'gaila' trader_code, 'Gaila the Wayfarer' display_name, 'Merchant Freighter' ship_type_name UNION ALL
      SELECT 'moogie', 'Moogie''s Ledger', 'Merchant Freighter' UNION ALL
      SELECT 'vek', 'Vek of the Freehold', 'Merchant Freighter') defs
JOIN shiptypes st ON st.name=defs.ship_type_name
ON DUPLICATE KEY UPDATE trader_code=VALUES(trader_code);

INSERT INTO ferengi_trader_definition_rotation (trader_code, position, commodity_code)
VALUES ('gaila',1,'ORE'),('gaila',2,'ORG'),('gaila',3,'EQU'),
       ('moogie',1,'ORE'),('moogie',2,'ORG'),('moogie',3,'EQU'),
       ('vek',1,'ORE'),('vek',2,'ORG'),('vek',3,'EQU')
ON DUPLICATE KEY UPDATE trader_code=VALUES(trader_code);

INSERT INTO ferengi_trader_definition_cargo (trader_code, commodity_code, quantity)
VALUES ('gaila','ORE',30),('gaila','ORG',20),('gaila','EQU',10),
       ('moogie','ORE',30),('moogie','ORG',20),('moogie','EQU',10),
       ('vek','ORE',30),('vek','ORG',20),('vek','EQU',10)
ON DUPLICATE KEY UPDATE trader_code=VALUES(trader_code);

INSERT INTO config (`key`, `value`, `type`)
VALUES ('ferengi.offer_lifetime_seconds','21600','int')
ON DUPLICATE KEY UPDATE `key`=VALUES(`key`);
