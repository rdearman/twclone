BEGIN;

ALTER TABLE ships
  ADD COLUMN IF NOT EXISTS combat_disabled_until bigint NOT NULL DEFAULT 0;

ALTER TABLE photon_fire_requests
  ADD COLUMN IF NOT EXISTS target_ship_id integer REFERENCES ships (ship_id),
  ADD COLUMN IF NOT EXISTS direct_impact_applied boolean NOT NULL DEFAULT FALSE,
  ADD COLUMN IF NOT EXISTS target_disabled_until bigint NOT NULL DEFAULT 0;

COMMIT;
