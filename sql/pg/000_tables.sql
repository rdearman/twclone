-- Generated from sqlite_schema.sql -> Postgres
CREATE TABLE config (
    key TEXT PRIMARY KEY,
    value text NOT NULL,
    type TEXT NOT NULL CHECK (type IN ('int', 'bool', 'string', 'double'))
);

CREATE TABLE sessions (
    token text PRIMARY KEY,
    player_id bigint NOT NULL,
    expires bigint NOT NULL,
    created_at timestamptz NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE idempotency (
    key TEXT PRIMARY KEY,
    cmd text NOT NULL,
    req_fp text NOT NULL,
    response text,
    created_at timestamptz NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at timestamptz NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE locks (
    lock_name text PRIMARY KEY,
    owner TEXT,
    until_ms bigint
);

CREATE TABLE engine_state (
    state_key text PRIMARY KEY,
    state_val text NOT NULL
);

CREATE TABLE sectors (
    sector_id bigserial PRIMARY KEY,
    name text,
    beacon text,
    nebulae text
);

CREATE TABLE commission (
    commission_id bigserial PRIMARY KEY,
    is_evil boolean NOT NULL DEFAULT FALSE CHECK (is_evil IN (TRUE, FALSE)),
    min_exp bigint NOT NULL,
    description text NOT NULL
);

CREATE TABLE shiptypes (
    shiptypes_id bigserial PRIMARY KEY,
    name text NOT NULL UNIQUE,
    basecost bigint,
    required_alignment bigint,
    required_commission bigint,
    required_experience bigint,
    maxattack bigint,
    initialholds bigint,
    maxholds bigint,
    maxfighters bigint,
    turns bigint,
    maxmines bigint,
    maxlimpets bigint,
    maxgenesis bigint,
    max_detonators bigint NOT NULL DEFAULT 0,
    max_probes bigint NOT NULL DEFAULT 0,
    can_transwarp boolean DEFAULT FALSE,
    transportrange bigint,
    maxshields bigint,
    offense bigint,
    defense bigint,
    maxbeacons bigint,
    can_long_range_scan boolean DEFAULT FALSE,
    can_planet_scan boolean DEFAULT FALSE,
    maxphotons bigint,
    max_cloaks bigint NOT NULL DEFAULT 0,
    can_purchase boolean DEFAULT TRUE,
    has_escape_pod boolean NOT NULL DEFAULT FALSE,
    enabled boolean DEFAULT TRUE,
    FOREIGN KEY (required_commission) REFERENCES commission (commission_id)
);

CREATE TABLE ships (
    ship_id bigserial PRIMARY KEY,
    name text NOT NULL,
    type_id bigint,
    attack bigint,
    holds bigint DEFAULT 1,
    mines bigint,
    limpets bigint,
    fighters bigint DEFAULT 1,
    genesis bigint,
    detonators bigint NOT NULL DEFAULT 0,
    probes bigint NOT NULL DEFAULT 0,
    photons bigint,
    sector_id bigint,
    shields bigint DEFAULT 1,
    installed_shields bigint DEFAULT 1,
    beacons bigint,
    colonists bigint,
    equipment bigint,
    organics bigint,
    ore bigint,
    slaves bigint DEFAULT 0,
    weapons bigint DEFAULT 0,
    drugs bigint DEFAULT 0,
    flags bigint,
    cloaking_devices bigint,
    has_transwarp bigint NOT NULL DEFAULT 0,
    has_planet_scanner bigint NOT NULL DEFAULT 0,
    has_long_range_scanner bigint NOT NULL DEFAULT 0,
    cloaked timestamp,
    ported bigint,
    onplanet bigint,
    destroyed bigint DEFAULT 0,
    hull bigint NOT NULL DEFAULT 100,
    perms bigint NOT NULL DEFAULT 731,
    CONSTRAINT check_current_cargo_limit CHECK ((colonists + equipment + organics + ore) <= holds),
    FOREIGN KEY (type_id) REFERENCES shiptypes (shiptypes_id),
    FOREIGN KEY (sector_id) REFERENCES sectors (sector_id)
);

CREATE TABLE players (
    player_id bigserial PRIMARY KEY,
    type BIGINT DEFAULT 2,
    number bigint,
    name text NOT NULL,
    passwd text NOT NULL,
    sector_id bigint DEFAULT 1,
    ship_id bigint,
    experience bigint DEFAULT 0,
    alignment bigint DEFAULT 0,
    commission_id bigint DEFAULT 1,
    credits bigint DEFAULT 1500,
    flags bigint,
    login_time timestamptz NOT NULL DEFAULT CURRENT_TIMESTAMP,
    last_update timestamptz NOT NULL DEFAULT CURRENT_TIMESTAMP,
    times_blown_up bigint NOT NULL DEFAULT 0,
    intransit boolean,
    beginmove bigint,
    movingto bigint,
    loggedin timestamptz NOT NULL DEFAULT CURRENT_TIMESTAMP,
    lastplanet bigint,
    score bigint,
    is_npc boolean DEFAULT FALSE,
    last_news_read_timestamp timestamptz NOT NULL DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY (commission_id) REFERENCES commission (commission_id),
    FOREIGN KEY (sector_id) REFERENCES sectors (sector_id),
    FOREIGN KEY (ship_id) REFERENCES ships (ship_id)
);

CREATE TABLE planettypes (
    planettypes_id bigserial PRIMARY KEY,
    code text UNIQUE,
    typeDescription text,
    typeName text,
    citadelUpgradeTime_lvl1 bigint,
    citadelUpgradeTime_lvl2 bigint,
    citadelUpgradeTime_lvl3 bigint,
    citadelUpgradeTime_lvl4 bigint,
    citadelUpgradeTime_lvl5 bigint,
    citadelUpgradeTime_lvl6 bigint,
    citadelUpgradeOre_lvl1 bigint,
    citadelUpgradeOre_lvl2 bigint,
    citadelUpgradeOre_lvl3 bigint,
    citadelUpgradeOre_lvl4 bigint,
    citadelUpgradeOre_lvl5 bigint,
    citadelUpgradeOre_lvl6 bigint,
    citadelUpgradeOrganics_lvl1 bigint,
    citadelUpgradeOrganics_lvl2 bigint,
    citadelUpgradeOrganics_lvl3 bigint,
    citadelUpgradeOrganics_lvl4 bigint,
    citadelUpgradeOrganics_lvl5 bigint,
    citadelUpgradeOrganics_lvl6 bigint,
    citadelUpgradeEquipment_lvl1 bigint,
    citadelUpgradeEquipment_lvl2 bigint,
    citadelUpgradeEquipment_lvl3 bigint,
    citadelUpgradeEquipment_lvl4 bigint,
    citadelUpgradeEquipment_lvl5 bigint,
    citadelUpgradeEquipment_lvl6 bigint,
    citadelUpgradeColonist_lvl1 bigint,
    citadelUpgradeColonist_lvl2 bigint,
    citadelUpgradeColonist_lvl3 bigint,
    citadelUpgradeColonist_lvl4 bigint,
    citadelUpgradeColonist_lvl5 bigint,
    citadelUpgradeColonist_lvl6 bigint,
    maxColonist_ore bigint,
    maxColonist_organics bigint,
    maxColonist_equipment bigint,
    fighters bigint,
    fuelProduction bigint,
    organicsProduction bigint,
    equipmentProduction bigint,
    fighterProduction bigint,
    maxore bigint,
    maxorganics bigint,
    maxequipment bigint,
    maxfighters bigint,
    breeding double precision,
    genesis_weight bigint NOT NULL DEFAULT 10
);

CREATE TABLE player_types (
    type BIGSERIAL PRIMARY KEY,
    description text
);

CREATE TABLE corporations (
    corporation_id bigserial PRIMARY KEY,
    name text NOT NULL,
    owner_id bigint,
    tag text,
    description text,
    tax_arrears bigint NOT NULL DEFAULT 0,
    credit_rating bigint NOT NULL DEFAULT 0,
    created_at timestamptz NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at timestamptz NOT NULL DEFAULT CURRENT_TIMESTAMP,
    CHECK (tag IS NULL OR (length(tag) BETWEEN 2 AND 5 AND tag ~ '^[A-Za-z0-9].*$'))
);

CREATE TABLE corp_members (
    corporation_id bigint NOT NULL,
    player_id bigint NOT NULL,
    role TEXT NOT NULL DEFAULT 'Member',
    join_date timestamptz NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (corporation_id, player_id),
    FOREIGN KEY (corporation_id) REFERENCES corporations (corporation_id) ON DELETE CASCADE ON UPDATE CASCADE,
    FOREIGN KEY (player_id) REFERENCES players (player_id),
    CHECK (ROLE IN ('Leader', 'Officer', 'Member'))
);

CREATE TABLE corp_mail (
    corp_mail_id bigserial PRIMARY KEY,
    corporation_id bigint NOT NULL,
    sender_id bigint,
    subject text,
    body text NOT NULL,
    posted_at timestamptz NOT NULL DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY (corporation_id) REFERENCES corporations (corporation_id) ON DELETE CASCADE,
    FOREIGN KEY (sender_id) REFERENCES players (player_id) ON DELETE SET NULL
);

CREATE TABLE corp_mail_cursors (
    corporation_id bigint NOT NULL,
    player_id bigint NOT NULL,
    last_seen_id bigint NOT NULL DEFAULT 0,
    PRIMARY KEY (corporation_id, player_id),
    FOREIGN KEY (corporation_id) REFERENCES corporations (corporation_id) ON DELETE CASCADE,
    FOREIGN KEY (player_id) REFERENCES players (player_id) ON DELETE CASCADE
);

CREATE TABLE corp_log (
    id bigserial PRIMARY KEY,
    corporation_id bigint NOT NULL,
    actor_id bigint,
    event_type text NOT NULL,
    payload text NOT NULL,
    created_at timestamptz NOT NULL DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY (corporation_id) REFERENCES corporations (corporation_id) ON DELETE CASCADE,
    FOREIGN KEY (actor_id) REFERENCES players (player_id) ON DELETE SET NULL
);

CREATE TABLE economy_curve (
    economy_curve_id bigserial PRIMARY KEY,
    curve_name text NOT NULL UNIQUE,
    base_restock_rate double precision NOT NULL,
    price_elasticity double precision NOT NULL,
    target_stock bigint NOT NULL,
    volatility_factor double precision NOT NULL
);

CREATE TABLE alignment_band (
    alignment_band_id bigserial PRIMARY KEY,
    code text NOT NULL UNIQUE,
    name text NOT NULL,
    min_align bigint NOT NULL,
    max_align bigint NOT NULL,
    is_good boolean NOT NULL DEFAULT TRUE,
    is_evil boolean NOT NULL DEFAULT FALSE,
    can_buy_iss boolean NOT NULL DEFAULT TRUE,
    can_rob_ports boolean NOT NULL DEFAULT FALSE,
    notes text
);

CREATE TABLE trade_idempotency (
    key TEXT PRIMARY KEY,
    player_id bigint NOT NULL,
    sector_id bigint NOT NULL,
    request_json text NOT NULL,
    response_json text NOT NULL,
    created_at timestamptz NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE used_sectors (
    used bigint
);

CREATE TABLE npc_shipnames (
    npc_shipnames_id bigint,
    name text
);

CREATE TABLE tavern_names (
    tavern_names_id bigserial PRIMARY KEY,
    name text NOT NULL UNIQUE,
    enabled boolean DEFAULT TRUE,
    weight bigint NOT NULL DEFAULT 1
);

CREATE TABLE taverns (
    sector_id bigserial PRIMARY KEY REFERENCES sectors (sector_id),
    name_id bigint NOT NULL REFERENCES tavern_names (tavern_names_id),
    enabled bigint NOT NULL DEFAULT 1
);

CREATE TABLE tavern_settings (
    tavern_settings_id bigserial PRIMARY KEY CHECK (tavern_settings_id = 1),
    max_bet_per_transaction bigint NOT NULL DEFAULT 5000,
    daily_max_wager bigint NOT NULL DEFAULT 50000,
    enable_dynamic_wager_limit bigint NOT NULL DEFAULT 0,
    graffiti_max_posts bigint NOT NULL DEFAULT 100,
    notice_expires_days bigint NOT NULL DEFAULT 7,
    buy_round_cost bigint NOT NULL DEFAULT 1000,
    buy_round_alignment_gain bigint NOT NULL DEFAULT 5,
    loan_shark_enabled bigint NOT NULL DEFAULT 1
);

CREATE TABLE tavern_lottery_state (
    draw_date text PRIMARY KEY,
    winning_number bigint,
    jackpot bigint NOT NULL,
    carried_over bigint NOT NULL DEFAULT 0
);

CREATE TABLE tavern_lottery_tickets (
    tavern_lottery_tickets_id bigserial PRIMARY KEY,
    draw_date text NOT NULL,
    player_id bigint NOT NULL REFERENCES players (player_id),
    number bigint NOT NULL,
    cost BIGINT NOT NULL,
    purchased_at timestamptz NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE tavern_deadpool_bets (
    tavern_deadpool_bets_id bigserial PRIMARY KEY,
    bettor_id bigint NOT NULL REFERENCES players (player_id),
    target_id bigint NOT NULL REFERENCES players (player_id),
    amount bigint NOT NULL,
    odds_bp bigint NOT NULL,
    placed_at timestamptz NOT NULL DEFAULT CURRENT_TIMESTAMP,
    expires_at timestamptz NOT NULL DEFAULT CURRENT_TIMESTAMP,
    resolved bigint NOT NULL DEFAULT 0,
    resolved_at timestamptz,
    result text
);

CREATE TABLE tavern_raffle_state (
    tavern_raffle_state_id bigserial PRIMARY KEY CHECK (tavern_raffle_state_id = 1),
    pot bigint NOT NULL,
    last_winner_id bigint,
    last_payout bigint,
    last_win_ts bigint
);

CREATE TABLE tavern_graffiti (
    tavern_graffiti_id bigserial PRIMARY KEY,
    player_id bigint NOT NULL REFERENCES players (player_id),
    text text NOT NULL,
    created_at timestamptz NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE tavern_notices (
    tavern_notices_id bigserial PRIMARY KEY,
    author_id bigint NOT NULL REFERENCES players (player_id),
    corp_id bigint,
    text text NOT NULL,
    created_at timestamptz NOT NULL DEFAULT CURRENT_TIMESTAMP,
    expires_at timestamptz NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE corp_recruiting (
    corp_id bigserial PRIMARY KEY REFERENCES corporations (corporation_id),
    tagline text NOT NULL,
    min_alignment bigint,
    play_style text,
    created_at timestamptz NOT NULL DEFAULT CURRENT_TIMESTAMP,
    expires_at timestamptz NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE corp_invites (
    corp_invites_id bigserial PRIMARY KEY,
    corp_id bigint NOT NULL,
    player_id bigint NOT NULL,
    invited_at timestamptz NOT NULL DEFAULT CURRENT_TIMESTAMP,
    expires_at timestamptz NOT NULL DEFAULT CURRENT_TIMESTAMP,
    UNIQUE (corp_id, player_id),
    FOREIGN KEY (corp_id) REFERENCES corporations (corporation_id) ON DELETE CASCADE,
    FOREIGN KEY (player_id) REFERENCES players (player_id) ON DELETE CASCADE
);

CREATE TABLE tavern_loans (
    player_id bigserial PRIMARY KEY REFERENCES players (player_id),
    principal bigint NOT NULL,
    interest_rate bigint NOT NULL,
    due_date bigint NOT NULL,
    is_defaulted bigint NOT NULL DEFAULT 0
);

CREATE TABLE ports (
    port_id bigserial PRIMARY KEY,
    number bigint,
    name text NOT NULL,
    sector_id bigint NOT NULL,
    size bigint,
    techlevel bigint,
    petty_cash bigint NOT NULL DEFAULT 0,
    invisible boolean DEFAULT FALSE,
    type BIGINT DEFAULT 1,
    economy_curve_id bigint NOT NULL DEFAULT 1,
    FOREIGN KEY (economy_curve_id) REFERENCES economy_curve (economy_curve_id),
    FOREIGN KEY (sector_id) REFERENCES sectors (sector_id)
);

CREATE TABLE port_trade (
    port_trade_id bigserial PRIMARY KEY,
    port_id bigint NOT NULL,
    maxproduct bigint,
    commodity text CHECK (commodity IN ('ore', 'organics', 'equipment')),
    mode text CHECK (mode IN ('buy', 'sell')),
    FOREIGN KEY (port_id) REFERENCES ports (port_id)
);

CREATE TABLE sector_warps (
    from_sector bigint,
    to_sector bigint,
    PRIMARY KEY (from_sector, to_sector),
    FOREIGN KEY (from_sector) REFERENCES sectors (sector_id) ON DELETE CASCADE,
    FOREIGN KEY (to_sector) REFERENCES sectors (sector_id) ON DELETE CASCADE
);

CREATE TABLE ship_markers (
    ship_id bigint NOT NULL REFERENCES ships (ship_id),
    owner_player bigint NOT NULL,
    owner_corp bigint NOT NULL DEFAULT 0,
    marker_type text NOT NULL,
    PRIMARY KEY (ship_id, owner_player, marker_type)
);

CREATE TABLE ship_roles (
    role_id bigserial PRIMARY KEY,
    role TEXT,
    role_description text
);

CREATE TABLE ship_ownership (
    ship_id bigint NOT NULL,
    player_id bigint NOT NULL,
    role_id bigint NOT NULL,
    is_primary boolean DEFAULT TRUE,
    acquired_at timestamptz NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (ship_id, player_id, role_id),
    FOREIGN KEY (ship_id) REFERENCES ships (ship_id),
    FOREIGN KEY (player_id) REFERENCES players (player_id)
);

CREATE TABLE planets (
    planet_id bigserial PRIMARY KEY,
    num bigint,
    sector_id bigint NOT NULL,
    name text NOT NULL,
    owner_id bigint NOT NULL,
    owner_type text NOT NULL DEFAULT 'player',
    class text NOT NULL DEFAULT 'M',
    population bigint,
    type BIGINT,
    creator text,
    colonist bigint,
    fighters bigint,
    created_at timestamptz NOT NULL DEFAULT CURRENT_TIMESTAMP,
    created_by bigint NOT NULL,
    genesis_flag boolean,
    citadel_level bigint DEFAULT 0,
    ore_on_hand bigint NOT NULL DEFAULT 0,
    organics_on_hand bigint NOT NULL DEFAULT 0,
    equipment_on_hand bigint NOT NULL DEFAULT 0,
    colonists_ore bigint NOT NULL DEFAULT 0,
    colonists_org bigint NOT NULL DEFAULT 0,
    colonists_eq bigint NOT NULL DEFAULT 0,
    colonists_mil bigint NOT NULL DEFAULT 0,
    colonists_unassigned bigint NOT NULL DEFAULT 0,
    terraform_turns_left bigint NOT NULL DEFAULT 1,
    FOREIGN KEY (sector_id) REFERENCES sectors (sector_id),
    FOREIGN KEY (type) REFERENCES planettypes (planettypes_id)
);

------------------ *******************************************
CREATE TABLE citadel_requirements (
    planet_type_id bigint NOT NULL REFERENCES planettypes (planettypes_id) ON DELETE CASCADE,
    citadel_level bigint NOT NULL,
    ore_cost bigint NOT NULL DEFAULT 0,
    organics_cost bigint NOT NULL DEFAULT 0,
    equipment_cost bigint NOT NULL DEFAULT 0,
    colonist_cost bigint NOT NULL DEFAULT 0,
    time_cost_days bigint NOT NULL DEFAULT 0,
    PRIMARY KEY (planet_type_id, citadel_level)
);

CREATE TABLE hardware_items (
    hardware_items_id bigserial PRIMARY KEY,
    code text UNIQUE NOT NULL,
    name text NOT NULL,
    price bigint NOT NULL,
    requires_stardock boolean DEFAULT TRUE,
    sold_in_class0 boolean DEFAULT TRUE,
    max_per_ship bigint,
    category text NOT NULL,
    enabled boolean DEFAULT FALSE
);

CREATE TABLE citadels (
    citadel_id bigserial PRIMARY KEY,
    planet_id bigint UNIQUE NOT NULL,
    level BIGINT,
    treasury bigint,
    militaryReactionLevel bigint,
    qCannonAtmosphere bigint,
    qCannonSector bigint,
    planetaryShields bigint,
    transporterlvl bigint,
    interdictor bigint,
    upgradePercent double precision,
    upgradestart bigint,
    owner_id bigint,
    shields bigint,
    torps bigint,
    fighters bigint,
    qtorps bigint,
    qcannon bigint,
    qcannontype bigint,
    qtorpstype bigint,
    military bigint,
    construction_start_time bigint DEFAULT 0,
    construction_end_time bigint DEFAULT 0,
    target_level bigint DEFAULT 0,
    construction_status text DEFAULT 'idle',
    FOREIGN KEY (planet_id) REFERENCES planets (planet_id) ON DELETE CASCADE,
    FOREIGN KEY (owner_id) REFERENCES players (player_id)
);

CREATE TABLE turns (
    player_id bigint NOT NULL,
    turns_remaining bigint NOT NULL,
    last_update timestamp NOT NULL,
    PRIMARY KEY (player_id),
    FOREIGN KEY (player_id) REFERENCES players (player_id) ON DELETE CASCADE
);

CREATE TABLE mail (
    mail_id bigserial PRIMARY KEY,
    thread_id bigint,
    sender_id bigint NOT NULL,
    recipient_id bigint NOT NULL,
    subject text,
    body text NOT NULL,
    sent_at timestamptz NOT NULL DEFAULT CURRENT_TIMESTAMP,
    read_at timestamptz NOT NULL DEFAULT CURRENT_TIMESTAMP,
    archived timestamptz NOT NULL DEFAULT CURRENT_TIMESTAMP,
    deleted timestamptz NOT NULL DEFAULT CURRENT_TIMESTAMP,
    idempotency_key text,
    FOREIGN KEY (sender_id) REFERENCES players (player_id) ON DELETE CASCADE,
    FOREIGN KEY (recipient_id) REFERENCES players (player_id) ON DELETE CASCADE
);

CREATE TABLE subspace (
    subspace_id bigserial PRIMARY KEY,
    sender_id bigint,
    message text NOT NULL,
    kind text NOT NULL DEFAULT 'chat',
    posted_at timestamptz NOT NULL DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY (sender_id) REFERENCES players (player_id) ON DELETE SET NULL
);

CREATE TABLE subspace_cursors (
    player_id bigserial PRIMARY KEY,
    last_seen_id bigint NOT NULL DEFAULT 0,
    FOREIGN KEY (player_id) REFERENCES players (player_id) ON DELETE CASCADE
);

CREATE TABLE system_events (
    system_events_id bigserial PRIMARY KEY,
    scope text NOT NULL,
    event_type text NOT NULL,
    payload text NOT NULL,
    created_at timestamptz NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE subscriptions (
    subscriptions_id bigserial PRIMARY KEY,
    player_id bigint NOT NULL,
    event_type text NOT NULL,
    delivery text NOT NULL,
    filter_json text,
    ephemeral bigint NOT NULL DEFAULT 0,
    locked BIGINT NOT NULL DEFAULT 0,
    enabled bigint NOT NULL DEFAULT 1,
    UNIQUE (player_id, event_type),
    FOREIGN KEY (player_id) REFERENCES players (player_id) ON DELETE CASCADE
);

CREATE TABLE player_block (
    blocker_id bigint NOT NULL,
    blocked_id bigint NOT NULL,
    created_at timestamptz NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (blocker_id, blocked_id)
);

CREATE TABLE notice_seen (
    notice_id bigint NOT NULL,
    player_id bigint NOT NULL,
    seen_at timestamptz NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (notice_id, player_id)
);

CREATE TABLE system_notice (
    system_notice_id bigserial PRIMARY KEY,
    created_at timestamptz NOT NULL DEFAULT CURRENT_TIMESTAMP,
    title text NOT NULL,
    body text NOT NULL,
    severity text NOT NULL CHECK (severity IN ('info', 'warn', 'error')),
    expires_at timestamptz
);

CREATE TABLE player_prefs (
    player_prefs_id bigint NOT NULL,
    key TEXT NOT NULL,
    type TEXT NOT NULL CHECK (type IN ('bool', 'int', 'string', 'json')),
    value text NOT NULL,
    updated_at timestamptz NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE player_bookmarks (
    player_bookmarks_id bigserial PRIMARY KEY,
    player_id bigint NOT NULL,
    name text NOT NULL,
    sector_id bigint NOT NULL,
    updated_at timestamptz NOT NULL DEFAULT CURRENT_TIMESTAMP,
    UNIQUE (player_id, name),
    FOREIGN KEY (player_id) REFERENCES players (player_id) ON DELETE CASCADE,
    FOREIGN KEY (sector_id) REFERENCES sectors (sector_id) ON DELETE CASCADE
);

CREATE TABLE player_avoid (
    player_id bigint NOT NULL,
    sector_id bigint NOT NULL,
    updated_at timestamptz NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (player_id, sector_id),
    FOREIGN KEY (player_id) REFERENCES players (player_id) ON DELETE CASCADE,
    FOREIGN KEY (sector_id) REFERENCES sectors (sector_id) ON DELETE CASCADE
);

CREATE TABLE player_notes (
    player_notes_id bigserial PRIMARY KEY,
    player_id bigint NOT NULL,
    scope text NOT NULL,
    key TEXT NOT NULL,
    note text NOT NULL,
    updated_at timestamptz NOT NULL DEFAULT CURRENT_TIMESTAMP,
    UNIQUE (player_id, scope, key),
    FOREIGN KEY (player_id) REFERENCES players (player_id) ON DELETE CASCADE
);

CREATE TABLE sector_assets (
    sector_assets_id bigserial PRIMARY KEY,
    sector_id bigint NOT NULL REFERENCES sectors (sector_id),
    owner_id bigint REFERENCES players (player_id),
    corporation_id bigint NOT NULL DEFAULT 0,
    asset_type bigint NOT NULL,
    offensive_setting bigint DEFAULT 0,
    quantity bigint,
    ttl bigint,
    deployed_at timestamptz NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE limpet_attached (
    limpet_attached_id bigserial PRIMARY KEY,
    ship_id bigint NOT NULL,
    owner_player_id bigint NOT NULL,
    created_ts bigint NOT NULL,
    UNIQUE (ship_id, owner_player_id),
    FOREIGN KEY (ship_id) REFERENCES ships (ship_id) ON DELETE CASCADE,
    FOREIGN KEY (owner_player_id) REFERENCES players (player_id) ON DELETE CASCADE
);

CREATE TABLE msl_sectors (
    sector_id bigserial PRIMARY KEY REFERENCES sectors (sector_id)
);

CREATE TABLE trade_log (
    trade_log_id bigserial PRIMARY KEY,
    player_id bigint NOT NULL,
    port_id bigint NOT NULL,
    sector_id bigint NOT NULL,
    commodity text NOT NULL,
    units bigint NOT NULL,
    price_per_unit double precision NOT NULL,
    action text CHECK (action IN ('buy', 'sell')) NOT NULL,
    timestamp bigint NOT NULL,
    FOREIGN KEY (player_id) REFERENCES players (player_id),
    FOREIGN KEY (port_id) REFERENCES ports (port_id),
    FOREIGN KEY (sector_id) REFERENCES sectors (sector_id)
);

CREATE TABLE stardock_assets (
    sector_id bigserial PRIMARY KEY,
    owner_id bigint NOT NULL,
    fighters bigint NOT NULL DEFAULT 0,
    defenses bigint NOT NULL DEFAULT 0,
    ship_capacity bigint NOT NULL DEFAULT 1,
    created_at timestamptz NOT NULL DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY (sector_id) REFERENCES sectors (sector_id),
    FOREIGN KEY (owner_id) REFERENCES players (player_id)
);

CREATE TABLE shipyard_inventory (
    port_id bigint NOT NULL REFERENCES ports (port_id),
    ship_type_id bigint NOT NULL REFERENCES shiptypes (shiptypes_id),
    enabled bigint NOT NULL DEFAULT 1,
    PRIMARY KEY (port_id, ship_type_id)
);

CREATE TABLE podded_status (
    player_id bigserial PRIMARY KEY REFERENCES players (player_id),
    status text NOT NULL DEFAULT 'active',
    big_sleep_until bigint,
    reason text,
    podded_count_today bigint NOT NULL DEFAULT 0,
    podded_last_reset bigint
);

CREATE TABLE planet_goods (
    planet_id bigint NOT NULL,
    commodity text NOT NULL CHECK (commodity IN ('ore', 'organics', 'equipment', 'food', 'fuel')),
    quantity bigint NOT NULL DEFAULT 0,
    max_capacity bigint NOT NULL,
    production_rate bigint NOT NULL,
    PRIMARY KEY (planet_id, commodity),
    FOREIGN KEY (planet_id) REFERENCES planets (planet_id)
);

CREATE TABLE commodities (
    commodities_id bigserial PRIMARY KEY,
    code text UNIQUE NOT NULL,
    name text NOT NULL,
    illegal bigint NOT NULL DEFAULT 0,
    base_price bigint NOT NULL DEFAULT 0 CHECK (base_price >= 0),
    volatility bigint NOT NULL DEFAULT 0 CHECK (volatility >= 0)
);

CREATE TABLE clusters (
    clusters_id bigserial PRIMARY KEY,
    name text NOT NULL UNIQUE,
    role TEXT NOT NULL,
    kind text NOT NULL,
    center_sector bigint,
    law_severity bigint NOT NULL DEFAULT 1,
    alignment bigint NOT NULL DEFAULT 0,
    created_at timestamptz NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE cluster_sectors (
    cluster_id bigint NOT NULL,
    sector_id bigint NOT NULL,
    PRIMARY KEY (cluster_id, sector_id),
    FOREIGN KEY (cluster_id) REFERENCES clusters (clusters_id) ON DELETE CASCADE,
    FOREIGN KEY (sector_id) REFERENCES sectors (sector_id) ON DELETE CASCADE
);

CREATE TABLE cluster_commodity_index (
    cluster_id bigint NOT NULL,
    commodity_code text NOT NULL,
    mid_price bigint NOT NULL,
    last_updated text NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (cluster_id, commodity_code),
    FOREIGN KEY (cluster_id) REFERENCES clusters (clusters_id) ON DELETE CASCADE,
    FOREIGN KEY (commodity_code) REFERENCES commodities (code) ON DELETE CASCADE
);

CREATE TABLE cluster_player_status (
    cluster_id bigint NOT NULL,
    player_id bigint NOT NULL,
    suspicion bigint NOT NULL DEFAULT 0,
    bust_count bigint NOT NULL DEFAULT 0,
    last_bust_at text,
    wanted_level bigint NOT NULL DEFAULT 0,
    banned bigint NOT NULL DEFAULT 0,
    PRIMARY KEY (cluster_id, player_id),
    FOREIGN KEY (cluster_id) REFERENCES clusters (clusters_id),
    FOREIGN KEY (player_id) REFERENCES players (player_id)
);

CREATE TABLE law_enforcement (
    law_enforcement_id bigserial PRIMARY KEY CHECK (law_enforcement_id = 1),
    robbery_evil_threshold bigint DEFAULT -10,
    robbery_xp_per_hold bigint DEFAULT 20,
    robbery_credits_per_xp bigint DEFAULT 10,
    robbery_bust_chance_base double precision DEFAULT 0.05,
    robbery_turn_cost bigint DEFAULT 1,
    good_guy_bust_bonus double precision DEFAULT 0.10,
    pro_criminal_bust_delta double precision DEFAULT -0.02,
    evil_cluster_bust_bonus double precision DEFAULT 0.05,
    good_align_penalty_mult double precision DEFAULT 3.0,
    robbery_real_bust_ttl_days bigint DEFAULT 7
);

CREATE TABLE port_busts (
    port_id bigint NOT NULL,
    player_id bigint NOT NULL,
    last_bust_at bigint NOT NULL,
    bust_type text NOT NULL,
    active bigint NOT NULL DEFAULT 1,
    PRIMARY KEY (port_id, player_id),
    FOREIGN KEY (port_id) REFERENCES ports (port_id),
    FOREIGN KEY (player_id) REFERENCES players (player_id)
);

CREATE TABLE player_last_rob (
    player_id bigserial PRIMARY KEY,
    port_id bigint NOT NULL,
    last_attempt_at bigint NOT NULL,
    was_success bigint NOT NULL
);

CREATE TABLE currencies (
    code text PRIMARY KEY,
    name text NOT NULL,
    minor_unit bigint NOT NULL DEFAULT 1 CHECK (minor_unit > 0),
    is_default boolean NOT NULL DEFAULT FALSE
);

CREATE TABLE commodity_orders (
    commodity_orders_id bigserial PRIMARY KEY,
    actor_type text NOT NULL CHECK (actor_type IN ('player', 'corp', 'npc_planet', 'port')),
    actor_id bigint NOT NULL,
    location_type text NOT NULL CHECK (location_type IN ('planet', 'port')),
    location_id bigint NOT NULL,
    commodity_id bigint NOT NULL REFERENCES commodities (commodities_id) ON DELETE CASCADE,
    side text NOT NULL CHECK (side IN ('buy', 'sell')),
    quantity bigint NOT NULL CHECK (quantity > 0),
    filled_quantity bigint NOT NULL DEFAULT 0 CHECK (filled_quantity >= 0),
    price bigint NOT NULL CHECK (price >= 0),
    status text NOT NULL DEFAULT 'open' CHECK (status IN ('open', 'filled', 'cancelled', 'expired')),
    ts timestamptz NOT NULL DEFAULT CURRENT_TIMESTAMP,
    expires_at timestamptz
);

CREATE TABLE commodity_trades (
    id bigserial PRIMARY KEY,
    commodity_id bigint NOT NULL REFERENCES commodities (commodities_id) ON DELETE CASCADE,
    buyer_actor_type text NOT NULL CHECK (buyer_actor_type IN ('player', 'corp', 'npc_planet', 'port')),
    buyer_actor_id bigint NOT NULL,
    buyer_location_type text NOT NULL CHECK (buyer_location_type IN ('planet', 'port')),
    buyer_location_id bigint NOT NULL,
    seller_actor_type text NOT NULL CHECK (seller_actor_type IN ('player', 'corp', 'npc_planet', 'port')),
    seller_actor_id bigint NOT NULL,
    seller_location_type text NOT NULL CHECK (seller_location_type IN ('planet', 'port')),
    seller_location_id bigint NOT NULL,
    quantity bigint NOT NULL CHECK (quantity > 0),
    price bigint NOT NULL CHECK (price >= 0),
    ts timestamptz NOT NULL DEFAULT CURRENT_TIMESTAMP,
    settlement_tx_buy bigint,
    settlement_tx_sell bigint
);

CREATE TABLE entity_stock (
    entity_type text NOT NULL CHECK (entity_type IN ('port', 'planet')),
    entity_id bigint NOT NULL,
    commodity_code text NOT NULL,
    quantity bigint NOT NULL,
    price bigint NULL,
    last_updated_ts bigint NOT NULL DEFAULT (EXTRACT(EPOCH FROM now())::bigint),
    PRIMARY KEY (entity_type, entity_id, commodity_code),
    FOREIGN KEY (commodity_code) REFERENCES commodities (code)
);

CREATE TABLE planet_production (
    planet_type_id bigint NOT NULL,
    commodity_code text NOT NULL,
    base_prod_rate bigint NOT NULL,
    base_cons_rate bigint NOT NULL,
    PRIMARY KEY (planet_type_id, commodity_code),
    FOREIGN KEY (planet_type_id) REFERENCES planettypes (planettypes_id) ON DELETE CASCADE,
    FOREIGN KEY (commodity_code) REFERENCES commodities (code) ON DELETE CASCADE
);

CREATE TABLE traps (
    id bigserial PRIMARY KEY,
    sector_id bigint NOT NULL,
    owner_player_id bigint,
    kind text NOT NULL,
    armed bigint NOT NULL DEFAULT 0,
    arming_at timestamptz NOT NULL DEFAULT CURRENT_TIMESTAMP,
    expires_at timestamptz,
    trigger_at timestamptz,
    payload text
);

CREATE TABLE bank_accounts (
    id bigserial PRIMARY KEY,
    owner_type text NOT NULL,
    owner_id bigint NOT NULL,
    currency text NOT NULL DEFAULT 'CRD' REFERENCES currencies (code),
    balance bigint NOT NULL DEFAULT 0 CHECK (balance >= 0),
    interest_rate_bp bigint NOT NULL DEFAULT 0,
    last_interest_tick bigint,
    tx_alert_threshold bigint DEFAULT 0,
    is_active bigint NOT NULL DEFAULT 1
);

CREATE TABLE bank_transactions (
    id bigserial PRIMARY KEY,
    account_id bigint NOT NULL REFERENCES bank_accounts (id),
    tx_type text NOT NULL CHECK (tx_type IN ('DEPOSIT', 'WITHDRAWAL', 'TRANSFER', 'INTEREST', 'FEE', 'WIRE', 'TAX', 'TRADE_BUY', 'TRADE_SELL', 'TRADE_BUY_FEE', 'TRADE_SELL_FEE', 'WITHDRAWAL_FEE', 'ADJUSTMENT')),
    direction text NOT NULL CHECK (direction IN ('CREDIT', 'DEBIT')),
    amount bigint NOT NULL CHECK (amount > 0),
    currency text NOT NULL,
    tx_group_id text,
    related_account_id bigint,
    description text,
    ts bigint NOT NULL,
    balance_after bigint DEFAULT 0,
    idempotency_key text,
    engine_event_id bigint
);

CREATE TABLE bank_fee_schedules (
    id bigserial PRIMARY KEY,
    tx_type text NOT NULL,
    fee_code text NOT NULL,
    owner_type text,
    currency text NOT NULL DEFAULT 'CRD',
    value bigint NOT NULL,
    is_percentage bigint NOT NULL DEFAULT 0 CHECK (is_percentage IN (0, 1)),
    min_tx_amount bigint DEFAULT 0,
    max_tx_amount bigint,
    effective_from bigint NOT NULL,
    effective_to bigint
);

CREATE TABLE bank_interest_policy (
    id bigserial PRIMARY KEY CHECK (id = 1),
    apr_bps bigint NOT NULL DEFAULT 0 CHECK (apr_bps >= 0),
    min_balance bigint NOT NULL DEFAULT 0 CHECK (min_balance >= 0),
    max_balance bigint NOT NULL DEFAULT 9223372036854775807,
    last_run_at timestamptz,
    compounding text NOT NULL DEFAULT 'none',
    currency text NOT NULL DEFAULT 'CRD' REFERENCES currencies (code)
);

CREATE TABLE bank_orders (
    id bigserial PRIMARY KEY,
    player_id bigint NOT NULL REFERENCES players (player_id) ON DELETE CASCADE,
    kind text NOT NULL CHECK (kind IN ('recurring', 'once')),
    schedule text NOT NULL,
    next_run_at timestamptz,
    enabled bigint NOT NULL DEFAULT 1 CHECK (enabled IN (0, 1)),
    amount bigint NOT NULL CHECK (amount > 0),
    currency text NOT NULL DEFAULT 'CRD' REFERENCES currencies (code),
    to_entity text NOT NULL CHECK (to_entity IN ('player', 'corp', 'gov', 'npc')),
    to_id bigint NOT NULL,
    memo text
);

CREATE TABLE bank_flags (
    player_id bigserial PRIMARY KEY REFERENCES players (player_id) ON DELETE CASCADE,
    is_frozen bigint NOT NULL DEFAULT 0 CHECK (is_frozen IN (0, 1)),
    risk_tier text NOT NULL DEFAULT 'normal' CHECK (risk_tier IN ('normal', 'elevated', 'high', 'blocked'))
);

CREATE TABLE corp_accounts (
    corp_id bigserial PRIMARY KEY REFERENCES corporations (corporation_id) ON DELETE CASCADE,
    currency text NOT NULL DEFAULT 'CRD' REFERENCES currencies (code),
    balance bigint NOT NULL DEFAULT 0 CHECK (balance >= 0),
    last_interest_at timestamptz
);

CREATE TABLE corp_tx (
    id bigserial PRIMARY KEY,
    corp_id bigint NOT NULL REFERENCES corporations (corporation_id) ON DELETE CASCADE,
    ts timestamptz NOT NULL DEFAULT CURRENT_TIMESTAMP,
    kind text NOT NULL CHECK (kind IN ('deposit', 'withdraw', 'transfer_in', 'transfer_out', 'interest', 'dividend', 'salary', 'adjustment')),
    amount bigint NOT NULL CHECK (amount > 0),
    balance_after bigint,
    currency text NOT NULL DEFAULT 'CRD' REFERENCES currencies (code),
    memo text,
    idempotency_key text UNIQUE
);

CREATE TABLE corp_interest_policy (
    id bigserial PRIMARY KEY CHECK (id = 1),
    apr_bps bigint NOT NULL DEFAULT 0 CHECK (apr_bps >= 0),
    compounding text NOT NULL DEFAULT 'none' CHECK (compounding IN ('none', 'daily', 'weekly', 'monthly')),
    last_run_at timestamptz NOT NULL DEFAULT CURRENT_TIMESTAMP,
    currency text NOT NULL DEFAULT 'CRD' REFERENCES currencies (code)
);

CREATE TABLE stocks (
    id bigserial PRIMARY KEY,
    corp_id bigint NOT NULL REFERENCES corporations (corporation_id) ON DELETE CASCADE,
    ticker text NOT NULL UNIQUE,
    total_shares bigint NOT NULL CHECK (total_shares > 0),
    par_value bigint NOT NULL DEFAULT 0 CHECK (par_value >= 0),
    current_price bigint NOT NULL DEFAULT 0 CHECK (current_price >= 0),
    last_dividend_ts text
);

CREATE TABLE corp_shareholders (
    corp_id bigint NOT NULL REFERENCES corporations (corporation_id) ON DELETE CASCADE,
    player_id bigint NOT NULL REFERENCES players (player_id) ON DELETE CASCADE,
    shares bigint NOT NULL CHECK (shares >= 0),
    PRIMARY KEY (corp_id, player_id)
);

CREATE TABLE stock_orders (
    id bigserial PRIMARY KEY,
    player_id bigint NOT NULL REFERENCES players (player_id) ON DELETE CASCADE,
    stock_id bigint NOT NULL REFERENCES stocks (id) ON DELETE CASCADE,
    type TEXT NOT NULL CHECK (type IN ('buy', 'sell')),
    quantity bigint NOT NULL CHECK (quantity > 0),
    price bigint NOT NULL CHECK (price >= 0),
    status text NOT NULL DEFAULT 'open' CHECK (status IN ('open', 'filled', 'cancelled', 'expired')),
    ts timestamptz NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE stock_trades (
    id bigserial PRIMARY KEY,
    stock_id bigint NOT NULL REFERENCES stocks (id) ON DELETE CASCADE,
    buyer_id bigint NOT NULL REFERENCES players (player_id) ON DELETE CASCADE,
    seller_id bigint NOT NULL REFERENCES players (player_id) ON DELETE CASCADE,
    quantity bigint NOT NULL CHECK (quantity > 0),
    price bigint NOT NULL CHECK (price >= 0),
    ts timestamptz NOT NULL DEFAULT CURRENT_TIMESTAMP,
    settlement_tx_buy bigint,
    settlement_tx_sell bigint
);

CREATE TABLE stock_dividends (
    id bigserial PRIMARY KEY,
    stock_id bigint NOT NULL REFERENCES stocks (id) ON DELETE CASCADE,
    amount_per_share bigint NOT NULL CHECK (amount_per_share >= 0),
    declared_ts text NOT NULL,
    paid_ts text
);

CREATE TABLE stock_indices (
    id bigserial PRIMARY KEY,
    name text UNIQUE NOT NULL
);

CREATE TABLE stock_index_members (
    index_id bigint NOT NULL REFERENCES stock_indices (id) ON DELETE CASCADE,
    stock_id bigint NOT NULL REFERENCES stocks (id) ON DELETE CASCADE,
    weight double precision NOT NULL DEFAULT 1.0,
    PRIMARY KEY (index_id, stock_id)
);

CREATE TABLE insurance_funds (
    fund_id bigserial PRIMARY KEY,
    owner_type text NOT NULL CHECK (owner_type IN ('system', 'corp', 'player')),
    owner_id bigint,
    balance bigint NOT NULL DEFAULT 0 CHECK (balance >= 0)
);

CREATE TABLE insurance_policies (
    insurance_policies_id bigserial PRIMARY KEY,
    holder_type text NOT NULL CHECK (holder_type IN ('player', 'corp')),
    holder_id bigint NOT NULL,
    subject_type text NOT NULL CHECK (subject_type IN ('ship', 'cargo', 'planet')),
    subject_id bigint NOT NULL,
    premium bigint NOT NULL CHECK (premium >= 0),
    payout bigint NOT NULL CHECK (payout >= 0),
    fund_id bigint REFERENCES insurance_funds (fund_id) ON DELETE SET NULL,
    start_ts text NOT NULL,
    expiry_ts text,
    active bigint NOT NULL DEFAULT 1 CHECK (active IN (0, 1))
);

CREATE TABLE insurance_claims (
    insurance_claims_id bigserial PRIMARY KEY,
    policy_id bigint NOT NULL REFERENCES insurance_policies (insurance_policies_id) ON DELETE CASCADE,
    event_id text,
    amount bigint NOT NULL CHECK (amount >= 0),
    status text NOT NULL DEFAULT 'open' CHECK (status IN ('open', 'paid', 'denied')),
    ts timestamptz NOT NULL DEFAULT CURRENT_TIMESTAMP,
    paid_bank_tx bigint
);

CREATE TABLE risk_profiles (
    risk_profiles_id bigserial PRIMARY KEY,
    entity_type text NOT NULL CHECK (entity_type IN ('player', 'corp')),
    entity_id bigint NOT NULL,
    risk_score bigint NOT NULL DEFAULT 0
);

CREATE TABLE loans (
    loans_id bigserial PRIMARY KEY,
    lender_type text NOT NULL CHECK (lender_type IN ('player', 'corp', 'bank')),
    lender_id bigint,
    borrower_type text NOT NULL CHECK (borrower_type IN ('player', 'corp')),
    borrower_id bigint NOT NULL,
    principal bigint NOT NULL CHECK (principal > 0),
    rate_bps bigint NOT NULL DEFAULT 0 CHECK (rate_bps >= 0),
    term_days bigint NOT NULL CHECK (term_days > 0),
    next_due text,
    status text NOT NULL DEFAULT 'active' CHECK (status IN ('active', 'paid', 'defaulted', 'written_off')),
    created_ts timestamptz NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE loan_payments (
    loan_payments_id bigserial PRIMARY KEY,
    loan_id bigint NOT NULL REFERENCES loans (loans_id) ON DELETE CASCADE,
    ts timestamptz NOT NULL DEFAULT CURRENT_TIMESTAMP,
    amount bigint NOT NULL CHECK (amount > 0),
    status text NOT NULL DEFAULT 'posted' CHECK (status IN ('posted', 'reversed')),
    bank_tx_id bigint
);

CREATE TABLE collateral (
    collateral_id bigserial PRIMARY KEY,
    loan_id bigint NOT NULL REFERENCES loans (loans_id) ON DELETE CASCADE,
    asset_type text NOT NULL CHECK (asset_type IN ('ship', 'planet', 'cargo', 'stock', 'other')),
    asset_id bigint NOT NULL,
    appraised_value bigint NOT NULL DEFAULT 0 CHECK (appraised_value >= 0)
);

CREATE TABLE credit_ratings (
    entity_type text NOT NULL CHECK (entity_type IN ('player', 'corp')),
    entity_id bigint NOT NULL,
    score bigint NOT NULL DEFAULT 600 CHECK (score BETWEEN 300 AND 900),
    last_update text,
    PRIMARY KEY (entity_type, entity_id)
);

CREATE TABLE charters (
    charters_id bigserial PRIMARY KEY,
    name text NOT NULL UNIQUE,
    granted_by text NOT NULL DEFAULT 'federation',
    monopoly_scope text,
    start_ts text NOT NULL,
    expiry_ts text
);

CREATE TABLE expeditions (
    expeditions_id bigserial PRIMARY KEY,
    leader_player_id bigint NOT NULL REFERENCES players (player_id) ON DELETE CASCADE,
    charter_id bigint REFERENCES charters (charters_id) ON DELETE SET NULL,
    goal text NOT NULL,
    target_region text,
    pledged_total bigint NOT NULL DEFAULT 0 CHECK (pledged_total >= 0),
    duration_days bigint NOT NULL DEFAULT 7 CHECK (duration_days > 0),
    status text NOT NULL DEFAULT 'planning' CHECK (status IN ('planning', 'launched', 'complete', 'failed', 'aborted')),
    created_ts timestamptz NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE expedition_backers (
    expedition_id bigint NOT NULL REFERENCES expeditions (expeditions_id) ON DELETE CASCADE,
    backer_type text NOT NULL CHECK (backer_type IN ('player', 'corp')),
    backer_id bigint NOT NULL,
    pledged_amount bigint NOT NULL CHECK (pledged_amount >= 0),
    share_pct double precision NOT NULL CHECK (share_pct >= 0),
    PRIMARY KEY (expedition_id, backer_type, backer_id)
);

CREATE TABLE expedition_returns (
    expedition_returns_id bigserial PRIMARY KEY,
    expedition_id bigint NOT NULL REFERENCES expeditions (expeditions_id) ON DELETE CASCADE,
    ts timestamptz NOT NULL DEFAULT CURRENT_TIMESTAMP,
    amount bigint NOT NULL CHECK (amount >= 0),
    bank_tx_id bigint
);

CREATE TABLE futures_contracts (
    futures_contracts_id bigserial PRIMARY KEY,
    commodity_id bigint NOT NULL REFERENCES commodities (commodities_id) ON DELETE CASCADE,
    buyer_type text NOT NULL CHECK (buyer_type IN ('player', 'corp')),
    buyer_id bigint NOT NULL,
    seller_type text NOT NULL CHECK (seller_type IN ('player', 'corp')),
    seller_id bigint NOT NULL,
    strike_price bigint NOT NULL CHECK (strike_price >= 0),
    expiry_ts text NOT NULL,
    status text NOT NULL DEFAULT 'open' CHECK (status IN ('open', 'settled', 'defaulted', 'cancelled')),
    FOREIGN KEY (buyer_id) REFERENCES players (player_id) ON DELETE CASCADE,
    FOREIGN KEY (seller_id) REFERENCES players (player_id) ON DELETE CASCADE
);

CREATE TABLE warehouses (
    warehouses_id bigserial PRIMARY KEY,
    location_type text NOT NULL CHECK (location_type IN ('sector', 'planet', 'port')),
    location_id bigint NOT NULL,
    owner_type text NOT NULL CHECK (owner_type IN ('player', 'corp')),
    owner_id bigint NOT NULL
);

CREATE TABLE gov_accounts (
    gov_accounts_id bigserial PRIMARY KEY,
    name text NOT NULL UNIQUE,
    balance bigint NOT NULL DEFAULT 0 CHECK (balance >= 0)
);

CREATE TABLE tax_policies (
    tax_policies_id bigserial PRIMARY KEY,
    name text NOT NULL,
    tax_type text NOT NULL CHECK (tax_type IN ('trade', 'income', 'corp', 'wealth', 'transfer')),
    rate_bps bigint NOT NULL DEFAULT 0 CHECK (rate_bps >= 0),
    active bigint NOT NULL DEFAULT 1 CHECK (active IN (0, 1))
);

CREATE TABLE tax_ledgers (
    tax_ledgers_id bigserial PRIMARY KEY,
    policy_id bigint NOT NULL REFERENCES tax_policies (tax_policies_id) ON DELETE CASCADE,
    payer_type text NOT NULL CHECK (payer_type IN ('player', 'corp')),
    payer_id bigint NOT NULL,
    amount bigint NOT NULL CHECK (amount >= 0),
    ts timestamptz NOT NULL DEFAULT CURRENT_TIMESTAMP,
    bank_tx_id bigint
);

CREATE TABLE fines (
    fines_id bigserial PRIMARY KEY,
    issued_by text NOT NULL DEFAULT 'federation',
    recipient_type text NOT NULL CHECK (recipient_type IN ('player', 'corp')),
    recipient_id bigint NOT NULL,
    reason text,
    amount bigint NOT NULL CHECK (amount >= 0),
    status text NOT NULL DEFAULT 'unpaid' CHECK (status IN ('unpaid', 'paid', 'void')),
    issued_ts timestamptz NOT NULL DEFAULT CURRENT_TIMESTAMP,
    paid_bank_tx bigint
);

CREATE TABLE bounties (
    bounties_id bigserial PRIMARY KEY,
    posted_by_type text NOT NULL CHECK (posted_by_type IN ('player', 'corp', 'gov', 'npc')),
    posted_by_id bigint,
    target_type text NOT NULL CHECK (target_type IN ('player', 'corp', 'npc')),
    target_id bigint NOT NULL,
    reward bigint NOT NULL CHECK (reward >= 0),
    escrow_bank_tx bigint,
    status text NOT NULL DEFAULT 'open' CHECK (status IN ('open', 'claimed', 'cancelled', 'expired')),
    posted_ts timestamptz NOT NULL DEFAULT CURRENT_TIMESTAMP,
    claimed_by bigint,
    paid_bank_tx bigint
);

CREATE TABLE grants (
    grants_id bigserial PRIMARY KEY,
    name text NOT NULL,
    recipient_type text NOT NULL CHECK (recipient_type IN ('player', 'corp')),
    recipient_id bigint NOT NULL,
    amount bigint NOT NULL CHECK (amount >= 0),
    awarded_ts timestamptz NOT NULL DEFAULT CURRENT_TIMESTAMP,
    bank_tx_id bigint
);

CREATE TABLE research_projects (
    research_projects_id bigserial PRIMARY KEY,
    sponsor_type text NOT NULL CHECK (sponsor_type IN ('player', 'corp', 'gov')),
    sponsor_id bigint,
    title text NOT NULL,
    field text NOT NULL,
    cost BIGINT NOT NULL CHECK (
    COST >= 0),
    progress bigint NOT NULL DEFAULT 0 CHECK (progress BETWEEN 0 AND 100),
    status text NOT NULL DEFAULT 'active' CHECK (status IN ('active', 'paused', 'complete', 'failed')),
    created_ts timestamptz NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE research_contributors (
    project_id bigint NOT NULL REFERENCES research_projects (research_projects_id) ON DELETE CASCADE,
    actor_type text NOT NULL CHECK (actor_type IN ('player', 'corp')),
    actor_id bigint NOT NULL,
    amount bigint NOT NULL CHECK (amount >= 0),
    PRIMARY KEY (project_id, actor_type, actor_id)
);

CREATE TABLE research_results (
    research_results_id bigserial PRIMARY KEY,
    project_id bigint NOT NULL REFERENCES research_projects (research_projects_id) ON DELETE CASCADE,
    blueprint_code text NOT NULL,
    unlocked_ts timestamptz NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE black_accounts (
    black_accounts_id bigserial PRIMARY KEY,
    owner_type text NOT NULL CHECK (owner_type IN ('player', 'corp', 'npc')),
    owner_id bigint NOT NULL,
    balance bigint NOT NULL DEFAULT 0 CHECK (balance >= 0)
);

CREATE TABLE laundering_ops (
    laundering_ops_id bigserial PRIMARY KEY,
    from_black_id bigint REFERENCES black_accounts (black_accounts_id) ON DELETE SET NULL,
    to_player_id bigint REFERENCES players (player_id) ON DELETE SET NULL,
    amount bigint NOT NULL CHECK (amount > 0),
    risk_pct bigint NOT NULL DEFAULT 25 CHECK (risk_pct BETWEEN 0 AND 100),
    status text NOT NULL DEFAULT 'pending' CHECK (status IN ('pending', 'cleaned', 'seized', 'failed')),
    ts timestamptz NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE contracts_illicit (
    contracts_illicit_id bigserial PRIMARY KEY,
    contractor_type text NOT NULL CHECK (contractor_type IN ('player', 'corp', 'npc')),
    contractor_id bigint NOT NULL,
    target_type text NOT NULL CHECK (target_type IN ('player', 'corp', 'npc')),
    target_id bigint NOT NULL,
    reward bigint NOT NULL CHECK (reward >= 0),
    escrow_black_id bigint REFERENCES black_accounts (black_accounts_id) ON DELETE SET NULL,
    status text NOT NULL DEFAULT 'open' CHECK (status IN ('open', 'fulfilled', 'failed', 'cancelled')),
    ts timestamptz NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE fences (
    fences_id bigserial PRIMARY KEY,
    npc_id bigint,
    sector_id bigint,
    reputation bigint NOT NULL DEFAULT 0
);

CREATE TABLE economic_indicators (
    economic_indicators_id bigserial PRIMARY KEY,
    ts timestamptz NOT NULL DEFAULT CURRENT_TIMESTAMP,
    inflation_bps bigint NOT NULL DEFAULT 0,
    liquidity bigint NOT NULL DEFAULT 0,
    credit_velocity double precision NOT NULL DEFAULT 0.0
);

CREATE TABLE sector_gdp (
    sector_id bigserial PRIMARY KEY,
    gdp bigint NOT NULL DEFAULT 0,
    last_update text
);

CREATE TABLE event_triggers (
    event_triggers_id bigserial PRIMARY KEY,
    name text NOT NULL,
    condition_json text NOT NULL,
    action_json text NOT NULL
);

CREATE TABLE charities (
    charities_id bigserial PRIMARY KEY,
    name text NOT NULL UNIQUE,
    description text
);

CREATE TABLE donations (
    donations_id bigserial PRIMARY KEY,
    charity_id bigint NOT NULL REFERENCES charities (charities_id) ON DELETE CASCADE,
    donor_type text NOT NULL CHECK (donor_type IN ('player', 'corp')),
    donor_id bigint NOT NULL,
    amount bigint NOT NULL CHECK (amount >= 0),
    ts timestamptz NOT NULL DEFAULT CURRENT_TIMESTAMP,
    bank_tx_id bigint
);

CREATE TABLE temples (
    temples_id bigserial PRIMARY KEY,
    name text NOT NULL UNIQUE,
    sector_id bigint,
    favour bigint NOT NULL DEFAULT 0
);

CREATE TABLE guilds (
    guilds_id bigserial PRIMARY KEY,
    name text NOT NULL UNIQUE,
    description text
);

CREATE TABLE guild_memberships (
    guild_id bigint NOT NULL REFERENCES guilds (guilds_id) ON DELETE CASCADE,
    member_type text NOT NULL CHECK (member_type IN ('player', 'corp')),
    member_id bigint NOT NULL,
    role TEXT NOT NULL DEFAULT 'member',
    PRIMARY KEY (guild_id, member_type, member_id)
);

CREATE TABLE guild_dues (
    guild_dues_id bigserial PRIMARY KEY,
    guild_id bigint NOT NULL REFERENCES guilds (guilds_id) ON DELETE CASCADE,
    amount bigint NOT NULL CHECK (amount >= 0),
    period text NOT NULL DEFAULT 'monthly' CHECK (period IN ('weekly', 'monthly', 'quarterly', 'yearly'))
);

CREATE TABLE economy_snapshots (
    economy_snapshots_id bigserial PRIMARY KEY,
    ts timestamptz NOT NULL DEFAULT CURRENT_TIMESTAMP,
    money_supply bigint NOT NULL DEFAULT 0,
    total_deposits bigint NOT NULL DEFAULT 0,
    total_loans bigint NOT NULL DEFAULT 0,
    total_insured bigint NOT NULL DEFAULT 0,
    notes text
);

CREATE TABLE ai_economy_agents (
    ai_economy_agents_id bigserial PRIMARY KEY,
    name text NOT NULL,
    role TEXT NOT NULL,
    config_json text NOT NULL
);

CREATE TABLE anomaly_reports (
    anomaly_reports_id bigserial PRIMARY KEY,
    ts timestamptz NOT NULL DEFAULT CURRENT_TIMESTAMP,
    severity text NOT NULL CHECK (severity IN ('low', 'medium', 'high', 'critical')),
    subject text NOT NULL,
    details text NOT NULL,
    resolved bigint NOT NULL DEFAULT 0 CHECK (resolved IN (0, 1))
);

CREATE TABLE economy_policies (
    economy_policies_id bigserial PRIMARY KEY,
    name text NOT NULL UNIQUE,
    config_json text NOT NULL,
    active bigint NOT NULL DEFAULT 1 CHECK (active IN (0, 1))
);

CREATE TABLE s2s_keys (
    key_id text PRIMARY KEY,
    key_b64 text NOT NULL,
    is_default_tx bigint NOT NULL DEFAULT 0,
    active boolean DEFAULT TRUE,
    created_ts timestamptz NOT NULL
);

CREATE TABLE cron_tasks (
    cron_tasks_id bigserial PRIMARY KEY,
    name text UNIQUE NOT NULL,
    schedule text NOT NULL,
    last_run_at timestamptz,
    next_due_at timestamptz NOT NULL,
    enabled boolean DEFAULT TRUE,
    payload text
);

CREATE TABLE engine_events (
    engine_events_id bigserial PRIMARY KEY,
    ts bigint NOT NULL,
    type TEXT NOT NULL,
    actor_player_id bigint,
    sector_id bigint,
    payload text NOT NULL,
    idem_key text,
    processed_at timestamptz
);

CREATE TABLE engine_offset (
    key TEXT PRIMARY KEY,
    last_event_id bigint NOT NULL,
    last_event_ts bigint NOT NULL
);

CREATE TABLE engine_events_deadletter (
    engine_events_deadletter_id bigserial PRIMARY KEY,
    ts bigint NOT NULL,
    type TEXT NOT NULL,
    payload text NOT NULL,
    error text NOT NULL,
    moved_at timestamptz NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE engine_commands (
    engine_commands_id bigserial PRIMARY KEY,
    type TEXT NOT NULL,
    payload text NOT NULL,
    status text NOT NULL DEFAULT 'ready',
    priority bigint NOT NULL DEFAULT 100,
    attempts bigint NOT NULL DEFAULT 0,
    created_at timestamptz NOT NULL DEFAULT CURRENT_TIMESTAMP,
    due_at timestamptz NOT NULL DEFAULT CURRENT_TIMESTAMP,
    started_at timestamptz,
    finished_at timestamptz,
    worker text,
    idem_key text
);

CREATE TABLE engine_audit (
    engine_audit_id bigserial PRIMARY KEY,
    ts bigint NOT NULL,
    cmd_type text NOT NULL,
    correlation_id text,
    actor_player_id bigint,
    details text
);

CREATE TABLE news_feed (
    news_id bigserial PRIMARY KEY,
    published_ts bigint NOT NULL,
    news_category text NOT NULL,
    article_text text NOT NULL,
    author_id bigint,
    source_ids text
);

CREATE TABLE eligible_tows (
    ship_id bigserial PRIMARY KEY,
    sector_id bigint,
    owner_id bigint,
    fighters bigint,
    alignment bigint,
    experience bigint
);

