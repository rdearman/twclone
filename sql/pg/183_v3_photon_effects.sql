BEGIN;

CREATE TABLE IF NOT EXISTS sector_photon_effects (
    sector_id integer PRIMARY KEY REFERENCES sectors (sector_id) ON DELETE CASCADE,
    source_player_id integer NOT NULL REFERENCES players (player_id),
    expires_at bigint NOT NULL,
    created_at timestamptz NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE IF NOT EXISTS photon_fire_requests (
    request_id text PRIMARY KEY,
    player_id integer NOT NULL REFERENCES players (player_id),
    ship_id integer NOT NULL REFERENCES ships (ship_id),
    sector_id integer NOT NULL REFERENCES sectors (sector_id),
    expires_at bigint NOT NULL,
    created_at timestamptz NOT NULL DEFAULT CURRENT_TIMESTAMP
);

INSERT INTO config (key,value,type)
VALUES ('combat.photon_effect_duration_seconds','600','int')
ON CONFLICT (key) DO NOTHING;

COMMIT;
