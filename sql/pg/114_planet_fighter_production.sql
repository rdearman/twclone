BEGIN;

ALTER TABLE planets
    ADD COLUMN IF NOT EXISTS colonists_weapons bigint NOT NULL DEFAULT 0,
    ADD COLUMN IF NOT EXISTS fighter_production_remainder bigint NOT NULL DEFAULT 0,
    ADD COLUMN IF NOT EXISTS fighter_production_last_tick bigint NOT NULL DEFAULT -1;

-- Reuse the legacy assignment as weapons labor, not as a soldier pool. Zeroing
-- the compatibility field makes replay safe while preserving its assignment.
UPDATE planets
SET colonists_weapons = colonists_weapons + colonists_mil,
    colonists_mil = 0
WHERE colonists_mil <> 0;

-- Class-specific weapons worker-ticks required per fighter in one 10-minute
-- production interval. One stored EQU unit is consumed per fighter.
-- Preserve any nonzero local configuration; U class has no fighter output.
UPDATE planettypes
SET fighterProduction = CASE code
    WHEN 'M' THEN 10
    WHEN 'K' THEN 15
    WHEN 'O' THEN 15
    WHEN 'L' THEN 12
    WHEN 'C' THEN 25
    WHEN 'H' THEN 50
    WHEN 'U' THEN 0
    ELSE fighterProduction
END
WHERE code IN ('M', 'K', 'O', 'L', 'C', 'H', 'U')
  AND COALESCE(fighterProduction, 0) = 0;

COMMIT;
