-- MySQL equivalent of PostgreSQL migration 111.
-- Ship override > corporation default > ship-type default > balanced fallback.

SET @ddl = IF((SELECT COUNT(*) FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='ships' AND column_name='personality')=0,
 'ALTER TABLE ships ADD COLUMN personality VARCHAR(16) CHARACTER SET ascii COLLATE ascii_bin NULL, ADD CONSTRAINT ships_personality_check CHECK (personality IS NULL OR personality IN (''balanced'',''offensive'',''defensive'',''looter'',''blockader''))', 'SELECT 1');
PREPARE stmt FROM @ddl; EXECUTE stmt; DEALLOCATE PREPARE stmt;
SET @ddl = IF((SELECT COUNT(*) FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='corporations' AND column_name='ship_personality')=0,
 'ALTER TABLE corporations ADD COLUMN ship_personality VARCHAR(16) CHARACTER SET ascii COLLATE ascii_bin NULL, ADD CONSTRAINT corporations_ship_personality_check CHECK (ship_personality IS NULL OR ship_personality IN (''balanced'',''offensive'',''defensive'',''looter'',''blockader''))', 'SELECT 1');
PREPARE stmt FROM @ddl; EXECUTE stmt; DEALLOCATE PREPARE stmt;
SET @ddl = IF((SELECT COUNT(*) FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='shiptypes' AND column_name='default_personality')=0,
 'ALTER TABLE shiptypes ADD COLUMN default_personality VARCHAR(16) CHARACTER SET ascii COLLATE ascii_bin NULL, ADD CONSTRAINT shiptypes_default_personality_check CHECK (default_personality IS NULL OR default_personality IN (''balanced'',''offensive'',''defensive'',''looter'',''blockader''))', 'SELECT 1');
PREPARE stmt FROM @ddl; EXECUTE stmt; DEALLOCATE PREPARE stmt;

UPDATE shiptypes
   SET default_personality = CASE `name`
     WHEN 'Orion Heavy Fighter Patrol' THEN 'offensive'
     WHEN 'Orion Scout/Looter' THEN 'looter'
     WHEN 'Orion Contraband Runner' THEN 'looter'
     WHEN 'Orion Smuggler''s Kiss' THEN 'looter'
     WHEN 'Orion Black Market Guard' THEN 'blockader'
     ELSE default_personality
   END
 WHERE default_personality IS NULL
   AND `name` IN ('Orion Heavy Fighter Patrol', 'Orion Scout/Looter',
                  'Orion Contraband Runner', 'Orion Smuggler''s Kiss',
                  'Orion Black Market Guard');
