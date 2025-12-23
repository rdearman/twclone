-- Generated from sqlite_schema.sql -> Postgres

CREATE OR REPLACE VIEW longest_tunnels AS
 WITH RECURSIVE
 all_sectors AS (
 SELECT from_sector AS id FROM sector_warps
 UNION
 SELECT to_sector AS id FROM sector_warps
 ),
 outdeg AS (
 SELECT a.id, COALESCE(COUNT(w.to_sector),0) AS deg
 FROM all_sectors a
 LEFT JOIN sector_warps w ON w.from_sector = a.id
 GROUP BY a.id
 ),
 edges AS (
 SELECT from_sector, to_sector FROM sector_warps
 ),
 entry AS (
 SELECT e.from_sector AS entry, e.to_sector AS next
 FROM edges e
 JOIN outdeg df ON df.id = e.from_sector AND df.deg > 1
 JOIN outdeg dn ON dn.id = e.to_sector AND dn.deg = 1
),
rec(entry, curr, path, steps) AS (
 SELECT entry, next, (entry)::text || '->' || (next)::text, 1
 FROM entry
 UNION ALL
 SELECT r.entry, e.to_sector,
 r.path || '->' || (e.to_sector)::text,
 r.steps + 1
 FROM rec r
 JOIN edges e ON e.from_sector = r.curr
 JOIN outdeg d ON d.id = r.curr AND d.deg = 1
 WHERE position(('->' || e.to_sector::text || '->') IN ('->' || r.path || '->')) = 0
)
SELECT
 r.entry AS entry_sector,
 r.curr AS exit_sector,
 r.path AS tunnel_path,
 r.steps AS tunnel_length_edges
FROM rec r
JOIN outdeg d_exit ON d_exit.id = r.curr
WHERE d_exit.deg <> 1 AND r.steps >= 2
ORDER BY r.steps DESC, r.entry, r.curr;

CREATE OR REPLACE VIEW sector_degrees AS
WITH outdeg AS (
 SELECT s.id, COUNT(w.to_sector) AS outdeg
 FROM sectors s
 LEFT JOIN sector_warps w ON w.from_sector = s.id
 GROUP BY s.id
), indeg AS (
 SELECT s.id, COUNT(w.from_sector) AS indeg
 FROM sectors s
 LEFT JOIN sector_warps w ON w.to_sector = s.id
 GROUP BY s.id
)
SELECT o.id AS sector_id, o.outdeg, i.indeg
FROM outdeg o JOIN indeg i USING(id);

CREATE OR REPLACE VIEW sectors_dead_out AS
SELECT sector_id FROM sector_degrees WHERE outdeg = 0;

CREATE OR REPLACE VIEW sectors_dead_in AS
SELECT sector_id FROM sector_degrees WHERE indeg = 0;

CREATE OR REPLACE VIEW sectors_isolated AS
SELECT sector_id FROM sector_degrees WHERE outdeg = 0 AND indeg = 0;

CREATE OR REPLACE VIEW one_way_edges AS
SELECT s.from_sector, s.to_sector
FROM sector_warps s
LEFT JOIN sector_warps r
 ON r.from_sector = s.to_sector AND r.to_sector = s.from_sector
WHERE r.from_sector IS NULL;

CREATE OR REPLACE VIEW bidirectional_edges AS
SELECT s.from_sector, s.to_sector
FROM sector_warps s
JOIN sector_warps r
 ON r.from_sector = s.to_sector AND r.to_sector = s.from_sector
WHERE s.from_sector < s.to_sector;

CREATE OR REPLACE VIEW sector_adjacency AS
SELECT s.id AS sector_id,
 COALESCE(string_agg(w.to_sector::text, ','), '') AS neighbors
FROM sectors s
LEFT JOIN sector_warps w ON w.from_sector = s.id
GROUP BY s.id;

CREATE OR REPLACE VIEW sector_summary AS
WITH pc AS (
 SELECT sector AS sector_id, COUNT(*) AS planet_count
 FROM planets GROUP BY sector
), prt AS (
 SELECT sector AS sector_id, COUNT(*) AS port_count
 FROM ports GROUP BY sector
)
SELECT s.id AS sector_id,
 COALESCE(d.outdeg,0) AS outdeg,
 COALESCE(d.indeg,0) AS indeg,
 COALESCE(prt.port_count,0) AS ports,
 COALESCE(pc.planet_count,0) AS planets
FROM sectors s
LEFT JOIN sector_degrees d ON d.sector_id = s.id
LEFT JOIN prt ON prt.sector_id = s.id
LEFT JOIN pc ON pc.sector_id = s.id;

CREATE OR REPLACE VIEW port_trade_code AS
WITH m AS (
 SELECT p.id AS port_id,
 MAX(CASE WHEN t.commodity='ore' THEN (CASE t.mode WHEN 'buy' THEN 'B' ELSE 'S' END) END) AS ore,
 MAX(CASE WHEN t.commodity='organics' THEN (CASE t.mode WHEN 'buy' THEN 'B' ELSE 'S' END) END) AS org,
 MAX(CASE WHEN t.commodity='equipment' THEN (CASE t.mode WHEN 'buy' THEN 'B' ELSE 'S' END) END) AS eqp
 FROM ports p
 LEFT JOIN port_trade t ON t.port_id = p.id
 GROUP BY p.id
)
SELECT p.id, p.number, p.name, p.sector AS sector_id, p.size, p.techlevel, p.petty_cash,
 COALESCE(m.ore,'-') || COALESCE(m.org,'-') || COALESCE(m.eqp,'-') AS trade_code
FROM ports p
LEFT JOIN m ON m.port_id = p.id;

CREATE OR REPLACE VIEW sector_ports AS
SELECT s.id AS sector_id,
 COUNT(p.id) AS port_count,
 COALESCE(string_agg(p.name || ':' || pt.trade_code, ' | '), '') AS ports
FROM sectors s
LEFT JOIN port_trade_code pt ON pt.sector_id = s.id
LEFT JOIN ports p ON p.id = pt.id
GROUP BY s.id;

CREATE OR REPLACE VIEW stardock_location AS
SELECT id AS port_id, number, name, sector AS sector_id
FROM ports
WHERE type = 9 OR name LIKE '%Stardock%';

CREATE OR REPLACE VIEW planet_citadels AS
SELECT c.id AS citadel_id,
 c.level AS citadel_level,
 p.id AS planet_id,
 p.name AS planet_name,
 p.sector AS sector_id,
 c.owner AS owner_id,
 pl.name AS owner_name
FROM citadels c
JOIN planets p ON p.id = c.planet_id
LEFT JOIN players pl ON pl.id = c.owner;

CREATE OR REPLACE VIEW sector_planets AS
SELECT s.id AS sector_id,
 COUNT(p.id) AS planet_count,
 COALESCE(string_agg(p.name, ', '), '') AS planets
FROM sectors s
LEFT JOIN planets p ON p.sector = s.id
GROUP BY s.id;

CREATE OR REPLACE VIEW player_locations AS
SELECT
 p.id AS player_id,
 p.name AS player_name,
 sh.sector AS sector_id,
 sh.id AS ship_id,
 CASE
 WHEN sh.ported = 1 THEN 'docked_at_port'
 WHEN sh.onplanet = 1 THEN 'landed_on_planet'
 WHEN sh.sector IS NOT NULL THEN 'in_space'
 ELSE 'unknown'
 END AS location_kind,
 sh.ported AS is_ported,
 sh.onplanet AS is_onplanet
FROM players p
LEFT JOIN ships sh ON sh.id = p.ship;

CREATE OR REPLACE VIEW ships_by_sector AS
SELECT s.id AS sector_id,
 COUNT(sh.id) AS ship_count
FROM sectors s
LEFT JOIN ships sh ON sh.sector = s.id
GROUP BY s.id;

CREATE OR REPLACE VIEW sector_ops AS WITH weighted_assets AS ( SELECT sector AS sector_id, COALESCE(SUM( quantity * CASE asset_type WHEN 1 THEN 10 WHEN 2 THEN 5 WHEN 3 THEN 1 WHEN 4 THEN 10 ELSE 0 END ), 0) AS asset_score FROM sector_assets GROUP BY sector ) SELECT ss.sector_id, ss.outdeg, ss.indeg, sp.port_count, spp.planet_count, sbs.ship_count, ( (COALESCE(spp.planet_count, 0) * 500) + (COALESCE(sp.port_count, 0) * 100) + (COALESCE(sbs.ship_count, 0) * 40) + (COALESCE(wa.asset_score, 0)) ) AS total_density_score, wa.asset_score AS weighted_asset_score FROM sector_summary ss LEFT JOIN sector_ports sp ON sp.sector_id = ss.sector_id LEFT JOIN sector_planets spp ON spp.sector_id = ss.sector_id LEFT JOIN ships_by_sector sbs ON sbs.sector_id = ss.sector_id LEFT JOIN weighted_assets wa ON wa.sector_id = ss.sector_id;

CREATE OR REPLACE VIEW world_summary AS
WITH a AS (SELECT COUNT(*) AS sectors FROM sectors),
 b AS (SELECT COUNT(*) AS warps FROM sector_warps),
 c AS (SELECT COUNT(*) AS ports FROM ports),
 d AS (SELECT COUNT(*) AS planets FROM planets),
 e AS (SELECT COUNT(*) AS players FROM players),
 f AS (SELECT COUNT(*) AS ships FROM ships)
SELECT a.sectors, b.warps, c.ports, d.planets, e.players, f.ships
FROM a,b,c,d,e,f;

CREATE OR REPLACE VIEW v_bidirectional_warps AS
SELECT
 CASE WHEN w1.from_sector < w1.to_sector THEN w1.from_sector ELSE w1.to_sector END AS a,
 CASE WHEN w1.from_sector < w1.to_sector THEN w1.to_sector ELSE w1.from_sector END AS b
FROM sector_warps AS w1
JOIN sector_warps AS w2
 ON w1.from_sector = w2.to_sector
 AND w1.to_sector = w2.from_sector
GROUP BY a, b;

CREATE OR REPLACE VIEW player_info_v1 AS SELECT p.id AS player_id, p.name AS player_name, p.number AS player_number, sh.sector AS sector_id, sctr.name AS sector_name, p.credits AS petty_cash, p.alignment AS alignment, p.experience AS experience, p.ship AS ship_number, sh.id AS ship_id, sh.name AS ship_name, sh.type_id AS ship_type_id, st.name AS ship_type_name, st.maxholds AS ship_holds_capacity, sh.holds AS ship_holds_current, sh.fighters AS ship_fighters, sh.mines AS ship_mines, sh.limpets AS ship_limpets, sh.genesis AS ship_genesis, sh.photons AS ship_photons, sh.beacons AS ship_beacons, sh.colonists AS ship_colonists, sh.equipment AS ship_equipment, sh.organics AS ship_organics, sh.ore AS ship_ore, sh.ported AS ship_ported, sh.onplanet AS ship_onplanet, (COALESCE(p.credits,0) + COALESCE(sh.fighters,0)*2) AS approx_worth FROM players p LEFT JOIN ships sh ON sh.id = p.ship LEFT JOIN shiptypes st ON st.id = sh.type_id LEFT JOIN sectors sctr ON sctr.id = sh.sector;

CREATE OR REPLACE VIEW sector_search_index AS SELECT 'sector' AS kind, s.id AS id, s.name AS name, s.id AS sector_id, s.name AS sector_name, s.name AS search_term_1 FROM sectors s UNION ALL SELECT 'port' AS kind, p.id AS id, p.name AS name, p.sector AS sector_id, s.name AS sector_name, p.name AS search_term_1 FROM ports p JOIN sectors s ON s.id = p.sector;

CREATE OR REPLACE VIEW v_player_networth AS SELECT p.id AS player_id, p.name AS player_name, COALESCE(ba.balance,0) AS bank_balance FROM players p LEFT JOIN bank_accounts ba ON ba.owner_type = 'player' AND ba.owner_id = p.id;

CREATE OR REPLACE VIEW v_corp_treasury AS SELECT c.id AS corp_id, c.name AS corp_name, COALESCE(ca.balance,0) AS bank_balance FROM corporations c LEFT JOIN corp_accounts ca ON ca.corp_id = c.id;

CREATE OR REPLACE VIEW v_bounty_board AS SELECT b.id, b.target_type, b.target_id, p_target.name AS target_name, b.reward, b.status, b.posted_by_type, b.posted_by_id, CASE b.posted_by_type WHEN 'player' THEN p_poster.name WHEN 'corp' THEN c_poster.name ELSE b.posted_by_type END AS poster_name, b.posted_ts FROM bounties b LEFT JOIN players p_target ON b.target_type = 'player' AND b.target_id = p_target.id LEFT JOIN players p_poster ON b.posted_by_type = 'player' AND b.posted_by_id = p_poster.id LEFT JOIN corporations c_poster ON b.posted_by_type = 'corp' AND b.posted_by_id = c_poster.id WHERE b.status = 'open';

CREATE OR REPLACE VIEW v_bank_leaderboard AS SELECT ba.owner_id AS player_id, p.name, ba.balance FROM bank_accounts ba JOIN players p ON ba.owner_type = 'player' AND ba.owner_id = p.id LEFT JOIN player_prefs pp ON ba.owner_id = pp.player_id AND pp.key = 'privacy.show_leaderboard' WHERE COALESCE(pp.value, 'true') = 'true' ORDER BY ba.balance DESC;

-- FIX: Removed to_timestamp() because the source column is ALREADY a timestamp
CREATE OR REPLACE VIEW cronjobs AS SELECT id, name, next_due_at AS next_due_utc, last_run_at AS last_run_utc FROM cron_tasks ORDER BY next_due_at;
