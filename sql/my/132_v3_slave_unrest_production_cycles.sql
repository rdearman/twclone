ALTER TABLE planets
  ADD COLUMN slave_unrest_level integer NOT NULL DEFAULT 0,
  ADD CONSTRAINT planets_slave_unrest_level_check
    CHECK (slave_unrest_level BETWEEN 0 AND 100);

CREATE TABLE planet_slave_production_events (
  planet_id integer NOT NULL,
  production_tick bigint NOT NULL,
  unrest_before integer NOT NULL,
  unrest_after integer NOT NULL,
  disruption_probability integer NOT NULL,
  disruption_roll integer NOT NULL,
  disrupted boolean NOT NULL,
  created_at timestamp NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (planet_id, production_tick),
  CONSTRAINT planet_slave_production_events_planet_fk
    FOREIGN KEY (planet_id) REFERENCES planets(planet_id) ON DELETE CASCADE,
  CONSTRAINT planet_slave_production_events_unrest_before_check
    CHECK (unrest_before BETWEEN 0 AND 100),
  CONSTRAINT planet_slave_production_events_unrest_after_check
    CHECK (unrest_after BETWEEN 0 AND 100),
  CONSTRAINT planet_slave_production_events_probability_check
    CHECK (disruption_probability BETWEEN 0 AND 10),
  CONSTRAINT planet_slave_production_events_roll_check
    CHECK (disruption_roll BETWEEN 0 AND 99)
);
