-- V2 economy and commerce contracts: special-port hardware, travelling
-- Ferengi traders, temporary market shocks, and daily colony taxes.
-- Additive PostgreSQL migration; no existing envelope or base price changes.

BEGIN;

CREATE TABLE IF NOT EXISTS port_hardware_stock (
    port_id integer NOT NULL REFERENCES ports (port_id) ON DELETE CASCADE,
    hardware_items_id integer NOT NULL REFERENCES hardware_items (hardware_items_id) ON DELETE CASCADE,
    stock_quantity integer NOT NULL DEFAULT 0 CHECK (stock_quantity >= 0),
    max_stock integer NOT NULL CHECK (max_stock BETWEEN 1 AND 100000000),
    restock_quantity integer NOT NULL DEFAULT 0 CHECK (restock_quantity >= 0),
    restock_interval_seconds integer NOT NULL DEFAULT 86400 CHECK (restock_interval_seconds > 0),
    next_restock_at timestamptz NOT NULL,
    updated_at timestamptz NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (port_id, hardware_items_id),
    CHECK (stock_quantity <= max_stock)
);
CREATE INDEX IF NOT EXISTS idx_port_hardware_stock_restock_due
    ON port_hardware_stock (next_restock_at)
    WHERE restock_quantity > 0;

CREATE TABLE IF NOT EXISTS ferengi_traders (
    ferengi_trader_id serial PRIMARY KEY,
    trader_code text NOT NULL UNIQUE,
    display_name text NOT NULL,
    corporation_id integer NOT NULL REFERENCES corporations (corporation_id) ON DELETE CASCADE,
    ship_id integer NOT NULL UNIQUE REFERENCES ships (ship_id) ON DELETE CASCADE,
    reputation integer NOT NULL DEFAULT 0 CHECK (reputation BETWEEN -10000 AND 10000),
    visit_number bigint NOT NULL DEFAULT 0 CHECK (visit_number >= 0),
    active boolean NOT NULL DEFAULT TRUE,
    last_interaction_at timestamptz,
    created_at timestamptz NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at timestamptz NOT NULL DEFAULT CURRENT_TIMESTAMP
);
CREATE INDEX IF NOT EXISTS idx_ferengi_traders_active
    ON ferengi_traders (ferengi_trader_id) WHERE active = TRUE;

CREATE TABLE IF NOT EXISTS ferengi_trader_deals (
    ferengi_trader_deal_id serial PRIMARY KEY,
    ferengi_trader_id integer NOT NULL REFERENCES ferengi_traders (ferengi_trader_id) ON DELETE CASCADE,
    player_id integer NOT NULL REFERENCES players (player_id) ON DELETE CASCADE,
    commodity_code text NOT NULL REFERENCES commodities (code),
    side text NOT NULL CHECK (side IN ('trader_buys', 'trader_sells')),
    quantity integer NOT NULL CHECK (quantity > 0),
    unit_price bigint NOT NULL CHECK (unit_price >= 0),
    status text NOT NULL DEFAULT 'open' CHECK (status IN ('open', 'settled', 'declined', 'expired', 'cancelled')),
    idempotency_key text NOT NULL,
    created_at timestamptz NOT NULL DEFAULT CURRENT_TIMESTAMP,
    expires_at timestamptz NOT NULL,
    settled_at timestamptz,
    UNIQUE (ferengi_trader_id, idempotency_key),
    CHECK (expires_at > created_at)
);
CREATE INDEX IF NOT EXISTS idx_ferengi_trader_deals_open_expiry
    ON ferengi_trader_deals (expires_at)
    WHERE status = 'open';
CREATE INDEX IF NOT EXISTS idx_ferengi_trader_deals_player
    ON ferengi_trader_deals (player_id, created_at DESC);

CREATE TABLE IF NOT EXISTS ferengi_trader_interactions (
    ferengi_trader_interaction_id bigserial PRIMARY KEY,
    ferengi_trader_id integer NOT NULL REFERENCES ferengi_traders (ferengi_trader_id) ON DELETE CASCADE,
    player_id integer REFERENCES players (player_id) ON DELETE SET NULL,
    deal_id integer REFERENCES ferengi_trader_deals (ferengi_trader_deal_id) ON DELETE SET NULL,
    interaction_type text NOT NULL CHECK (interaction_type IN ('encounter', 'offer', 'accepted', 'declined', 'expired')),
    reputation_delta integer NOT NULL DEFAULT 0 CHECK (reputation_delta BETWEEN -10000 AND 10000),
    idempotency_key text NOT NULL UNIQUE,
    occurred_at timestamptz NOT NULL DEFAULT CURRENT_TIMESTAMP
);
CREATE INDEX IF NOT EXISTS idx_ferengi_interactions_trader_time
    ON ferengi_trader_interactions (ferengi_trader_id, occurred_at DESC);
CREATE TABLE IF NOT EXISTS ferengi_player_relationships (
    ferengi_trader_id integer NOT NULL REFERENCES ferengi_traders (ferengi_trader_id) ON DELETE CASCADE,
    player_id integer NOT NULL REFERENCES players (player_id) ON DELETE CASCADE,
    reputation integer NOT NULL DEFAULT 0 CHECK (reputation BETWEEN -10000 AND 10000),
    encounters integer NOT NULL DEFAULT 0 CHECK (encounters >= 0),
    last_interaction_at timestamptz,
    PRIMARY KEY (ferengi_trader_id, player_id)
);
CREATE INDEX IF NOT EXISTS idx_ferengi_relationship_player
    ON ferengi_player_relationships (player_id, reputation DESC);

CREATE TABLE IF NOT EXISTS market_shocks (
    market_shock_id bigserial PRIMARY KEY,
    trigger_source text NOT NULL CHECK (trigger_source IN ('random', 'player', 'npc')),
    player_id integer REFERENCES players (player_id) ON DELETE SET NULL,
    ferengi_trader_id integer REFERENCES ferengi_traders (ferengi_trader_id) ON DELETE SET NULL,
    npc_code text,
    scope_type text NOT NULL CHECK (scope_type IN ('universe', 'sector', 'port')),
    sector_id integer REFERENCES sectors (sector_id) ON DELETE CASCADE,
    port_id integer REFERENCES ports (port_id) ON DELETE CASCADE,
    commodity_code text REFERENCES commodities (code) ON DELETE CASCADE,
    multiplier_bps integer NOT NULL CHECK (multiplier_bps BETWEEN 2500 AND 40000),
    starts_at timestamptz NOT NULL DEFAULT CURRENT_TIMESTAMP,
    expires_at timestamptz NOT NULL,
    idempotency_key text NOT NULL UNIQUE,
    created_at timestamptz NOT NULL DEFAULT CURRENT_TIMESTAMP,
    CHECK (expires_at > starts_at),
    CHECK ((scope_type = 'universe' AND sector_id IS NULL AND port_id IS NULL)
        OR (scope_type = 'sector' AND sector_id IS NOT NULL AND port_id IS NULL)
        OR (scope_type = 'port' AND port_id IS NOT NULL AND sector_id IS NULL)),
    CHECK ((trigger_source = 'random' AND player_id IS NULL
            AND ferengi_trader_id IS NULL AND npc_code IS NULL)
        OR (trigger_source = 'player' AND player_id IS NOT NULL
            AND ferengi_trader_id IS NULL AND npc_code IS NULL)
        OR (trigger_source = 'npc' AND player_id IS NULL AND npc_code IS NOT NULL
            AND length(npc_code) BETWEEN 1 AND 64))
);
CREATE INDEX IF NOT EXISTS idx_market_shocks_active_scope
    ON market_shocks (starts_at, expires_at, scope_type, sector_id, port_id)
    WHERE expires_at > starts_at;
CREATE INDEX IF NOT EXISTS idx_market_shocks_expiry
    ON market_shocks (expires_at);
CREATE INDEX IF NOT EXISTS idx_market_shocks_commodity
    ON market_shocks (commodity_code, expires_at);

CREATE TABLE IF NOT EXISTS planet_tax_policies (
    planet_id integer PRIMARY KEY REFERENCES planets (planet_id) ON DELETE CASCADE,
    rate_bps integer NOT NULL DEFAULT 0 CHECK (rate_bps BETWEEN 0 AND 10000),
    enabled boolean NOT NULL DEFAULT FALSE,
    updated_at timestamptz NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE IF NOT EXISTS planet_economic_activity (
    planet_economic_activity_id bigserial PRIMARY KEY,
    planet_id integer NOT NULL REFERENCES planets (planet_id) ON DELETE CASCADE,
    activity_type text NOT NULL CHECK (activity_type IN ('production', 'market_trade')),
    commodity_code text NOT NULL REFERENCES commodities (code),
    quantity bigint NOT NULL CHECK (quantity > 0),
    unit_value bigint NOT NULL CHECK (unit_value >= 0),
    taxable_value bigint NOT NULL CHECK (taxable_value >= 0),
    idempotency_key text NOT NULL UNIQUE,
    occurred_at timestamptz NOT NULL DEFAULT CURRENT_TIMESTAMP,
    assessed_at timestamptz
);
CREATE INDEX IF NOT EXISTS idx_planet_activity_unassessed
    ON planet_economic_activity (planet_id, occurred_at)
    WHERE assessed_at IS NULL;

CREATE TABLE IF NOT EXISTS planet_tax_assessments (
    planet_id integer NOT NULL REFERENCES planets (planet_id) ON DELETE CASCADE,
    tax_date date NOT NULL,
    owner_type text NOT NULL CHECK (owner_type IN ('player', 'corp')),
    owner_id integer NOT NULL,
    taxable_value bigint NOT NULL CHECK (taxable_value >= 0),
    rate_bps integer NOT NULL CHECK (rate_bps BETWEEN 0 AND 10000),
    tax_amount bigint NOT NULL CHECK (tax_amount >= 0),
    assessed_at timestamptz NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (planet_id, tax_date)
);
CREATE INDEX IF NOT EXISTS idx_planet_tax_owner_date
    ON planet_tax_assessments (owner_type, owner_id, tax_date DESC);

INSERT INTO cron_tasks (name, schedule, last_run_at, next_due_at, enabled, payload)
VALUES
    ('port_hardware_restock', 'daily@04:15Z', NULL, CURRENT_TIMESTAMP, TRUE, NULL),
    ('market_shock_tick', 'every:5m', NULL, CURRENT_TIMESTAMP, TRUE, NULL),
    ('daily_planet_tax', 'daily@05:15Z', NULL, CURRENT_TIMESTAMP, TRUE, NULL)
ON CONFLICT (name) DO NOTHING;

COMMIT;
