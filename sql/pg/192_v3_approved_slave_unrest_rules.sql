-- Approved TWClone V3 daily slave unrest policy.
-- Existing event rows retain their historical fixed-unit configuration value;
-- new events record the approved percentage separately.
BEGIN;

ALTER TABLE planet_slave_unrest_events
  RENAME COLUMN configured_population_loss TO legacy_configured_population_loss;
ALTER TABLE planet_slave_unrest_events
  ALTER COLUMN legacy_configured_population_loss DROP NOT NULL;
ALTER TABLE planet_slave_unrest_events
  ADD COLUMN configured_population_loss_percent bigint NOT NULL DEFAULT 0
    CHECK (configured_population_loss_percent BETWEEN 0 AND 100);

-- The old fixed-headcount SysOp key has no compatible interpretation under
-- the approved percentage rule. Remove it and seed the approved parameters.
DELETE FROM config WHERE key = 'planet.slave_unrest_population_loss';
INSERT INTO config (key, value, type) VALUES
  ('planet.slave_unrest_interval_days', '1', 'int'),
  ('planet.slave_unrest_stoppage_cycles', '1', 'int'),
  ('planet.slave_unrest_population_loss_percent', '1', 'int')
ON CONFLICT (key) DO UPDATE SET value = EXCLUDED.value, type = EXCLUDED.type;

COMMIT;
