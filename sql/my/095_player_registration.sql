-- Generated from PostgreSQL 095 player registration.sql -> MySQL
-- 110_player_registration.sql
-- Player account registration (runs AFTER 100_procs.sql so register_player function exists)

-- Preserve the bootstrap identities used by the PostgreSQL seed and V2
-- operational/test paths. These encoded hashes are copied from the canonical
-- PostgreSQL seed; no new bootstrap credentials are introduced here.
DELIMITER $$
DROP PROCEDURE IF EXISTS seed_bootstrap_players$$
CREATE PROCEDURE seed_bootstrap_players()
BEGIN
    DECLARE v_player_id BIGINT DEFAULT NULL;

    SELECT player_id INTO v_player_id FROM players WHERE name = 'System' LIMIT 1;
    IF v_player_id IS NULL THEN
        SET v_player_id = register_player(
            'System',
            '$argon2id$v=19$m=65536,t=3,p=1$j/0jDwAJQQUwfgDYQjGAAA$YaJmLV0pAMHtG+UfJlb84gIGN+8lRpg3h8uypyGgVT8',
            'Used Scout Marauder', FALSE, 1);
    END IF;
    UPDATE players SET type = 1 WHERE player_id = v_player_id;

    SET v_player_id = NULL;
    SELECT player_id INTO v_player_id FROM players WHERE name = 'Federation Administrator' LIMIT 1;
    IF v_player_id IS NULL THEN
        SET v_player_id = register_player(
            'Federation Administrator',
            '$argon2id$v=19$m=65536,t=3,p=1$yJ6dAW16yJ/1FzQKXDJiNw$7fH9ojxGhj3sdhQclML251ExOv7sQWJdTDITKzPkkik',
            'Used Scout Marauder', FALSE, 1);
    END IF;
    UPDATE players SET type = 1 WHERE player_id = v_player_id;
END$$
CALL seed_bootstrap_players()$$
DROP PROCEDURE seed_bootstrap_players$$
DELIMITER ;

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
    VALUES (1, 1, now(), 'Zydras, Heavy Fighter Captain', '$argon2id$v=19$m=65536,t=3,p=1$/twfzOyfqftZ5LRE14qLig$suUaDIcqYopqes1HnS+wuZmEngZVTdS2OHmsIJ0AX7M', 1, 550000, -10000, 9000),
    (1, 1, now(), 'Krell, Scout Captain', '$argon2id$v=19$m=65536,t=3,p=1$VUaeg7VC534uXwgR3o/kUw$aEwPaGTtSnJ4sBStCjnzA+v1cwOGPMgI3yGiBdm/ztY', 1, 300000, -1900, 8000),
    (1, 1, now(), 'Vex, Contraband Captain', '$argon2id$v=19$m=65536,t=3,p=1$edZm8Fczhi5hp4iZ6yn3cg$vbxsw+GJeXihTMyRiOIBKoM10voywkSAXjaX2LiBUvw', 1, 200000, -1000, 7000),
    (1, 1, now(), 'Jaxx, Smuggler Captain', '$argon2id$v=19$m=65536,t=3,p=1$vs6Wb7PbLaCIbDZ5kC1Erw$3Jl3hPUIPkkHxAhVElsHMicRIYsirDNggUN+UIwhceQ', 1, 100000, -750, 6000),
    (1, 1, now(), 'Sira, Market Guard Captain', '$argon2id$v=19$m=65536,t=3,p=1$RH5JajjREjd+HFyF6vQ2jQ$Jw21DdHOdQUAR/MssJ69sjVR1h6/KJQtlK6jKXmId2M', 1, 10000, -500, 5000),
    (1, 1, now(), 'Fer', '$argon2id$v=19$m=65536,t=3,p=1$pnFXK40uC5w0bhnhXmDBVg$4CuFnvcYChpVw/GE2jFbScGCnx7fmVYWBc04gHjsngs', 1, 200, -100, 1000);;

-- 15. Corporations
INSERT INTO corporations (name, owner_id, tag)
    VALUES ('Orion Syndicate', (
            SELECT
                player_id
            FROM
                players
            WHERE
                name LIKE 'Zydras%'
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
