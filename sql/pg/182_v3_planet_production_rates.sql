BEGIN;

ALTER TABLE planet_production
    ADD COLUMN IF NOT EXISTS workers_per_unit integer NOT NULL DEFAULT 0;

ALTER TABLE planets
    ADD COLUMN IF NOT EXISTS ore_production_remainder bigint NOT NULL DEFAULT 0,
    ADD COLUMN IF NOT EXISTS organics_production_remainder bigint NOT NULL DEFAULT 0,
    ADD COLUMN IF NOT EXISTS equipment_production_remainder bigint NOT NULL DEFAULT 0;

-- Noncanonical twclone V3 defaults, version 1. Rates are workers required
-- to produce one unit during a 10-minute cycle. Preserve local overrides.
INSERT INTO planet_production (planet_type_id, commodity_code,
                               base_prod_rate, base_cons_rate,
                               workers_per_unit)
SELECT pt.planettypes_id, r.commodity, 0, 0, r.workers_per_unit
FROM planettypes pt
CROSS JOIN (VALUES
    ('ORE', 'M', 100), ('ORG', 'M', 100), ('EQU', 'M', 100),
    ('ORE', 'L', 50),  ('ORG', 'L', 100), ('EQU', 'L', 100),
    ('ORE', 'O', 100), ('ORG', 'O', 50),  ('EQU', 'O', 100),
    ('ORE', 'K', 50),  ('ORG', 'K', 100), ('EQU', 'K', 100),
    ('ORE', 'H', 50),  ('ORG', 'H', 100), ('EQU', 'H', 50),
    ('ORE', 'C', 100), ('ORG', 'C', 100), ('EQU', 'C', 100),
    ('ORE', 'U', 0),   ('ORG', 'U', 0),   ('EQU', 'U', 0)
) AS r(commodity, class_code, workers_per_unit)
WHERE pt.code = r.class_code
  AND EXISTS (SELECT 1 FROM commodities c WHERE c.code = r.commodity)
ON CONFLICT (planet_type_id, commodity_code) DO UPDATE
SET workers_per_unit = EXCLUDED.workers_per_unit
WHERE planet_production.workers_per_unit = 0;

COMMIT;
