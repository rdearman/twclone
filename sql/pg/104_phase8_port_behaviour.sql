-- Phase 8: Data-Driven Port Behaviour
-- Replace hardcoded port trading rules with DB-driven configuration
-- Enables new port type behaviours via SQL only (no code recompile)

-- Table: porttype_rules
-- Stores behaviour flags and alignment gates for entire port types
CREATE TABLE IF NOT EXISTS porttype_rules (
    porttype_rule_id serial PRIMARY KEY,
    porttype_id integer NOT NULL,
    allow_illegal boolean NOT NULL DEFAULT FALSE,
    min_alignment integer DEFAULT NULL,
    max_alignment integer DEFAULT NULL,
    notes text,
    created_at timestamp DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY (porttype_id) REFERENCES porttypes (porttype_id) ON DELETE CASCADE,
    CONSTRAINT unique_porttype_rules UNIQUE (porttype_id)
);

-- Table: porttype_commodity_rules
-- Defines which commodities each port type can buy/sell and at what price multiplier
CREATE TABLE IF NOT EXISTS porttype_commodity_rules (
    porttype_commodity_rule_id serial PRIMARY KEY,
    porttype_id integer NOT NULL,
    commodity_code text NOT NULL,
    can_buy boolean NOT NULL DEFAULT TRUE,
    can_sell boolean NOT NULL DEFAULT TRUE,
    base_price_mul integer NOT NULL DEFAULT 100,  -- integer scaling (100 = 1.0x, 150 = 1.5x)
    qty_max_mul integer NOT NULL DEFAULT 100,     -- optional multiplier for max quantity
    allow_illegal_override boolean DEFAULT NULL,   -- NULL = use porttype_rules.allow_illegal
    min_alignment integer DEFAULT NULL,
    max_alignment integer DEFAULT NULL,
    notes text,
    created_at timestamp DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY (porttype_id) REFERENCES porttypes (porttype_id) ON DELETE CASCADE,
    FOREIGN KEY (commodity_code) REFERENCES commodities (code) ON DELETE CASCADE,
    CONSTRAINT unique_porttype_commodity UNIQUE (porttype_id, commodity_code)
);

-- Table: porttype_cluster_modifiers (optional, recommended for future expansion)
-- Allows price/availability modifiers based on cluster role/alignment
CREATE TABLE IF NOT EXISTS porttype_cluster_modifiers (
    porttype_cluster_modifier_id serial PRIMARY KEY,
    porttype_id integer NOT NULL,
    cluster_id integer NOT NULL,
    price_mul integer NOT NULL DEFAULT 100,        -- multiplier on base_price_mul
    contraband_price_mul integer NOT NULL DEFAULT 100,
    notes text,
    created_at timestamp DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY (porttype_id) REFERENCES porttypes (porttype_id) ON DELETE CASCADE,
    FOREIGN KEY (cluster_id) REFERENCES clusters (clusters_id) ON DELETE CASCADE,
    CONSTRAINT unique_porttype_cluster UNIQUE (porttype_id, cluster_id)
);

-- Indexes for frequent lookups
CREATE INDEX IF NOT EXISTS idx_porttype_commodity_rules_porttype_id ON porttype_commodity_rules (porttype_id);
CREATE INDEX IF NOT EXISTS idx_porttype_commodity_rules_commodity_code ON porttype_commodity_rules (commodity_code);
CREATE INDEX IF NOT EXISTS idx_porttype_cluster_modifiers_porttype_id ON porttype_cluster_modifiers (porttype_id);
CREATE INDEX IF NOT EXISTS idx_porttype_cluster_modifiers_cluster_id ON porttype_cluster_modifiers (cluster_id);
CREATE INDEX IF NOT EXISTS idx_porttype_rules_porttype_id ON porttype_rules (porttype_id);

-- Seed defaults for existing port types
-- These maintain backward compatibility with pre-Phase8 behaviour
INSERT INTO porttype_rules (porttype_id, allow_illegal, min_alignment, max_alignment, notes)
SELECT porttype_id, FALSE, NULL, NULL, 'Default rules for ' || code
FROM porttypes
WHERE code IN ('CLASS0')
AND NOT EXISTS (SELECT 1 FROM porttype_rules WHERE porttype_id = porttypes.porttype_id)
ON CONFLICT (porttype_id) DO NOTHING;

INSERT INTO porttype_rules (porttype_id, allow_illegal, min_alignment, max_alignment, notes)
SELECT porttype_id, FALSE, NULL, NULL, 'Stardock rules'
FROM porttypes
WHERE code = 'STARDOCK'
AND NOT EXISTS (SELECT 1 FROM porttype_rules WHERE porttype_id = porttypes.porttype_id)
ON CONFLICT (porttype_id) DO NOTHING;

INSERT INTO porttype_rules (porttype_id, allow_illegal, min_alignment, max_alignment, notes)
SELECT porttype_id, TRUE, NULL, NULL, 'Black market allows illegal goods'
FROM porttypes
WHERE code = 'BLACKMARKET'
AND NOT EXISTS (SELECT 1 FROM porttype_rules WHERE porttype_id = porttypes.porttype_id)
ON CONFLICT (porttype_id) DO NOTHING;

-- Seed commodity rules for CLASS0 (standard trading)
-- These reflect the pre-Phase8 hardcoded behavior
INSERT INTO porttype_commodity_rules (porttype_id, commodity_code, can_buy, can_sell, base_price_mul, notes)
SELECT pt.porttype_id, 'ORE', TRUE, TRUE, 100, 'Ore trading at CLASS0'
FROM porttypes pt
WHERE pt.code = 'CLASS0'
AND NOT EXISTS (
    SELECT 1 FROM porttype_commodity_rules pcr
    WHERE pcr.porttype_id = pt.porttype_id AND pcr.commodity_code = 'ORE'
)
ON CONFLICT (porttype_id, commodity_code) DO NOTHING;

INSERT INTO porttype_commodity_rules (porttype_id, commodity_code, can_buy, can_sell, base_price_mul, notes)
SELECT pt.porttype_id, 'ORG', TRUE, TRUE, 100, 'Organics trading at CLASS0'
FROM porttypes pt
WHERE pt.code = 'CLASS0'
AND NOT EXISTS (
    SELECT 1 FROM porttype_commodity_rules pcr
    WHERE pcr.porttype_id = pt.porttype_id AND pcr.commodity_code = 'ORG'
)
ON CONFLICT (porttype_id, commodity_code) DO NOTHING;

INSERT INTO porttype_commodity_rules (porttype_id, commodity_code, can_buy, can_sell, base_price_mul, notes)
SELECT pt.porttype_id, 'EQU', TRUE, TRUE, 100, 'Equipment trading at CLASS0'
FROM porttypes pt
WHERE pt.code = 'CLASS0'
AND NOT EXISTS (
    SELECT 1 FROM porttype_commodity_rules pcr
    WHERE pcr.porttype_id = pt.porttype_id AND pcr.commodity_code = 'EQU'
)
ON CONFLICT (porttype_id, commodity_code) DO NOTHING;

INSERT INTO porttype_commodity_rules (porttype_id, commodity_code, can_buy, can_sell, base_price_mul, notes)
SELECT pt.porttype_id, 'SLV', TRUE, TRUE, 100, 'Slaves trading at CLASS0'
FROM porttypes pt
WHERE pt.code = 'CLASS0'
AND NOT EXISTS (
    SELECT 1 FROM porttype_commodity_rules pcr
    WHERE pcr.porttype_id = pt.porttype_id AND pcr.commodity_code = 'SLV'
)
ON CONFLICT (porttype_id, commodity_code) DO NOTHING;

INSERT INTO porttype_commodity_rules (porttype_id, commodity_code, can_buy, can_sell, base_price_mul, notes)
SELECT pt.porttype_id, 'WPN', TRUE, TRUE, 100, 'Weapons trading at CLASS0'
FROM porttypes pt
WHERE pt.code = 'CLASS0'
AND NOT EXISTS (
    SELECT 1 FROM porttype_commodity_rules pcr
    WHERE pcr.porttype_id = pt.porttype_id AND pcr.commodity_code = 'WPN'
)
ON CONFLICT (porttype_id, commodity_code) DO NOTHING;

INSERT INTO porttype_commodity_rules (porttype_id, commodity_code, can_buy, can_sell, base_price_mul, notes)
SELECT pt.porttype_id, 'DRG', TRUE, TRUE, 100, 'Drugs trading at CLASS0'
FROM porttypes pt
WHERE pt.code = 'CLASS0'
AND NOT EXISTS (
    SELECT 1 FROM porttype_commodity_rules pcr
    WHERE pcr.porttype_id = pt.porttype_id AND pcr.commodity_code = 'DRG'
)
ON CONFLICT (porttype_id, commodity_code) DO NOTHING;

-- Seed commodity rules for STARDOCK (ships/hardware only)
-- Stardocks only deal in equipment (assume for now, can expand)
INSERT INTO porttype_commodity_rules (porttype_id, commodity_code, can_buy, can_sell, base_price_mul, notes)
SELECT pt.porttype_id, 'EQU', TRUE, TRUE, 100, 'Equipment at Stardock'
FROM porttypes pt
WHERE pt.code = 'STARDOCK'
AND NOT EXISTS (
    SELECT 1 FROM porttype_commodity_rules pcr
    WHERE pcr.porttype_id = pt.porttype_id AND pcr.commodity_code = 'EQU'
)
ON CONFLICT (porttype_id, commodity_code) DO NOTHING;

-- Seed commodity rules for BLACKMARKET (all commodities including illegal)
INSERT INTO porttype_commodity_rules (porttype_id, commodity_code, can_buy, can_sell, base_price_mul, allow_illegal_override, notes)
SELECT pt.porttype_id, 'ORE', TRUE, TRUE, 100, TRUE, 'Ore at black market'
FROM porttypes pt
WHERE pt.code = 'BLACKMARKET'
AND NOT EXISTS (
    SELECT 1 FROM porttype_commodity_rules pcr
    WHERE pcr.porttype_id = pt.porttype_id AND pcr.commodity_code = 'ORE'
)
ON CONFLICT (porttype_id, commodity_code) DO NOTHING;

INSERT INTO porttype_commodity_rules (porttype_id, commodity_code, can_buy, can_sell, base_price_mul, allow_illegal_override, notes)
SELECT pt.porttype_id, 'ORG', TRUE, TRUE, 100, TRUE, 'Organics at black market'
FROM porttypes pt
WHERE pt.code = 'BLACKMARKET'
AND NOT EXISTS (
    SELECT 1 FROM porttype_commodity_rules pcr
    WHERE pcr.porttype_id = pt.porttype_id AND pcr.commodity_code = 'ORG'
)
ON CONFLICT (porttype_id, commodity_code) DO NOTHING;

INSERT INTO porttype_commodity_rules (porttype_id, commodity_code, can_buy, can_sell, base_price_mul, allow_illegal_override, notes)
SELECT pt.porttype_id, 'EQU', TRUE, TRUE, 100, TRUE, 'Equipment at black market'
FROM porttypes pt
WHERE pt.code = 'BLACKMARKET'
AND NOT EXISTS (
    SELECT 1 FROM porttype_commodity_rules pcr
    WHERE pcr.porttype_id = pt.porttype_id AND pcr.commodity_code = 'EQU'
)
ON CONFLICT (porttype_id, commodity_code) DO NOTHING;

INSERT INTO porttype_commodity_rules (porttype_id, commodity_code, can_buy, can_sell, base_price_mul, allow_illegal_override, notes)
SELECT pt.porttype_id, 'SLV', TRUE, TRUE, 100, TRUE, 'Slaves at black market'
FROM porttypes pt
WHERE pt.code = 'BLACKMARKET'
AND NOT EXISTS (
    SELECT 1 FROM porttype_commodity_rules pcr
    WHERE pcr.porttype_id = pt.porttype_id AND pcr.commodity_code = 'SLV'
)
ON CONFLICT (porttype_id, commodity_code) DO NOTHING;

INSERT INTO porttype_commodity_rules (porttype_id, commodity_code, can_buy, can_sell, base_price_mul, allow_illegal_override, notes)
SELECT pt.porttype_id, 'WPN', TRUE, TRUE, 100, TRUE, 'Weapons at black market'
FROM porttypes pt
WHERE pt.code = 'BLACKMARKET'
AND NOT EXISTS (
    SELECT 1 FROM porttype_commodity_rules pcr
    WHERE pcr.porttype_id = pt.porttype_id AND pcr.commodity_code = 'WPN'
)
ON CONFLICT (porttype_id, commodity_code) DO NOTHING;

INSERT INTO porttype_commodity_rules (porttype_id, commodity_code, can_buy, can_sell, base_price_mul, allow_illegal_override, notes)
SELECT pt.porttype_id, 'DRG', TRUE, TRUE, 150, TRUE, 'Drugs at black market (premium price)'
FROM porttypes pt
WHERE pt.code = 'BLACKMARKET'
AND NOT EXISTS (
    SELECT 1 FROM porttype_commodity_rules pcr
    WHERE pcr.porttype_id = pt.porttype_id AND pcr.commodity_code = 'DRG'
)
ON CONFLICT (porttype_id, commodity_code) DO NOTHING;
