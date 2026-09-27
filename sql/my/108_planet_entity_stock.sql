-- Make entity_stock the sole runtime balance for planet commodities.
-- This is an operational data migration; it does not alter universe generation.
-- MySQL commits ALTER TABLE independently. The data copy below remains atomic;
-- if it fails, legacy quantities remain and this file can be rerun.
SET @planet_goods_check = (
  SELECT tc.CONSTRAINT_NAME
  FROM information_schema.TABLE_CONSTRAINTS tc
  LEFT JOIN information_schema.CHECK_CONSTRAINTS cc
    ON cc.CONSTRAINT_SCHEMA = tc.CONSTRAINT_SCHEMA
   AND cc.CONSTRAINT_NAME = tc.CONSTRAINT_NAME
  WHERE tc.CONSTRAINT_SCHEMA = DATABASE()
    AND tc.TABLE_NAME = 'planet_goods'
    AND tc.CONSTRAINT_TYPE = 'CHECK'
    AND (tc.CONSTRAINT_NAME = 'planet_goods_commodity_check'
         OR cc.CHECK_CLAUSE LIKE '%commodity%')
  ORDER BY (tc.CONSTRAINT_NAME = 'planet_goods_commodity_check') DESC
  LIMIT 1
);
SET @planet_goods_ddl = IF(@planet_goods_check IS NULL,
  'SELECT 1',
  CONCAT('ALTER TABLE planet_goods DROP CHECK `', REPLACE(@planet_goods_check, '`', '``'), '`'));
PREPARE planet_goods_stmt FROM @planet_goods_ddl;
EXECUTE planet_goods_stmt;
DEALLOCATE PREPARE planet_goods_stmt;

START TRANSACTION;

INSERT INTO planet_goods (planet_id, commodity, quantity, max_capacity, production_rate)
SELECT p.planet_id, cap.commodity, 0,
       CASE cap.commodity WHEN 'ORE' THEN pt.maxore WHEN 'ORG' THEN pt.maxorganics ELSE pt.maxequipment END,
       0
FROM planets p
JOIN planettypes pt ON pt.planettypes_id = p.type
JOIN (
  SELECT 'ORE' AS commodity
  UNION ALL SELECT 'ORG'
  UNION ALL SELECT 'EQU'
) cap
WHERE NOT EXISTS (
  SELECT 1 FROM planet_goods pg
  WHERE pg.planet_id = p.planet_id AND pg.commodity = cap.commodity
);

INSERT INTO entity_stock (entity_type, entity_id, commodity_code, quantity, price, last_updated_ts)
SELECT 'planet', pg.planet_id, pg.commodity, 0, 0, UNIX_TIMESTAMP()
FROM planet_goods pg WHERE pg.commodity IN ('ORE', 'ORG', 'EQU')
ON DUPLICATE KEY UPDATE entity_id = entity_stock.entity_id;

-- Add legacy balances to any existing canonical balance. Sources are zeroed below,
-- which makes rerunning the data portion safe after an interrupted deployment.
INSERT INTO entity_stock (entity_type, entity_id, commodity_code, quantity, price, last_updated_ts)
SELECT 'planet', p.planet_id, goods.commodity, goods.quantity, 0, UNIX_TIMESTAMP()
FROM (
  SELECT planet_id, 'ORE' AS commodity, ore_on_hand AS quantity FROM planets
  UNION ALL SELECT planet_id, 'ORG', organics_on_hand FROM planets
  UNION ALL SELECT planet_id, 'EQU', equipment_on_hand FROM planets
  UNION ALL SELECT planet_id, commodity, quantity FROM planet_goods
) goods
JOIN planets p ON p.planet_id = goods.planet_id
WHERE COALESCE(goods.quantity, 0) <> 0
ON DUPLICATE KEY UPDATE quantity = entity_stock.quantity + VALUES(quantity), last_updated_ts = VALUES(last_updated_ts);

UPDATE planets SET ore_on_hand = 0, organics_on_hand = 0, equipment_on_hand = 0
WHERE COALESCE(ore_on_hand, 0) <> 0 OR COALESCE(organics_on_hand, 0) <> 0 OR COALESCE(equipment_on_hand, 0) <> 0;
UPDATE planet_goods SET quantity = 0 WHERE COALESCE(quantity, 0) <> 0;
COMMIT;
