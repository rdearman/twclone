-- Phase 8: Data-Driven Port Behaviour (MySQL version)
-- Replace hardcoded port trading rules with DB-driven configuration

-- Table: porttype_rules
CREATE TABLE IF NOT EXISTS porttype_rules (
    porttype_rule_id int AUTO_INCREMENT PRIMARY KEY,
    porttype_id int NOT NULL,
    allow_illegal boolean NOT NULL DEFAULT FALSE,
    min_alignment int DEFAULT NULL,
    max_alignment int DEFAULT NULL,
    notes text,
    created_at timestamp DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY (porttype_id) REFERENCES porttypes (porttype_id) ON DELETE CASCADE,
    CONSTRAINT unique_porttype_rules UNIQUE (porttype_id)
);

-- Table: porttype_commodity_rules
CREATE TABLE IF NOT EXISTS porttype_commodity_rules (
    porttype_commodity_rule_id int AUTO_INCREMENT PRIMARY KEY,
    porttype_id int NOT NULL,
    commodity_code varchar(10) NOT NULL,
    can_buy boolean NOT NULL DEFAULT TRUE,
    can_sell boolean NOT NULL DEFAULT TRUE,
    base_price_mul int NOT NULL DEFAULT 100,
    qty_max_mul int NOT NULL DEFAULT 100,
    allow_illegal_override boolean DEFAULT NULL,
    min_alignment int DEFAULT NULL,
    max_alignment int DEFAULT NULL,
    notes text,
    created_at timestamp DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY (porttype_id) REFERENCES porttypes (porttype_id) ON DELETE CASCADE,
    FOREIGN KEY (commodity_code) REFERENCES commodities (code) ON DELETE CASCADE,
    CONSTRAINT unique_porttype_commodity UNIQUE (porttype_id, commodity_code)
);

-- Table: porttype_cluster_modifiers
CREATE TABLE IF NOT EXISTS porttype_cluster_modifiers (
    porttype_cluster_modifier_id int AUTO_INCREMENT PRIMARY KEY,
    porttype_id int NOT NULL,
    cluster_id int NOT NULL,
    price_mul int NOT NULL DEFAULT 100,
    contraband_price_mul int NOT NULL DEFAULT 100,
    notes text,
    created_at timestamp DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY (porttype_id) REFERENCES porttypes (porttype_id) ON DELETE CASCADE,
    FOREIGN KEY (cluster_id) REFERENCES clusters (clusters_id) ON DELETE CASCADE,
    CONSTRAINT unique_porttype_cluster UNIQUE (porttype_id, cluster_id)
);

-- Indexes for frequent lookups
CREATE INDEX idx_porttype_commodity_rules_porttype_id ON porttype_commodity_rules (porttype_id);
CREATE INDEX idx_porttype_commodity_rules_commodity_code ON porttype_commodity_rules (commodity_code);
CREATE INDEX idx_porttype_cluster_modifiers_porttype_id ON porttype_cluster_modifiers (porttype_id);
CREATE INDEX idx_porttype_cluster_modifiers_cluster_id ON porttype_cluster_modifiers (cluster_id);
CREATE INDEX idx_porttype_rules_porttype_id ON porttype_rules (porttype_id);

-- Seed defaults for existing port types (MySQL-style)
INSERT INTO porttype_rules (porttype_id, allow_illegal, min_alignment, max_alignment, notes)
SELECT porttype_id, FALSE, NULL, NULL, CONCAT('Default rules for ', code)
FROM porttypes
WHERE code IN ('CLASS0')
AND NOT EXISTS (SELECT 1 FROM porttype_rules WHERE porttype_id = porttypes.porttype_id)
ON DUPLICATE KEY UPDATE porttype_rule_id=porttype_rule_id;

INSERT INTO porttype_rules (porttype_id, allow_illegal, min_alignment, max_alignment, notes)
SELECT porttype_id, FALSE, NULL, NULL, 'Stardock rules'
FROM porttypes
WHERE code = 'STARDOCK'
AND NOT EXISTS (SELECT 1 FROM porttype_rules WHERE porttype_id = porttypes.porttype_id)
ON DUPLICATE KEY UPDATE porttype_rule_id=porttype_rule_id;

INSERT INTO porttype_rules (porttype_id, allow_illegal, min_alignment, max_alignment, notes)
SELECT porttype_id, TRUE, NULL, NULL, 'Black market allows illegal goods'
FROM porttypes
WHERE code = 'BLACKMARKET'
AND NOT EXISTS (SELECT 1 FROM porttype_rules WHERE porttype_id = porttypes.porttype_id)
ON DUPLICATE KEY UPDATE porttype_rule_id=porttype_rule_id;

-- Seed commodity rules for CLASS0
INSERT INTO porttype_commodity_rules (porttype_id, commodity_code, can_buy, can_sell, base_price_mul, notes)
SELECT pt.porttype_id, 'ORE', TRUE, TRUE, 100, 'Ore trading at CLASS0'
FROM porttypes pt
WHERE pt.code = 'CLASS0'
AND NOT EXISTS (
    SELECT 1 FROM porttype_commodity_rules pcr
    WHERE pcr.porttype_id = pt.porttype_id AND pcr.commodity_code = 'ORE'
)
ON DUPLICATE KEY UPDATE porttype_commodity_rule_id=porttype_commodity_rule_id;

INSERT INTO porttype_commodity_rules (porttype_id, commodity_code, can_buy, can_sell, base_price_mul, notes)
SELECT pt.porttype_id, 'ORG', TRUE, TRUE, 100, 'Organics trading at CLASS0'
FROM porttypes pt
WHERE pt.code = 'CLASS0'
AND NOT EXISTS (
    SELECT 1 FROM porttype_commodity_rules pcr
    WHERE pcr.porttype_id = pt.porttype_id AND pcr.commodity_code = 'ORG'
)
ON DUPLICATE KEY UPDATE porttype_commodity_rule_id=porttype_commodity_rule_id;

INSERT INTO porttype_commodity_rules (porttype_id, commodity_code, can_buy, can_sell, base_price_mul, notes)
SELECT pt.porttype_id, 'EQU', TRUE, TRUE, 100, 'Equipment trading at CLASS0'
FROM porttypes pt
WHERE pt.code = 'CLASS0'
AND NOT EXISTS (
    SELECT 1 FROM porttype_commodity_rules pcr
    WHERE pcr.porttype_id = pt.porttype_id AND pcr.commodity_code = 'EQU'
)
ON DUPLICATE KEY UPDATE porttype_commodity_rule_id=porttype_commodity_rule_id;

INSERT INTO porttype_commodity_rules (porttype_id, commodity_code, can_buy, can_sell, base_price_mul, notes)
SELECT pt.porttype_id, 'SLV', TRUE, TRUE, 100, 'Slaves trading at CLASS0'
FROM porttypes pt
WHERE pt.code = 'CLASS0'
AND NOT EXISTS (
    SELECT 1 FROM porttype_commodity_rules pcr
    WHERE pcr.porttype_id = pt.porttype_id AND pcr.commodity_code = 'SLV'
)
ON DUPLICATE KEY UPDATE porttype_commodity_rule_id=porttype_commodity_rule_id;

INSERT INTO porttype_commodity_rules (porttype_id, commodity_code, can_buy, can_sell, base_price_mul, notes)
SELECT pt.porttype_id, 'WPN', TRUE, TRUE, 100, 'Weapons trading at CLASS0'
FROM porttypes pt
WHERE pt.code = 'CLASS0'
AND NOT EXISTS (
    SELECT 1 FROM porttype_commodity_rules pcr
    WHERE pcr.porttype_id = pt.porttype_id AND pcr.commodity_code = 'WPN'
)
ON DUPLICATE KEY UPDATE porttype_commodity_rule_id=porttype_commodity_rule_id;

INSERT INTO porttype_commodity_rules (porttype_id, commodity_code, can_buy, can_sell, base_price_mul, notes)
SELECT pt.porttype_id, 'DRG', TRUE, TRUE, 100, 'Drugs trading at CLASS0'
FROM porttypes pt
WHERE pt.code = 'CLASS0'
AND NOT EXISTS (
    SELECT 1 FROM porttype_commodity_rules pcr
    WHERE pcr.porttype_id = pt.porttype_id AND pcr.commodity_code = 'DRG'
)
ON DUPLICATE KEY UPDATE porttype_commodity_rule_id=porttype_commodity_rule_id;

-- Seed commodity rules for STARDOCK
INSERT INTO porttype_commodity_rules (porttype_id, commodity_code, can_buy, can_sell, base_price_mul, notes)
SELECT pt.porttype_id, 'EQU', TRUE, TRUE, 100, 'Equipment at Stardock'
FROM porttypes pt
WHERE pt.code = 'STARDOCK'
AND NOT EXISTS (
    SELECT 1 FROM porttype_commodity_rules pcr
    WHERE pcr.porttype_id = pt.porttype_id AND pcr.commodity_code = 'EQU'
)
ON DUPLICATE KEY UPDATE porttype_commodity_rule_id=porttype_commodity_rule_id;

-- Seed commodity rules for BLACKMARKET
INSERT INTO porttype_commodity_rules (porttype_id, commodity_code, can_buy, can_sell, base_price_mul, allow_illegal_override, notes)
SELECT pt.porttype_id, 'ORE', TRUE, TRUE, 100, TRUE, 'Ore at black market'
FROM porttypes pt
WHERE pt.code = 'BLACKMARKET'
AND NOT EXISTS (
    SELECT 1 FROM porttype_commodity_rules pcr
    WHERE pcr.porttype_id = pt.porttype_id AND pcr.commodity_code = 'ORE'
)
ON DUPLICATE KEY UPDATE porttype_commodity_rule_id=porttype_commodity_rule_id;

INSERT INTO porttype_commodity_rules (porttype_id, commodity_code, can_buy, can_sell, base_price_mul, allow_illegal_override, notes)
SELECT pt.porttype_id, 'ORG', TRUE, TRUE, 100, TRUE, 'Organics at black market'
FROM porttypes pt
WHERE pt.code = 'BLACKMARKET'
AND NOT EXISTS (
    SELECT 1 FROM porttype_commodity_rules pcr
    WHERE pcr.porttype_id = pt.porttype_id AND pcr.commodity_code = 'ORG'
)
ON DUPLICATE KEY UPDATE porttype_commodity_rule_id=porttype_commodity_rule_id;

INSERT INTO porttype_commodity_rules (porttype_id, commodity_code, can_buy, can_sell, base_price_mul, allow_illegal_override, notes)
SELECT pt.porttype_id, 'EQU', TRUE, TRUE, 100, TRUE, 'Equipment at black market'
FROM porttypes pt
WHERE pt.code = 'BLACKMARKET'
AND NOT EXISTS (
    SELECT 1 FROM porttype_commodity_rules pcr
    WHERE pcr.porttype_id = pt.porttype_id AND pcr.commodity_code = 'EQU'
)
ON DUPLICATE KEY UPDATE porttype_commodity_rule_id=porttype_commodity_rule_id;

INSERT INTO porttype_commodity_rules (porttype_id, commodity_code, can_buy, can_sell, base_price_mul, allow_illegal_override, notes)
SELECT pt.porttype_id, 'SLV', TRUE, TRUE, 100, TRUE, 'Slaves at black market'
FROM porttypes pt
WHERE pt.code = 'BLACKMARKET'
AND NOT EXISTS (
    SELECT 1 FROM porttype_commodity_rules pcr
    WHERE pcr.porttype_id = pt.porttype_id AND pcr.commodity_code = 'SLV'
)
ON DUPLICATE KEY UPDATE porttype_commodity_rule_id=porttype_commodity_rule_id;

INSERT INTO porttype_commodity_rules (porttype_id, commodity_code, can_buy, can_sell, base_price_mul, allow_illegal_override, notes)
SELECT pt.porttype_id, 'WPN', TRUE, TRUE, 100, TRUE, 'Weapons at black market'
FROM porttypes pt
WHERE pt.code = 'BLACKMARKET'
AND NOT EXISTS (
    SELECT 1 FROM porttype_commodity_rules pcr
    WHERE pcr.porttype_id = pt.porttype_id AND pcr.commodity_code = 'WPN'
)
ON DUPLICATE KEY UPDATE porttype_commodity_rule_id=porttype_commodity_rule_id;

INSERT INTO porttype_commodity_rules (porttype_id, commodity_code, can_buy, can_sell, base_price_mul, allow_illegal_override, notes)
SELECT pt.porttype_id, 'DRG', TRUE, TRUE, 150, TRUE, 'Drugs at black market (premium price)'
FROM porttypes pt
WHERE pt.code = 'BLACKMARKET'
AND NOT EXISTS (
    SELECT 1 FROM porttype_commodity_rules pcr
    WHERE pcr.porttype_id = pt.porttype_id AND pcr.commodity_code = 'DRG'
)
ON DUPLICATE KEY UPDATE porttype_commodity_rule_id=porttype_commodity_rule_id;
