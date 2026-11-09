-- Approved V3 single-type planetary population state and production idempotency.
BEGIN;

ALTER TABLE planets
  ADD COLUMN IF NOT EXISTS population_type text NOT NULL DEFAULT 'UNPOPULATED',
  ADD COLUMN IF NOT EXISTS slave_unrest_stopped boolean NOT NULL DEFAULT FALSE,
  ADD COLUMN IF NOT EXISTS slave_unrest_next_at bigint,
  ADD COLUMN IF NOT EXISTS slave_unrest_stoppage_until bigint,
  ADD COLUMN IF NOT EXISTS slave_unrest_last_event_at bigint,
  ADD COLUMN IF NOT EXISTS production_last_tick bigint NOT NULL DEFAULT -1;

-- Existing populated planets have always used the colonist allocation model;
-- preserve every count/allocation and classify only the population type.
UPDATE planets
   SET population_type = CASE
     WHEN COALESCE(population, 0) > 0
       OR colonists_unassigned > 0 OR colonists_ore > 0 OR colonists_org > 0
       OR colonists_eq > 0 OR colonists_weapons > 0
     THEN 'COLONIST'
     ELSE 'UNPOPULATED'
   END;

DO $$
BEGIN
  IF NOT EXISTS (
    SELECT 1 FROM pg_constraint
     WHERE conname = 'planets_population_type_check'
       AND conrelid = 'planets'::regclass
  ) THEN
    ALTER TABLE planets ADD CONSTRAINT planets_population_type_check
      CHECK (population_type IN ('COLONIST', 'SLAVE', 'UNPOPULATED'));
  END IF;
END $$;

CREATE TABLE IF NOT EXISTS planet_slave_unrest_events (
  event_id bigserial PRIMARY KEY,
  planet_id integer NOT NULL REFERENCES planets (planet_id) ON DELETE CASCADE,
  scheduled_at bigint NOT NULL,
  stoppage_until bigint NOT NULL,
  configured_population_loss bigint NOT NULL CHECK (configured_population_loss >= 0),
  applied_population_loss bigint NOT NULL DEFAULT 0 CHECK (applied_population_loss >= 0),
  processed_at timestamptz NOT NULL DEFAULT CURRENT_TIMESTAMP,
  UNIQUE (planet_id, scheduled_at),
  CHECK (stoppage_until >= scheduled_at)
);

-- Keep legacy planet-generation and worker-assignment writers compatible:
-- nonzero populations created through older SQL paths default to COLONIST,
-- while an explicitly persisted SLAVE population remains SLAVE.
CREATE OR REPLACE FUNCTION maintain_planet_population_type()
RETURNS trigger
LANGUAGE plpgsql
AS $$
DECLARE
  tracked_population bigint;
BEGIN
  tracked_population := GREATEST(
    COALESCE(NEW.population, 0),
    COALESCE(NEW.colonists_unassigned, 0)
      + COALESCE(NEW.colonists_ore, 0)
      + COALESCE(NEW.colonists_org, 0)
      + COALESCE(NEW.colonists_eq, 0)
      + COALESCE(NEW.colonists_weapons, 0)
  );
  IF tracked_population <= 0 THEN
    NEW.population_type := 'UNPOPULATED';
  ELSIF NEW.population_type = 'UNPOPULATED' THEN
    NEW.population_type := 'COLONIST';
  END IF;
  RETURN NEW;
END;
$$;

DROP TRIGGER IF EXISTS trg_planet_population_type_compat ON planets;
CREATE TRIGGER trg_planet_population_type_compat
BEFORE INSERT OR UPDATE ON planets
FOR EACH ROW EXECUTE FUNCTION maintain_planet_population_type();

COMMIT;
