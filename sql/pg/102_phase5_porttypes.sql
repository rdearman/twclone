-- Phase 5: Data-Driven Port Types
-- Create porttypes table and migrate ports to use it

-- Create porttypes table
CREATE TABLE porttypes (
    porttype_id serial PRIMARY KEY,
    code text NOT NULL UNIQUE,
    description text,
    can_buy boolean NOT NULL DEFAULT TRUE,
    can_sell boolean NOT NULL DEFAULT TRUE,
    is_stardock boolean NOT NULL DEFAULT FALSE,
    is_black_market boolean NOT NULL DEFAULT FALSE,
    special_rules text,
    created_at timestamp DEFAULT CURRENT_TIMESTAMP
);

-- Seed base port types
INSERT INTO porttypes (code, description, can_buy, can_sell, is_stardock, is_black_market)
VALUES 
    ('CLASS0', 'Standard Trading Port', TRUE, TRUE, FALSE, FALSE),
    ('STARDOCK', 'Shipyard and Hardware Station', TRUE, TRUE, TRUE, FALSE),
    ('BLACKMARKET', 'Black Market Port', TRUE, TRUE, FALSE, TRUE)
ON CONFLICT (code) DO NOTHING;

-- Add foreign key constraint to porttype_id (column already exists from 000_tables.sql)
ALTER TABLE ports ADD CONSTRAINT fk_ports_porttype
    FOREIGN KEY (porttype_id) REFERENCES porttypes (porttype_id) ON DELETE SET NULL;

-- Backfill porttype_id from existing type values
UPDATE ports
SET porttype_id = pt.porttype_id
FROM porttypes pt
WHERE ports.porttype_id IS NULL
  AND (
    (ports.type = 0 AND pt.code = 'CLASS0') OR
    (ports.type = 9 AND pt.code = 'STARDOCK') OR
    (ports.type = 10 AND pt.code = 'BLACKMARKET')
  );

-- Set default for any unmapped types to CLASS0
UPDATE ports
SET porttype_id = (SELECT porttype_id FROM porttypes WHERE code = 'CLASS0')
WHERE porttype_id IS NULL;

-- Add index for frequent lookups
CREATE INDEX IF NOT EXISTS idx_ports_porttype_id ON ports (porttype_id);
CREATE INDEX IF NOT EXISTS idx_porttypes_code ON porttypes (code);
