BEGIN;

CREATE TABLE IF NOT EXISTS toll_passage_offers (
    offer_id text PRIMARY KEY,
    request_id text NOT NULL,
    player_id integer NOT NULL REFERENCES players(player_id),
    ship_id integer NOT NULL REFERENCES ships(ship_id),
    from_sector_id integer NOT NULL REFERENCES sectors(sector_id),
    to_sector_id integer NOT NULL REFERENCES sectors(sector_id),
    owner_player_id integer NOT NULL REFERENCES players(player_id),
    fighter_count integer NOT NULL CHECK (fighter_count > 0),
    unit_rate integer NOT NULL CHECK (unit_rate >= 0),
    amount bigint NOT NULL CHECK (amount >= 0),
    state text NOT NULL CHECK (state IN ('pending','accepted','refused')),
    expires_at bigint NOT NULL,
    created_at timestamptz NOT NULL DEFAULT CURRENT_TIMESTAMP,
    resolved_at timestamptz,
    UNIQUE(player_id, request_id)
);

CREATE TABLE IF NOT EXISTS toll_passage_payees (
    offer_id text NOT NULL REFERENCES toll_passage_offers(offer_id) ON DELETE CASCADE,
    owner_player_id integer NOT NULL REFERENCES players(player_id),
    fighter_count integer NOT NULL CHECK (fighter_count > 0),
    amount bigint NOT NULL CHECK (amount >= 0),
    PRIMARY KEY (offer_id, owner_player_id)
);

CREATE INDEX IF NOT EXISTS toll_passage_offers_pending_expiry
    ON toll_passage_offers(expires_at) WHERE state='pending';

INSERT INTO config (key,value,type)
VALUES ('combat.toll_credits_per_fighter','1','int')
ON CONFLICT (key) DO NOTHING;

COMMIT;
