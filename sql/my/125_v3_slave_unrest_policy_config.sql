-- V3 population policy controls. Zero is an explicit unset/disabled sentinel;
-- it is not an active unrest rule or a gameplay interval/loss default.
INSERT IGNORE INTO config (`key`, `value`, `type`) VALUES
  ('planet.slave_unrest_interval_days', '0', 'int'),
  ('planet.slave_unrest_stoppage_cycles', '0', 'int'),
  ('planet.slave_unrest_population_loss', '0', 'int');
