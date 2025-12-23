-- 030_seeds.sql
-- Default data and seeding based on src/database.c
-- Manually restored and verified for PostgreSQL compatibility

BEGIN;

-- 1. Law Enforcement
INSERT INTO law_enforcement (id)  VALUES  (1)
ON CONFLICT  DO NOTHING;

-- 2. Alignment Bands
INSERT INTO alignment_band (id, code, name, min_align, max_align, is_good, is_evil, can_buy_iss, can_rob_ports)
 VALUES  (1, 'VERY_GOOD', 'Very Good', 750, 2000, TRUE, FALSE, TRUE, FALSE),
  (2, 'GOOD', 'Good', 250, 749, TRUE, FALSE, FALSE, FALSE),
  (3, 'NEUTRAL', 'Neutral', -249, 249, FALSE, FALSE, FALSE, FALSE),
  (4, 'SHADY', 'Shady', -500, -250, FALSE, TRUE, FALSE, TRUE),
  (5, 'VERY_EVIL', 'Very Evil', -1000, -501, FALSE, TRUE, FALSE, TRUE),
  (6, 'MONSTROUS', 'Monstrous', -2000, -1001, FALSE, TRUE, FALSE, TRUE)
ON CONFLICT  (id) DO NOTHING;

-- 3. Currencies
INSERT INTO currencies(code, name, minor_unit, is_default)
 VALUES  ('CRD', 'Galactic Credits', 1, TRUE)
ON CONFLICT  (code) DO NOTHING;

-- 4. Commodities
INSERT INTO commodities (id, code, name, base_price, volatility, illegal)
 VALUES  (1, 'ORE', 'Ore', 100, 20, 0),
  (2, 'ORG', 'Organics', 150, 30, 0),
  (3, 'EQU', 'Equipment', 200, 25, 0),
  (4, 'SLV', 'Slaves', 1000, 50, 1),
  (5, 'WPN', 'Weapons', 750, 40, 1),
  (6, 'DRG', 'Drugs', 500, 60, 1)
ON CONFLICT  (code) DO NOTHING;

-- 5. Ship Roles
INSERT INTO ship_roles (role_id, role, role_description) 
 VALUES  (1, 'owner', 'Legal owner; can sell/rename, set availability, assign others'),
  (2, 'pilot', 'Currently flying the ship; usually the active ship for the player'),
  (3, 'crew', 'Can board and use limited functions (e.g., scan, fire fighters)'),
  (4, 'leasee', 'Temporary control with limits; can pilot but not sell/rename'),
  (5, 'lender', 'Party that lent/leased the ship; can revoke lease'),
  (6, 'corp', 'Corporate ownership/control (for future org/corp features)'),
  (7, 'manager', 'Delegated admin; can assign crew/pilot but not sell')
ON CONFLICT  (role_id) DO NOTHING;

-- 6. Commissions
INSERT INTO commission (id, is_evil, min_exp, description)  VALUES  (1, FALSE, 0, 'Civilian'),
  (2, TRUE, 0, 'Civilian'),
  (3, FALSE, 100, 'Cadet'),
  (4, TRUE, 100, 'Thug'),
  (5, FALSE, 400, 'Ensign'),
  (6, TRUE, 400, 'Pirate'),
  (7, FALSE, 1000, 'Lieutenant'),
  (8, TRUE, 1000, 'Raider'),
  (9, FALSE, 2500, 'Lt. Commander'),
  (10, TRUE, 2500, 'Marauder'),
  (11, FALSE, 5000, 'Commander'),
  (12, TRUE, 5000, 'Buccaneer'),
  (13, FALSE, 10000, 'Captain'),
  (14, TRUE, 10000, 'Corsair'),
  (15, FALSE, 20000, 'Commodore'),
  (16, TRUE, 20000, 'Terrorist'),
  (17, FALSE, 35000, 'Rear Admiral'),
  (18, TRUE, 35000, 'Anarchist'),
  (19, FALSE, 75000, 'Vice Admiral'),
  (20, TRUE, 75000, 'Warlord'),
  (21, FALSE, 100000, 'Admiral'),
  (22, TRUE, 100000, 'Despot'),
  (23, FALSE, 150000, 'Fleet Admiral'),
  (24, TRUE, 150000, 'Tyrant'),
  (25, FALSE, 20000, 'Grand Admiral'),
  (26, TRUE, 200000, 'Warmonger'),
  (27, FALSE, 300000, 'Lord Commander'),
  (28, TRUE, 300000, 'Dread Pirate'),
  (29, FALSE, 400000, 'High Commander'),
  (30, TRUE, 400000, 'Cosmic Destroyer'),
  (31, FALSE, 550000, 'Star Marshal'),
  (32, TRUE, 550000, 'Galactic Menace'),
  (33, FALSE, 700000, 'Grand Star Marshal'),
  (34, TRUE, 700000, 'Void Reaver'),
  (35, FALSE, 1000000, 'Supreme Commander'),
  (36, TRUE, 1000000, 'Grim Reaper'),
  (37, FALSE, 1500000, 'Galactic Commander'),
  (38, TRUE, 1500000, 'Annihilator'),
  (39, FALSE, 2000000, 'Galactic Captain'),
  (40, TRUE, 3000000, 'Supreme Annihilator'),
  (41, FALSE, 4000000, 'Galactic Commodore'),
  (42, TRUE, 4000000, 'Chaos Bringer'),
  (43, FALSE, 5000000, 'Galactic Admiral'),
  (44, TRUE, 5000000, 'Death Lord')
ON CONFLICT  (id) DO NOTHING;

-- 7. Planet Types
INSERT INTO planettypes (code, typeDescription, typeName, 
  citadelUpgradeTime_lvl1, citadelUpgradeTime_lvl2, citadelUpgradeTime_lvl3, citadelUpgradeTime_lvl4, citadelUpgradeTime_lvl5, citadelUpgradeTime_lvl6, 
  citadelUpgradeOre_lvl1, citadelUpgradeOre_lvl2, citadelUpgradeOre_lvl3, citadelUpgradeOre_lvl4, citadelUpgradeOre_lvl5, citadelUpgradeOre_lvl6, 
  citadelUpgradeOrganics_lvl1, citadelUpgradeOrganics_lvl2, citadelUpgradeOrganics_lvl3, citadelUpgradeOrganics_lvl4, citadelUpgradeOrganics_lvl5, citadelUpgradeOrganics_lvl6, 
  citadelUpgradeEquipment_lvl1, citadelUpgradeEquipment_lvl2, citadelUpgradeEquipment_lvl3, citadelUpgradeEquipment_lvl4, citadelUpgradeEquipment_lvl5, citadelUpgradeEquipment_lvl6, 
  citadelUpgradeColonist_lvl1, citadelUpgradeColonist_lvl2, citadelUpgradeColonist_lvl3, citadelUpgradeColonist_lvl4, citadelUpgradeColonist_lvl5, citadelUpgradeColonist_lvl6, 
  maxColonist_ore, maxColonist_organics, maxColonist_equipment, 
  fighters, fuelProduction, organicsProduction, equipmentProduction, fighterProduction, 
  maxore, maxorganics, maxequipment, maxfighters, breeding)
 VALUES  ('M', 'Earth type', 'Earth', 4, 4, 5, 10, 5, 15, 300, 200, 500, 1000, 300, 1000, 200, 50, 250, 1200, 400, 1200, 250, 250, 500, 1000, 1000, 2000, 1000000, 2000000, 4000000, 6000000, 6000000, 6000000, 100000, 100000, 100000, 0, 0, 0, 0, 0, 10000000, 100000, 100000, 1000000, 0.75),
  ('L', 'Mountainous', 'Mountain', 2, 5, 5, 8, 5, 12, 150, 200, 600, 1000, 300, 1000, 100, 50, 250, 1200, 400, 1200, 150, 250, 700, 1000, 1000, 2000, 400000, 1400000, 3600000, 5600000, 7000000, 5600000, 200000, 200000, 200000, 0, 0, 0, 0, 0, 200000, 200000, 100000, 1000000, 0.24),
  ('O', 'Oceanic', 'Ocean', 6, 5, 8, 5, 4, 8, 500, 200, 600, 700, 300, 700, 200, 50, 400, 900, 400, 900, 400, 300, 650, 800, 1000, 1600, 1400000, 2400000, 4400000, 7000000, 8000000, 7000000, 100000, 1000000, 1000000, 0, 0, 0, 0, 0, 50000, 1000000, 50000, 1000000, 0.30),
  ('K', 'Desert Wasteland', 'Desert', 6, 5, 8, 5, 4, 8, 400, 300, 700, 700, 300, 700, 300, 80, 900, 900, 400, 900, 600, 400, 800, 800, 1000, 1600, 1000000, 2400000, 4000000, 7000000, 8000000, 7000000, 20000, 50000, 50000, 0, 0, 0, 0, 0, 20000, 50000, 10000, 1000000, 0.50),
  ('H', 'Volcanic', 'Volcano', 4, 5, 8, 12, 18, 8, 500, 300, 1200, 2000, 3000, 2000, 300, 100, 400, 2000, 1200, 2000, 600, 400, 1500, 2500, 2000, 5000, 800000, 1600000, 4400000, 7000000, 10000000, 7000000, 1000000, 10000, 10000, 0, 0, 0, 0, 0, 1000000, 10000, 100000, 1000000, 0.30),
  ('U', 'Gaseous', 'Gas Giant', 8, 4, 5, 5, 4, 8, 1200, 300, 500, 500, 200, 500, 400, 100, 500, 200, 200, 200, 2500, 400, 2000, 600, 600, 1200, 3000000, 3000000, 8000000, 6000000, 8000000, 6000000, 10000, 10000, 10000, 0, 0, 0, 0, 0, 10000, 10000, 10000, 1000000, -0.10),
  ('C', 'Glacial/Ice', 'Ice World', 5, 5, 7, 5, 4, 8, 400, 300, 600, 700, 300, 700, 300, 80, 400, 900, 400, 900, 600, 400, 650, 700, 1000, 1400, 1000000, 24000000, 4400000, 6600000, 9000000, 6600000, 20000, 50000, 20000, 0, 0, 0, 0, 0, 20000, 50000, 10000, 1000000, -0.10)
ON CONFLICT  (code) DO NOTHING;

-- 8. Ship Types
INSERT INTO shiptypes (name, basecost, maxattack, initialholds, maxholds, maxfighters, turns, maxmines, maxlimpets, maxgenesis, can_transwarp, transportrange, maxshields, offense, defense, maxbeacons, can_long_range_scan, can_planet_scan, maxphotons, max_cloaks, can_purchase, enabled)
 VALUES  ('Merchant Cruiser', 41300, 750, 20, 75, 2500, 3, 50, 0, 5, FALSE, 5, 400, 10, 10, 0, TRUE, TRUE, 0, 0, TRUE, TRUE),
  ('Scout Marauder', 15950, 250, 10, 25, 250, 2, 0, 0, 0, FALSE, 0, 100, 20, 20, 0, TRUE, TRUE, 0, 0, TRUE, TRUE),
  ('Missile Frigate', 100000, 2000, 12, 60, 5000, 3, 5, 0, 0, FALSE, 2, 400, 13, 13, 5, FALSE, FALSE, 1, 0, TRUE, TRUE),
  ('Battleship', 88500, 3000, 16, 80, 10000, 4, 25, 0, 1, FALSE, 8, 750, 16, 16, 50, TRUE, TRUE, 0, 0, TRUE, TRUE),
  ('Corporate Flagship', 163500, 6000, 20, 85, 20000, 3, 100, 0, 10, TRUE, 10, 1500, 12, 12, 100, TRUE, TRUE, 1, 0, TRUE, TRUE),
  ('Colonial Transport', 63600, 100, 50, 250, 200, 6, 0, 0, 5, FALSE, 7, 500, 6, 6, 10, FALSE, TRUE, 0, 0, TRUE, TRUE),
  ('Cargo Transport', 51950, 125, 50, 125, 400, 4, 1, 0, 2, FALSE, 5, 1000, 8, 8, 20, TRUE, TRUE, 0, 0, TRUE, TRUE),
  ('Merchant Freighter', 33400, 100, 30, 65, 300, 2, 2, 0, 2, FALSE, 5, 500, 8, 8, 20, TRUE, TRUE, 0, 0, TRUE, TRUE),
  ('Imperial Starship', 329000, 10000, 40, 150, 50000, 4, 125, 0, 10, TRUE, 15, 2000, 15, 15, 150, TRUE, TRUE, 1, 0, TRUE, TRUE),
  ('Havoc Gunstar', 79000, 1000, 12, 50, 10000, 3, 5, 0, 1, TRUE, 6, 3000, 13, 13, 5, TRUE, FALSE, 0, 0, TRUE, TRUE),
  ('Constellation', 72500, 2000, 20, 80, 5000, 3, 25, 0, 2, FALSE, 6, 750, 14, 14, 50, TRUE, TRUE, 0, 0, TRUE, TRUE),
  ('T''khasi Orion', 42500, 250, 30, 60, 750, 2, 5, 0, 1, FALSE, 3, 750, 11, 11, 20, TRUE, TRUE, 0, 0, TRUE, TRUE),
  ('Tholian Sentinel', 47500, 800, 10, 50, 2500, 4, 50, 0, 1, FALSE, 3, 4000, 1, 1, 10, TRUE, FALSE, 0, 0, TRUE, TRUE),
  ('Taurean Mule', 63600, 150, 50, 150, 300, 4, 0, 0, 1, FALSE, 5, 600, 5, 5, 20, TRUE, TRUE, 0, 0, TRUE, TRUE),
  ('Interdictor Cruiser', 539000, 15000, 10, 40, 100000, 15, 200, 0, 20, FALSE, 20, 4000, 12, 12, 100, TRUE, TRUE, 0, 0, TRUE, TRUE),
  ('Ferrengi Warship', 150000, 5000, 20, 100, 15000, 5, 20, 0, 5, FALSE, 10, 5000, 15, 15, 50, TRUE, TRUE, 1, 0, FALSE, TRUE),
  ('Imperial Starship (NPC)', 329000, 10000, 40, 150, 50000, 4, 125, 0, 10, TRUE, 15, 2000, 15, 15, 150, TRUE, TRUE, 1, 0, FALSE, TRUE),
  ('Orion Heavy Fighter Patrol', 150000, 5000, 20, 50, 20000, 5, 10, 0, 5, FALSE, 10, 5000, 20, 10, 25, TRUE, TRUE, 1, 0, FALSE, TRUE),
  ('Orion Scout/Looter', 80000, 4000, 10, 150, 5000, 5, 10, 0, 5, FALSE, 10, 3000, 8, 8, 25, TRUE, TRUE, 1, 0, FALSE, TRUE),
  ('Orion Contraband Runner', 120000, 3000, 10, 200, 3000, 5, 10, 0, 5, FALSE, 10, 4000, 10, 5, 25, TRUE, TRUE, 1, 0, FALSE, TRUE),
  ('Orion Smuggler''s Kiss', 130000, 5000, 15, 100, 10000, 5, 10, 0, 5, FALSE, 10, 5000, 15, 15, 25, TRUE, TRUE, 1, 0, FALSE, TRUE),
  ('Orion Black Market Guard', 180000, 6000, 20, 60, 8000, 5, 10, 0, 5, FALSE, 10, 8000, 12, 25, 25, TRUE, TRUE, 1, 0, FALSE, TRUE)
ON CONFLICT  (name) DO NOTHING;

-- 9. Player Types
INSERT INTO player_types (type, description)  VALUES  (1, 'NPC'),
  (2, 'Human Player')
ON CONFLICT  (type) DO NOTHING;

-- 10. NPC Ship Names
INSERT INTO npc_shipnames (id, name)  VALUES  (1, 'Starlight Voyager'),
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
-- ON CONFLICT (id) DO NOTHING;

-- 11. Tavern Names & Settings
INSERT INTO tavern_names (name, enabled, weight)  VALUES
('The Rusty Flange', TRUE, 10),
  ('The Starfall Inn', TRUE, 10),
  ('Orions Den', TRUE, 5),
  ('FedSpace Cantina', TRUE, 8)
ON CONFLICT  (name) DO NOTHING;

INSERT INTO tavern_settings (id, max_bet_per_transaction, daily_max_wager, enable_dynamic_wager_limit, graffiti_max_posts, notice_expires_days, buy_round_cost, buy_round_alignment_gain, loan_shark_enabled)
 VALUES  (1, 5000, 50000, 0, 100, 7, 1000, 5, 1)
ON CONFLICT  (id) DO NOTHING;

-- 12. Hardware Items
INSERT INTO hardware_items (code, name, price, requires_stardock, sold_in_class0, max_per_ship, category, enabled)  VALUES  ('FIGHTERS', 'Fighters', 100, FALSE, FALSE, NULL, 'FIGHTER', TRUE),
  ('SHIELDS', 'Shields', 200, FALSE, TRUE, NULL, 'SHIELD', TRUE),
  ('HOLDS', 'Holds', 50, FALSE, TRUE, NULL, 'HOLD', TRUE),
  ('GENESIS', 'Genesis Torpedo', 25000, TRUE, TRUE, NULL, 'SPECIAL', TRUE),
  ('DETONATOR', 'Atomic Detonator', 10000, TRUE, TRUE, NULL, 'SPECIAL', TRUE),
  ('CLOAK', 'Cloaking Device', 50000, FALSE, TRUE, 1, 'MODULE', TRUE),
  ('LSCANNER', 'Long-Range Scanner', 30000, FALSE, TRUE, 1, 'MODULE', TRUE),
  ('PSCANNER', 'Planet Scanner', 15000, FALSE, TRUE, 1, 'MODULE', TRUE),
  ('TWARP', 'TransWarp Drive', 100000, FALSE, TRUE, 1, 'MODULE', TRUE)
ON CONFLICT  (code) DO NOTHING;

-- 13. FedSpace Sectors (1-10)
INSERT INTO sectors (id, name, beacon, nebulae)  VALUES  (1, 'Fedspace 1', 'The Federation -- Do Not Dump!', 'The Federation'),
  (2, 'Fedspace 2', 'The Federation -- Do Not Dump!', 'The Federation'),
  (3, 'Fedspace 3', 'The Federation -- Do Not Dump!', 'The Federation'),
  (4, 'Fedspace 4', 'The Federation -- Do Not Dump!', 'The Federation'),
  (5, 'Fedspace 5', 'The Federation -- Do Not Dump!', 'The Federation'),
  (6, 'Fedspace 6', 'The Federation -- Do Not Dump!', 'The Federation'),
  (7, 'Fedspace 7', 'The Federation -- Do Not Dump!', 'The Federation'),
  (8, 'Fedspace 8', 'The Federation -- Do Not Dump!', 'The Federation'),
  (9, 'Fedspace 9', 'The Federation -- Do Not Dump!', 'The Federation'),
  (10, 'Fedspace 10', 'The Federation -- Do Not Dump!', 'The Federation')
ON CONFLICT  (id) DO NOTHING;

-- 14. NPC Players
-- FIX: Replaced TRUE/FALSE with now() or old timestamp
INSERT INTO players (id, number, is_npc, loggedin, name, passwd, sector, ship, type, commission, experience, alignment, credits)  VALUES  
  (1, 1, TRUE, now(), 'System', 'BOT', 1, 1, 1, 1, 0, 0, 1500),
  (2, 1, TRUE, now(), 'Federation Administrator', 'BOT', 1, 1, 1, 1, 0, 0, 1500),
  (3, 7, FALSE, (now() - interval '1 year'), 'newguy', 'pass123', 1, 1, 2, 1, 0, 0, 1500),
  (4, 8, TRUE, (now() - interval '1 year'), 'ai_qa_bot', 'quality', 1, 0, 2, 1, 0, 0, 10000)
ON CONFLICT  (id) DO NOTHING;

-- Orion Captains (Placeholder NPC players)
-- FIX: Replaced TRUE with now()
INSERT INTO players (type, is_npc, loggedin, name, passwd, sector, experience, alignment, credits)  VALUES  
  (1, TRUE, now(), 'Zydras, Heavy Fighter Captain', '', 50, 550000, -10000, 9000),
  (1, TRUE, now(), 'Krell, Scout Captain', '', 50, 300000, -1900, 8000),
  (1, TRUE, now(), 'Vex, Contraband Captain', '', 50, 200000, -1000, 7000),
  (1, TRUE, now(), 'Jaxx, Smuggler Captain', '', 50, 100000, -750, 6000),
  (1, TRUE, now(), 'Sira, Market Guard Captain', '', 50, 10000, -500, 5000),
  (1, TRUE, now(), 'Fer', '', 50, 200, -100, 1000)
ON CONFLICT  DO NOTHING;

-- 15. Corporations
INSERT INTO corporations (name, owner_id, tag)  VALUES
('Orion Syndicate', (SELECT id FROM players WHERE name = 'Zydras' LIMIT 1), 'ORION'),
('Ferrengi Alliance', (SELECT id FROM players WHERE name LIKE 'Fer%' LIMIT 1), 'FENG')
ON CONFLICT DO NOTHING;

-- 16. Planets
-- FIX: Replaced EXTRACT(EPOCH...) with now() for TIMESTAMPTZ column
INSERT INTO planets
(num, sector, name, owner_id, owner_type, population, type, creator, colonist, fighters, created_at, created_by, genesis_flag)
 VALUES
 (1, 1, 'Earth', (SELECT id FROM players where name='Federation Administrator'), 'npc_fraction', 8000000000, (SELECT id FROM planettypes WHERE code='M'), 'System', 18000000, 1000000, now(), 1, TRUE),
 (2, 2, 'Ferringhi Homeworld', COALESCE((SELECT id FROM players where name like 'Fer%' LIMIT 1),1), 'npc_faction', 8000000000, (SELECT id FROM planettypes WHERE code='M'), 'System', 18000000, 1000000, now(), 1, TRUE),
 (3, 2, 'Orion Hideout', COALESCE((SELECT id FROM players WHERE name LIKE '%Zydras%' LIMIT 1), 1), 'npc_faction', 20000000, (SELECT id FROM planettypes WHERE code='M'), 'Syndicate', 20010000, 200000, now(), 1, TRUE)
ON CONFLICT  DO NOTHING;

-- 17. Planet Goods
INSERT INTO planet_goods (planet_id, commodity, quantity, max_capacity, production_rate)  VALUES  ((SELECT id FROM planets WHERE name='Earth' LIMIT 1), 'ore', 10000000, 10000000, 0),
  ((SELECT id FROM planets WHERE name='Earth' LIMIT 1), 'organics', 10000000, 10000000, 0),
  ((SELECT id FROM planets WHERE name='Earth' LIMIT 1), 'equipment', 10000000, 10000000, 0),
  ((SELECT id FROM planets WHERE name='Ferringhi Homeworld'  LIMIT 1), 'ore', 10000000, 10000000, 0),
  ((SELECT id FROM planets WHERE name='Ferringhi Homeworld' LIMIT 1), 'organics', 50000000, 50000000, 10),
  ((SELECT id FROM planets WHERE name='Ferringhi Homeworld' LIMIT 1), 'equipment', 10000000, 10000000, 0),
  ((SELECT id FROM planets WHERE name='Orion Hideout' LIMIT 1), 'ore', 50000000, 50000000, 10),
  ((SELECT id FROM planets WHERE name='Orion Hideout' LIMIT 1), 'organics', 100, 100, 0),
  ((SELECT id FROM planets WHERE name='Orion Hideout' LIMIT 1), 'equipment', 30000000, 30000000, 10)
ON CONFLICT  DO NOTHING;


-- [MOVED UP] 21. Economy & Interest Policies (MUST BE DONE BEFORE PORTS)
INSERT INTO economy_curve (id, curve_name, base_restock_rate, price_elasticity, target_stock, volatility_factor) 
 VALUES  (1, 'default', 0.1, 0.5, 10000, 0.2)
ON CONFLICT  (id) DO NOTHING;

INSERT INTO bank_interest_policy (id, apr_bps, compounding, last_run_at, currency)
 VALUES  (1, 0, 'none', NULL, 'CRD')
ON CONFLICT  (id) DO NOTHING;

INSERT INTO corp_interest_policy (id, apr_bps, compounding, last_run_at, currency)
 VALUES  (1, 0, 'none', CURRENT_TIMESTAMP, 'CRD')
ON CONFLICT  (id) DO NOTHING;

-- 18. Earth Port (Now this will work because Curve #1 exists)
INSERT INTO ports (number, name, sector, size, techlevel, invisible, economy_curve_id) 
 VALUES  (1, 'Earth Port', 1, 10, 10, FALSE, 1)
ON CONFLICT  DO NOTHING;

-- Initialize Stock for Earth Port
INSERT INTO entity_stock (entity_type, entity_id, commodity_code, quantity)  VALUES 
  ('port', 1, 'ORE', 10000),
  ('port', 1, 'ORG', 10000),
  ('port', 1, 'EQU', 10000)
ON CONFLICT  DO NOTHING;

-- 19. FedSpace Warps
INSERT INTO sector_warps (from_sector, to_sector)  VALUES  
  (1, 2), (1, 3), (1, 4), (1, 5), (1, 6), (1, 7),
  (2, 1), (2, 3), (2, 7), (2, 8), (2, 9), (2, 10),
  (3, 1), (3, 2), (3, 4),
  (4, 1), (4, 3), (4, 5),
  (5, 1), (5, 4), (5, 6),
  (6, 1), (6, 5), (6, 7),
  (7, 1), (7, 2), (7, 6), (7, 8),
  (8, 2), (8, 7),
  (9, 2), (9, 10),
  (10, 2), (10, 9)
ON CONFLICT  DO NOTHING;

-- 20. Initial Ships & Ownership
-- (This block is fine as is, but ensuring the order is maintained)
INSERT INTO ships (id, name, type_id, attack, holds, mines, limpets, fighters, genesis, photons, sector, shields, beacons, colonists, equipment, organics, ore, flags, cloaking_devices, cloaked, ported, onplanet) 
 VALUES  (1, 'Bit Banger', (SELECT id FROM shiptypes WHERE name='Merchant Cruiser'), 110, 20, 25, 5, 2300, 5, 1, 1, 400, 10, 5, 5, 5, 5, 0, 5, NULL, 1, 1)
ON CONFLICT  (id) DO NOTHING;

INSERT INTO ship_ownership (player_id, ship_id, is_primary, role_id)  VALUES  (1, 1, TRUE, 1)
ON CONFLICT  DO NOTHING;

-- 18. Earth Port
INSERT INTO ports (number, name, sector, size, techlevel, invisible, economy_curve_id) 
 VALUES  (1, 'Earth Port', 1, 10, 10, FALSE, 1)
ON CONFLICT  DO NOTHING;

INSERT INTO entity_stock (entity_type, entity_id, commodity_code, quantity)  VALUES  ('port', 1, 'ORE', 10000),
  ('port', 1, 'ORG', 10000),
  ('port', 1, 'EQU', 10000)
ON CONFLICT  DO NOTHING;

-- 20. Initial Ships & Ownership
INSERT INTO ships (id, name, type_id, attack, holds, mines, limpets, fighters, genesis, photons, sector, shields, beacons, colonists, equipment, organics, ore, flags, cloaking_devices, cloaked, ported, onplanet) 
 VALUES  (1, 'Bit Banger', (SELECT id FROM shiptypes WHERE name='Merchant Cruiser'), 110, 20, 25, 5, 2300, 5, 1, 1, 400, 10, 5, 5, 5, 5, 0, 5, NULL, 1, 1)
ON CONFLICT  (id) DO NOTHING;

INSERT INTO ship_ownership (player_id, ship_id, is_primary, role_id)  VALUES  (1, 1, TRUE, 1)
ON CONFLICT  DO NOTHING;

-- 21. Economy & Interest Policies
INSERT INTO economy_curve (id, curve_name, base_restock_rate, price_elasticity, target_stock, volatility_factor) 
 VALUES  (1, 'default', 0.1, 0.5, 10000, 0.2)
ON CONFLICT  (id) DO NOTHING;

INSERT INTO bank_interest_policy (id, apr_bps, compounding, last_run_at, currency)
 VALUES  (1, 0, 'none', NULL, 'CRD')
ON CONFLICT  (id) DO NOTHING;

INSERT INTO corp_interest_policy (id, apr_bps, compounding, last_run_at, currency)
 VALUES  (1, 0, 'none', CURRENT_TIMESTAMP, 'CRD')
ON CONFLICT  (id) DO NOTHING;

-- 22. S2S Keys
-- KEEPING EXTRACT(EPOCH) because s2s_keys.created_ts is BIGINT in your schema
INSERT INTO s2s_keys(key_id, key_b64, is_default_tx, active, created_ts)
 VALUES  ('k0', 'c3VwZXJzZWNyZXRrZXlzZWNyZXRrZXlzZWNyZXQxMjM0NTY3OA==', 1, TRUE, CURRENT_TIMESTAMP)
ON CONFLICT  (key_id) DO NOTHING;

-- 23. Cron Tasks
-- FIX: Replaced EXTRACT(EPOCH...) with now() for next_due_at (TIMESTAMPTZ)
INSERT INTO cron_tasks(name,schedule,last_run_at,next_due_at,enabled,payload)  VALUES
('daily_turn_reset', 'daily@03:00Z', NULL, now(), TRUE, NULL),
  ('terra_replenish', 'daily@04:00Z', NULL, now(), TRUE, NULL),
  ('planet_growth', 'every:10m', NULL, now(), TRUE, NULL),
  ('fedspace_cleanup', 'every:2m', NULL, now(), TRUE, NULL),
  ('autouncloak_sweeper', 'every:15m', NULL, now(), TRUE, NULL),
  ('npc_step', 'every:30s', NULL, now(), TRUE, NULL),
  ('broadcast_ttl_cleanup', 'every:5m', NULL, now(), TRUE, NULL),
  ('daily_news_compiler', 'daily@06:00Z', NULL, now(), TRUE, NULL),
  ('traps_process', 'every:1m', NULL, now(), TRUE, NULL),
  ('cleanup_old_news', 'daily@07:00Z', NULL, now(), TRUE, NULL),
  ('limpet_ttl_cleanup', 'every:5m', NULL, now(), TRUE, NULL),
  ('daily_lottery_draw', 'daily@23:00Z', NULL, now(), TRUE, NULL),
  ('deadpool_resolution_cron', 'daily@01:00Z', NULL, now(), TRUE, NULL),
  ('tavern_notice_expiry_cron', 'daily@07:00Z', NULL, now(), TRUE, NULL),
  ('loan_shark_interest_cron', 'daily@00:00Z', NULL, now(), TRUE, NULL),
  ('dividend_payout', 'daily@05:00Z', NULL, now(), TRUE, NULL),
  ('daily_stock_price_recalculation', 'daily@04:30Z', NULL, now(), TRUE, NULL),
  ('daily_market_settlement', 'daily@05:30Z', NULL, now(), TRUE, NULL),
  ('system_notice_ttl', 'daily@00:05Z', NULL, now(), TRUE, NULL),
  ('shield_regen', 'every:1m', NULL, now(), TRUE, '{"regen_percent":5}'),
  ('deadletter_retry', 'every:1m', NULL, now(), TRUE, NULL),
  ('daily_corp_tax', 'daily@05:00Z', NULL, now(), TRUE, NULL)
ON CONFLICT  (name) DO NOTHING;

-- Engine Offset
INSERT INTO engine_offset(key, last_event_id, last_event_ts)  VALUES  ('events', 0, 0)
ON CONFLICT  (key) DO NOTHING;

COMMIT;
