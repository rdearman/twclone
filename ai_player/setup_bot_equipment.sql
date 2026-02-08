-- Setup script to configure AI QA bot equipment and alignment
-- Runs after bots are created by spawn.py
-- 
-- Rules:
--   - Odd-numbered bots (001, 003, 005, ...) are fully equipped
--   - Even-numbered bots (002, 004, 006, ...) are minimally equipped
--   - Every 3rd bot (003, 006, 009, ...) has evil alignment (-666)

-- Reset alignment to default (1) for all QA bots first
UPDATE players SET alignment = 1 WHERE name LIKE 'ai_qa_bot_%';

-- Now set only every 3rd bot to -666 (evil)
UPDATE players SET alignment = -666
WHERE name LIKE 'ai_qa_bot_%'
  AND CAST(regexp_replace(name, '^.*_(\d+)$', '\1') AS INTEGER) % 3 = 0;

-- Update ship equipment based on bot number (odd = fully equipped)
UPDATE ships SET
    -- Systems (boolean flags)
    has_transwarp = (
        CAST(regexp_replace(p.name, '^.*_(\d+)$', '\1') AS INTEGER) % 2 = 1
    )::boolean,
    has_planet_scanner = (
        CAST(regexp_replace(p.name, '^.*_(\d+)$', '\1') AS INTEGER) % 2 = 1
    )::boolean,
    has_long_range_scanner = (
        CAST(regexp_replace(p.name, '^.*_(\d+)$', '\1') AS INTEGER) % 2 = 1
    )::boolean,
    cloaking_devices = CASE WHEN (
        CAST(regexp_replace(p.name, '^.*_(\d+)$', '\1') AS INTEGER) % 2 = 1
    ) THEN 1 ELSE 0 END,
    
    -- Ship resources (cargo items)
    fighters = CASE WHEN (
        CAST(regexp_replace(p.name, '^.*_(\d+)$', '\1') AS INTEGER) % 2 = 1
    ) THEN 50 ELSE 1 END,
    
    mines = CASE WHEN (
        CAST(regexp_replace(p.name, '^.*_(\d+)$', '\1') AS INTEGER) % 2 = 1
    ) THEN 25 ELSE 0 END,
    
    limpets = CASE WHEN (
        CAST(regexp_replace(p.name, '^.*_(\d+)$', '\1') AS INTEGER) % 2 = 1
    ) THEN 20 ELSE 0 END,
    
    genesis = CASE WHEN (
        CAST(regexp_replace(p.name, '^.*_(\d+)$', '\1') AS INTEGER) % 2 = 1
    ) THEN 10 ELSE 0 END,
    
    detonators = CASE WHEN (
        CAST(regexp_replace(p.name, '^.*_(\d+)$', '\1') AS INTEGER) % 2 = 1
    ) THEN 15 ELSE 0 END,
    
    probes = CASE WHEN (
        CAST(regexp_replace(p.name, '^.*_(\d+)$', '\1') AS INTEGER) % 2 = 1
    ) THEN 12 ELSE 0 END,
    
    photons = CASE WHEN (
        CAST(regexp_replace(p.name, '^.*_(\d+)$', '\1') AS INTEGER) % 2 = 1
    ) THEN 30 ELSE 0 END,
    
    beacons = CASE WHEN (
        CAST(regexp_replace(p.name, '^.*_(\d+)$', '\1') AS INTEGER) % 2 = 1
    ) THEN 5 ELSE 0 END,
    
    -- Shields
    shields = CASE WHEN (
        CAST(regexp_replace(p.name, '^.*_(\d+)$', '\1') AS INTEGER) % 2 = 1
    ) THEN 10 ELSE 1 END,
    
    installed_shields = CASE WHEN (
        CAST(regexp_replace(p.name, '^.*_(\d+)$', '\1') AS INTEGER) % 2 = 1
    ) THEN 10 ELSE 1 END
FROM players p
WHERE ships.ship_id = p.ship_id
  AND p.name LIKE 'ai_qa_bot_%';

-- Verify the changes
SELECT 
    p.player_id,
    p.name,
    p.alignment,
    s.fighters,
    s.shields,
    s.mines,
    s.limpets,
    s.genesis,
    s.detonators,
    s.probes,
    s.photons,
    s.beacons,
    s.cloaking_devices,
    s.has_transwarp,
    s.has_planet_scanner,
    s.has_long_range_scanner
FROM players p
LEFT JOIN ships s ON p.ship_id = s.ship_id
WHERE p.name LIKE 'ai_qa_bot_%'
ORDER BY p.player_id;
