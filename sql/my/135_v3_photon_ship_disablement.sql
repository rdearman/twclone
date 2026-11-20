ALTER TABLE ships
  ADD COLUMN combat_disabled_until BIGINT NOT NULL DEFAULT 0;

ALTER TABLE photon_fire_requests
  ADD COLUMN target_ship_id BIGINT NULL,
  ADD COLUMN direct_impact_applied BOOLEAN NOT NULL DEFAULT FALSE,
  ADD COLUMN target_disabled_until BIGINT NOT NULL DEFAULT 0,
  ADD CONSTRAINT fk_photon_target_ship FOREIGN KEY (target_ship_id)
    REFERENCES ships (ship_id);
