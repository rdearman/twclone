-- Phase 6: Data-Driven Devices/Items + Legality Gates
-- Extend hardware_items with legality and alignment gates
-- Add porttype_items mapping for availability configuration

-- Extend hardware_items table with legality and alignment gates
ALTER TABLE hardware_items ADD COLUMN IF NOT EXISTS is_illegal BOOLEAN NOT NULL DEFAULT FALSE;
ALTER TABLE hardware_items ADD COLUMN IF NOT EXISTS min_alignment INTEGER DEFAULT NULL;
ALTER TABLE hardware_items ADD COLUMN IF NOT EXISTS max_alignment INTEGER DEFAULT NULL;

-- Create porttype_items mapping table
-- This allows configuration of which items are available at which port types
CREATE TABLE IF NOT EXISTS porttype_items (
    porttype_item_id serial PRIMARY KEY,
    porttype_id integer NOT NULL,
    hardware_items_id integer NOT NULL,
    can_buy boolean NOT NULL DEFAULT true,
    can_sell boolean NOT NULL DEFAULT true,
    created_at timestamp DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY (porttype_id) REFERENCES porttypes (porttype_id) ON DELETE CASCADE,
    FOREIGN KEY (hardware_items_id) REFERENCES hardware_items (hardware_items_id) ON DELETE CASCADE,
    CONSTRAINT unique_porttype_item UNIQUE (porttype_id, hardware_items_id)
);

-- Create indexes for frequent lookups
CREATE INDEX IF NOT EXISTS idx_porttype_items_porttype ON porttype_items (porttype_id);
CREATE INDEX IF NOT EXISTS idx_porttype_items_hardware ON porttype_items (hardware_items_id);
CREATE INDEX IF NOT EXISTS idx_hardware_items_code ON hardware_items (code);
CREATE INDEX IF NOT EXISTS idx_hardware_items_is_illegal ON hardware_items (is_illegal);

-- Seed porttype_items for existing hardware items
-- Map all existing hardware items to all porttypes (maintains backward compat)
INSERT INTO porttype_items (porttype_id, hardware_items_id, can_buy, can_sell)
SELECT pt.porttype_id, hi.hardware_items_id, true, true
FROM porttypes pt
CROSS JOIN hardware_items hi
WHERE NOT EXISTS (
    SELECT 1 FROM porttype_items pi
    WHERE pi.porttype_id = pt.porttype_id
    AND pi.hardware_items_id = hi.hardware_items_id
)
ON CONFLICT (porttype_id, hardware_items_id) DO NOTHING;
