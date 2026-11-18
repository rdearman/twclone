BEGIN;

ALTER TABLE planets
  ADD COLUMN IF NOT EXISTS slave_unrest_level integer NOT NULL DEFAULT 0;

DO $$
BEGIN
  IF NOT EXISTS (
    SELECT 1 FROM pg_constraint
     WHERE conname='planets_slave_unrest_level_check'
       AND conrelid='planets'::regclass
  ) THEN
    ALTER TABLE planets ADD CONSTRAINT planets_slave_unrest_level_check
      CHECK (slave_unrest_level BETWEEN 0 AND 100);
  END IF;
END $$;

CREATE TABLE IF NOT EXISTS planet_slave_production_events (
  planet_id integer NOT NULL REFERENCES planets(planet_id) ON DELETE CASCADE,
  production_tick bigint NOT NULL,
  unrest_before integer NOT NULL CHECK (unrest_before BETWEEN 0 AND 100),
  unrest_after integer NOT NULL CHECK (unrest_after BETWEEN 0 AND 100),
  disruption_probability integer NOT NULL CHECK (disruption_probability BETWEEN 0 AND 10),
  disruption_roll integer NOT NULL CHECK (disruption_roll BETWEEN 0 AND 99),
  disrupted boolean NOT NULL,
  created_at timestamptz NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (planet_id, production_tick)
);

COMMIT;
