-- Generated from PostgreSQL 095 player registration.sql -> MySQL
-- 110_player_registration.sql
-- Player account registration (runs AFTER 100_procs.sql so register_player function exists)
-- Create starter ship for ai_qa_bot (NPC)
-- PostgreSQL DO block removed (not directly supported in MySQL)

-- PostgreSQL DO block removed (not directly supported in MySQL)

-- Create starter ship for newguy (Human Player)
-- PostgreSQL DO block removed (not directly supported in MySQL)

-- Create starter ship for ai_qa_bot (NPC)
-- PostgreSQL DO block removed (not directly supported in MySQL)

-- Orion Captains (Placeholder NPC players)
-- FIX: Replaced 1 with now()
INSERT INTO players (type, is_npc, loggedin, name, passwd, sector_id, experience, alignment, credits)
    VALUES (1, 1, now(), 'Zydras, Heavy Fighter Captain', '', 1, 550000, -10000, 9000),
    (1, 1, now(), 'Krell, Scout Captain', '', 1, 300000, -1900, 8000),
    (1, 1, now(), 'Vex, Contraband Captain', '', 1, 200000, -1000, 7000),
    (1, 1, now(), 'Jaxx, Smuggler Captain', '', 1, 100000, -750, 6000),
    (1, 1, now(), 'Sira, Market Guard Captain', '', 1, 10000, -500, 5000),
    (1, 1, now(), 'Fer', '', 1, 200, -100, 1000);;

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
    'FENG');;

-- 16. Planets
-- FIX: Replaced EXTRACT(EPOCH...) with now() for TIMESTAMPTZ column
