-- Prevent negative persisted ship resources and impossible hull values.
BEGIN;

DO $$
BEGIN
  IF NOT EXISTS (SELECT 1 FROM pg_constraint WHERE conname = 'ships_nonnegative_resources') THEN
    ALTER TABLE ships ADD CONSTRAINT ships_nonnegative_resources CHECK (
      COALESCE(holds, 0) >= 0 AND COALESCE(mines, 0) >= 0 AND
      COALESCE(limpets, 0) >= 0 AND COALESCE(fighters, 0) >= 0 AND
      COALESCE(genesis, 0) >= 0 AND COALESCE(detonators, 0) >= 0 AND
      COALESCE(probes, 0) >= 0 AND COALESCE(photons, 0) >= 0 AND
      COALESCE(shields, 0) >= 0 AND COALESCE(installed_shields, 0) >= 0 AND
      COALESCE(beacons, 0) >= 0 AND COALESCE(colonists, 0) >= 0 AND
      COALESCE(equipment, 0) >= 0 AND COALESCE(organics, 0) >= 0 AND
      COALESCE(ore, 0) >= 0 AND COALESCE(slaves, 0) >= 0 AND
      COALESCE(weapons, 0) >= 0 AND COALESCE(drugs, 0) >= 0 AND
      COALESCE(cloaking_devices, 0) >= 0
    );
  END IF;
  IF NOT EXISTS (SELECT 1 FROM pg_constraint WHERE conname = 'ships_hull_bounds') THEN
    ALTER TABLE ships ADD CONSTRAINT ships_hull_bounds CHECK (hull BETWEEN 0 AND 100);
  END IF;
END $$;

COMMIT;
