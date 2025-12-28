-- Generated from sqlite_schema.sql -> Postgres

CREATE TABLE config ( key TEXT PRIMARY KEY, value TEXT NOT NULL, type TEXT NOT NULL CHECK (type IN ('int', 'bool', 'string', 'double')) );

CREATE TABLE sessions ( token TEXT PRIMARY KEY, player_id BIGINT NOT NULL, expires BIGINT NOT NULL, created_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP);

CREATE TABLE idempotency ( key TEXT PRIMARY KEY, cmd TEXT NOT NULL, req_fp TEXT NOT NULL, response TEXT, created_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP, updated_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP);

CREATE TABLE locks ( lock_name TEXT PRIMARY KEY, owner TEXT, until_ms BIGINT);

CREATE TABLE engine_state ( state_key TEXT PRIMARY KEY, state_val TEXT NOT NULL);

CREATE TABLE sectors (id BIGSERIAL PRIMARY KEY, name TEXT, beacon TEXT, nebulae TEXT);

CREATE TABLE commission ( id BIGSERIAL PRIMARY KEY, is_evil BOOLEAN NOT NULL DEFAULT FALSE CHECK (is_evil IN (TRUE, FALSE)), min_exp BIGINT NOT NULL, description TEXT NOT NULL );

CREATE TABLE players (
    id BIGSERIAL PRIMARY KEY,
    type BIGINT DEFAULT 2,
    number BIGINT,
    name TEXT NOT NULL,
    passwd TEXT NOT NULL,
    sector BIGINT DEFAULT 1,
    ship BIGINT,
    experience BIGINT DEFAULT 0,
    alignment BIGINT DEFAULT 0,
    commission BIGINT DEFAULT 1,
    credits BIGINT DEFAULT 1500,
    flags BIGINT,
    login_time TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
    last_update TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
    times_blown_up BIGINT NOT NULL DEFAULT 0,
    intransit BOOLEAN,
    beginmove BIGINT,
    movingto BIGINT,
    loggedin TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
    lastplanet BIGINT,
    score BIGINT,
    is_npc BOOLEAN DEFAULT FALSE,
    last_news_read_timestamp TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY (commission) REFERENCES commission(id)
);

CREATE TABLE player_types (type BIGSERIAL PRIMARY KEY, description TEXT);

CREATE TABLE corporations ( id BIGSERIAL PRIMARY KEY, name TEXT NOT NULL, owner_id BIGINT, tag TEXT,
description TEXT, tax_arrears BIGINT NOT NULL DEFAULT 0,
credit_rating BIGINT NOT NULL DEFAULT 0,
created_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
updated_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
CHECK (tag IS NULL OR (length(tag) BETWEEN 2 AND 5 AND tag ~ '^[A-Za-z0-9].*$')) );

CREATE TABLE corp_members ( corp_id BIGINT NOT NULL, player_id BIGINT NOT NULL, role TEXT NOT NULL DEFAULT 'Member',
join_date TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP, 
PRIMARY KEY (corp_id, player_id), FOREIGN KEY(corp_id) REFERENCES corporations(id) ON DELETE CASCADE ON UPDATE CASCADE, CHECK (role IN ('Leader','Officer','Member')) );

CREATE TABLE corp_mail ( id BIGSERIAL PRIMARY KEY, corp_id BIGINT NOT NULL, sender_id BIGINT, subject TEXT, body TEXT NOT NULL, posted_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,  FOREIGN KEY(corp_id) REFERENCES corporations(id) ON DELETE CASCADE, FOREIGN KEY(sender_id) REFERENCES players(id) ON DELETE SET NULL );


CREATE TABLE corp_mail_cursors ( corp_id BIGINT NOT NULL, player_id BIGINT NOT NULL, last_seen_id BIGINT NOT NULL DEFAULT 0, PRIMARY KEY (corp_id, player_id), FOREIGN KEY(corp_id) REFERENCES corporations(id) ON DELETE CASCADE, FOREIGN KEY(player_id) REFERENCES players(id) ON DELETE CASCADE );

CREATE TABLE corp_log ( id BIGSERIAL PRIMARY KEY, corp_id BIGINT NOT NULL, actor_id BIGINT, event_type TEXT NOT NULL, payload TEXT NOT NULL,
created_at TIMESTAMPTZ  NOT NULL DEFAULT CURRENT_TIMESTAMP,
FOREIGN KEY(corp_id) REFERENCES corporations(id) ON DELETE CASCADE, FOREIGN KEY(actor_id) REFERENCES players(id) ON DELETE SET NULL );

CREATE TABLE economy_curve ( id BIGSERIAL PRIMARY KEY, curve_name TEXT NOT NULL UNIQUE, base_restock_rate DOUBLE PRECISION NOT NULL, price_elasticity DOUBLE PRECISION NOT NULL, target_stock BIGINT NOT NULL, volatility_factor DOUBLE PRECISION NOT NULL );

CREATE TABLE alignment_band (
id BIGSERIAL PRIMARY KEY ,
code TEXT NOT NULL UNIQUE,
name TEXT NOT NULL,
min_align BIGINT NOT NULL ,
max_align BIGINT NOT NULL ,
is_good BOOLEAN NOT NULL DEFAULT TRUE,
is_evil BOOLEAN NOT NULL DEFAULT FALSE,
can_buy_iss BOOLEAN NOT NULL DEFAULT TRUE,
can_rob_ports BOOLEAN NOT NULL DEFAULT FALSE,
notes TEXT );

CREATE TABLE trade_idempotency ( key TEXT PRIMARY KEY, player_id BIGINT NOT NULL, sector_id BIGINT NOT NULL, request_json TEXT NOT NULL, response_json TEXT NOT NULL, created_at  TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP );

CREATE TABLE used_sectors (used BIGINT);

CREATE TABLE npc_shipnames (id BIGINT, name TEXT);

CREATE TABLE tavern_names ( id BIGSERIAL PRIMARY KEY, name TEXT NOT NULL UNIQUE, enabled BOOLEAN DEFAULT TRUE, weight BIGINT NOT NULL DEFAULT 1 );

CREATE TABLE taverns ( sector_id BIGSERIAL PRIMARY KEY REFERENCES sectors(id), name_id BIGINT NOT NULL REFERENCES tavern_names(id), enabled BIGINT NOT NULL DEFAULT 1 );

CREATE TABLE tavern_settings ( id BIGSERIAL PRIMARY KEY CHECK (id = 1), max_bet_per_transaction BIGINT NOT NULL DEFAULT 5000, daily_max_wager BIGINT NOT NULL DEFAULT 50000, enable_dynamic_wager_limit BIGINT NOT NULL DEFAULT 0, graffiti_max_posts BIGINT NOT NULL DEFAULT 100, notice_expires_days BIGINT NOT NULL DEFAULT 7, buy_round_cost BIGINT NOT NULL DEFAULT 1000, buy_round_alignment_gain BIGINT NOT NULL DEFAULT 5, loan_shark_enabled BIGINT NOT NULL DEFAULT 1 );

CREATE TABLE tavern_lottery_state ( draw_date TEXT PRIMARY KEY, winning_number BIGINT, jackpot BIGINT NOT NULL, carried_over BIGINT NOT NULL DEFAULT 0 );

CREATE TABLE tavern_lottery_tickets ( id BIGSERIAL PRIMARY KEY, draw_date TEXT NOT NULL, player_id BIGINT NOT NULL REFERENCES players(id), number BIGINT NOT NULL, cost BIGINT NOT NULL, purchased_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP );

CREATE TABLE tavern_deadpool_bets ( id BIGSERIAL PRIMARY KEY, bettor_id BIGINT NOT NULL REFERENCES players(id), target_id BIGINT NOT NULL REFERENCES players(id), amount BIGINT NOT NULL, odds_bp BIGINT NOT NULL, placed_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP, expires_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP, resolved BIGINT NOT NULL DEFAULT 0, resolved_at BIGINT, result TEXT );

CREATE TABLE tavern_raffle_state ( id BIGSERIAL PRIMARY KEY CHECK (id = 1), pot BIGINT NOT NULL, last_winner_id BIGINT, last_payout BIGINT, last_win_ts BIGINT );

CREATE TABLE tavern_graffiti ( id BIGSERIAL PRIMARY KEY, player_id BIGINT NOT NULL REFERENCES players(id), text TEXT NOT NULL, created_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP );

CREATE TABLE tavern_notices ( id BIGSERIAL PRIMARY KEY, author_id BIGINT NOT NULL REFERENCES players(id), corp_id BIGINT, text TEXT NOT NULL, created_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP, expires_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP );

CREATE TABLE corp_recruiting ( corp_id BIGSERIAL PRIMARY KEY REFERENCES corporations(id), tagline TEXT NOT NULL, min_alignment BIGINT, play_style TEXT, created_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP, expires_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP );

CREATE TABLE corp_invites ( id BIGSERIAL PRIMARY KEY, corp_id BIGINT NOT NULL, player_id BIGINT NOT NULL, invited_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP, expires_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP, UNIQUE(corp_id, player_id), FOREIGN KEY(corp_id) REFERENCES corporations(id) ON DELETE CASCADE, FOREIGN KEY(player_id) REFERENCES players(id) ON DELETE CASCADE );

CREATE TABLE tavern_loans ( player_id BIGSERIAL PRIMARY KEY REFERENCES players(id), principal BIGINT NOT NULL, interest_rate BIGINT NOT NULL, due_date BIGINT NOT NULL, is_defaulted BIGINT NOT NULL DEFAULT 0 );

CREATE TABLE planettypes (id BIGSERIAL PRIMARY KEY, code TEXT UNIQUE, typeDescription TEXT, typeName TEXT, citadelUpgradeTime_lvl1 BIGINT, citadelUpgradeTime_lvl2 BIGINT, citadelUpgradeTime_lvl3 BIGINT, citadelUpgradeTime_lvl4 BIGINT, citadelUpgradeTime_lvl5 BIGINT, citadelUpgradeTime_lvl6 BIGINT, citadelUpgradeOre_lvl1 BIGINT, citadelUpgradeOre_lvl2 BIGINT, citadelUpgradeOre_lvl3 BIGINT, citadelUpgradeOre_lvl4 BIGINT, citadelUpgradeOre_lvl5 BIGINT, citadelUpgradeOre_lvl6 BIGINT, citadelUpgradeOrganics_lvl1 BIGINT, citadelUpgradeOrganics_lvl2 BIGINT, citadelUpgradeOrganics_lvl3 BIGINT, citadelUpgradeOrganics_lvl4 BIGINT, citadelUpgradeOrganics_lvl5 BIGINT, citadelUpgradeOrganics_lvl6 BIGINT, citadelUpgradeEquipment_lvl1 BIGINT, citadelUpgradeEquipment_lvl2 BIGINT, citadelUpgradeEquipment_lvl3 BIGINT, citadelUpgradeEquipment_lvl4 BIGINT, citadelUpgradeEquipment_lvl5 BIGINT, citadelUpgradeEquipment_lvl6 BIGINT, citadelUpgradeColonist_lvl1 BIGINT, citadelUpgradeColonist_lvl2 BIGINT, citadelUpgradeColonist_lvl3 BIGINT, citadelUpgradeColonist_lvl4 BIGINT, citadelUpgradeColonist_lvl5 BIGINT, citadelUpgradeColonist_lvl6 BIGINT, maxColonist_ore BIGINT, maxColonist_organics BIGINT, maxColonist_equipment BIGINT, fighters BIGINT, fuelProduction BIGINT, organicsProduction BIGINT, equipmentProduction BIGINT, fighterProduction BIGINT, maxore BIGINT, maxorganics BIGINT, maxequipment BIGINT, maxfighters BIGINT, breeding DOUBLE PRECISION, genesis_weight BIGINT NOT NULL DEFAULT 10);

CREATE TABLE ports (
id BIGSERIAL PRIMARY KEY,
number BIGINT,
name TEXT NOT NULL,
sector BIGINT NOT NULL,
size BIGINT,
techlevel BIGINT,
petty_cash BIGINT NOT NULL DEFAULT 0,
invisible BOOLEAN DEFAULT FALSE, type BIGINT DEFAULT 1, economy_curve_id BIGINT NOT NULL DEFAULT 1, FOREIGN KEY (economy_curve_id) REFERENCES economy_curve(id), FOREIGN KEY (sector) REFERENCES sectors(id));

CREATE TABLE port_trade ( id BIGSERIAL PRIMARY KEY, port_id BIGINT NOT NULL, maxproduct BIGINT, commodity TEXT CHECK(commodity IN ('ore','organics','equipment')), mode TEXT CHECK(mode IN ('buy','sell')), FOREIGN KEY (port_id) REFERENCES ports(id));


CREATE TABLE sector_warps (from_sector BIGINT, to_sector BIGINT, PRIMARY KEY (from_sector, to_sector), FOREIGN KEY (from_sector) REFERENCES sectors(id) ON DELETE CASCADE, FOREIGN KEY (to_sector) REFERENCES sectors(id) ON DELETE CASCADE);

CREATE TABLE shiptypes ( id BIGSERIAL PRIMARY KEY, name TEXT NOT NULL UNIQUE,
basecost BIGINT,
required_alignment BIGINT,
required_commission BIGINT,
required_experience BIGINT,
maxattack BIGINT,
initialholds BIGINT,
maxholds BIGINT,
maxfighters BIGINT,
turns BIGINT,
maxmines BIGINT,
maxlimpets BIGINT,
maxgenesis BIGINT,
max_detonators BIGINT NOT NULL DEFAULT 0,
max_probes BIGINT NOT NULL DEFAULT 0,
can_transwarp  BOOLEAN DEFAULT FALSE,
transportrange BIGINT,
maxshields BIGINT,
offense BIGINT,
defense BIGINT,
maxbeacons BIGINT,
can_long_range_scan BOOLEAN DEFAULT FALSE,
can_planet_scan BOOLEAN DEFAULT FALSE,
maxphotons BIGINT,
max_cloaks BIGINT NOT NULL DEFAULT 0,
can_purchase BOOLEAN DEFAULT TRUE,
has_escape_pod BOOLEAN NOT NULL DEFAULT FALSE,
enabled BOOLEAN DEFAULT TRUE, FOREIGN KEY (required_commission) REFERENCES commission(id) );

CREATE TABLE ships ( id BIGSERIAL PRIMARY KEY, name TEXT NOT NULL, type_id BIGINT, attack BIGINT, holds BIGINT DEFAULT 1, mines BIGINT, limpets BIGINT, fighters BIGINT DEFAULT 1, genesis BIGINT, detonators BIGINT NOT NULL DEFAULT 0, probes BIGINT NOT NULL DEFAULT 0, photons BIGINT, sector BIGINT, shields BIGINT DEFAULT 1, installed_shields BIGINT DEFAULT 1, beacons BIGINT, colonists BIGINT, equipment BIGINT, organics BIGINT, ore BIGINT, slaves BIGINT DEFAULT 0, weapons BIGINT DEFAULT 0, drugs BIGINT DEFAULT 0, flags BIGINT, cloaking_devices BIGINT, has_transwarp BIGINT NOT NULL DEFAULT 0, has_planet_scanner BIGINT NOT NULL DEFAULT 0, has_long_range_scanner BIGINT NOT NULL DEFAULT 0, cloaked TIMESTAMP, ported BIGINT, onplanet BIGINT, destroyed BIGINT DEFAULT 0, hull BIGINT NOT NULL DEFAULT 100, perms BIGINT NOT NULL DEFAULT 731, CONSTRAINT check_current_cargo_limit CHECK ( (colonists + equipment + organics + ore) <= holds ), FOREIGN KEY(type_id) REFERENCES shiptypes(id), FOREIGN KEY(sector) REFERENCES sectors(id) );

CREATE TABLE ship_markers ( ship_id BIGINT NOT NULL REFERENCES ships(id), owner_player BIGINT NOT NULL, owner_corp BIGINT NOT NULL DEFAULT 0, marker_type TEXT NOT NULL, PRIMARY KEY (ship_id, owner_player, marker_type) );

CREATE TABLE ship_roles ( role_id BIGSERIAL PRIMARY KEY, role TEXT, role_description TEXT);

CREATE TABLE ship_ownership ( ship_id BIGINT NOT NULL, player_id BIGINT NOT NULL, role_id BIGINT NOT NULL, is_primary BOOLEAN DEFAULT TRUE,
acquired_at TIMESTAMPTZ  NOT NULL DEFAULT CURRENT_TIMESTAMP, 
PRIMARY KEY (ship_id, player_id, role_id), FOREIGN KEY(ship_id) REFERENCES ships(id), FOREIGN KEY(player_id) REFERENCES players(id));

CREATE TABLE planets ( id BIGSERIAL PRIMARY KEY, num BIGINT, sector BIGINT NOT NULL, name TEXT NOT NULL, owner_id BIGINT NOT NULL, owner_type TEXT NOT NULL DEFAULT 'player', class TEXT NOT NULL DEFAULT 'M', population BIGINT, type BIGINT, creator TEXT, colonist BIGINT, fighters BIGINT,
created_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
created_by BIGINT NOT NULL,
genesis_flag BOOLEAN,
citadel_level BIGINT DEFAULT 0, ore_on_hand BIGINT NOT NULL DEFAULT 0, organics_on_hand BIGINT NOT NULL DEFAULT 0, equipment_on_hand BIGINT NOT NULL DEFAULT 0, colonists_ore BIGINT NOT NULL DEFAULT 0, colonists_org BIGINT NOT NULL DEFAULT 0, colonists_eq BIGINT NOT NULL DEFAULT 0, colonists_mil BIGINT NOT NULL DEFAULT 0, colonists_unassigned BIGINT NOT NULL DEFAULT 0, terraform_turns_left BIGINT NOT NULL DEFAULT 1, FOREIGN KEY (sector) REFERENCES sectors(id), FOREIGN KEY (type) REFERENCES planettypes(id) );


------------------ *******************************************



CREATE TABLE citadel_requirements ( planet_type_id BIGINT NOT NULL REFERENCES planettypes(id) ON DELETE CASCADE, citadel_level BIGINT NOT NULL, ore_cost BIGINT NOT NULL DEFAULT 0, organics_cost BIGINT NOT NULL DEFAULT 0, equipment_cost BIGINT NOT NULL DEFAULT 0, colonist_cost BIGINT NOT NULL DEFAULT 0, time_cost_days BIGINT NOT NULL DEFAULT 0, PRIMARY KEY (planet_type_id, citadel_level) );

CREATE TABLE hardware_items ( id BIGSERIAL PRIMARY KEY,
code TEXT UNIQUE NOT NULL,
name TEXT NOT NULL,
price BIGINT NOT NULL,
requires_stardock BOOLEAN DEFAULT TRUE,
sold_in_class0  BOOLEAN DEFAULT TRUE,
max_per_ship BIGINT,
category TEXT NOT NULL,
enabled  BOOLEAN DEFAULT FALSE );

CREATE TABLE citadels ( id BIGSERIAL PRIMARY KEY, planet_id BIGINT UNIQUE NOT NULL, level BIGINT, treasury BIGINT, militaryReactionLevel BIGINT, qCannonAtmosphere BIGINT, qCannonSector BIGINT, planetaryShields BIGINT, transporterlvl BIGINT, interdictor BIGINT, upgradePercent DOUBLE PRECISION, upgradestart BIGINT, owner BIGINT, shields BIGINT, torps BIGINT, fighters BIGINT, qtorps BIGINT, qcannon BIGINT, qcannontype BIGINT, qtorpstype BIGINT, military BIGINT, construction_start_time BIGINT DEFAULT 0, construction_end_time BIGINT DEFAULT 0, target_level BIGINT DEFAULT 0, construction_status TEXT DEFAULT 'idle', FOREIGN KEY (planet_id) REFERENCES planets(id) ON DELETE CASCADE, FOREIGN KEY (owner) REFERENCES players(id) );

CREATE TABLE turns( player BIGINT NOT NULL, turns_remaining BIGINT NOT NULL, last_update TIMESTAMP NOT NULL, PRIMARY KEY (player), FOREIGN KEY (player) REFERENCES players(id) ON DELETE CASCADE );

CREATE TABLE mail ( id BIGSERIAL PRIMARY KEY, thread_id BIGINT, sender_id BIGINT NOT NULL, recipient_id BIGINT NOT NULL, subject TEXT, body TEXT NOT NULL, sent_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
read_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
archived TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
deleted TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
idempotency_key TEXT, FOREIGN KEY(sender_id) REFERENCES players(id) ON DELETE CASCADE, FOREIGN KEY(recipient_id) REFERENCES players(id) ON DELETE CASCADE );

CREATE TABLE subspace ( id BIGSERIAL PRIMARY KEY, sender_id BIGINT, message TEXT NOT NULL, kind TEXT NOT NULL DEFAULT 'chat',
posted_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
FOREIGN KEY(sender_id) REFERENCES players(id) ON DELETE SET NULL );

CREATE TABLE subspace_cursors ( player_id BIGSERIAL PRIMARY KEY, last_seen_id BIGINT NOT NULL DEFAULT 0, FOREIGN KEY(player_id) REFERENCES players(id) ON DELETE CASCADE );

CREATE TABLE system_events ( id BIGSERIAL PRIMARY KEY, scope TEXT NOT NULL, event_type TEXT NOT NULL, payload TEXT NOT NULL,
created_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP);

CREATE TABLE subscriptions ( id BIGSERIAL PRIMARY KEY, player_id BIGINT NOT NULL, event_type TEXT NOT NULL, delivery TEXT NOT NULL, filter_json TEXT, ephemeral BIGINT NOT NULL DEFAULT 0, locked BIGINT NOT NULL DEFAULT 0, enabled BIGINT NOT NULL DEFAULT 1, UNIQUE(player_id, event_type), FOREIGN KEY(player_id) REFERENCES players(id) ON DELETE CASCADE );

CREATE TABLE player_block ( blocker_id BIGINT NOT NULL, blocked_id BIGINT NOT NULL,
created_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
PRIMARY KEY (blocker_id, blocked_id));

CREATE TABLE notice_seen ( notice_id BIGINT NOT NULL, player_id BIGINT NOT NULL,
seen_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
PRIMARY KEY (notice_id, player_id));

CREATE TABLE system_notice ( id BIGSERIAL PRIMARY KEY,
created_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
title TEXT NOT NULL, body TEXT NOT NULL, severity TEXT NOT NULL CHECK(severity IN ('info','warn','error')),
expires_at TIMESTAMPTZ );

CREATE TABLE player_prefs ( player_id BIGINT NOT NULL , key TEXT NOT NULL, type TEXT NOT NULL CHECK (type IN ('bool','int','string','json')) , value TEXT NOT NULL,
updated_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
PRIMARY KEY (player_id, key) , FOREIGN KEY (player_id) REFERENCES players(id) ON DELETE CASCADE );

CREATE TABLE player_bookmarks ( id BIGSERIAL PRIMARY KEY , player_id BIGINT NOT NULL , name TEXT NOT NULL, sector_id BIGINT NOT NULL ,
updated_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
UNIQUE(player_id, name) , FOREIGN KEY (player_id) REFERENCES players(id) ON DELETE CASCADE , FOREIGN KEY (sector_id) REFERENCES sectors(id) ON DELETE CASCADE );

CREATE TABLE player_avoid ( player_id BIGINT NOT NULL , sector_id BIGINT NOT NULL ,
updated_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
PRIMARY KEY (player_id, sector_id) , FOREIGN KEY (player_id) REFERENCES players(id) ON DELETE CASCADE , FOREIGN KEY (sector_id) REFERENCES sectors(id) ON DELETE CASCADE );

CREATE TABLE player_notes ( id BIGSERIAL PRIMARY KEY , player_id BIGINT NOT NULL , scope TEXT NOT NULL, key TEXT NOT NULL, note TEXT NOT NULL ,
updated_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
UNIQUE(player_id, scope, key) , FOREIGN KEY (player_id) REFERENCES players(id) ON DELETE CASCADE );

CREATE TABLE sector_assets ( id BIGSERIAL PRIMARY KEY, sector BIGINT NOT NULL REFERENCES sectors(id), player BIGINT REFERENCES players(id), corporation BIGINT NOT NULL DEFAULT 0, asset_type BIGINT NOT NULL, offensive_setting BIGINT DEFAULT 0, quantity BIGINT, ttl BIGINT, deployed_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP );

CREATE TABLE limpet_attached ( id BIGSERIAL PRIMARY KEY, ship_id BIGINT NOT NULL, owner_player_id BIGINT NOT NULL, created_ts BIGINT NOT NULL, UNIQUE(ship_id, owner_player_id), FOREIGN KEY(ship_id) REFERENCES ships(id) ON DELETE CASCADE, FOREIGN KEY(owner_player_id) REFERENCES players(id) ON DELETE CASCADE );

CREATE TABLE msl_sectors ( sector_id BIGSERIAL PRIMARY KEY REFERENCES sectors(id));

CREATE TABLE trade_log ( id BIGSERIAL PRIMARY KEY, player_id BIGINT NOT NULL, port_id BIGINT NOT NULL, sector_id BIGINT NOT NULL, commodity TEXT NOT NULL, units BIGINT NOT NULL, price_per_unit DOUBLE PRECISION NOT NULL, action TEXT CHECK(action IN ('buy', 'sell')) NOT NULL, timestamp BIGINT NOT NULL, FOREIGN KEY (player_id) REFERENCES players(id), FOREIGN KEY (port_id) REFERENCES ports(id), FOREIGN KEY (sector_id) REFERENCES sectors(id));

CREATE TABLE stardock_assets ( sector_id BIGSERIAL PRIMARY KEY, owner_id BIGINT NOT NULL, fighters BIGINT NOT NULL DEFAULT 0, defenses BIGINT NOT NULL DEFAULT 0, ship_capacity BIGINT NOT NULL DEFAULT 1, created_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP, FOREIGN KEY (sector_id) REFERENCES sectors(id), FOREIGN KEY (owner_id) REFERENCES players(id));

CREATE TABLE shipyard_inventory ( port_id BIGINT NOT NULL REFERENCES ports(id), ship_type_id BIGINT NOT NULL REFERENCES shiptypes(id), enabled BIGINT NOT NULL DEFAULT 1, PRIMARY KEY (port_id, ship_type_id));

CREATE TABLE podded_status ( player_id BIGSERIAL PRIMARY KEY REFERENCES players(id), status TEXT NOT NULL DEFAULT 'active', big_sleep_until BIGINT, reason TEXT, podded_count_today BIGINT NOT NULL DEFAULT 0, podded_last_reset BIGINT );

CREATE TABLE planet_goods ( planet_id BIGINT NOT NULL, commodity TEXT NOT NULL CHECK(commodity IN ('ore', 'organics', 'equipment', 'food', 'fuel')), quantity BIGINT NOT NULL DEFAULT 0, max_capacity BIGINT NOT NULL, production_rate BIGINT NOT NULL, PRIMARY KEY (planet_id, commodity), FOREIGN KEY (planet_id) REFERENCES planets(id));

CREATE TABLE clusters ( id BIGSERIAL PRIMARY KEY, name TEXT NOT NULL UNIQUE, role TEXT NOT NULL, kind TEXT NOT NULL, center_sector BIGINT, law_severity BIGINT NOT NULL DEFAULT 1, alignment BIGINT NOT NULL DEFAULT 0, created_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP);

CREATE TABLE cluster_sectors ( cluster_id BIGINT NOT NULL, sector_id BIGINT NOT NULL, PRIMARY KEY (cluster_id, sector_id), FOREIGN KEY (cluster_id) REFERENCES clusters(id), FOREIGN KEY (sector_id) REFERENCES sectors(id));

CREATE TABLE cluster_commodity_index ( cluster_id BIGINT NOT NULL, commodity_code TEXT NOT NULL, mid_price BIGINT NOT NULL, last_updated TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP, PRIMARY KEY (cluster_id, commodity_code), FOREIGN KEY (cluster_id) REFERENCES clusters(id));

CREATE TABLE cluster_player_status ( cluster_id BIGINT NOT NULL, player_id BIGINT NOT NULL, suspicion BIGINT NOT NULL DEFAULT 0, bust_count BIGINT NOT NULL DEFAULT 0, last_bust_at TEXT, wanted_level BIGINT NOT NULL DEFAULT 0, banned BIGINT NOT NULL DEFAULT 0, PRIMARY KEY (cluster_id, player_id), FOREIGN KEY (cluster_id) REFERENCES clusters(id), FOREIGN KEY (player_id) REFERENCES players(id));

CREATE TABLE law_enforcement ( id BIGSERIAL PRIMARY KEY CHECK (id = 1), robbery_evil_threshold BIGINT DEFAULT -10, robbery_xp_per_hold BIGINT DEFAULT 20, robbery_credits_per_xp BIGINT DEFAULT 10, robbery_bust_chance_base DOUBLE PRECISION DEFAULT 0.05, robbery_turn_cost BIGINT DEFAULT 1, good_guy_bust_bonus DOUBLE PRECISION DEFAULT 0.10, pro_criminal_bust_delta DOUBLE PRECISION DEFAULT -0.02, evil_cluster_bust_bonus DOUBLE PRECISION DEFAULT 0.05, good_align_penalty_mult DOUBLE PRECISION DEFAULT 3.0, robbery_real_bust_ttl_days BIGINT DEFAULT 7);

CREATE TABLE port_busts ( port_id BIGINT NOT NULL, player_id BIGINT NOT NULL, last_bust_at BIGINT NOT NULL, bust_type TEXT NOT NULL, active BIGINT NOT NULL DEFAULT 1, PRIMARY KEY (port_id, player_id), FOREIGN KEY (port_id) REFERENCES ports(id), FOREIGN KEY (player_id) REFERENCES players(id));

CREATE TABLE player_last_rob ( player_id BIGSERIAL PRIMARY KEY, port_id BIGINT NOT NULL, last_attempt_at BIGINT NOT NULL, was_success BIGINT NOT NULL);

CREATE TABLE currencies ( code TEXT PRIMARY KEY,
name TEXT NOT NULL,
minor_unit BIGINT NOT NULL DEFAULT 1 CHECK (minor_unit > 0),
is_default BOOLEAN NOT NULL DEFAULT FALSE );

CREATE TABLE commodities ( id BIGSERIAL PRIMARY KEY, code TEXT UNIQUE NOT NULL, name TEXT NOT NULL, illegal BIGINT NOT NULL DEFAULT 0, base_price BIGINT NOT NULL DEFAULT 0 CHECK (base_price >= 0), volatility BIGINT NOT NULL DEFAULT 0 CHECK (volatility >= 0) );

CREATE TABLE commodity_orders ( id BIGSERIAL PRIMARY KEY, actor_type TEXT NOT NULL CHECK (actor_type IN ('player','corp','npc_planet','port')), actor_id BIGINT NOT NULL, location_type TEXT NOT NULL CHECK (location_type IN ('planet','port')), location_id BIGINT NOT NULL, commodity_id BIGINT NOT NULL REFERENCES commodities(id) ON DELETE CASCADE, side TEXT NOT NULL CHECK (side IN ('buy','sell')), quantity BIGINT NOT NULL CHECK (quantity > 0), filled_quantity BIGINT NOT NULL DEFAULT 0 CHECK (filled_quantity >= 0), price BIGINT NOT NULL CHECK (price >= 0), status TEXT NOT NULL DEFAULT 'open' CHECK (status IN ('open','filled','cancelled','expired')), ts TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP  );

CREATE TABLE commodity_trades ( id BIGSERIAL PRIMARY KEY, commodity_id BIGINT NOT NULL REFERENCES commodities(id) ON DELETE CASCADE, buyer_actor_type TEXT NOT NULL CHECK (buyer_actor_type IN ('player','corp','npc_planet','port')), buyer_actor_id BIGINT NOT NULL, buyer_location_type TEXT NOT NULL CHECK (buyer_location_type IN ('planet','port')), buyer_location_id BIGINT NOT NULL, seller_actor_type TEXT NOT NULL CHECK (seller_actor_type IN ('player','corp','npc_planet','port')), seller_actor_id BIGINT NOT NULL, seller_location_type TEXT NOT NULL CHECK (seller_location_type IN ('planet','port')), seller_location_id BIGINT NOT NULL, quantity BIGINT NOT NULL CHECK (quantity > 0), price BIGINT NOT NULL CHECK (price >= 0), ts TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP, settlement_tx_buy BIGINT, settlement_tx_sell BIGINT );

CREATE TABLE entity_stock ( entity_type TEXT NOT NULL CHECK(entity_type IN ('port','planet')), entity_id BIGINT NOT NULL, commodity_code TEXT NOT NULL, quantity BIGINT NOT NULL, price BIGINT NULL, last_updated_ts BIGINT NOT NULL DEFAULT (EXTRACT(EPOCH FROM now())::bigint), PRIMARY KEY (entity_type, entity_id, commodity_code), FOREIGN KEY (commodity_code) REFERENCES commodities(code) );


CREATE TABLE planet_production ( planet_type_id BIGINT NOT NULL, commodity_code TEXT NOT NULL, base_prod_rate BIGINT NOT NULL, base_cons_rate BIGINT NOT NULL, PRIMARY KEY (planet_type_id, commodity_code), FOREIGN KEY (planet_type_id) REFERENCES planettypes(id), FOREIGN KEY (commodity_code) REFERENCES commodities(code) );

CREATE TABLE traps ( id BIGSERIAL PRIMARY KEY, sector_id BIGINT NOT NULL, owner_player_id BIGINT, kind TEXT NOT NULL, armed BIGINT NOT NULL DEFAULT 0, arming_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP, expires_at TIMESTAMPTZ , trigger_at TIMESTAMPTZ , payload TEXT );

CREATE TABLE bank_accounts ( id BIGSERIAL PRIMARY KEY, owner_type TEXT NOT NULL, owner_id BIGINT NOT NULL, currency TEXT NOT NULL DEFAULT 'CRD' REFERENCES currencies(code), balance BIGINT NOT NULL DEFAULT 0 CHECK (balance >= 0), interest_rate_bp BIGINT NOT NULL DEFAULT 0, last_interest_tick BIGINT, tx_alert_threshold BIGINT DEFAULT 0, is_active BIGINT NOT NULL DEFAULT 1 );

CREATE TABLE bank_transactions ( id BIGSERIAL PRIMARY KEY, account_id BIGINT NOT NULL REFERENCES bank_accounts(id), tx_type TEXT NOT NULL CHECK (tx_type IN ( 'DEPOSIT', 'WITHDRAWAL', 'TRANSFER', 'INTEREST', 'FEE', 'WIRE', 'TAX', 'TRADE_BUY', 'TRADE_SELL', 'TRADE_BUY_FEE', 'TRADE_SELL_FEE', 'WITHDRAWAL_FEE', 'ADJUSTMENT' )), direction TEXT NOT NULL CHECK(direction IN ('CREDIT','DEBIT')), amount BIGINT NOT NULL CHECK (amount > 0), currency TEXT NOT NULL, tx_group_id TEXT, related_account_id BIGINT, description TEXT, ts BIGINT NOT NULL, balance_after BIGINT DEFAULT 0, idempotency_key TEXT, engine_event_id BIGINT );

CREATE TABLE bank_fee_schedules ( id BIGSERIAL PRIMARY KEY, tx_type TEXT NOT NULL, fee_code TEXT NOT NULL, owner_type TEXT, currency TEXT NOT NULL DEFAULT 'CRD', value BIGINT NOT NULL, is_percentage BIGINT NOT NULL DEFAULT 0 CHECK (is_percentage IN (0,1)), min_tx_amount BIGINT DEFAULT 0, max_tx_amount BIGINT, effective_from BIGINT NOT NULL, effective_to BIGINT );

CREATE TABLE bank_interest_policy ( id BIGSERIAL PRIMARY KEY CHECK (id = 1), apr_bps BIGINT NOT NULL DEFAULT 0 CHECK (apr_bps >= 0), min_balance BIGINT NOT NULL DEFAULT 0 CHECK (min_balance >= 0), max_balance BIGINT NOT NULL DEFAULT 9223372036854775807, last_run_at TIMESTAMPTZ , compounding TEXT NOT NULL DEFAULT 'none', currency TEXT NOT NULL DEFAULT 'CRD' REFERENCES currencies(code) );

CREATE TABLE bank_orders ( id BIGSERIAL PRIMARY KEY, player_id BIGINT NOT NULL REFERENCES players(id) ON DELETE CASCADE, kind TEXT NOT NULL CHECK (kind IN ('recurring','once')), schedule TEXT NOT NULL, next_run_at TIMESTAMPTZ, enabled BIGINT NOT NULL DEFAULT 1 CHECK (enabled IN (0,1)), amount BIGINT NOT NULL CHECK (amount > 0), currency TEXT NOT NULL DEFAULT 'CRD' REFERENCES currencies(code), to_entity TEXT NOT NULL CHECK (to_entity IN ('player','corp','gov','npc')), to_id BIGINT NOT NULL, memo TEXT );

CREATE TABLE bank_flags ( player_id BIGSERIAL PRIMARY KEY REFERENCES players(id) ON DELETE CASCADE, is_frozen BIGINT NOT NULL DEFAULT 0 CHECK (is_frozen IN (0,1)), risk_tier TEXT NOT NULL DEFAULT 'normal' CHECK (risk_tier IN ('normal','elevated','high','blocked')) );

CREATE TABLE corp_accounts ( corp_id BIGSERIAL PRIMARY KEY REFERENCES corporations(id) ON DELETE CASCADE, currency TEXT NOT NULL DEFAULT 'CRD' REFERENCES currencies(code), balance BIGINT NOT NULL DEFAULT 0 CHECK (balance >= 0), last_interest_at TIMESTAMPTZ  );

CREATE TABLE corp_tx ( id BIGSERIAL PRIMARY KEY, corp_id BIGINT NOT NULL REFERENCES corporations(id) ON DELETE CASCADE, ts TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP, kind TEXT NOT NULL CHECK (kind IN ( 'deposit', 'withdraw', 'transfer_in', 'transfer_out', 'interest', 'dividend', 'salary', 'adjustment' )), amount BIGINT NOT NULL CHECK (amount > 0), balance_after BIGINT, currency TEXT NOT NULL DEFAULT 'CRD' REFERENCES currencies(code), memo TEXT, idempotency_key TEXT UNIQUE );

CREATE TABLE corp_interest_policy ( id BIGSERIAL PRIMARY KEY CHECK (id = 1), apr_bps BIGINT NOT NULL DEFAULT 0 CHECK (apr_bps >= 0), compounding TEXT NOT NULL DEFAULT 'none' CHECK (compounding IN ('none','daily','weekly','monthly')), last_run_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP, currency TEXT NOT NULL DEFAULT 'CRD' REFERENCES currencies(code) );

CREATE TABLE stocks ( id BIGSERIAL PRIMARY KEY, corp_id BIGINT NOT NULL REFERENCES corporations(id) ON DELETE CASCADE, ticker TEXT NOT NULL UNIQUE, total_shares BIGINT NOT NULL CHECK (total_shares > 0), par_value BIGINT NOT NULL DEFAULT 0 CHECK (par_value >= 0), current_price BIGINT NOT NULL DEFAULT 0 CHECK (current_price >= 0), last_dividend_ts TEXT );

CREATE TABLE corp_shareholders ( corp_id BIGINT NOT NULL REFERENCES corporations(id) ON DELETE CASCADE, player_id BIGINT NOT NULL REFERENCES players(id) ON DELETE CASCADE, shares BIGINT NOT NULL CHECK (shares >= 0), PRIMARY KEY (corp_id, player_id) );

CREATE TABLE stock_orders ( id BIGSERIAL PRIMARY KEY, player_id BIGINT NOT NULL REFERENCES players(id) ON DELETE CASCADE, stock_id BIGINT NOT NULL REFERENCES stocks(id) ON DELETE CASCADE, type TEXT NOT NULL CHECK (type IN ('buy','sell')), quantity BIGINT NOT NULL CHECK (quantity > 0), price BIGINT NOT NULL CHECK (price >= 0), status TEXT NOT NULL DEFAULT 'open' CHECK (status IN ('open','filled','cancelled','expired')), ts TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP );

CREATE TABLE stock_trades ( id BIGSERIAL PRIMARY KEY, stock_id BIGINT NOT NULL REFERENCES stocks(id) ON DELETE CASCADE, buyer_id BIGINT NOT NULL REFERENCES players(id) ON DELETE CASCADE, seller_id BIGINT NOT NULL REFERENCES players(id) ON DELETE CASCADE, quantity BIGINT NOT NULL CHECK (quantity > 0), price BIGINT NOT NULL CHECK (price >= 0), ts TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP, settlement_tx_buy BIGINT, settlement_tx_sell BIGINT );

CREATE TABLE stock_dividends ( id BIGSERIAL PRIMARY KEY, stock_id BIGINT NOT NULL REFERENCES stocks(id) ON DELETE CASCADE, amount_per_share BIGINT NOT NULL CHECK (amount_per_share >= 0), declared_ts TEXT NOT NULL, paid_ts TEXT );

CREATE TABLE stock_indices ( id BIGSERIAL PRIMARY KEY, name TEXT UNIQUE NOT NULL );

CREATE TABLE stock_index_members ( index_id BIGINT NOT NULL REFERENCES stock_indices(id) ON DELETE CASCADE, stock_id BIGINT NOT NULL REFERENCES stocks(id) ON DELETE CASCADE, weight DOUBLE PRECISION NOT NULL DEFAULT 1.0, PRIMARY KEY (index_id, stock_id) );

CREATE TABLE insurance_funds ( id BIGSERIAL PRIMARY KEY, owner_type TEXT NOT NULL CHECK (owner_type IN ('system','corp','player')), owner_id BIGINT, balance BIGINT NOT NULL DEFAULT 0 CHECK (balance >= 0) );

CREATE TABLE insurance_policies ( id BIGSERIAL PRIMARY KEY, holder_type TEXT NOT NULL CHECK (holder_type IN ('player','corp')), holder_id BIGINT NOT NULL, subject_type TEXT NOT NULL CHECK (subject_type IN ('ship','cargo','planet')), subject_id BIGINT NOT NULL, premium BIGINT NOT NULL CHECK (premium >= 0), payout BIGINT NOT NULL CHECK (payout >= 0), fund_id BIGINT REFERENCES insurance_funds(id) ON DELETE SET NULL, start_ts TEXT NOT NULL, expiry_ts TEXT, active BIGINT NOT NULL DEFAULT 1 CHECK (active IN (0,1)) );

CREATE TABLE insurance_claims ( id BIGSERIAL PRIMARY KEY, policy_id BIGINT NOT NULL REFERENCES insurance_policies(id) ON DELETE CASCADE, event_id TEXT, amount BIGINT NOT NULL CHECK (amount >= 0), status TEXT NOT NULL DEFAULT 'open' CHECK (status IN ('open','paid','denied')), ts TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP, paid_bank_tx BIGINT );

CREATE TABLE risk_profiles ( id BIGSERIAL PRIMARY KEY, entity_type TEXT NOT NULL CHECK (entity_type IN ('player','corp')), entity_id BIGINT NOT NULL, risk_score BIGINT NOT NULL DEFAULT 0 );

CREATE TABLE loans ( id BIGSERIAL PRIMARY KEY, lender_type TEXT NOT NULL CHECK (lender_type IN ('player','corp','bank')), lender_id BIGINT, borrower_type TEXT NOT NULL CHECK (borrower_type IN ('player','corp')), borrower_id BIGINT NOT NULL, principal BIGINT NOT NULL CHECK (principal > 0), rate_bps BIGINT NOT NULL DEFAULT 0 CHECK (rate_bps >= 0), term_days BIGINT NOT NULL CHECK (term_days > 0), next_due TEXT, status TEXT NOT NULL DEFAULT 'active' CHECK (status IN ('active','paid','defaulted','written_off')), created_ts TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP);

CREATE TABLE loan_payments ( id BIGSERIAL PRIMARY KEY, loan_id BIGINT NOT NULL REFERENCES loans(id) ON DELETE CASCADE, ts TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP, amount BIGINT NOT NULL CHECK (amount > 0), status TEXT NOT NULL DEFAULT 'posted' CHECK (status IN ('posted','reversed')), bank_tx_id BIGINT );

CREATE TABLE collateral ( id BIGSERIAL PRIMARY KEY, loan_id BIGINT NOT NULL REFERENCES loans(id) ON DELETE CASCADE, asset_type TEXT NOT NULL CHECK (asset_type IN ('ship','planet','cargo','stock','other')), asset_id BIGINT NOT NULL, appraised_value BIGINT NOT NULL DEFAULT 0 CHECK (appraised_value >= 0) );

CREATE TABLE credit_ratings ( entity_type TEXT NOT NULL CHECK (entity_type IN ('player','corp')), entity_id BIGINT NOT NULL, score BIGINT NOT NULL DEFAULT 600 CHECK (score BETWEEN 300 AND 900), last_update TEXT, PRIMARY KEY (entity_type, entity_id) );

CREATE TABLE charters ( id BIGSERIAL PRIMARY KEY, name TEXT NOT NULL UNIQUE, granted_by TEXT NOT NULL DEFAULT 'federation', monopoly_scope TEXT, start_ts TEXT NOT NULL, expiry_ts TEXT );

CREATE TABLE expeditions ( id BIGSERIAL PRIMARY KEY, leader_player_id BIGINT NOT NULL REFERENCES players(id) ON DELETE CASCADE, charter_id BIGINT REFERENCES charters(id) ON DELETE SET NULL, goal TEXT NOT NULL, target_region TEXT, pledged_total BIGINT NOT NULL DEFAULT 0 CHECK (pledged_total >= 0), duration_days BIGINT NOT NULL DEFAULT 7 CHECK (duration_days > 0), status TEXT NOT NULL DEFAULT 'planning' CHECK (status IN ('planning','launched','complete','failed','aborted')), created_ts TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP );

CREATE TABLE expedition_backers ( expedition_id BIGINT NOT NULL REFERENCES expeditions(id) ON DELETE CASCADE, backer_type TEXT NOT NULL CHECK (backer_type IN ('player','corp')), backer_id BIGINT NOT NULL, pledged_amount BIGINT NOT NULL CHECK (pledged_amount >= 0), share_pct DOUBLE PRECISION NOT NULL CHECK (share_pct >= 0), PRIMARY KEY (expedition_id, backer_type, backer_id) );

CREATE TABLE expedition_returns ( id BIGSERIAL PRIMARY KEY, expedition_id BIGINT NOT NULL REFERENCES expeditions(id) ON DELETE CASCADE, ts TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP, amount BIGINT NOT NULL CHECK (amount >= 0), bank_tx_id BIGINT );

CREATE TABLE futures_contracts ( id BIGSERIAL PRIMARY KEY, commodity_id BIGINT NOT NULL REFERENCES commodities(id) ON DELETE CASCADE, buyer_type TEXT NOT NULL CHECK (buyer_type IN ('player','corp')), buyer_id BIGINT NOT NULL, seller_type TEXT NOT NULL CHECK (seller_type IN ('player','corp')), seller_id BIGINT NOT NULL, strike_price BIGINT NOT NULL CHECK (strike_price >= 0), expiry_ts TEXT NOT NULL, status TEXT NOT NULL DEFAULT 'open' CHECK (status IN ('open','settled','defaulted','cancelled')) );

CREATE TABLE warehouses ( id BIGSERIAL PRIMARY KEY, location_type TEXT NOT NULL CHECK (location_type IN ('sector','planet','port')), location_id BIGINT NOT NULL, owner_type TEXT NOT NULL CHECK (owner_type IN ('player','corp')), owner_id BIGINT NOT NULL );

CREATE TABLE gov_accounts ( id BIGSERIAL PRIMARY KEY, name TEXT NOT NULL UNIQUE, balance BIGINT NOT NULL DEFAULT 0 CHECK (balance >= 0) );

CREATE TABLE tax_policies ( id BIGSERIAL PRIMARY KEY, name TEXT NOT NULL, tax_type TEXT NOT NULL CHECK (tax_type IN ('trade','income','corp','wealth','transfer')), rate_bps BIGINT NOT NULL DEFAULT 0 CHECK (rate_bps >= 0), active BIGINT NOT NULL DEFAULT 1 CHECK (active IN (0,1)) );

CREATE TABLE tax_ledgers ( id BIGSERIAL PRIMARY KEY, policy_id BIGINT NOT NULL REFERENCES tax_policies(id) ON DELETE CASCADE, payer_type TEXT NOT NULL CHECK (payer_type IN ('player','corp')), payer_id BIGINT NOT NULL, amount BIGINT NOT NULL CHECK (amount >= 0), ts TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP, bank_tx_id BIGINT );

CREATE TABLE fines ( id BIGSERIAL PRIMARY KEY, issued_by TEXT NOT NULL DEFAULT 'federation', recipient_type TEXT NOT NULL CHECK (recipient_type IN ('player','corp')), recipient_id BIGINT NOT NULL, reason TEXT, amount BIGINT NOT NULL CHECK (amount >= 0), status TEXT NOT NULL DEFAULT 'unpaid' CHECK (status IN ('unpaid','paid','void')), issued_ts TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP, paid_bank_tx BIGINT );

CREATE TABLE bounties ( id BIGSERIAL PRIMARY KEY, posted_by_type TEXT NOT NULL CHECK (posted_by_type IN ('player','corp','gov','npc')), posted_by_id BIGINT, target_type TEXT NOT NULL CHECK (target_type IN ('player','corp','npc')), target_id BIGINT NOT NULL, reward BIGINT NOT NULL CHECK (reward >= 0), escrow_bank_tx BIGINT, status TEXT NOT NULL DEFAULT 'open' CHECK (status IN ('open','claimed','cancelled','expired')), posted_ts TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP, claimed_by BIGINT, paid_bank_tx BIGINT );

CREATE TABLE grants ( id BIGSERIAL PRIMARY KEY, name TEXT NOT NULL, recipient_type TEXT NOT NULL CHECK (recipient_type IN ('player','corp')), recipient_id BIGINT NOT NULL, amount BIGINT NOT NULL CHECK (amount >= 0), awarded_ts TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP, bank_tx_id BIGINT );

CREATE TABLE research_projects ( id BIGSERIAL PRIMARY KEY, sponsor_type TEXT NOT NULL CHECK (sponsor_type IN ('player','corp','gov')), sponsor_id BIGINT, title TEXT NOT NULL, field TEXT NOT NULL, cost BIGINT NOT NULL CHECK (cost >= 0), progress BIGINT NOT NULL DEFAULT 0 CHECK (progress BETWEEN 0 AND 100), status TEXT NOT NULL DEFAULT 'active' CHECK (status IN ('active','paused','complete','failed')), created_ts TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP );

CREATE TABLE research_contributors ( project_id BIGINT NOT NULL REFERENCES research_projects(id) ON DELETE CASCADE, actor_type TEXT NOT NULL CHECK (actor_type IN ('player','corp')), actor_id BIGINT NOT NULL, amount BIGINT NOT NULL CHECK (amount >= 0), PRIMARY KEY (project_id, actor_type, actor_id) );

CREATE TABLE research_results ( id BIGSERIAL PRIMARY KEY, project_id BIGINT NOT NULL REFERENCES research_projects(id) ON DELETE CASCADE, blueprint_code TEXT NOT NULL, unlocked_ts TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP );

CREATE TABLE black_accounts ( id BIGSERIAL PRIMARY KEY, owner_type TEXT NOT NULL CHECK (owner_type IN ('player','corp','npc')), owner_id BIGINT NOT NULL, balance BIGINT NOT NULL DEFAULT 0 CHECK (balance >= 0) );

CREATE TABLE laundering_ops ( id BIGSERIAL PRIMARY KEY, from_black_id BIGINT REFERENCES black_accounts(id) ON DELETE SET NULL, to_player_id BIGINT REFERENCES players(id) ON DELETE SET NULL, amount BIGINT NOT NULL CHECK (amount > 0), risk_pct BIGINT NOT NULL DEFAULT 25 CHECK (risk_pct BETWEEN 0 AND 100), status TEXT NOT NULL DEFAULT 'pending' CHECK (status IN ('pending','cleaned','seized','failed')), ts TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP );

CREATE TABLE contracts_illicit ( id BIGSERIAL PRIMARY KEY, contractor_type TEXT NOT NULL CHECK (contractor_type IN ('player','corp','npc')), contractor_id BIGINT NOT NULL, target_type TEXT NOT NULL CHECK (target_type IN ('player','corp','npc')), target_id BIGINT NOT NULL, reward BIGINT NOT NULL CHECK (reward >= 0), escrow_black_id BIGINT REFERENCES black_accounts(id) ON DELETE SET NULL, status TEXT NOT NULL DEFAULT 'open' CHECK (status IN ('open','fulfilled','failed','cancelled')), ts TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP );

CREATE TABLE fences ( id BIGSERIAL PRIMARY KEY, npc_id BIGINT, sector_id BIGINT, reputation BIGINT NOT NULL DEFAULT 0 );

CREATE TABLE economic_indicators ( id BIGSERIAL PRIMARY KEY, ts TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP, inflation_bps BIGINT NOT NULL DEFAULT 0, liquidity BIGINT NOT NULL DEFAULT 0, credit_velocity DOUBLE PRECISION NOT NULL DEFAULT 0.0 );

CREATE TABLE sector_gdp ( sector_id BIGSERIAL PRIMARY KEY, gdp BIGINT NOT NULL DEFAULT 0, last_update TEXT );

CREATE TABLE event_triggers ( id BIGSERIAL PRIMARY KEY, name TEXT NOT NULL, condition_json TEXT NOT NULL, action_json TEXT NOT NULL );

CREATE TABLE charities ( id BIGSERIAL PRIMARY KEY, name TEXT NOT NULL UNIQUE, description TEXT );

CREATE TABLE donations ( id BIGSERIAL PRIMARY KEY, charity_id BIGINT NOT NULL REFERENCES charities(id) ON DELETE CASCADE, donor_type TEXT NOT NULL CHECK (donor_type IN ('player','corp')), donor_id BIGINT NOT NULL, amount BIGINT NOT NULL CHECK (amount >= 0), ts TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP, bank_tx_id BIGINT );

CREATE TABLE temples ( id BIGSERIAL PRIMARY KEY, name TEXT NOT NULL UNIQUE, sector_id BIGINT, favour BIGINT NOT NULL DEFAULT 0 );

CREATE TABLE guilds ( id BIGSERIAL PRIMARY KEY, name TEXT NOT NULL UNIQUE, description TEXT );

CREATE TABLE guild_memberships ( guild_id BIGINT NOT NULL REFERENCES guilds(id) ON DELETE CASCADE, member_type TEXT NOT NULL CHECK (member_type IN ('player','corp')), member_id BIGINT NOT NULL, role TEXT NOT NULL DEFAULT 'member', PRIMARY KEY (guild_id, member_type, member_id) );

CREATE TABLE guild_dues ( id BIGSERIAL PRIMARY KEY, guild_id BIGINT NOT NULL REFERENCES guilds(id) ON DELETE CASCADE, amount BIGINT NOT NULL CHECK (amount >= 0), period TEXT NOT NULL DEFAULT 'monthly' CHECK (period IN ('weekly','monthly','quarterly','yearly')) );

CREATE TABLE economy_snapshots ( id BIGSERIAL PRIMARY KEY, ts TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP, money_supply BIGINT NOT NULL DEFAULT 0, total_deposits BIGINT NOT NULL DEFAULT 0, total_loans BIGINT NOT NULL DEFAULT 0, total_insured BIGINT NOT NULL DEFAULT 0, notes TEXT );

CREATE TABLE ai_economy_agents ( id BIGSERIAL PRIMARY KEY, name TEXT NOT NULL, role TEXT NOT NULL, config_json TEXT NOT NULL );

CREATE TABLE anomaly_reports ( id BIGSERIAL PRIMARY KEY, ts TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP, severity TEXT NOT NULL CHECK (severity IN ('low','medium','high','critical')), subject TEXT NOT NULL, details TEXT NOT NULL, resolved BIGINT NOT NULL DEFAULT 0 CHECK (resolved IN (0,1)) );

CREATE TABLE economy_policies ( id BIGSERIAL PRIMARY KEY, name TEXT NOT NULL UNIQUE, config_json TEXT NOT NULL, active BIGINT NOT NULL DEFAULT 1 CHECK (active IN (0,1)) );

CREATE TABLE s2s_keys( key_id TEXT PRIMARY KEY,
key_b64 TEXT NOT NULL,
is_default_tx BIGINT NOT NULL DEFAULT 0,
active BOOLEAN DEFAULT TRUE,
created_ts TIMESTAMPTZ NOT NULL);

CREATE TABLE cron_tasks( id BIGSERIAL PRIMARY KEY, name TEXT UNIQUE NOT NULL, schedule TEXT NOT NULL, last_run_at TIMESTAMPTZ , next_due_at TIMESTAMPTZ NOT NULL , enabled BOOLEAN DEFAULT TRUE, payload TEXT);

CREATE TABLE engine_events( id BIGSERIAL PRIMARY KEY, ts BIGINT NOT NULL, type TEXT NOT NULL, actor_player_id BIGINT, sector_id BIGINT, payload TEXT NOT NULL, idem_key TEXT, processed_at TIMESTAMPTZ );

CREATE TABLE engine_offset( key TEXT PRIMARY KEY, last_event_id BIGINT NOT NULL, last_event_ts BIGINT NOT NULL);

CREATE TABLE engine_events_deadletter( id BIGSERIAL PRIMARY KEY, ts BIGINT NOT NULL, type TEXT NOT NULL, payload TEXT NOT NULL, error TEXT NOT NULL, moved_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP);

CREATE TABLE engine_commands( id BIGSERIAL PRIMARY KEY, type TEXT NOT NULL, payload TEXT NOT NULL, status TEXT NOT NULL DEFAULT 'ready', priority BIGINT NOT NULL DEFAULT 100, attempts BIGINT NOT NULL DEFAULT 0, created_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP, due_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP, started_at TIMESTAMPTZ, finished_at TIMESTAMPTZ , worker TEXT, idem_key TEXT);

CREATE TABLE engine_audit( id BIGSERIAL PRIMARY KEY, ts BIGINT NOT NULL, cmd_type TEXT NOT NULL, correlation_id TEXT, actor_player_id BIGINT, details TEXT);

CREATE TABLE news_feed( news_id BIGSERIAL PRIMARY KEY, published_ts BIGINT NOT NULL, news_category TEXT NOT NULL, article_text TEXT NOT NULL, author_id BIGINT, source_ids TEXT);

CREATE TABLE eligible_tows (ship_id BIGSERIAL PRIMARY KEY, sector_id BIGINT, owner_id BIGINT, fighters BIGINT, alignment BIGINT, experience BIGINT);
