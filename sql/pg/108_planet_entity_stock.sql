-- Make entity_stock the sole runtime balance for planet commodities.
-- This is an operational data migration; it does not alter universe generation.
BEGIN;

ALTER TABLE planet_goods
  DROP CONSTRAINT IF EXISTS planet_goods_commodity_check;

INSERT INTO planet_goods (planet_id, commodity, quantity, max_capacity, production_rate)
SELECT p.planet_id, cap.commodity, 0, cap.capacity, 0
FROM planets p
JOIN planettypes pt ON pt.planettypes_id = p.type
CROSS JOIN LATERAL (VALUES
  ('ORE', pt.maxore), ('ORG', pt.maxorganics), ('EQU', pt.maxequipment)
) AS cap(commodity, capacity)
WHERE NOT EXISTS (
  SELECT 1 FROM planet_goods pg
  WHERE pg.planet_id = p.planet_id AND pg.commodity = cap.commodity
);

INSERT INTO entity_stock (entity_type, entity_id, commodity_code, quantity, price, last_updated_ts)
SELECT 'planet', pg.planet_id, pg.commodity, 0, 0, EXTRACT(EPOCH FROM NOW())::bigint
FROM planet_goods pg WHERE pg.commodity IN ('ORE', 'ORG', 'EQU')
ON CONFLICT (entity_type, entity_id, commodity_code) DO NOTHING;

INSERT INTO entity_stock (entity_type, entity_id, commodity_code, quantity, price, last_updated_ts)
SELECT 'planet', p.planet_id, 'ORE', p.ore_on_hand, 0, EXTRACT(EPOCH FROM NOW())::bigint
FROM planets p WHERE COALESCE(p.ore_on_hand, 0) <> 0
ON CONFLICT (entity_type, entity_id, commodity_code) DO UPDATE
SET quantity = entity_stock.quantity + EXCLUDED.quantity,
    last_updated_ts = EXCLUDED.last_updated_ts;

INSERT INTO entity_stock (entity_type, entity_id, commodity_code, quantity, price, last_updated_ts)
SELECT 'planet', p.planet_id, 'ORG', p.organics_on_hand, 0, EXTRACT(EPOCH FROM NOW())::bigint
FROM planets p WHERE COALESCE(p.organics_on_hand, 0) <> 0
ON CONFLICT (entity_type, entity_id, commodity_code) DO UPDATE
SET quantity = entity_stock.quantity + EXCLUDED.quantity,
    last_updated_ts = EXCLUDED.last_updated_ts;

INSERT INTO entity_stock (entity_type, entity_id, commodity_code, quantity, price, last_updated_ts)
SELECT 'planet', p.planet_id, 'EQU', p.equipment_on_hand, 0, EXTRACT(EPOCH FROM NOW())::bigint
FROM planets p WHERE COALESCE(p.equipment_on_hand, 0) <> 0
ON CONFLICT (entity_type, entity_id, commodity_code) DO UPDATE
SET quantity = entity_stock.quantity + EXCLUDED.quantity,
    last_updated_ts = EXCLUDED.last_updated_ts;

INSERT INTO entity_stock (entity_type, entity_id, commodity_code, quantity, price, last_updated_ts)
SELECT 'planet', pg.planet_id, pg.commodity, pg.quantity, 0, EXTRACT(EPOCH FROM NOW())::bigint
FROM planet_goods pg WHERE COALESCE(pg.quantity, 0) <> 0
ON CONFLICT (entity_type, entity_id, commodity_code) DO UPDATE
SET quantity = entity_stock.quantity + EXCLUDED.quantity,
    last_updated_ts = EXCLUDED.last_updated_ts;

UPDATE planets SET ore_on_hand = 0, organics_on_hand = 0, equipment_on_hand = 0
WHERE COALESCE(ore_on_hand, 0) <> 0 OR COALESCE(organics_on_hand, 0) <> 0 OR COALESCE(equipment_on_hand, 0) <> 0;
UPDATE planet_goods SET quantity = 0 WHERE COALESCE(quantity, 0) <> 0;

COMMIT;
