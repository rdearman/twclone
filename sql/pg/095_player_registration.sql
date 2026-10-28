-- 110_player_registration.sql
-- Player account registration (runs AFTER 100_procs.sql so register_player function exists)
-- Create ai_qa_bot as an ordinary, login-capable QA player.
DO $$
DECLARE
    v_player_id integer;
BEGIN
    IF NOT EXISTS (
        SELECT
            1
        FROM
            players
        WHERE
            name = 'System') THEN
    v_player_id := register_player ('System', '$argon2id$v=19$m=65536,t=3,p=1$j/0jDwAJQQUwfgDYQjGAAA$YaJmLV0pAMHtG+UfJlb84gIGN+8lRpg3h8uypyGgVT8', 'Used Scout Marauder', FALSE, 1, 1);
END IF;
END
$$;

DO $$
DECLARE
    v_player_id integer;
BEGIN
    IF NOT EXISTS (
        SELECT
            1
        FROM
            players
        WHERE
            name = 'Federation Administrator') THEN
    v_player_id := register_player ('Federation Administrator', '$argon2id$v=19$m=65536,t=3,p=1$yJ6dAW16yJ/1FzQKXDJiNw$7fH9ojxGhj3sdhQclML251ExOv7sQWJdTDITKzPkkik', 'Used Scout Marauder', FALSE, 1, 1);
END IF;
END
$$;

-- Create starter ship for newguy (Human Player)
DO $$
DECLARE
    v_player_id integer;
BEGIN
    IF NOT EXISTS (
        SELECT
            1
        FROM
            players
        WHERE
            name = 'newguy') THEN
    v_player_id := register_player ('newguy', '$argon2id$v=19$m=65536,t=3,p=1$m9e+qkJe5du1SC25jkIB0A$fd2PpiLEjU5d2AXlAf1nznGko41GPU7Z2jxX3gX0QZE', 'Bit Banger', FALSE, 1, 2);
END IF;
END
$$;

-- Seed reusable ai_qa_bot as an ordinary-player QA fixture.
DO $$
DECLARE
    v_player_id integer;
BEGIN
    IF NOT EXISTS (
        SELECT
            1
        FROM
            players
        WHERE
            name = 'ai_qa_bot') THEN
    v_player_id := register_player ('ai_qa_bot', '$argon2id$v=19$m=65536,t=3,p=1$NTSvCAXrheLkz/vijxs6IA$0Nxu/GtuIyG2Kwl35g9gYdf41/xA5Ki2WsZox99b+5Y', 'Used Scout Marauder', FALSE, 1, 2);
END IF;
END
$$;

-- Orion Captains (Placeholder NPC players)
-- FIX: Replaced TRUE with now()
INSERT INTO players (type, is_npc, loggedin, name, passwd, sector_id, experience, alignment, credits)
    VALUES (1, TRUE, now(), 'Zydras, Heavy Fighter Captain', '$argon2id$v=19$m=65536,t=3,p=1$XbEcXVeUHFlj+JPahA7OSA$msDxJff7eHo3Gdg+Xezovvg1gnSG1ESrS6tEvxaR7xo', 1, 550000, -10000, 9000),
    (1, TRUE, now(), 'Krell, Scout Captain', '$argon2id$v=19$m=65536,t=3,p=1$Bd5B78azo5VXB33TxckeWw$acsrQfcUVcaNgx00vPTfJFmrpYnLFGNFofG8uELzJ4M', 1, 300000, -1900, 8000),
    (1, TRUE, now(), 'Vex, Contraband Captain', '$argon2id$v=19$m=65536,t=3,p=1$uh0XcJ5Av47kdCJGUiWWsQ$j2B6ZXYqBhhqZxGgHR3iDWzj/nue/ZwUwUcsZ6brxmY', 1, 200000, -1000, 7000),
    (1, TRUE, now(), 'Jaxx, Smuggler Captain', '$argon2id$v=19$m=65536,t=3,p=1$0P0gfdBJG2ilahD+LpyHag$VX6ptOsHLtADMqKerRxHblNK0NtzHItBgtWgykI79yA', 1, 100000, -750, 6000),
    (1, TRUE, now(), 'Sira, Market Guard Captain', '$argon2id$v=19$m=65536,t=3,p=1$3zXehq9RBU2ZJTqz8tdTMg$DJxHy6MuaNugfLRTArm8/0w+cRFzXDaR68NBkekoNjs', 1, 10000, -500, 5000),
    (1, TRUE, now(), 'Fer', '$argon2id$v=19$m=65536,t=3,p=1$FEAPcY7tvc/M8LnFAXfqsA$QT5gZiL9gFqp8r0LYSdVPnQfBX7TpYQkB+iD5z0r0bs', 1, 200, -100, 1000)
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
                name LIKE 'Zydras%'
            LIMIT 1),
        'ORION'),
('Ferengi Alliance',
    (
        SELECT
            player_id
        FROM
            players
        WHERE
            name = 'Fer'
        LIMIT 1),
    'FENG')
ON CONFLICT
    DO NOTHING;

-- 16. Planets
-- FIX: Replaced EXTRACT(EPOCH...) with now() for TIMESTAMPTZ column
