-- V3 NPC phases 2, 6 and 8: durable action/idempotency and security state.

CREATE TABLE IF NOT EXISTS npc_action_log (
    idempotency_key text PRIMARY KEY,
    faction text NOT NULL,
    action_type text NOT NULL,
    npc_ship_id integer REFERENCES ships(ship_id) ON DELETE SET NULL,
    target_player_id integer REFERENCES players(player_id) ON DELETE SET NULL,
    sector_id integer REFERENCES sectors(sector_id) ON DELETE SET NULL,
    status text NOT NULL DEFAULT 'started'
        CHECK (status IN ('started', 'completed', 'refused', 'failed')),
    payload text,
    created_at timestamptz NOT NULL DEFAULT CURRENT_TIMESTAMP,
    completed_at timestamptz
);

CREATE INDEX IF NOT EXISTS idx_npc_action_log_ship_time
    ON npc_action_log(npc_ship_id, created_at);

CREATE TABLE IF NOT EXISTS federation_protected_assets (
    asset_kind text NOT NULL,
    asset_id integer NOT NULL,
    reason text NOT NULL,
    enabled boolean NOT NULL DEFAULT TRUE,
    created_at timestamptz NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (asset_kind, asset_id)
);

CREATE TABLE IF NOT EXISTS npc_security_incidents (
    incident_id bigserial PRIMARY KEY,
    idempotency_key text NOT NULL UNIQUE,
    offender_player_id integer NOT NULL REFERENCES players(player_id),
    protected_asset_kind text NOT NULL,
    protected_asset_id integer NOT NULL,
    origin_sector_id integer NOT NULL REFERENCES sectors(sector_id),
    status text NOT NULL DEFAULT 'open'
        CHECK (status IN ('open', 'dispatched', 'pursuing', 'resolved', 'expired')),
    created_at timestamptz NOT NULL DEFAULT CURRENT_TIMESTAMP,
    expires_at timestamptz NOT NULL,
    resolved_at timestamptz,
    resolution text
);

CREATE INDEX IF NOT EXISTS idx_npc_security_incidents_active
    ON npc_security_incidents(status, expires_at);

CREATE TABLE IF NOT EXISTS npc_intelligence (
    npc_ship_id integer NOT NULL REFERENCES ships(ship_id) ON DELETE CASCADE,
    target_player_id integer NOT NULL REFERENCES players(player_id) ON DELETE CASCADE,
    last_known_sector_id integer NOT NULL REFERENCES sectors(sector_id),
    source text NOT NULL,
    observed_at timestamptz NOT NULL DEFAULT CURRENT_TIMESTAMP,
    expires_at timestamptz NOT NULL,
    PRIMARY KEY (npc_ship_id, target_player_id)
);

CREATE INDEX IF NOT EXISTS idx_npc_intelligence_active
    ON npc_intelligence(expires_at, last_known_sector_id);

INSERT INTO config(key, value, type)
VALUES
    ('npc.iss_incident_lifetime_seconds', '86400', 'int'),
    ('npc.iss_pursuit_information_lifetime_seconds', '1800', 'int'),
    ('npc.action_stale_seconds', '3600', 'int')
ON CONFLICT (key) DO NOTHING;

-- Existing Federation-owned ships and the StarDock asset are protected by
-- default.  This is an idempotent registration of current-world assets; it
-- does not generate or alter MSL topology.
INSERT INTO federation_protected_assets (asset_kind, asset_id, reason)
SELECT 'ship', so.ship_id, 'Federation-owned ship'
FROM ship_ownership so
JOIN players p ON p.player_id = so.player_id
WHERE so.is_primary = TRUE
  AND lower(p.name) = lower('Federation Administrator')
ON CONFLICT (asset_kind, asset_id) DO NOTHING;

INSERT INTO federation_protected_assets (asset_kind, asset_id, reason)
SELECT 'stardock', sector_id, 'Federation StarDock installation'
FROM stardock_assets
ON CONFLICT (asset_kind, asset_id) DO NOTHING;
