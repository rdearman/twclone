-- Approved TWClone V3 daily slave unrest policy (noncanonical game rule).
ALTER TABLE planet_slave_unrest_events
  CHANGE COLUMN configured_population_loss
    legacy_configured_population_loss BIGINT NULL,
  ADD COLUMN configured_population_loss_percent BIGINT NOT NULL DEFAULT 0,
  ADD CONSTRAINT chk_unrest_configured_loss_percent
    CHECK (configured_population_loss_percent BETWEEN 0 AND 100);

DELETE FROM config WHERE `key` = 'planet.slave_unrest_population_loss';
INSERT INTO config (`key`, `value`, `type`) VALUES
  ('planet.slave_unrest_interval_days', '1', 'int'),
  ('planet.slave_unrest_stoppage_cycles', '1', 'int'),
  ('planet.slave_unrest_population_loss_percent', '1', 'int')
ON DUPLICATE KEY UPDATE `value` = VALUES(`value`);
