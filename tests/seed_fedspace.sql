PRAGMA foreign_keys=ON;
BEGIN;

-- 0) Make sure sectors 1 and 2 exist (sectors: id,name,beacon,nebulae)
INSERT OR IGNORE INTO sectors(id, name) VALUES (1, 'Sector 1');
INSERT OR IGNORE INTO sectors(id, name) VALUES (2, 'Sector 2');

-- 1) Create two test players (players: name, passwd are NOT NULL)
INSERT OR IGNORE INTO players(name, passwd) VALUES ('test_cron_p1', 'x');
INSERT OR IGNORE INTO players(name, passwd) VALUES ('test_cron_p2', 'x');

-- Capture their IDs
WITH p AS (
  SELECT
    (SELECT id FROM players WHERE name='test_cron_p1' LIMIT 1) AS p1,
    (SELECT id FROM players WHERE name='test_cron_p2' LIMIT 1) AS p2
)
-- 2) Insert 7 ships in sector 1 (ships: name, location, fighters; perms has a default)
INSERT INTO ships(name, location, fighters) SELECT 'test_tow_1_' || i, 1, 1000 FROM generate_series(1,7) AS g(i);

-- 3) Insert 2 ships in sector 2
INSERT INTO ships(name, location, fighters) SELECT 'test_tow_2_' || i, 2, 1000 FROM generate_series(1,2) AS g(i);

-- If you don't have generate_series(), use explicit rows:
-- INSERT INTO ships(name, location, fighters) VALUES
--   ('test_tow_1_1',1,1000),('test_tow_1_2',1,1000),('test_tow_1_3',1,1000),
--   ('test_tow_1_4',1,1000),('test_tow_1_5',1,1000),('test_tow_1_6',1,1000),
--   ('test_tow_1_7',1,1000),
--   ('test_tow_2_1',2,1000),('test_tow_2_2',2,1000);

-- 4) Ownership links (ship_ownership: ship_id, player_id, role_id are NOT NULL)
--    Assign sector 1 ships to p1, sector 2 ships to p2, role_id=1
INSERT INTO ship_ownership(ship_id, player_id, role_id, is_primary)
SELECT sh.id,
       (SELECT id FROM players WHERE name='test_cron_p1' LIMIT 1),
       1, 1
FROM ships sh
WHERE sh.name LIKE 'test_tow_1_%';

INSERT INTO ship_ownership(ship_id, player_id, role_id, is_primary)
SELECT sh.id,
       (SELECT id FROM players WHERE name='test_cron_p2' LIMIT 1),
       1, 1
FROM ships sh
WHERE sh.name LIKE 'test_tow_2_%';

COMMIT;

-- Verify
SELECT id, name, location, fighters FROM ships WHERE name LIKE 'test_tow_%' ORDER BY id;
SELECT so.ship_id, so.player_id, so.role_id FROM ship_ownership so
JOIN ships sh ON sh.id=so.ship_id
WHERE sh.name LIKE 'test_tow_%'
ORDER BY so.ship_id;
