-- Generated from PostgreSQL 000_tables.sql
-- MySQL version

-- Generated from sqlite_schema.sql -> Postgres
CREATE TABLE config (
    `key` TEXT PRIMARY KEY,
    `value` TEXT NOT NULL,
    `type` TEXT NOT NULL CHECK (`type` IN ('int', 'bool', 'string', 'double'))
);

CREATE TABLE sessions (
    token TEXT PRIMARY KEY,
    player_id bigint NOT NULL,
    expires TIMESTAMP NOT NULL,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE idempotency (
    `key` TEXT PRIMARY KEY,
    cmd TEXT NOT NULL,
    req_fp TEXT NOT NULL,
    response TEXT,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE locks (
    lock_name TEXT PRIMARY KEY,
    owner TEXT,
    until_ms bigint
);

CREATE TABLE engine_state (
    state_key TEXT PRIMARY KEY,
    state_val TEXT NOT NULL
);

CREATE TABLE sectors (
    sector_id BIGINT AUTO_INCREMENT PRIMARY KEY,
    `name` TEXT,
    beacon TEXT,
    nebulae TEXT
);

CREATE TABLE commission (
    commission_id BIGINT AUTO_INCREMENT PRIMARY KEY,
    is_evil boolean NOT NULL DEFAULT FALSE CHECK (is_evil IN (TRUE, FALSE)),
    min_exp bigint NOT NULL,
    description TEXT NOT NULL
);

CREATE TABLE shiptypes (
    shiptypes_id BIGINT AUTO_INCREMENT PRIMARY KEY,
    `name` TEXT NOT NULL UNIQUE,
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
    ship_id BIGINT AUTO_INCREMENT PRIMARY KEY,
    `name` TEXT NOT NULL,
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
    shields integer DEFAULT 1,
    installed_shields integer DEFAULT 1,
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
    has_transwarp boolean NOT NULL DEFAULT FALSE,
    has_planet_scanner boolean NOT NULL DEFAULT FALSE,
    has_long_range_scanner boolean NOT NULL DEFAULT FALSE,
    cloaked timestamp NULL,
    ported bigint DEFAULT 0,
    onplanet boolean NOT NULL DEFAULT FALSE,
    destroyed boolean NOT NULL DEFAULT FALSE,
    hull bigint NOT NULL DEFAULT 100,
    perms bigint NOT NULL DEFAULT 731,
    towing_ship_id BIGINT DEFAULT 0,
    is_being_towed_by BIGINT DEFAULT 0,
    CONSTRAINT check_current_cargo_limit CHECK ((colonists + equipment + organics + ore) <= holds),
    FOREIGN KEY (type_id) REFERENCES shiptypes (shiptypes_id),
    FOREIGN KEY (sector_id) REFERENCES sectors (sector_id)
);

CREATE TABLE players (
    player_id BIGINT AUTO_INCREMENT PRIMARY KEY,
    `type` BIGINT DEFAULT 2,
    `number` bigint,
    `name` TEXT NOT NULL,
    passwd TEXT NOT NULL,
    sector_id bigint DEFAULT 1,
    ship_id bigint,
    experience bigint DEFAULT 0,
    alignment bigint DEFAULT 0,
    commission_id bigint DEFAULT 1,
    credits bigint DEFAULT 1500,
    flags bigint,
    login_time TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    last_update TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    times_blown_up bigint NOT NULL DEFAULT 0,
    intransit boolean,
    beginmove bigint,
    movingto bigint,
    loggedin TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    lastplanet bigint,
    score bigint,
    is_npc boolean DEFAULT FALSE,
    last_news_read_timestamp TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY (commission_id) REFERENCES commission (commission_id),
    FOREIGN KEY (sector_id) REFERENCES sectors (sector_id),
    FOREIGN KEY (ship_id) REFERENCES ships (ship_id)
);

CREATE TABLE planettypes (
    planettypes_id BIGINT AUTO_INCREMENT PRIMARY KEY,
    code TEXT UNIQUE,
    typeDescription TEXT,
    typeName TEXT,
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
    breeding DOUBLE,
    genesis_weight bigint NOT NULL DEFAULT 10
);

CREATE TABLE player_types (
    `type` BIGSERIAL PRIMARY KEY,
    description TEXT
);

CREATE TABLE corporations (
    corporation_id BIGINT AUTO_INCREMENT PRIMARY KEY,
    `name` TEXT NOT NULL,
    owner_id bigint,
    tag TEXT,
    description TEXT,
    tax_arrears bigint NOT NULL DEFAULT 0,
    credit_rating bigint NOT NULL DEFAULT 0,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    CHECK (tag IS NULL OR (length(tag) BETWEEN 2 AND 5 AND tag ~ '^[A-Za-z0-9].*$'))
);

CREATE TABLE corp_members (
    corporation_id bigint NOT NULL,
    player_id bigint NOT NULL,
    `role` TEXT NOT NULL DEFAULT 'Member',
    join_date TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (corporation_id, player_id),
    FOREIGN KEY (corporation_id) REFERENCES corporations (corporation_id) ON DELETE CASCADE ON UPDATE CASCADE,
    FOREIGN KEY (player_id) REFERENCES players (player_id),
    CHECK (`role` IN ('Leader', 'Officer', 'Member'))
);

CREATE TABLE corp_mail (
    corp_mail_id BIGINT AUTO_INCREMENT PRIMARY KEY,
    corporation_id bigint NOT NULL,
    sender_id bigint,
    subject TEXT,
    body TEXT NOT NULL,
    posted_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
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
    id BIGINT AUTO_INCREMENT PRIMARY KEY,
    corporation_id bigint NOT NULL,
    actor_id bigint,
    event_type TEXT NOT NULL,
    payload TEXT NOT NULL,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY (corporation_id) REFERENCES corporations (corporation_id) ON DELETE CASCADE,
    FOREIGN KEY (actor_id) REFERENCES players (player_id) ON DELETE SET NULL
);

CREATE TABLE economy_curve (
    economy_curve_id BIGINT AUTO_INCREMENT PRIMARY KEY,
    curve_name TEXT NOT NULL UNIQUE,
    base_restock_rate DOUBLE NOT NULL,
    price_elasticity DOUBLE NOT NULL,
    target_stock bigint NOT NULL,
    volatility_factor DOUBLE NOT NULL
);

CREATE TABLE alignment_band (
    alignment_band_id BIGINT AUTO_INCREMENT PRIMARY KEY,
    code TEXT NOT NULL UNIQUE,
    `name` TEXT NOT NULL,
    min_align bigint NOT NULL,
    max_align bigint NOT NULL,
    is_good boolean NOT NULL DEFAULT TRUE,
    is_evil boolean NOT NULL DEFAULT FALSE,
    can_buy_iss boolean NOT NULL DEFAULT TRUE,
    can_rob_ports boolean NOT NULL DEFAULT FALSE,
    notes TEXT
);

CREATE TABLE trade_idempotency (
    `key` TEXT PRIMARY KEY,
    player_id bigint NOT NULL,
    sector_id bigint NOT NULL,
    request_json TEXT NOT NULL,
    response_json TEXT NOT NULL,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE used_sectors (
    used bigint
);

CREATE TABLE npc_shipnames (
    npc_shipnames_id bigint,
    `name` TEXT
);

CREATE TABLE tavern_names (
    tavern_names_id BIGINT AUTO_INCREMENT PRIMARY KEY,
    `name` TEXT NOT NULL UNIQUE,
    enabled boolean DEFAULT TRUE,
    weight bigint NOT NULL DEFAULT 1
);

CREATE TABLE taverns (
    sector_id BIGINT AUTO_INCREMENT PRIMARY KEY REFERENCES sectors (sector_id),
    name_id bigint NOT NULL REFERENCES tavern_names (tavern_names_id),
    enabled boolean NOT NULL DEFAULT TRUE
);

CREATE TABLE tavern_settings (
    tavern_settings_id BIGINT AUTO_INCREMENT PRIMARY KEY CHECK (tavern_settings_id = 1),
    max_bet_per_transaction bigint NOT NULL DEFAULT 5000,
    daily_max_wager bigint NOT NULL DEFAULT 50000,
    enable_dynamic_wager_limit boolean NOT NULL DEFAULT FALSE,
    graffiti_max_posts bigint NOT NULL DEFAULT 100,
    notice_expires_days bigint NOT NULL DEFAULT 7,
    buy_round_cost bigint NOT NULL DEFAULT 1000,
    buy_round_alignment_gain bigint NOT NULL DEFAULT 5,
    loan_shark_enabled boolean NOT NULL DEFAULT TRUE
);

CREATE TABLE tavern_lottery_state (
    draw_date TEXT PRIMARY KEY,
    winning_number bigint,
    jackpot bigint NOT NULL,
    carried_over bigint NOT NULL DEFAULT 0
);

CREATE TABLE tavern_lottery_tickets (
    tavern_lottery_tickets_id BIGINT AUTO_INCREMENT PRIMARY KEY,
    draw_date TEXT NOT NULL,
    player_id bigint NOT NULL REFERENCES players (player_id),
    `number` bigint NOT NULL,
    cost BIGINT NOT NULL,
    purchased_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE tavern_deadpool_bets (
    tavern_deadpool_bets_id BIGINT AUTO_INCREMENT PRIMARY KEY,
    bettor_id bigint NOT NULL REFERENCES players (player_id),
    target_id bigint NOT NULL REFERENCES players (player_id),
    amount bigint NOT NULL,
    odds_bp bigint NOT NULL,
    placed_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    expires_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    resolved boolean NOT NULL DEFAULT FALSE,
    resolved_at bigint,
    result TEXT
);

CREATE TABLE tavern_raffle_state (
    tavern_raffle_state_id BIGINT AUTO_INCREMENT PRIMARY KEY CHECK (tavern_raffle_state_id = 1),
    pot bigint NOT NULL,
    last_winner_id bigint,
    last_payout bigint,
    last_win_ts bigint
);

CREATE TABLE tavern_graffiti (
    tavern_graffiti_id BIGINT AUTO_INCREMENT PRIMARY KEY,
    player_id bigint NOT NULL REFERENCES players (player_id),
    TEXT TEXT NOT NULL,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE tavern_notices (
    tavern_notices_id BIGINT AUTO_INCREMENT PRIMARY KEY,
    author_id bigint NOT NULL REFERENCES players (player_id),
    corp_id bigint,
    TEXT TEXT NOT NULL,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    expires_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE corp_recruiting (
    corp_id BIGINT AUTO_INCREMENT PRIMARY KEY REFERENCES corporations (corporation_id),
    tagline TEXT NOT NULL,
    min_alignment bigint,
    play_style TEXT,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    expires_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE corp_invites (
    corp_invites_id BIGINT AUTO_INCREMENT PRIMARY KEY,
    corp_id bigint NOT NULL,
    player_id bigint NOT NULL,
    invited_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    expires_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    UNIQUE (corp_id, player_id),
    FOREIGN KEY (corp_id) REFERENCES corporations (corporation_id) ON DELETE CASCADE,
    FOREIGN KEY (player_id) REFERENCES players (player_id) ON DELETE CASCADE
);

CREATE TABLE tavern_loans (
    player_id BIGINT AUTO_INCREMENT PRIMARY KEY REFERENCES players (player_id),
    principal bigint NOT NULL,
    interest_rate bigint NOT NULL,
    due_date TIMESTAMP NOT NULL,
    is_defaulted bigint NOT NULL DEFAULT 0
);

CREATE TABLE ports (
    port_id BIGINT AUTO_INCREMENT PRIMARY KEY,
    `number` bigint,
    `name` TEXT NOT NULL,
    sector_id bigint NOT NULL,
    size bigint,
    techlevel bigint,
    petty_cash bigint NOT NULL DEFAULT 0,
    invisible boolean DEFAULT FALSE,
    `type` BIGINT DEFAULT 1,
    economy_curve_id bigint NOT NULL DEFAULT 1,
    FOREIGN KEY (economy_curve_id) REFERENCES economy_curve (economy_curve_id),
    FOREIGN KEY (sector_id) REFERENCES sectors (sector_id)
);

CREATE TABLE port_trade (
    port_trade_id BIGINT AUTO_INCREMENT PRIMARY KEY,
    port_id bigint NOT NULL,
    maxproduct bigint,
    commodity TEXT CHECK (commodity IN ('ore', 'organics', 'equipment')),
    mode TEXT CHECK (mode IN ('buy', 'sell')),
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
    marker_type TEXT NOT NULL,
    PRIMARY KEY (ship_id, owner_player, marker_type)
);

CREATE TABLE ship_roles (
    role_id BIGINT AUTO_INCREMENT PRIMARY KEY,
    `role` TEXT,
    role_description TEXT
);

CREATE TABLE ship_ownership (
    ship_id bigint NOT NULL,
    player_id bigint NOT NULL,
    role_id bigint NOT NULL,
    is_primary boolean DEFAULT TRUE,
    acquired_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (ship_id, player_id, role_id),
    FOREIGN KEY (ship_id) REFERENCES ships (ship_id),
    FOREIGN KEY (player_id) REFERENCES players (player_id)
);

CREATE TABLE planets (
    planet_id BIGINT AUTO_INCREMENT PRIMARY KEY,
    num bigint,
    sector_id bigint NOT NULL,
    `name` TEXT NOT NULL,
    owner_id bigint NOT NULL,
    owner_type TEXT NOT NULL DEFAULT 'player',
    class TEXT NOT NULL DEFAULT 'M',
    population bigint,
    `type` BIGINT,
    creator TEXT,
    colonist bigint,
    fighters bigint,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
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
    hardware_items_id BIGINT AUTO_INCREMENT PRIMARY KEY,
    code TEXT UNIQUE NOT NULL,
    `name` TEXT NOT NULL,
    price bigint NOT NULL,
    requires_stardock boolean DEFAULT TRUE,
    sold_in_class0 boolean DEFAULT TRUE,
    max_per_ship bigint,
    category TEXT NOT NULL,
    enabled boolean DEFAULT FALSE
);

CREATE TABLE citadels (
    citadel_id BIGINT AUTO_INCREMENT PRIMARY KEY,
    planet_id bigint UNIQUE NOT NULL,
    level BIGINT,
    treasury bigint,
    militaryReactionLevel bigint,
    qCannonAtmosphere bigint,
    qCannonSector bigint,
    planetaryShields bigint,
    transporterlvl bigint,
    interdictor bigint,
    upgradePercent DOUBLE,
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
    construction_status TEXT DEFAULT 'idle',
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
    mail_id BIGINT AUTO_INCREMENT PRIMARY KEY,
    thread_id bigint,
    sender_id bigint NOT NULL,
    recipient_id bigint NOT NULL,
    subject TEXT,
    body TEXT NOT NULL,
    sent_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    read_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    archived TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    deleted TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    idempotency_key TEXT,
    FOREIGN KEY (sender_id) REFERENCES players (player_id) ON DELETE CASCADE,
    FOREIGN KEY (recipient_id) REFERENCES players (player_id) ON DELETE CASCADE
);

CREATE TABLE subspace (
    subspace_id BIGINT AUTO_INCREMENT PRIMARY KEY,
    sender_id bigint,
    message TEXT NOT NULL,
    kind TEXT NOT NULL DEFAULT 'chat',
    posted_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY (sender_id) REFERENCES players (player_id) ON DELETE SET NULL
);

CREATE TABLE subspace_cursors (
    player_id BIGINT AUTO_INCREMENT PRIMARY KEY,
    last_seen_id bigint NOT NULL DEFAULT 0,
    FOREIGN KEY (player_id) REFERENCES players (player_id) ON DELETE CASCADE
);

CREATE TABLE system_events (
    system_events_id BIGINT AUTO_INCREMENT PRIMARY KEY,
    scope TEXT NOT NULL,
    event_type TEXT NOT NULL,
    payload TEXT NOT NULL,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE subscriptions (
    subscriptions_id BIGINT AUTO_INCREMENT PRIMARY KEY,
    player_id bigint NOT NULL,
    event_type TEXT NOT NULL,
    delivery TEXT NOT NULL,
    filter_json TEXT,
    ephemeral boolean NOT NULL DEFAULT FALSE,
    locked boolean NOT NULL DEFAULT FALSE,
    enabled boolean NOT NULL DEFAULT TRUE,
    UNIQUE (player_id, event_type),
    FOREIGN KEY (player_id) REFERENCES players (player_id) ON DELETE CASCADE
);

CREATE TABLE player_block (
    blocker_id bigint NOT NULL,
    blocked_id bigint NOT NULL,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (blocker_id, blocked_id)
);

CREATE TABLE notice_seen (
    notice_id bigint NOT NULL,
    player_id bigint NOT NULL,
    seen_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (notice_id, player_id)
);

CREATE TABLE system_notice (
    system_notice_id BIGINT AUTO_INCREMENT PRIMARY KEY,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    title TEXT NOT NULL,
    body TEXT NOT NULL,
    severity TEXT NOT NULL CHECK (severity IN ('info', 'warn', 'error')),
    expires_at TIMESTAMP
);

CREATE TABLE player_prefs (
    player_prefs_id bigint NOT NULL,
    `key` TEXT NOT NULL,
    `type` TEXT NOT NULL CHECK (`type` IN ('bool', 'int', 'string', 'json')),
    `value` TEXT NOT NULL,
    updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE player_bookmarks (
    player_bookmarks_id BIGINT AUTO_INCREMENT PRIMARY KEY,
    player_id bigint NOT NULL,
    `name` TEXT NOT NULL,
    sector_id bigint NOT NULL,
    updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    UNIQUE (player_id, name),
    FOREIGN KEY (player_id) REFERENCES players (player_id) ON DELETE CASCADE,
    FOREIGN KEY (sector_id) REFERENCES sectors (sector_id) ON DELETE CASCADE
);

CREATE TABLE player_avoid (
    player_id bigint NOT NULL,
    sector_id bigint NOT NULL,
    updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (player_id, sector_id),
    FOREIGN KEY (player_id) REFERENCES players (player_id) ON DELETE CASCADE,
    FOREIGN KEY (sector_id) REFERENCES sectors (sector_id) ON DELETE CASCADE
);

CREATE TABLE player_notes (
    player_notes_id BIGINT AUTO_INCREMENT PRIMARY KEY,
    player_id bigint NOT NULL,
    scope TEXT NOT NULL,
    `key` TEXT NOT NULL,
    note TEXT NOT NULL,
    updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    UNIQUE (player_id, scope, key),
    FOREIGN KEY (player_id) REFERENCES players (player_id) ON DELETE CASCADE
);

CREATE TABLE sector_assets (
    sector_assets_id BIGINT AUTO_INCREMENT PRIMARY KEY,
    sector_id bigint NOT NULL REFERENCES sectors (sector_id),
    owner_id bigint REFERENCES players (player_id),
    corporation_id bigint NOT NULL DEFAULT 0,
    asset_type bigint NOT NULL,
    offensive_setting bigint DEFAULT 0,
    quantity bigint,
    ttl bigint,
    deployed_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE limpet_attached (
    limpet_attached_id BIGINT AUTO_INCREMENT PRIMARY KEY,
    ship_id bigint NOT NULL,
    owner_player_id bigint NOT NULL,
    created_ts TIMESTAMP NOT NULL,
    UNIQUE (ship_id, owner_player_id),
    FOREIGN KEY (ship_id) REFERENCES ships (ship_id) ON DELETE CASCADE,
    FOREIGN KEY (owner_player_id) REFERENCES players (player_id) ON DELETE CASCADE
);

CREATE TABLE msl_sectors (
    sector_id BIGINT AUTO_INCREMENT PRIMARY KEY REFERENCES sectors (sector_id)
);

CREATE TABLE trade_log (
    trade_log_id BIGINT AUTO_INCREMENT PRIMARY KEY,
    player_id bigint NOT NULL,
    port_id bigint NOT NULL,
    sector_id bigint NOT NULL,
    commodity TEXT NOT NULL,
    units bigint NOT NULL,
    price_per_unit DOUBLE NOT NULL,
    action TEXT CHECK (action IN ('buy', 'sell')) NOT NULL,
    timestamp TIMESTAMP NOT NULL,
    FOREIGN KEY (player_id) REFERENCES players (player_id),
    FOREIGN KEY (port_id) REFERENCES ports (port_id),
    FOREIGN KEY (sector_id) REFERENCES sectors (sector_id)
);

CREATE TABLE stardock_assets (
    sector_id BIGINT AUTO_INCREMENT PRIMARY KEY,
    owner_id bigint NOT NULL,
    fighters bigint NOT NULL DEFAULT 0,
    defenses bigint NOT NULL DEFAULT 0,
    ship_capacity bigint NOT NULL DEFAULT 1,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY (sector_id) REFERENCES sectors (sector_id),
    FOREIGN KEY (owner_id) REFERENCES players (player_id)
);

CREATE TABLE shipyard_inventory (
    port_id bigint NOT NULL REFERENCES ports (port_id),
    ship_type_id bigint NOT NULL REFERENCES shiptypes (shiptypes_id),
    enabled boolean NOT NULL DEFAULT TRUE,
    PRIMARY KEY (port_id, ship_type_id)
);

CREATE TABLE podded_status (
    player_id BIGINT AUTO_INCREMENT PRIMARY KEY REFERENCES players (player_id),
    status TEXT NOT NULL DEFAULT 'active',
    big_sleep_until bigint,
    reason TEXT,
    podded_count_today bigint NOT NULL DEFAULT 0,
    podded_last_reset bigint
);

CREATE TABLE planet_goods (
    planet_id bigint NOT NULL,
    commodity TEXT NOT NULL CHECK (commodity IN ('ore', 'organics', 'equipment', 'food', 'fuel')),
    quantity bigint NOT NULL DEFAULT 0,
    max_capacity bigint NOT NULL,
    production_rate bigint NOT NULL,
    PRIMARY KEY (planet_id, commodity),
    FOREIGN KEY (planet_id) REFERENCES planets (planet_id)
);

CREATE TABLE commodities (
    commodities_id BIGINT AUTO_INCREMENT PRIMARY KEY,
    code TEXT UNIQUE NOT NULL,
    `name` TEXT NOT NULL,
    illegal boolean NOT NULL DEFAULT FALSE,
    base_price bigint NOT NULL DEFAULT 0 CHECK (base_price >= 0),
    volatility bigint NOT NULL DEFAULT 0 CHECK (volatility >= 0)
);

CREATE TABLE clusters (
    clusters_id BIGINT AUTO_INCREMENT PRIMARY KEY,
    `name` TEXT NOT NULL UNIQUE,
    `role` TEXT NOT NULL,
    kind TEXT NOT NULL,
    center_sector bigint,
    law_severity bigint NOT NULL DEFAULT 1,
    alignment bigint NOT NULL DEFAULT 0,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP
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
    commodity_code TEXT NOT NULL,
    mid_price bigint NOT NULL,
    last_updated TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (cluster_id, commodity_code),
    FOREIGN KEY (cluster_id) REFERENCES clusters (clusters_id) ON DELETE CASCADE,
    FOREIGN KEY (commodity_code) REFERENCES commodities (code) ON DELETE CASCADE
);

CREATE TABLE cluster_player_status (
    cluster_id bigint NOT NULL,
    player_id bigint NOT NULL,
    suspicion bigint NOT NULL DEFAULT 0,
    bust_count bigint NOT NULL DEFAULT 0,
    last_bust_at TEXT,
    wanted_level bigint NOT NULL DEFAULT 0,
    banned bigint NOT NULL DEFAULT 0,
    PRIMARY KEY (cluster_id, player_id),
    FOREIGN KEY (cluster_id) REFERENCES clusters (clusters_id),
    FOREIGN KEY (player_id) REFERENCES players (player_id)
);

CREATE TABLE law_enforcement (
    law_enforcement_id BIGINT AUTO_INCREMENT PRIMARY KEY CHECK (law_enforcement_id = 1),
    robbery_evil_threshold bigint DEFAULT -10,
    robbery_xp_per_hold bigint DEFAULT 20,
    robbery_credits_per_xp bigint DEFAULT 10,
    robbery_bust_chance_base DOUBLE DEFAULT 0.05,
    robbery_turn_cost bigint DEFAULT 1,
    good_guy_bust_bonus DOUBLE DEFAULT 0.10,
    pro_criminal_bust_delta DOUBLE DEFAULT -0.02,
    evil_cluster_bust_bonus DOUBLE DEFAULT 0.05,
    good_align_penalty_mult DOUBLE DEFAULT 3.0,
    robbery_real_bust_ttl_days bigint DEFAULT 7
);

CREATE TABLE port_busts (
    port_id bigint NOT NULL,
    player_id bigint NOT NULL,
    last_bust_at TIMESTAMP NOT NULL,
    bust_type TEXT NOT NULL,
    active boolean NOT NULL DEFAULT TRUE,
    PRIMARY KEY (port_id, player_id),
    FOREIGN KEY (port_id) REFERENCES ports (port_id),
    FOREIGN KEY (player_id) REFERENCES players (player_id)
);

CREATE TABLE player_last_rob (
    player_id BIGINT AUTO_INCREMENT PRIMARY KEY,
    port_id bigint NOT NULL,
    last_attempt_at TIMESTAMP NOT NULL,
    was_success boolean NOT NULL
);

CREATE TABLE currencies (
    code TEXT PRIMARY KEY,
    `name` TEXT NOT NULL,
    minor_unit bigint NOT NULL DEFAULT 1 CHECK (minor_unit > 0),
    is_default boolean NOT NULL DEFAULT FALSE
);

CREATE TABLE commodity_orders (
    commodity_orders_id BIGINT AUTO_INCREMENT PRIMARY KEY,
    actor_type TEXT NOT NULL CHECK (actor_type IN ('player', 'corp', 'npc_planet', 'port')),
    actor_id bigint NOT NULL,
    location_type TEXT NOT NULL CHECK (location_type IN ('planet', 'port')),
    location_id bigint NOT NULL,
    commodity_id bigint NOT NULL REFERENCES commodities (commodities_id) ON DELETE CASCADE,
    side TEXT NOT NULL CHECK (side IN ('buy', 'sell')),
    quantity bigint NOT NULL CHECK (quantity > 0),
    filled_quantity bigint NOT NULL DEFAULT 0 CHECK (filled_quantity >= 0),
    price bigint NOT NULL CHECK (price >= 0),
    status TEXT NOT NULL DEFAULT 'open' CHECK (status IN ('open', 'filled', 'cancelled', 'expired')),
    ts TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE commodity_trades (
    id BIGINT AUTO_INCREMENT PRIMARY KEY,
    commodity_id bigint NOT NULL REFERENCES commodities (commodities_id) ON DELETE CASCADE,
    buyer_actor_type TEXT NOT NULL CHECK (buyer_actor_type IN ('player', 'corp', 'npc_planet', 'port')),
    buyer_actor_id bigint NOT NULL,
    buyer_location_type TEXT NOT NULL CHECK (buyer_location_type IN ('planet', 'port')),
    buyer_location_id bigint NOT NULL,
    seller_actor_type TEXT NOT NULL CHECK (seller_actor_type IN ('player', 'corp', 'npc_planet', 'port')),
    seller_actor_id bigint NOT NULL,
    seller_location_type TEXT NOT NULL CHECK (seller_location_type IN ('planet', 'port')),
    seller_location_id bigint NOT NULL,
    quantity bigint NOT NULL CHECK (quantity > 0),
    price bigint NOT NULL CHECK (price >= 0),
    ts TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    settlement_tx_buy bigint,
    settlement_tx_sell bigint
);

CREATE TABLE entity_stock (
    entity_type TEXT NOT NULL CHECK (entity_type IN ('port', 'planet')),
    entity_id bigint NOT NULL,
    commodity_code TEXT NOT NULL,
    quantity bigint NOT NULL,
    price bigint NULL,
    last_updated_ts bigint NOT NULL DEFAULT (UNIX_TIMESTAMP()),
    PRIMARY KEY (entity_type, entity_id, commodity_code),
    FOREIGN KEY (commodity_code) REFERENCES commodities (code)
);

CREATE TABLE planet_production (
    planet_type_id bigint NOT NULL,
    commodity_code TEXT NOT NULL,
    base_prod_rate bigint NOT NULL,
    base_cons_rate bigint NOT NULL,
    PRIMARY KEY (planet_type_id, commodity_code),
    FOREIGN KEY (planet_type_id) REFERENCES planettypes (planettypes_id) ON DELETE CASCADE,
    FOREIGN KEY (commodity_code) REFERENCES commodities (code) ON DELETE CASCADE
);

CREATE TABLE traps (
    id BIGINT AUTO_INCREMENT PRIMARY KEY,
    sector_id bigint NOT NULL,
    owner_player_id bigint,
    kind TEXT NOT NULL,
    armed boolean NOT NULL DEFAULT FALSE,
    arming_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    expires_at TIMESTAMP,
    trigger_at TIMESTAMP,
    payload TEXT
);

CREATE TABLE bank_accounts (
    id BIGINT AUTO_INCREMENT PRIMARY KEY,
    owner_type TEXT NOT NULL,
    owner_id bigint NOT NULL,
    currency TEXT NOT NULL DEFAULT 'CRD' REFERENCES currencies (code),
    balance bigint NOT NULL DEFAULT 0 CHECK (balance >= 0),
    interest_rate_bp bigint NOT NULL DEFAULT 0,
    last_interest_tick bigint,
    tx_alert_threshold bigint DEFAULT 0,
    is_active boolean NOT NULL DEFAULT TRUE
);

CREATE TABLE bank_transactions (
    id BIGINT AUTO_INCREMENT PRIMARY KEY,
    account_id bigint NOT NULL REFERENCES bank_accounts (id),
    tx_type TEXT NOT NULL CHECK (tx_type IN ('DEPOSIT', 'WITHDRAWAL', 'TRANSFER', 'INTEREST', 'FEE', 'WIRE', 'TAX', 'TRADE_BUY', 'TRADE_SELL', 'TRADE_BUY_FEE', 'TRADE_SELL_FEE', 'WITHDRAWAL_FEE', 'ADJUSTMENT')),
    direction TEXT NOT NULL CHECK (direction IN ('CREDIT', 'DEBIT')),
    amount bigint NOT NULL CHECK (amount > 0),
    currency TEXT NOT NULL,
    tx_group_id TEXT,
    related_account_id bigint,
    description TEXT,
    ts bigint NOT NULL,
    balance_after bigint DEFAULT 0,
    idempotency_key TEXT,
    engine_event_id bigint
);

CREATE TABLE bank_fee_schedules (
    id BIGINT AUTO_INCREMENT PRIMARY KEY,
    tx_type TEXT NOT NULL,
    fee_code TEXT NOT NULL,
    owner_type TEXT,
    currency TEXT NOT NULL DEFAULT 'CRD',
    `value` bigint NOT NULL,
    is_percentage boolean NOT NULL DEFAULT FALSE,
    min_tx_amount bigint DEFAULT 0,
    max_tx_amount bigint,
    effective_from bigint NOT NULL,
    effective_to bigint
);

CREATE TABLE bank_interest_policy (
    id BIGINT AUTO_INCREMENT PRIMARY KEY CHECK (id = 1),
    apr_bps bigint NOT NULL DEFAULT 0 CHECK (apr_bps >= 0),
    min_balance bigint NOT NULL DEFAULT 0 CHECK (min_balance >= 0),
    max_balance bigint NOT NULL DEFAULT 9223372036854775807,
    last_run_at TIMESTAMP,
    compounding TEXT NOT NULL DEFAULT 'none',
    currency TEXT NOT NULL DEFAULT 'CRD' REFERENCES currencies (code)
);

CREATE TABLE bank_orders (
    id BIGINT AUTO_INCREMENT PRIMARY KEY,
    player_id bigint NOT NULL REFERENCES players (player_id) ON DELETE CASCADE,
    kind TEXT NOT NULL CHECK (kind IN ('recurring', 'once')),
    schedule TEXT NOT NULL,
    next_run_at TIMESTAMP,
    enabled boolean NOT NULL DEFAULT TRUE,
    amount bigint NOT NULL CHECK (amount > 0),
    currency TEXT NOT NULL DEFAULT 'CRD' REFERENCES currencies (code),
    to_entity TEXT NOT NULL CHECK (to_entity IN ('player', 'corp', 'gov', 'npc')),
    to_id bigint NOT NULL,
    memo TEXT
);

CREATE TABLE bank_flags (
    player_id BIGINT AUTO_INCREMENT PRIMARY KEY REFERENCES players (player_id) ON DELETE CASCADE,
    is_frozen boolean NOT NULL DEFAULT FALSE,
    risk_tier TEXT NOT NULL DEFAULT 'normal' CHECK (risk_tier IN ('normal', 'elevated', 'high', 'blocked'))
);

CREATE TABLE corp_accounts (
    corp_id BIGINT AUTO_INCREMENT PRIMARY KEY REFERENCES corporations (corporation_id) ON DELETE CASCADE,
    currency TEXT NOT NULL DEFAULT 'CRD' REFERENCES currencies (code),
    balance bigint NOT NULL DEFAULT 0 CHECK (balance >= 0),
    last_interest_at TIMESTAMP
);

CREATE TABLE corp_tx (
    id BIGINT AUTO_INCREMENT PRIMARY KEY,
    corp_id bigint NOT NULL REFERENCES corporations (corporation_id) ON DELETE CASCADE,
    ts TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    kind TEXT NOT NULL CHECK (kind IN ('deposit', 'withdraw', 'transfer_in', 'transfer_out', 'interest', 'dividend', 'salary', 'adjustment')),
    amount bigint NOT NULL CHECK (amount > 0),
    balance_after bigint,
    currency TEXT NOT NULL DEFAULT 'CRD' REFERENCES currencies (code),
    memo TEXT,
    idempotency_key TEXT UNIQUE
);

CREATE TABLE corp_interest_policy (
    id BIGINT AUTO_INCREMENT PRIMARY KEY CHECK (id = 1),
    apr_bps bigint NOT NULL DEFAULT 0 CHECK (apr_bps >= 0),
    compounding TEXT NOT NULL DEFAULT 'none' CHECK (compounding IN ('none', 'daily', 'weekly', 'monthly')),
    last_run_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    currency TEXT NOT NULL DEFAULT 'CRD' REFERENCES currencies (code)
);

CREATE TABLE stocks (
    id BIGINT AUTO_INCREMENT PRIMARY KEY,
    corp_id bigint NOT NULL REFERENCES corporations (corporation_id) ON DELETE CASCADE,
    ticker TEXT NOT NULL UNIQUE,
    total_shares bigint NOT NULL CHECK (total_shares > 0),
    par_value bigint NOT NULL DEFAULT 0 CHECK (par_value >= 0),
    current_price bigint NOT NULL DEFAULT 0 CHECK (current_price >= 0),
    last_dividend_ts TEXT
);

CREATE TABLE corp_shareholders (
    corp_id bigint NOT NULL REFERENCES corporations (corporation_id) ON DELETE CASCADE,
    player_id bigint NOT NULL REFERENCES players (player_id) ON DELETE CASCADE,
    shares bigint NOT NULL CHECK (shares >= 0),
    PRIMARY KEY (corp_id, player_id)
);

CREATE TABLE stock_orders (
    id BIGINT AUTO_INCREMENT PRIMARY KEY,
    player_id bigint NOT NULL REFERENCES players (player_id) ON DELETE CASCADE,
    equity_id bigint NOT NULL REFERENCES stocks (id) ON DELETE CASCADE,
    `type` TEXT NOT NULL CHECK (`type` IN ('buy', 'sell')),
    quantity bigint NOT NULL CHECK (quantity > 0),
    price bigint NOT NULL CHECK (price >= 0),
    status TEXT NOT NULL DEFAULT 'open' CHECK (status IN ('open', 'filled', 'cancelled', 'expired')),
    ts TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE stock_trades (
    id BIGINT AUTO_INCREMENT PRIMARY KEY,
    equity_id bigint NOT NULL REFERENCES stocks (id) ON DELETE CASCADE,
    buyer_id bigint NOT NULL REFERENCES players (player_id) ON DELETE CASCADE,
    seller_id bigint NOT NULL REFERENCES players (player_id) ON DELETE CASCADE,
    quantity bigint NOT NULL CHECK (quantity > 0),
    price bigint NOT NULL CHECK (price >= 0),
    ts TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    settlement_tx_buy bigint,
    settlement_tx_sell bigint
);

CREATE TABLE stock_dividends (
    id BIGINT AUTO_INCREMENT PRIMARY KEY,
    equity_id bigint NOT NULL REFERENCES stocks (id) ON DELETE CASCADE,
    amount_per_share bigint NOT NULL CHECK (amount_per_share >= 0),
    declared_ts TEXT NOT NULL,
    paid_ts TEXT
);

CREATE TABLE stock_indices (
    id BIGINT AUTO_INCREMENT PRIMARY KEY,
    `name` TEXT UNIQUE NOT NULL
);

CREATE TABLE stock_index_members (
    index_id bigint NOT NULL REFERENCES stock_indices (id) ON DELETE CASCADE,
    equity_id bigint NOT NULL REFERENCES stocks (id) ON DELETE CASCADE,
    weight DOUBLE NOT NULL DEFAULT 1.0,
    PRIMARY KEY (index_id, equity_id)
);

CREATE TABLE insurance_funds (
    fund_id BIGINT AUTO_INCREMENT PRIMARY KEY,
    owner_type TEXT NOT NULL CHECK (owner_type IN ('system', 'corp', 'player')),
    owner_id bigint,
    balance bigint NOT NULL DEFAULT 0 CHECK (balance >= 0)
);

CREATE TABLE insurance_policies (
    insurance_policies_id BIGINT AUTO_INCREMENT PRIMARY KEY,
    holder_type TEXT NOT NULL CHECK (holder_type IN ('player', 'corp')),
    holder_id bigint NOT NULL,
    subject_type TEXT NOT NULL CHECK (subject_type IN ('ship', 'cargo', 'planet')),
    subject_id bigint NOT NULL,
    premium bigint NOT NULL CHECK (premium >= 0),
    payout bigint NOT NULL CHECK (payout >= 0),
    fund_id bigint REFERENCES insurance_funds (fund_id) ON DELETE SET NULL,
    start_ts TEXT NOT NULL,
    expiry_ts TEXT,
    active bigint NOT NULL DEFAULT 1 CHECK (active IN (0, 1))
);

CREATE TABLE insurance_claims (
    insurance_claims_id BIGINT AUTO_INCREMENT PRIMARY KEY,
    policy_id bigint NOT NULL REFERENCES insurance_policies (insurance_policies_id) ON DELETE CASCADE,
    event_id TEXT,
    amount bigint NOT NULL CHECK (amount >= 0),
    status TEXT NOT NULL DEFAULT 'open' CHECK (status IN ('open', 'paid', 'denied')),
    ts TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    paid_bank_tx bigint
);

CREATE TABLE risk_profiles (
    risk_profiles_id BIGINT AUTO_INCREMENT PRIMARY KEY,
    entity_type TEXT NOT NULL CHECK (entity_type IN ('player', 'corp')),
    entity_id bigint NOT NULL,
    risk_score bigint NOT NULL DEFAULT 0
);

CREATE TABLE loans (
    loans_id BIGINT AUTO_INCREMENT PRIMARY KEY,
    lender_type TEXT NOT NULL CHECK (lender_type IN ('player', 'corp', 'bank')),
    lender_id bigint,
    borrower_type TEXT NOT NULL CHECK (borrower_type IN ('player', 'corp')),
    borrower_id bigint NOT NULL,
    principal bigint NOT NULL CHECK (principal > 0),
    rate_bps bigint NOT NULL DEFAULT 0 CHECK (rate_bps >= 0),
    term_days bigint NOT NULL CHECK (term_days > 0),
    next_due TEXT,
    status TEXT NOT NULL DEFAULT 'active' CHECK (status IN ('active', 'paid', 'defaulted', 'written_off')),
    created_ts TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE loan_payments (
    loan_payments_id BIGINT AUTO_INCREMENT PRIMARY KEY,
    loan_id bigint NOT NULL REFERENCES loans (loans_id) ON DELETE CASCADE,
    ts TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    amount bigint NOT NULL CHECK (amount > 0),
    status TEXT NOT NULL DEFAULT 'posted' CHECK (status IN ('posted', 'reversed')),
    bank_tx_id bigint
);

CREATE TABLE collateral (
    collateral_id BIGINT AUTO_INCREMENT PRIMARY KEY,
    loan_id bigint NOT NULL REFERENCES loans (loans_id) ON DELETE CASCADE,
    asset_type TEXT NOT NULL CHECK (asset_type IN ('ship', 'planet', 'cargo', 'stock', 'other')),
    asset_id bigint NOT NULL,
    appraised_value bigint NOT NULL DEFAULT 0 CHECK (appraised_value >= 0)
);

CREATE TABLE credit_ratings (
    entity_type TEXT NOT NULL CHECK (entity_type IN ('player', 'corp')),
    entity_id bigint NOT NULL,
    score bigint NOT NULL DEFAULT 600 CHECK (score BETWEEN 300 AND 900),
    last_update TEXT,
    PRIMARY KEY (entity_type, entity_id)
);

CREATE TABLE charters (
    charters_id BIGINT AUTO_INCREMENT PRIMARY KEY,
    `name` TEXT NOT NULL UNIQUE,
    granted_by TEXT NOT NULL DEFAULT 'federation',
    monopoly_scope TEXT,
    start_ts TEXT NOT NULL,
    expiry_ts TEXT
);

CREATE TABLE expeditions (
    expeditions_id BIGINT AUTO_INCREMENT PRIMARY KEY,
    leader_player_id bigint NOT NULL REFERENCES players (player_id) ON DELETE CASCADE,
    charter_id bigint REFERENCES charters (charters_id) ON DELETE SET NULL,
    goal TEXT NOT NULL,
    target_region TEXT,
    pledged_total bigint NOT NULL DEFAULT 0 CHECK (pledged_total >= 0),
    duration_days bigint NOT NULL DEFAULT 7 CHECK (duration_days > 0),
    status TEXT NOT NULL DEFAULT 'planning' CHECK (status IN ('planning', 'launched', 'complete', 'failed', 'aborted')),
    created_ts TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE expedition_backers (
    expedition_id bigint NOT NULL REFERENCES expeditions (expeditions_id) ON DELETE CASCADE,
    backer_type TEXT NOT NULL CHECK (backer_type IN ('player', 'corp')),
    backer_id bigint NOT NULL,
    pledged_amount bigint NOT NULL CHECK (pledged_amount >= 0),
    share_pct DOUBLE NOT NULL CHECK (share_pct >= 0),
    PRIMARY KEY (expedition_id, backer_type, backer_id)
);

CREATE TABLE expedition_returns (
    expedition_returns_id BIGINT AUTO_INCREMENT PRIMARY KEY,
    expedition_id bigint NOT NULL REFERENCES expeditions (expeditions_id) ON DELETE CASCADE,
    ts TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    amount bigint NOT NULL CHECK (amount >= 0),
    bank_tx_id bigint
);

CREATE TABLE futures_contracts (
    futures_contracts_id BIGINT AUTO_INCREMENT PRIMARY KEY,
    commodity_id bigint NOT NULL REFERENCES commodities (commodities_id) ON DELETE CASCADE,
    buyer_type TEXT NOT NULL CHECK (buyer_type IN ('player', 'corp')),
    buyer_id bigint NOT NULL,
    seller_type TEXT NOT NULL CHECK (seller_type IN ('player', 'corp')),
    seller_id bigint NOT NULL,
    strike_price bigint NOT NULL CHECK (strike_price >= 0),
    expiry_ts TEXT NOT NULL,
    status TEXT NOT NULL DEFAULT 'open' CHECK (status IN ('open', 'settled', 'defaulted', 'cancelled')),
    FOREIGN KEY (buyer_id) REFERENCES players (player_id) ON DELETE CASCADE,
    FOREIGN KEY (seller_id) REFERENCES players (player_id) ON DELETE CASCADE
);

CREATE TABLE warehouses (
    warehouses_id BIGINT AUTO_INCREMENT PRIMARY KEY,
    location_type TEXT NOT NULL CHECK (location_type IN ('sector', 'planet', 'port')),
    location_id bigint NOT NULL,
    owner_type TEXT NOT NULL CHECK (owner_type IN ('player', 'corp')),
    owner_id bigint NOT NULL
);

CREATE TABLE gov_accounts (
    gov_accounts_id BIGINT AUTO_INCREMENT PRIMARY KEY,
    `name` TEXT NOT NULL UNIQUE,
    balance bigint NOT NULL DEFAULT 0 CHECK (balance >= 0)
);

CREATE TABLE tax_policies (
    tax_policies_id BIGINT AUTO_INCREMENT PRIMARY KEY,
    `name` TEXT NOT NULL,
    tax_type TEXT NOT NULL CHECK (tax_type IN ('trade', 'income', 'corp', 'wealth', 'transfer')),
    rate_bps bigint NOT NULL DEFAULT 0 CHECK (rate_bps >= 0),
    active bigint NOT NULL DEFAULT 1 CHECK (active IN (0, 1))
);

CREATE TABLE tax_ledgers (
    tax_ledgers_id BIGINT AUTO_INCREMENT PRIMARY KEY,
    policy_id bigint NOT NULL REFERENCES tax_policies (tax_policies_id) ON DELETE CASCADE,
    payer_type TEXT NOT NULL CHECK (payer_type IN ('player', 'corp')),
    payer_id bigint NOT NULL,
    amount bigint NOT NULL CHECK (amount >= 0),
    ts TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    bank_tx_id bigint
);

CREATE TABLE fines (
    fines_id BIGINT AUTO_INCREMENT PRIMARY KEY,
    issued_by TEXT NOT NULL DEFAULT 'federation',
    recipient_type TEXT NOT NULL CHECK (recipient_type IN ('player', 'corp')),
    recipient_id bigint NOT NULL,
    reason TEXT,
    amount bigint NOT NULL CHECK (amount >= 0),
    status TEXT NOT NULL DEFAULT 'unpaid' CHECK (status IN ('unpaid', 'paid', 'void')),
    issued_ts TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    paid_bank_tx bigint
);

CREATE TABLE bounties (
    bounties_id BIGINT AUTO_INCREMENT PRIMARY KEY,
    posted_by_type TEXT NOT NULL CHECK (posted_by_type IN ('player', 'corp', 'gov', 'npc')),
    posted_by_id bigint,
    target_type TEXT NOT NULL CHECK (target_type IN ('player', 'corp', 'npc')),
    target_id bigint NOT NULL,
    reward bigint NOT NULL CHECK (reward >= 0),
    escrow_bank_tx bigint,
    status TEXT NOT NULL DEFAULT 'open' CHECK (status IN ('open', 'claimed', 'cancelled', 'expired')),
    posted_ts TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    claimed_by bigint,
    paid_bank_tx bigint
);

CREATE TABLE grants (
    grants_id BIGINT AUTO_INCREMENT PRIMARY KEY,
    `name` TEXT NOT NULL,
    recipient_type TEXT NOT NULL CHECK (recipient_type IN ('player', 'corp')),
    recipient_id bigint NOT NULL,
    amount bigint NOT NULL CHECK (amount >= 0),
    awarded_ts TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    bank_tx_id bigint
);

CREATE TABLE research_projects (
    research_projects_id BIGINT AUTO_INCREMENT PRIMARY KEY,
    sponsor_type TEXT NOT NULL CHECK (sponsor_type IN ('player', 'corp', 'gov')),
    sponsor_id bigint,
    title TEXT NOT NULL,
    field TEXT NOT NULL,
    cost BIGINT NOT NULL CHECK (
    COST >= 0),
    progress bigint NOT NULL DEFAULT 0 CHECK (progress BETWEEN 0 AND 100),
    status TEXT NOT NULL DEFAULT 'active' CHECK (status IN ('active', 'paused', 'complete', 'failed')),
    created_ts TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE research_contributors (
    project_id bigint NOT NULL REFERENCES research_projects (research_projects_id) ON DELETE CASCADE,
    actor_type TEXT NOT NULL CHECK (actor_type IN ('player', 'corp')),
    actor_id bigint NOT NULL,
    amount bigint NOT NULL CHECK (amount >= 0),
    PRIMARY KEY (project_id, actor_type, actor_id)
);

CREATE TABLE research_results (
    research_results_id BIGINT AUTO_INCREMENT PRIMARY KEY,
    project_id bigint NOT NULL REFERENCES research_projects (research_projects_id) ON DELETE CASCADE,
    blueprint_code TEXT NOT NULL,
    unlocked_ts TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE black_accounts (
    black_accounts_id BIGINT AUTO_INCREMENT PRIMARY KEY,
    owner_type TEXT NOT NULL CHECK (owner_type IN ('player', 'corp', 'npc')),
    owner_id bigint NOT NULL,
    balance bigint NOT NULL DEFAULT 0 CHECK (balance >= 0)
);

CREATE TABLE laundering_ops (
    laundering_ops_id BIGINT AUTO_INCREMENT PRIMARY KEY,
    from_black_id bigint REFERENCES black_accounts (black_accounts_id) ON DELETE SET NULL,
    to_player_id bigint REFERENCES players (player_id) ON DELETE SET NULL,
    amount bigint NOT NULL CHECK (amount > 0),
    risk_pct bigint NOT NULL DEFAULT 25 CHECK (risk_pct BETWEEN 0 AND 100),
    status TEXT NOT NULL DEFAULT 'pending' CHECK (status IN ('pending', 'cleaned', 'seized', 'failed')),
    ts TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE contracts_illicit (
    contracts_illicit_id BIGINT AUTO_INCREMENT PRIMARY KEY,
    contractor_type TEXT NOT NULL CHECK (contractor_type IN ('player', 'corp', 'npc')),
    contractor_id bigint NOT NULL,
    target_type TEXT NOT NULL CHECK (target_type IN ('player', 'corp', 'npc')),
    target_id bigint NOT NULL,
    reward bigint NOT NULL CHECK (reward >= 0),
    escrow_black_id bigint REFERENCES black_accounts (black_accounts_id) ON DELETE SET NULL,
    status TEXT NOT NULL DEFAULT 'open' CHECK (status IN ('open', 'fulfilled', 'failed', 'cancelled')),
    ts TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE fences (
    fences_id BIGINT AUTO_INCREMENT PRIMARY KEY,
    npc_id bigint,
    sector_id bigint,
    reputation bigint NOT NULL DEFAULT 0
);

CREATE TABLE economic_indicators (
    economic_indicators_id BIGINT AUTO_INCREMENT PRIMARY KEY,
    ts TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    inflation_bps bigint NOT NULL DEFAULT 0,
    liquidity bigint NOT NULL DEFAULT 0,
    credit_velocity DOUBLE NOT NULL DEFAULT 0.0
);

CREATE TABLE sector_gdp (
    sector_id BIGINT AUTO_INCREMENT PRIMARY KEY,
    gdp bigint NOT NULL DEFAULT 0,
    last_update TEXT
);

CREATE TABLE event_triggers (
    event_triggers_id BIGINT AUTO_INCREMENT PRIMARY KEY,
    `name` TEXT NOT NULL,
    condition_json TEXT NOT NULL,
    action_json TEXT NOT NULL
);

CREATE TABLE charities (
    charities_id BIGINT AUTO_INCREMENT PRIMARY KEY,
    `name` TEXT NOT NULL UNIQUE,
    description TEXT
);

CREATE TABLE donations (
    donations_id BIGINT AUTO_INCREMENT PRIMARY KEY,
    charity_id bigint NOT NULL REFERENCES charities (charities_id) ON DELETE CASCADE,
    donor_type TEXT NOT NULL CHECK (donor_type IN ('player', 'corp')),
    donor_id bigint NOT NULL,
    amount bigint NOT NULL CHECK (amount >= 0),
    ts TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    bank_tx_id bigint
);

CREATE TABLE temples (
    temples_id BIGINT AUTO_INCREMENT PRIMARY KEY,
    `name` TEXT NOT NULL UNIQUE,
    sector_id bigint,
    favour bigint NOT NULL DEFAULT 0
);

CREATE TABLE guilds (
    guilds_id BIGINT AUTO_INCREMENT PRIMARY KEY,
    `name` TEXT NOT NULL UNIQUE,
    description TEXT
);

CREATE TABLE guild_memberships (
    guild_id bigint NOT NULL REFERENCES guilds (guilds_id) ON DELETE CASCADE,
    member_type TEXT NOT NULL CHECK (member_type IN ('player', 'corp')),
    member_id bigint NOT NULL,
    `role` TEXT NOT NULL DEFAULT 'member',
    PRIMARY KEY (guild_id, member_type, member_id)
);

CREATE TABLE guild_dues (
    guild_dues_id BIGINT AUTO_INCREMENT PRIMARY KEY,
    guild_id bigint NOT NULL REFERENCES guilds (guilds_id) ON DELETE CASCADE,
    amount bigint NOT NULL CHECK (amount >= 0),
    period TEXT NOT NULL DEFAULT 'monthly' CHECK (period IN ('weekly', 'monthly', 'quarterly', 'yearly'))
);

CREATE TABLE economy_snapshots (
    economy_snapshots_id BIGINT AUTO_INCREMENT PRIMARY KEY,
    ts TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    money_supply bigint NOT NULL DEFAULT 0,
    total_deposits bigint NOT NULL DEFAULT 0,
    total_loans bigint NOT NULL DEFAULT 0,
    total_insured bigint NOT NULL DEFAULT 0,
    notes TEXT
);

CREATE TABLE ai_economy_agents (
    ai_economy_agents_id BIGINT AUTO_INCREMENT PRIMARY KEY,
    `name` TEXT NOT NULL,
    `role` TEXT NOT NULL,
    config_json TEXT NOT NULL
);

CREATE TABLE anomaly_reports (
    anomaly_reports_id BIGINT AUTO_INCREMENT PRIMARY KEY,
    ts TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    severity TEXT NOT NULL CHECK (severity IN ('low', 'medium', 'high', 'critical')),
    subject TEXT NOT NULL,
    details TEXT NOT NULL,
    resolved boolean NOT NULL DEFAULT FALSE
);

CREATE TABLE economy_policies (
    economy_policies_id BIGINT AUTO_INCREMENT PRIMARY KEY,
    `name` TEXT NOT NULL UNIQUE,
    config_json TEXT NOT NULL,
    active boolean NOT NULL DEFAULT TRUE
);

CREATE TABLE s2s_keys (
    key_id TEXT PRIMARY KEY,
    key_b64 TEXT NOT NULL,
    is_default_tx boolean NOT NULL DEFAULT FALSE,
    active boolean DEFAULT TRUE,
    created_ts TIMESTAMP NOT NULL
);

CREATE TABLE cron_tasks (
    cron_tasks_id BIGINT AUTO_INCREMENT PRIMARY KEY,
    `name` TEXT UNIQUE NOT NULL,
    schedule TEXT NOT NULL,
    last_run_at TIMESTAMP,
    next_due_at TIMESTAMP NOT NULL,
    enabled boolean DEFAULT TRUE,
    payload TEXT
);

CREATE TABLE engine_events (
    engine_events_id BIGINT AUTO_INCREMENT PRIMARY KEY,
    ts TIMESTAMP NOT NULL,
    `type` TEXT NOT NULL,
    actor_player_id bigint,
    sector_id bigint,
    payload TEXT NOT NULL,
    idem_key TEXT,
    processed_at TIMESTAMP
);

CREATE TABLE engine_offset (
    `key` TEXT PRIMARY KEY,
    last_event_id bigint NOT NULL,
    last_event_ts TIMESTAMP NOT NULL
);

CREATE TABLE engine_events_deadletter (
    engine_events_deadletter_id BIGINT AUTO_INCREMENT PRIMARY KEY,
    ts TIMESTAMP NOT NULL,
    `type` TEXT NOT NULL,
    payload TEXT NOT NULL,
    error TEXT NOT NULL,
    moved_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE engine_commands (
    engine_commands_id BIGINT AUTO_INCREMENT PRIMARY KEY,
    `type` TEXT NOT NULL,
    payload TEXT NOT NULL,
    status TEXT NOT NULL DEFAULT 'ready',
    priority bigint NOT NULL DEFAULT 100,
    attempts bigint NOT NULL DEFAULT 0,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    due_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    started_at TIMESTAMP,
    finished_at TIMESTAMP,
    worker TEXT,
    idem_key TEXT
);

CREATE TABLE engine_audit (
    engine_audit_id BIGINT AUTO_INCREMENT PRIMARY KEY,
    ts bigint NOT NULL,
    cmd_type TEXT NOT NULL,
    correlation_id TEXT,
    actor_player_id bigint,
    details TEXT
);

CREATE TABLE news_feed (
    news_id BIGINT AUTO_INCREMENT PRIMARY KEY,
    published_ts bigint NOT NULL,
    news_category TEXT NOT NULL,
    article_text TEXT NOT NULL,
    author_id bigint,
    source_ids TEXT
);

CREATE TABLE eligible_tows (
    ship_id BIGINT AUTO_INCREMENT PRIMARY KEY,
    sector_id bigint,
    owner_id bigint,
    fighters bigint,
    alignment bigint,
    experience bigint
);

-- Player Knowledge Tables (Computer System)
CREATE TABLE player_known_ports (
    player_id BIGINT NOT NULL,
    port_id BIGINT NOT NULL,
    updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    PRIMARY KEY (player_id, port_id),
    FOREIGN KEY (player_id) REFERENCES players(player_id) ON DELETE CASCADE,
    FOREIGN KEY (port_id) REFERENCES ports(port_id) ON DELETE CASCADE
);

CREATE TABLE player_visited_sectors (
    player_id BIGINT NOT NULL,
    sector_id BIGINT NOT NULL,
    visit_count INT NOT NULL DEFAULT 1,
    first_visited_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    last_visited_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (player_id, sector_id),
    FOREIGN KEY (player_id) REFERENCES players(player_id) ON DELETE CASCADE,
    FOREIGN KEY (sector_id) REFERENCES sectors(sector_id) ON DELETE CASCADE
);

