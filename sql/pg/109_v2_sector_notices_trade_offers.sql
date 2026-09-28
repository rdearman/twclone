-- V2 durable sector notices and player trade offers (PostgreSQL only).
-- Existing system notices remain global by default and retain their legacy fields.

BEGIN;

ALTER TABLE system_notice
    ADD COLUMN IF NOT EXISTS scope text NOT NULL DEFAULT 'global',
    ADD COLUMN IF NOT EXISTS sector_id integer,
    ADD COLUMN IF NOT EXISTS player_id integer,
    ADD COLUMN IF NOT EXISTS meta jsonb,
    ADD COLUMN IF NOT EXISTS ephemeral boolean NOT NULL DEFAULT FALSE,
    ADD COLUMN IF NOT EXISTS notice_key text;

DO $migration$
BEGIN
    IF NOT EXISTS (
        SELECT 1 FROM pg_constraint
        WHERE conrelid = 'system_notice'::regclass
          AND conname = 'system_notice_scope_check'
    ) THEN
        ALTER TABLE system_notice
            ADD CONSTRAINT system_notice_scope_check
            CHECK (scope IN ('global', 'sector', 'corp', 'player'));
    END IF;

    IF NOT EXISTS (
        SELECT 1 FROM pg_constraint
        WHERE conrelid = 'system_notice'::regclass
          AND conname = 'system_notice_sector_scope_check'
    ) THEN
        ALTER TABLE system_notice
            ADD CONSTRAINT system_notice_sector_scope_check
            CHECK ((scope = 'sector' AND sector_id IS NOT NULL)
                OR (scope <> 'sector' AND sector_id IS NULL));
    END IF;

    IF NOT EXISTS (
        SELECT 1 FROM pg_constraint
        WHERE conrelid = 'system_notice'::regclass
          AND conname = 'system_notice_sector_fk'
    ) THEN
        ALTER TABLE system_notice
            ADD CONSTRAINT system_notice_sector_fk
            FOREIGN KEY (sector_id) REFERENCES sectors (sector_id)
            ON DELETE RESTRICT;
    END IF;

    IF NOT EXISTS (
        SELECT 1 FROM pg_constraint
        WHERE conrelid = 'system_notice'::regclass
          AND conname = 'system_notice_player_fk'
    ) THEN
        ALTER TABLE system_notice
            ADD CONSTRAINT system_notice_player_fk
            FOREIGN KEY (player_id) REFERENCES players (player_id)
            ON DELETE SET NULL;
    END IF;
END
$migration$;

CREATE INDEX IF NOT EXISTS idx_system_notice_sector_expiry
    ON system_notice (sector_id, expires_at, created_at DESC)
    WHERE sector_id IS NOT NULL;

CREATE INDEX IF NOT EXISTS idx_system_notice_player_expiry
    ON system_notice (player_id, expires_at, created_at DESC)
    WHERE player_id IS NOT NULL;

CREATE UNIQUE INDEX IF NOT EXISTS ux_system_notice_notice_key
    ON system_notice (notice_key)
    WHERE notice_key IS NOT NULL;

CREATE TABLE IF NOT EXISTS trade_offers (
    trade_offer_id bigserial PRIMARY KEY,
    sender_player_id integer NOT NULL REFERENCES players (player_id) ON DELETE CASCADE,
    recipient_player_id integer NOT NULL REFERENCES players (player_id) ON DELETE CASCADE,
    commodity_code text NOT NULL REFERENCES commodities (code) ON DELETE RESTRICT,
    mode text NOT NULL CHECK (mode IN ('buy', 'sell')),
    quantity integer NOT NULL CHECK (quantity > 0),
    unit_price bigint NOT NULL CHECK (unit_price >= 0),
    status text NOT NULL DEFAULT 'pending'
        CHECK (status IN ('pending', 'accepted', 'cancelled', 'expired')),
    created_at timestamptz NOT NULL DEFAULT CURRENT_TIMESTAMP,
    expires_at timestamptz NOT NULL,
    accepted_at timestamptz,
    cancelled_at timestamptz,
    expired_at timestamptz,
    idempotency_key text,
    CHECK (sender_player_id <> recipient_player_id),
    CHECK (expires_at > created_at)
);

CREATE INDEX IF NOT EXISTS idx_trade_offers_recipient_pending_expiry
    ON trade_offers (recipient_player_id, expires_at)
    WHERE status = 'pending';

CREATE INDEX IF NOT EXISTS idx_trade_offers_sender_status_created
    ON trade_offers (sender_player_id, status, created_at DESC);

CREATE INDEX IF NOT EXISTS idx_trade_offers_pending_expiry
    ON trade_offers (expires_at)
    WHERE status = 'pending';

CREATE UNIQUE INDEX IF NOT EXISTS ux_trade_offers_sender_idempotency
    ON trade_offers (sender_player_id, idempotency_key)
    WHERE idempotency_key IS NOT NULL;

COMMIT;
