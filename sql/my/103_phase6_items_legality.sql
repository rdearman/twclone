-- Phase 6: Data-Driven Devices/Items + Legality Gates (MySQL)
-- Extend hardware_items with legality and alignment gates
-- Add porttype_items mapping for availability configuration

-- Extend hardware_items table with legality and alignment gates
ALTER TABLE hardware_items ADD COLUMN is_illegal BOOLEAN NOT NULL DEFAULT FALSE;
ALTER TABLE hardware_items ADD COLUMN min_alignment INTEGER DEFAULT NULL;
ALTER TABLE hardware_items ADD COLUMN max_alignment INTEGER DEFAULT NULL;

-- Create porttype_items mapping table
CREATE TABLE IF NOT EXISTS porttype_items (
    porttype_item_id BIGINT AUTO_INCREMENT PRIMARY KEY,
    porttype_id bigint NOT NULL,
    hardware_items_id bigint NOT NULL,
    can_buy boolean NOT NULL DEFAULT true,
    can_sell boolean NOT NULL DEFAULT true,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY (porttype_id) REFERENCES porttypes (porttype_id) ON DELETE CASCADE,
    FOREIGN KEY (hardware_items_id) REFERENCES hardware_items (hardware_items_id) ON DELETE CASCADE,
    UNIQUE KEY unique_porttype_item (porttype_id, hardware_items_id)
);

-- Create indexes for frequent lookups
CREATE INDEX idx_porttype_items_porttype ON porttype_items (porttype_id);
CREATE INDEX idx_porttype_items_hardware ON porttype_items (hardware_items_id);
CREATE INDEX idx_hardware_items_code ON hardware_items (code);
CREATE INDEX idx_hardware_items_is_illegal ON hardware_items (is_illegal);

-- Seed porttype_items for existing hardware items
-- Map all existing hardware items to all porttypes (maintains backward compat)
INSERT IGNORE INTO porttype_items (porttype_id, hardware_items_id, can_buy, can_sell)
SELECT pt.porttype_id, hi.hardware_items_id, true, true
FROM porttypes pt
CROSS JOIN hardware_items hi;
