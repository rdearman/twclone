-- Approved V3 single-type planetary population state and production idempotency.
ALTER TABLE planets
  ADD COLUMN population_type VARCHAR(16) NOT NULL DEFAULT 'UNPOPULATED',
  ADD COLUMN slave_unrest_stopped BOOLEAN NOT NULL DEFAULT FALSE,
  ADD COLUMN slave_unrest_next_at BIGINT NULL,
  ADD COLUMN slave_unrest_stoppage_until BIGINT NULL,
  ADD COLUMN slave_unrest_last_event_at BIGINT NULL,
  ADD COLUMN production_last_tick BIGINT NOT NULL DEFAULT -1,
  ADD CONSTRAINT planets_population_type_check
    CHECK (population_type IN ('COLONIST', 'SLAVE', 'UNPOPULATED'));

UPDATE planets
   SET population_type = CASE
     WHEN COALESCE(population, 0) > 0
       OR colonists_unassigned > 0 OR colonists_ore > 0 OR colonists_org > 0
       OR colonists_eq > 0 OR colonists_weapons > 0
     THEN 'COLONIST'
     ELSE 'UNPOPULATED'
   END;

CREATE TABLE planet_slave_unrest_events (
  event_id BIGINT NOT NULL AUTO_INCREMENT PRIMARY KEY,
  planet_id INT NOT NULL,
  scheduled_at BIGINT NOT NULL,
  stoppage_until BIGINT NOT NULL,
  configured_population_loss BIGINT NOT NULL,
  applied_population_loss BIGINT NOT NULL DEFAULT 0,
  processed_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  CONSTRAINT fk_slave_unrest_planet FOREIGN KEY (planet_id)
    REFERENCES planets (planet_id) ON DELETE CASCADE,
  CONSTRAINT chk_unrest_configured_loss CHECK (configured_population_loss >= 0),
  CONSTRAINT chk_unrest_applied_loss CHECK (applied_population_loss >= 0),
  CONSTRAINT chk_unrest_duration CHECK (stoppage_until >= scheduled_at),
  UNIQUE KEY uq_planet_slave_unrest_occurrence (planet_id, scheduled_at)
) ENGINE=InnoDB;

DELIMITER $$
CREATE TRIGGER trg_planet_population_type_compat_insert
BEFORE INSERT ON planets FOR EACH ROW
BEGIN
  IF COALESCE(NEW.population, 0) = 0
     AND COALESCE(NEW.colonists_unassigned, 0) = 0
     AND COALESCE(NEW.colonists_ore, 0) = 0
     AND COALESCE(NEW.colonists_org, 0) = 0
     AND COALESCE(NEW.colonists_eq, 0) = 0
     AND COALESCE(NEW.colonists_weapons, 0) = 0 THEN
    SET NEW.population_type = 'UNPOPULATED';
  ELSEIF NEW.population_type = 'UNPOPULATED' THEN
    SET NEW.population_type = 'COLONIST';
  END IF;
END$$

CREATE TRIGGER trg_planet_population_type_compat_update
BEFORE UPDATE ON planets FOR EACH ROW
BEGIN
  IF COALESCE(NEW.population, 0) = 0
     AND COALESCE(NEW.colonists_unassigned, 0) = 0
     AND COALESCE(NEW.colonists_ore, 0) = 0
     AND COALESCE(NEW.colonists_org, 0) = 0
     AND COALESCE(NEW.colonists_eq, 0) = 0
     AND COALESCE(NEW.colonists_weapons, 0) = 0 THEN
    SET NEW.population_type = 'UNPOPULATED';
  ELSEIF NEW.population_type = 'UNPOPULATED' THEN
    SET NEW.population_type = 'COLONIST';
  END IF;
END$$
DELIMITER ;
