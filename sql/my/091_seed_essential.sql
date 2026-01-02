-- Generated from PostgreSQL 091 seed essential.sql -> MySQL
-- 030_seeds.sql
-- Default data and seeding based on src/database.c
-- Manually restored and verified for PostgreSQL compatibility
-- 0. Economy Curve (Essential for Ports)
INSERT INTO economy_curve (economy_curve_id, curve_name, base_restock_rate, price_elasticity, target_stock, volatility_factor)
    VALUES (1, 'Standard', 0.1, 1.0, 1000, 0.1)
;
-- 1. Law Enforcement
INSERT INTO law_enforcement (law_enforcement_id)
    VALUES (1);;
-- 2. Alignment Bands
INSERT INTO alignment_band (alignment_band_id, code, name, min_align, max_align, is_good, is_evil, can_buy_iss, can_rob_ports)
    VALUES (1, 'VERY_GOOD', 'Very Good', 750, 2000, 1, 0, 1, 0),
    (2, 'GOOD', 'Good', 250, 749, 1, 0, 0, 0),
    (3, 'NEUTRAL', 'Neutral', -249, 249, 0, 0, 0, 0),
    (4, 'SHADY', 'Shady', -500, -250, 0, 1, 0, 1),
    (5, 'VERY_EVIL', 'Very Evil', -1000, -501, 0, 1, 0, 1),
    (6, 'MONSTROUS', 'Monstrous', -2000, -1001, 0, 1, 0, 1)
;
-- 3. Currencies
INSERT INTO currencies (code, name, minor_unit, is_default)
    VALUES ('CRD', 'Galactic Credits', 1, 1)
;
-- 4. Commodities
INSERT INTO commodities (commodities_id, code, name, base_price, volatility, illegal)
    VALUES (1, 'ORE', 'Ore', 100, 20, 0),
    (2, 'ORG', 'Organics', 150, 30, 0),
    (3, 'EQU', 'Equipment', 200, 25, 0),
    (4, 'SLV', 'Slaves', 1000, 50, 1),
    (5, 'WPN', 'Weapons', 750, 40, 1),
    (6, 'DRG', 'Drugs', 500, 60, 1)
;
-- 5. Ship Roles
INSERT INTO ship_roles (role_id, ROLE, role_description)
    VALUES (1, 'owner', 'Legal owner; can sell/rename, set availability, assign others'),
    (2, 'pilot', 'Currently flying the ship; usually the active ship for the player'),
    (3, 'crew', 'Can board and use limited functions (e.g., scan, fire fighters)'),
    (4, 'leasee', 'Temporary control with limits; can pilot but not sell/rename'),
    (5, 'lender', 'Party that lent/leased the ship; can revoke lease'),
    (6, 'corp', 'Corporate ownership/control (for future org/corp features)'),
    (7, 'manager', 'Delegated admin; can assign crew/pilot but not sell')
;
-- 6. Commissions
INSERT INTO commission (commission_id, is_evil, min_exp, description)
    VALUES (1, 0, 0, 'Civilian'),
    (2, 1, 0, 'Civilian'),
    (3, 0, 100, 'Cadet'),
    (4, 1, 100, 'Thug'),
    (5, 0, 400, 'Ensign'),
    (6, 1, 400, 'Pirate'),
    (7, 0, 1000, 'Lieutenant'),
    (8, 1, 1000, 'Raider'),
    (9, 0, 2500, 'Lt. Commander'),
    (10, 1, 2500, 'Marauder'),
    (11, 0, 5000, 'Commander'),
    (12, 1, 5000, 'Buccaneer'),
    (13, 0, 10000, 'Captain'),
    (14, 1, 10000, 'Corsair'),
    (15, 0, 20000, 'Commodore'),
    (16, 1, 20000, 'Terrorist'),
    (17, 0, 35000, 'Rear Admiral'),
    (18, 1, 35000, 'Anarchist'),
    (19, 0, 75000, 'Vice Admiral'),
    (20, 1, 75000, 'Warlord'),
    (21, 0, 100000, 'Admiral'),
    (22, 1, 100000, 'Despot'),
    (23, 0, 150000, 'Fleet Admiral'),
    (24, 1, 150000, 'Tyrant'),
    (25, 0, 20000, 'Grand Admiral'),
    (26, 1, 200000, 'Warmonger'),
    (27, 0, 300000, 'Lord Commander'),
    (28, 1, 300000, 'Dread Pirate'),
    (29, 0, 400000, 'High Commander'),
    (30, 1, 400000, 'Cosmic Destroyer'),
    (31, 0, 550000, 'Star Marshal'),
    (32, 1, 550000, 'Galactic Menace'),
    (33, 0, 700000, 'Grand Star Marshal'),
    (34, 1, 700000, 'Void Reaver'),
    (35, 0, 1000000, 'Supreme Commander'),
    (36, 1, 1000000, 'Grim Reaper'),
    (37, 0, 1500000, 'Galactic Commander'),
    (38, 1, 1500000, 'Annihilator'),
    (39, 0, 2000000, 'Galactic Captain'),
    (40, 1, 3000000, 'Supreme Annihilator'),
    (41, 0, 4000000, 'Galactic Commodore'),
    (42, 1, 4000000, 'Chaos Bringer'),
    (43, 0, 5000000, 'Galactic Admiral'),
    (44, 1, 5000000, 'Death Lord')
;
-- 7. Planet Types
INSERT INTO planettypes (code, typeDescription, typeName, citadelUpgradeTime_lvl1, citadelUpgradeTime_lvl2, citadelUpgradeTime_lvl3, citadelUpgradeTime_lvl4, citadelUpgradeTime_lvl5, citadelUpgradeTime_lvl6, citadelUpgradeOre_lvl1, citadelUpgradeOre_lvl2, citadelUpgradeOre_lvl3, citadelUpgradeOre_lvl4, citadelUpgradeOre_lvl5, citadelUpgradeOre_lvl6, citadelUpgradeOrganics_lvl1, citadelUpgradeOrganics_lvl2, citadelUpgradeOrganics_lvl3, citadelUpgradeOrganics_lvl4, citadelUpgradeOrganics_lvl5, citadelUpgradeOrganics_lvl6, citadelUpgradeEquipment_lvl1, citadelUpgradeEquipment_lvl2, citadelUpgradeEquipment_lvl3, citadelUpgradeEquipment_lvl4, citadelUpgradeEquipment_lvl5, citadelUpgradeEquipment_lvl6, citadelUpgradeColonist_lvl1, citadelUpgradeColonist_lvl2, citadelUpgradeColonist_lvl3, citadelUpgradeColonist_lvl4, citadelUpgradeColonist_lvl5, citadelUpgradeColonist_lvl6, maxColonist_ore, maxColonist_organics, maxColonist_equipment, fighters, fuelProduction, organicsProduction, equipmentProduction, fighterProduction, maxore, maxorganics, maxequipment, maxfighters, breeding)
    VALUES ('M', 'Earth type', 'Earth', 4, 4, 5, 10, 5, 15, 300, 200, 500, 1000, 300, 1000, 200, 50, 250, 1200, 400, 1200, 250, 250, 500, 1000, 1000, 2000, 1000000, 2000000, 4000000, 6000000, 6000000, 6000000, 100000, 100000, 100000, 0, 0, 0, 0, 0, 10000000, 100000, 100000, 1000000, 0.75),
    ('L', 'Mountainous', 'Mountain', 2, 5, 5, 8, 5, 12, 150, 200, 600, 1000, 300, 1000, 100, 50, 250, 1200, 400, 1200, 150, 250, 700, 1000, 1000, 2000, 400000, 1400000, 3600000, 5600000, 7000000, 5600000, 200000, 200000, 200000, 0, 0, 0, 0, 0, 200000, 200000, 100000, 1000000, 0.24),
    ('O', 'Oceanic', 'Ocean', 6, 5, 8, 5, 4, 8, 500, 200, 600, 700, 300, 700, 200, 50, 400, 900, 400, 900, 400, 300, 650, 800, 1000, 1600, 1400000, 2400000, 4400000, 7000000, 8000000, 7000000, 100000, 1000000, 1000000, 0, 0, 0, 0, 0, 50000, 1000000, 50000, 1000000, 0.30),
    ('K', 'Desert Wasteland', 'Desert', 6, 5, 8, 5, 4, 8, 400, 300, 700, 700, 300, 700, 300, 80, 900, 900, 400, 900, 600, 400, 800, 800, 1000, 1600, 1000000, 2400000, 4000000, 7000000, 8000000, 7000000, 20000, 50000, 50000, 0, 0, 0, 0, 0, 20000, 50000, 10000, 1000000, 0.50),
    ('H', 'Volcanic', 'Volcano', 4, 5, 8, 12, 18, 8, 500, 300, 1200, 2000, 3000, 2000, 300, 100, 400, 2000, 1200, 2000, 600, 400, 1500, 2500, 2000, 5000, 800000, 1600000, 4400000, 7000000, 10000000, 7000000, 1000000, 10000, 10000, 0, 0, 0, 0, 0, 1000000, 10000, 100000, 1000000, 0.30),
    ('U', 'Gaseous', 'Gas Giant', 8, 4, 5, 5, 4, 8, 1200, 300, 500, 500, 200, 500, 400, 100, 500, 200, 200, 200, 2500, 400, 2000, 600, 600, 1200, 3000000, 3000000, 8000000, 6000000, 8000000, 6000000, 10000, 10000, 10000, 0, 0, 0, 0, 0, 10000, 10000, 10000, 1000000, -0.10),
    ('C', 'Glacial/Ice', 'Ice World', 5, 5, 7, 5, 4, 8, 400, 300, 600, 700, 300, 700, 300, 80, 400, 900, 400, 900, 600, 400, 650, 700, 1000, 1400, 1000000, 24000000, 4400000, 6600000, 9000000, 6600000, 20000, 50000, 20000, 0, 0, 0, 0, 0, 20000, 50000, 10000, 1000000, -0.10)
;
-- 8. Ship Types
INSERT INTO shiptypes (name, basecost, maxattack, initialholds, maxholds, maxfighters, turns, maxmines, maxlimpets, maxgenesis, can_transwarp, transportrange, maxshields, offense, defense, maxbeacons, can_long_range_scan, can_planet_scan, maxphotons, max_cloaks, can_purchase, enabled)
    VALUES ('Merchant Cruiser', 41300, 750, 20, 75, 2500, 3, 50, 0, 5, 0, 5, 400, 10, 10, 0, 1, 1, 0, 0, 1, 1),
    ('Scout Marauder', 15950, 250, 10, 25, 250, 2, 0, 0, 0, 0, 0, 100, 20, 20, 0, 1, 1, 0, 0, 1, 1),
    ('Missile Frigate', 100000, 2000, 12, 60, 5000, 3, 5, 0, 0, 0, 2, 400, 13, 13, 5, 0, 0, 1, 0, 1, 1),
    ('Battleship', 88500, 3000, 16, 80, 10000, 4, 25, 0, 1, 0, 8, 750, 16, 16, 50, 1, 1, 0, 0, 1, 1),
    ('Corporate Flagship', 163500, 6000, 20, 85, 20000, 3, 100, 0, 10, 1, 10, 1500, 12, 12, 100, 1, 1, 1, 0, 1, 1),
    ('Colonial Transport', 63600, 100, 50, 250, 200, 6, 0, 0, 5, 0, 7, 500, 6, 6, 10, 0, 1, 0, 0, 1, 1),
    ('Cargo Transport', 51950, 125, 50, 125, 400, 4, 1, 0, 2, 0, 5, 1000, 8, 8, 20, 1, 1, 0, 0, 1, 1),
    ('Merchant Freighter', 33400, 100, 30, 65, 300, 2, 2, 0, 2, 0, 5, 500, 8, 8, 20, 1, 1, 0, 0, 1, 1),
    ('Imperial Starship', 329000, 10000, 40, 150, 50000, 4, 125, 0, 10, 1, 15, 2000, 15, 15, 150, 1, 1, 1, 0, 1, 1),
    ('Havoc Gunstar', 79000, 1000, 12, 50, 10000, 3, 5, 0, 1, 1, 6, 3000, 13, 13, 5, 1, 0, 0, 0, 1, 1),
    ('Constellation', 72500, 2000, 20, 80, 5000, 3, 25, 0, 2, 0, 6, 750, 14, 14, 50, 1, 1, 0, 0, 1, 1),
    ('T''khasi Orion', 42500, 250, 30, 60, 750, 2, 5, 0, 1, 0, 3, 750, 11, 11, 20, 1, 1, 0, 0, 1, 1),
    ('Tholian Sentinel', 47500, 800, 10, 50, 2500, 4, 50, 0, 1, 0, 3, 4000, 1, 1, 10, 1, 0, 0, 0, 1, 1),
    ('Taurean Mule', 63600, 150, 50, 150, 300, 4, 0, 0, 1, 0, 5, 600, 5, 5, 20, 1, 1, 0, 0, 1, 1),
    ('Interdictor Cruiser', 539000, 15000, 10, 40, 100000, 15, 200, 0, 20, 0, 20, 4000, 12, 12, 100, 1, 1, 0, 0, 1, 1),
    ('Ferengi Warship', 150000, 5000, 20, 100, 15000, 5, 20, 0, 5, 0, 10, 5000, 15, 15, 50, 1, 1, 1, 0, 0, 1),
    ('Imperial Starship (NPC)', 329000, 10000, 40, 150, 50000, 4, 125, 0, 10, 1, 15, 2000, 15, 15, 150, 1, 1, 1, 0, 0, 1),
    ('Orion Heavy Fighter Patrol', 150000, 5000, 20, 50, 20000, 5, 10, 0, 5, 0, 10, 5000, 20, 10, 25, 1, 1, 1, 0, 0, 1),
    ('Orion Scout/Looter', 80000, 4000, 10, 150, 5000, 5, 10, 0, 5, 0, 10, 3000, 8, 8, 25, 1, 1, 1, 0, 0, 1),
    ('Orion Contraband Runner', 120000, 3000, 10, 200, 3000, 5, 10, 0, 5, 0, 10, 4000, 10, 5, 25, 1, 1, 1, 0, 0, 1),
    ('Orion Smuggler''s Kiss', 130000, 5000, 15, 100, 10000, 5, 10, 0, 5, 0, 10, 5000, 15, 15, 25, 1, 1, 1, 0, 0, 1),
    ('Orion Black Market Guard', 180000, 6000, 20, 60, 8000, 5, 10, 0, 5, 0, 10, 8000, 12, 25, 25, 1, 1, 1, 0, 0, 1)
;
-- 9. Player Types
INSERT INTO player_types (type, description)
    VALUES (1, 'NPC'),
    (2, 'Human Player')
;
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
    ('TWARP', 'TransWarp Drive', 100000, 0, 1, 1, 'MODULE', 1)
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
