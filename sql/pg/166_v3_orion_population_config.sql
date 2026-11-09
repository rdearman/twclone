-- Orion population settings use the existing config table.  This is kept
-- separate from Ferengi roster correction so each faction's configuration
-- has one clear owner.
BEGIN;

INSERT INTO config (key, value, type)
VALUES ('orion.population.basis_points', '50', 'int'),
       ('orion.population.minimum', '5', 'int')
ON CONFLICT (key) DO NOTHING;

COMMIT;
