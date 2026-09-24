-- Phase 4: Data-Driven Ship Types
-- Seed existing ship type restrictions into shiptype_restrictions table

-- Corporate Flagship: CEO only
INSERT INTO shiptype_restrictions (shiptypes_id, check_type, check_value, description, enabled)
SELECT shiptypes_id, 'CEO', '1', 'Must be a corporation CEO to purchase Corporate Flagship', TRUE
FROM shiptypes WHERE name = 'Corporate Flagship'
ON CONFLICT (shiptypes_id, check_type) DO NOTHING;

-- IIS (Interstellar Imperium Ship): High alignment requirement
INSERT INTO shiptype_restrictions (shiptypes_id, check_type, check_value, description, enabled)
SELECT shiptypes_id, 'ALIGNMENT_MIN', '5000', 'Must have alignment greater than 5000 to purchase IIS', TRUE
FROM shiptypes WHERE name = 'IIS'
ON CONFLICT (shiptypes_id, check_type) DO NOTHING;
