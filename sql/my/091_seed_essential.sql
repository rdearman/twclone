-- Generated from PostgreSQL 091 seed essential.sql -> MySQL
-- 030_seeds.sql
-- Default data and seeding based on src/database.c
-- Manually restored and verified for PostgreSQL compatibility
-- 0. Economy Curve (Essential for Ports)
INSERT INTO economy_curve (economy_curve_id, curve_name, base_restock_rate, price_elasticity, target_stock, volatility_factor)
    VALUES (1, 'Standard', 0.1, 1.0, 1000, 0.1)
;
-- 1. Law Enforcement
-- 2. Alignment Bands
-- 3. Currencies
-- 4. Commodities
-- 5. Ship Roles
-- 6. Commissions
-- 7. Planet Types
-- 8. Ship Types
-- 9. Player Types
-- 10. NPC Ship Names
INSERT INTO npc_shipnames (npc_shipnames_id, name)
    VALUES (1, 'Starlight Voyager'),
    (2, 'Iron Sentinel'),
    (3, 'Crimson Horizon'),
    (4, 'The Unrelenting'),
    (5, 'Vanguard of Sol'),
    (6, 'Aether''s Echo'),
    (7, 'Voiddrifter'),
    (8, 'Celestia'),
    (9, 'The Final Word'),
    (10, 'Sovereign''s Might'),
    (11, 'The Silence'),
    (12, 'Ghost of Proxima'),
    (13, 'Harbinger of Ruin'),
    (14, 'Blackstar'),
    (15, 'Fallen Angel'),
    (16, 'Grave Digger'),
    (17, 'The Empty Sky'),
    (18, 'Cinderclaw'),
    (19, 'Whisper of the Abyss'),
    (20, 'The Nameless Dread'),
    (21, 'Not My Fault'),
    (22, 'Totally Not a Trap'),
    (23, 'The Gravitational Pull'),
    (24, 'Unlicensed & Uninsured'),
    (25, 'Ship Happens'),
    (26, 'The Loan Shark''s Repossession'),
    (27, 'Where Are We Going?'),
    (28, 'Taxes Included'),
    (29, 'Error 404: Ship Not Found'),
    (30, 'The Padded Cell'),
    (31, 'Quantum Leap'),
    (32, 'The Data Stream'),
    (33, 'Sub-Light Cruiser'),
    (34, 'Temporal Paradox'),
    (35, 'Neon Genesis'),
    (36, 'The Warp Core'),
    (37, 'The Nanite Swarm'),
    (38, 'Synthetic Dream'),
    (39, 'The Singularity'),
    (40, 'Blink Drive'),
    (41, 'The Last Endeavor'),
    (42, 'Odyssey''s End'),
    (43, 'The Magellan'),
    (44, 'Star''s Fury'),
    (45, 'Cosmic Drifter'),
    (46, 'The Old Dog'),
    (47, 'The Wayfinder'),
    (48, 'The Horizon Breaker'),
    (49, 'Stormchaser'),
    (50, 'Beyond the Veil');
-- ;
-- 11. Tavern Names & Settings
INSERT INTO tavern_names (name, enabled, weight)
    VALUES ('The Rusty Flange', 1, 10),
    ('The Starfall Inn', 1, 10),
    ('Orions Den', 1, 5),
    ('FedSpace Cantina', 1, 8)
;
INSERT INTO tavern_settings (tavern_settings_id, max_bet_per_transaction, daily_max_wager, enable_dynamic_wager_limit, graffiti_max_posts, notice_expires_days, buy_round_cost, buy_round_alignment_gain, loan_shark_enabled)
    VALUES (1, 5000, 50000, 0, 100, 7, 1000, 5, 1)
;
-- 12. Hardware Items
INSERT INTO hardware_items (code, name, price, requires_stardock, sold_in_class0, max_per_ship, category, enabled)
    VALUES ('FIGHTERS', 'Fighters', 100, 0, 0, NULL, 'FIGHTER', 1),
    ('SHIELDS', 'Shields', 200, 0, 1, NULL, 'SHIELD', 1),
    ('HOLDS', 'Holds', 50, 0, 1, NULL, 'HOLD', 1),
    ('GENESIS', 'Genesis Torpedo', 25000, 1, 1, NULL, 'SPECIAL', 1),
    ('DETONATOR', 'Atomic Detonator', 10000, 1, 1, NULL, 'SPECIAL', 1),
    ('CLOAK', 'Cloaking Device', 50000, 0, 1, 1, 'MODULE', 1),
    ('LSCANNER', 'Long-Range Scanner', 30000, 0, 1, 1, 'MODULE', 1),
    ('PSCANNER', 'Planet Scanner', 15000, 0, 1, 1, 'MODULE', 1),
    ('TWARP', 'TransWarp Drive', 100000, 0, 1, 1, 'MODULE', 1),
    ('MINES', 'Armid Mine', 1000, 1, 0, NULL, 'SPECIAL', 1),
    ('LIMPETS', 'Limpet Mine', 10000, 1, 0, NULL, 'SPECIAL', 1),
    ('PROBE', 'Subspace Ether Probe', 3000, 1, 0, NULL, 'SPECIAL', 1),
    ('PHOTONS', 'Photon Missile', 40000, 1, 0, NULL, 'SPECIAL', 1),
    ('BEACONS', 'Marker Beacon', 100, 1, 0, NULL, 'SPECIAL', 1)
;
-- 13. FedSpace Sectors (1-10)
INSERT INTO sectors (sector_id, name, beacon, nebulae)
    VALUES (1, 'Fedspace 1', 'The Federation -- Do Not Dump!', 'The Federation'),
    (2, 'Fedspace 2', 'The Federation -- Do Not Dump!', 'The Federation'),
    (3, 'Fedspace 3', 'The Federation -- Do Not Dump!', 'The Federation'),
    (4, 'Fedspace 4', 'The Federation -- Do Not Dump!', 'The Federation'),
    (5, 'Fedspace 5', 'The Federation -- Do Not Dump!', 'The Federation'),
    (6, 'Fedspace 6', 'The Federation -- Do Not Dump!', 'The Federation'),
    (7, 'Fedspace 7', 'The Federation -- Do Not Dump!', 'The Federation'),
    (8, 'Fedspace 8', 'The Federation -- Do Not Dump!', 'The Federation'),
    (9, 'Fedspace 9', 'The Federation -- Do Not Dump!', 'The Federation'),
    (10, 'Fedspace 10', 'The Federation -- Do Not Dump!', 'The Federation')
;
