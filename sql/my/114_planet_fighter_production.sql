-- MySQL equivalent of PostgreSQL migration 114.

SET @ddl = IF((SELECT COUNT(*) FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='planets' AND column_name='colonists_weapons')=0,
 'ALTER TABLE planets ADD COLUMN colonists_weapons BIGINT NOT NULL DEFAULT 0', 'SELECT 1');
PREPARE stmt FROM @ddl; EXECUTE stmt; DEALLOCATE PREPARE stmt;
SET @ddl = IF((SELECT COUNT(*) FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='planets' AND column_name='fighter_production_remainder')=0,
 'ALTER TABLE planets ADD COLUMN fighter_production_remainder BIGINT NOT NULL DEFAULT 0', 'SELECT 1');
PREPARE stmt FROM @ddl; EXECUTE stmt; DEALLOCATE PREPARE stmt;
SET @ddl = IF((SELECT COUNT(*) FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='planets' AND column_name='fighter_production_last_tick')=0,
 'ALTER TABLE planets ADD COLUMN fighter_production_last_tick BIGINT NOT NULL DEFAULT -1', 'SELECT 1');
PREPARE stmt FROM @ddl; EXECUTE stmt; DEALLOCATE PREPARE stmt;

UPDATE planets
   SET colonists_weapons = colonists_weapons + colonists_mil,
       colonists_mil = 0
 WHERE colonists_mil <> 0;

UPDATE planettypes
   SET fighterProduction = CASE code
     WHEN 'M' THEN 10 WHEN 'K' THEN 15 WHEN 'O' THEN 15 WHEN 'L' THEN 12
     WHEN 'C' THEN 25 WHEN 'H' THEN 50 WHEN 'U' THEN 0
     ELSE fighterProduction
   END
 WHERE code IN ('M','K','O','L','C','H','U')
   AND COALESCE(fighterProduction, 0) = 0;
