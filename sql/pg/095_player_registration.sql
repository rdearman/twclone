-- 110_player_registration.sql
-- Player account registration (runs AFTER 100_procs.sql so register_player function exists)
-- Create starter ship for ai_qa_bot (NPC)
DO $$
DECLARE
    v_player_id bigint;
BEGIN
    IF NOT EXISTS (
        SELECT
            1
        FROM
            players
        WHERE
            name = 'System') THEN
    v_player_id := register_player ('System', 'BOT', 'Used Scout Marauder', FALSE, 1, 1);
END IF;
END
$$;

DO $$
DECLARE
    v_player_id bigint;
BEGIN
    IF NOT EXISTS (
        SELECT
            1
        FROM
            players
        WHERE
            name = 'Federation Administrator') THEN
    v_player_id := register_player ('Federation Administrator', 'BOT', 'Used Scout Marauder', FALSE, 1, 1);
END IF;
END
$$;

-- Create starter ship for newguy (Human Player)
DO $$
DECLARE
    v_player_id bigint;
BEGIN
    IF NOT EXISTS (
        SELECT
            1
        FROM
            players
        WHERE
            name = 'newguy') THEN
    v_player_id := register_player ('newguy', 'pass123', 'Bit Banger', FALSE, 1, 2);
END IF;
END
$$;

-- Create starter ship for ai_qa_bot (NPC)
DO $$
DECLARE
    v_player_id bigint;
BEGIN
    IF NOT EXISTS (
        SELECT
            1
        FROM
            players
        WHERE
            name = 'ai_qa_bot') THEN
    v_player_id := register_player ('ai_qa_bot', 'quality', 'Used Scout Marauder', TRUE, 1, 3);
END IF;
END
$$;

-- Orion Captains (Placeholder NPC players)
-- FIX: Replaced TRUE with now()
INSERT INTO players (type, is_npc, loggedin, name, passwd, sector_id, experience, alignment, credits)
    VALUES (1, TRUE, now(), 'Zydras, Heavy Fighter Captain', '', 1, 550000, -10000, 9000),
    (1, TRUE, now(), 'Krell, Scout Captain', '', 1, 300000, -1900, 8000),
    (1, TRUE, now(), 'Vex, Contraband Captain', '', 1, 200000, -1000, 7000),
    (1, TRUE, now(), 'Jaxx, Smuggler Captain', '', 1, 100000, -750, 6000),
    (1, TRUE, now(), 'Sira, Market Guard Captain', '', 1, 10000, -500, 5000),
    (1, TRUE, now(), 'Fer', '', 1, 200, -100, 1000)
ON CONFLICT
    DO NOTHING;

-- 15. Corporations
INSERT INTO corporations (name, owner_id, tag)
    VALUES ('Orion Syndicate', (
            SELECT
                player_id
            FROM
                players
            WHERE
                name = 'Zydras'
            LIMIT 1),
        'ORION'),
('Ferrengi Alliance',
    (
        SELECT
            player_id
        FROM
            players
        WHERE
            name LIKE 'Fer%'
        LIMIT 1),
    'FENG')
ON CONFLICT
    DO NOTHING;

-- 16. Planets
-- FIX: Replaced EXTRACT(EPOCH...) with now() for TIMESTAMPTZ column
