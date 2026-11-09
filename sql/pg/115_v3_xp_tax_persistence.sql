-- V3 foundations for idempotent player progress and global planet taxation.
-- XP totals/ranks remain in players.experience and the existing commission
-- rows. Global tax starts disabled at 0 bps until an operator configures it.

BEGIN;

CREATE TABLE IF NOT EXISTS player_progress_adjustments (
    player_progress_adjustment_id bigserial PRIMARY KEY,
    player_id integer NOT NULL REFERENCES players (player_id) ON DELETE CASCADE,
    source_type varchar(64) NOT NULL CHECK (length(source_type) > 0),
    source_key varchar(255) NOT NULL UNIQUE CHECK (length(source_key) > 0),
    xp_delta bigint NOT NULL,
    alignment_delta integer NOT NULL DEFAULT 0,
    formula_version integer NOT NULL DEFAULT 1 CHECK (formula_version > 0),
    created_at timestamptz NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE INDEX IF NOT EXISTS idx_player_progress_adjustments_player_time
    ON player_progress_adjustments (player_id, created_at DESC);

CREATE TABLE IF NOT EXISTS planet_tax_global_config (
    config_id smallint PRIMARY KEY CHECK (config_id = 1),
    rate_bps integer NOT NULL DEFAULT 0 CHECK (rate_bps BETWEEN 0 AND 10000),
    enabled boolean NOT NULL DEFAULT FALSE,
    updated_at timestamptz NOT NULL DEFAULT CURRENT_TIMESTAMP
);

INSERT INTO planet_tax_global_config (config_id, rate_bps, enabled)
VALUES (1, 0, FALSE)
ON CONFLICT (config_id) DO NOTHING;

COMMIT;
