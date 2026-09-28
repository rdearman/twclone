-- Data-driven Ferengi roster, starting cargo, commodity order, and offer TTL.
BEGIN;

CREATE TABLE IF NOT EXISTS ferengi_trader_definitions (
    trader_code text PRIMARY KEY CHECK (length(trader_code) BETWEEN 1 AND 31),
    display_name text NOT NULL CHECK (length(display_name) BETWEEN 1 AND 95),
    ship_type_id integer NOT NULL REFERENCES shiptypes (shiptypes_id),
    active boolean NOT NULL DEFAULT TRUE,
    updated_at timestamptz NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE IF NOT EXISTS ferengi_trader_definition_rotation (
    trader_code text NOT NULL REFERENCES ferengi_trader_definitions (trader_code) ON DELETE CASCADE,
    position integer NOT NULL CHECK (position > 0),
    commodity_code text NOT NULL REFERENCES commodities (code),
    PRIMARY KEY (trader_code, position),
    UNIQUE (trader_code, commodity_code)
);

CREATE TABLE IF NOT EXISTS ferengi_trader_definition_cargo (
    trader_code text NOT NULL REFERENCES ferengi_trader_definitions (trader_code) ON DELETE CASCADE,
    commodity_code text NOT NULL REFERENCES commodities (code),
    quantity integer NOT NULL CHECK (quantity >= 0),
    PRIMARY KEY (trader_code, commodity_code)
);

INSERT INTO ferengi_trader_definitions
    (trader_code, display_name, ship_type_id)
SELECT defs.trader_code, defs.display_name, st.shiptypes_id
FROM (VALUES
    ('gaila', 'Gaila the Wayfarer', 'Merchant Freighter'),
    ('moogie', 'Moogie''s Ledger', 'Merchant Freighter'),
    ('vek', 'Vek of the Freehold', 'Merchant Freighter')
) AS defs(trader_code, display_name, ship_type_name)
JOIN shiptypes st ON st.name = defs.ship_type_name
ON CONFLICT (trader_code) DO NOTHING;

INSERT INTO ferengi_trader_definition_rotation
    (trader_code, position, commodity_code)
VALUES
    ('gaila', 1, 'ORE'), ('gaila', 2, 'ORG'), ('gaila', 3, 'EQU'),
    ('moogie', 1, 'ORE'), ('moogie', 2, 'ORG'), ('moogie', 3, 'EQU'),
    ('vek', 1, 'ORE'), ('vek', 2, 'ORG'), ('vek', 3, 'EQU')
ON CONFLICT (trader_code, position) DO NOTHING;

INSERT INTO ferengi_trader_definition_cargo (trader_code, commodity_code, quantity)
VALUES
    ('gaila', 'ORE', 30), ('gaila', 'ORG', 20), ('gaila', 'EQU', 10),
    ('moogie', 'ORE', 30), ('moogie', 'ORG', 20), ('moogie', 'EQU', 10),
    ('vek', 'ORE', 30), ('vek', 'ORG', 20), ('vek', 'EQU', 10)
ON CONFLICT (trader_code, commodity_code) DO NOTHING;

INSERT INTO config (key, value, type)
VALUES ('ferengi.offer_lifetime_seconds', '21600', 'int')
ON CONFLICT (key) DO NOTHING;

COMMIT;
