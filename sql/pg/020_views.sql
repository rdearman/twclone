-- Basic sector views (needed for game logic)


CREATE OR REPLACE VIEW sector_degrees AS
WITH outdeg AS (
    SELECT
        s.sector_id,
        COUNT(w.to_sector) AS outdeg
    FROM
        sectors s
        LEFT JOIN sector_warps w ON w.from_sector = s.sector_id
    GROUP BY
        s.sector_id
),
indeg AS (
    SELECT
        s.sector_id,
        COUNT(w.from_sector) AS indeg
    FROM
        sectors s
        LEFT JOIN sector_warps w ON w.to_sector = s.sector_id
    GROUP BY
        s.sector_id
)
SELECT
    o.sector_id,
    o.outdeg,
    i.indeg
FROM
    outdeg o
    JOIN indeg i USING (sector_id);

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
SELECT s.sector_id AS sector_id,
 COALESCE(string_agg(w.to_sector::text, ','), '') AS neighbors
FROM sectors s
LEFT JOIN sector_warps w ON w.from_sector = s.sector_id
GROUP BY s.sector_id;

CREATE OR REPLACE VIEW sector_summary AS
WITH pc AS (
 SELECT sector_id AS sector_id, COUNT(*) AS planet_count
 FROM planets GROUP BY sector_id
), prt AS (
 SELECT sector_id AS sector_id, COUNT(*) AS port_count
 FROM ports GROUP BY sector_id
)
SELECT s.sector_id AS sector_id,
 COALESCE(d.outdeg,0) AS outdeg,
 COALESCE(d.indeg,0) AS indeg,
 COALESCE(prt.port_count,0) AS ports,
 COALESCE(pc.planet_count,0) AS planets
FROM sectors s
LEFT JOIN sector_degrees d ON d.sector_id = s.sector_id
LEFT JOIN prt ON prt.sector_id = s.sector_id
LEFT JOIN pc ON pc.sector_id = s.sector_id;

CREATE OR REPLACE VIEW port_trade_code AS
WITH m AS (
 SELECT p.port_id AS port_id,
 MAX(CASE WHEN t.commodity='ore' THEN (CASE t.mode WHEN 'buy' THEN 'B' ELSE 'S' END) END) AS ore,
 MAX(CASE WHEN t.commodity='organics' THEN (CASE t.mode WHEN 'buy' THEN 'B' ELSE 'S' END) END) AS org,
 MAX(CASE WHEN t.commodity='equipment' THEN (CASE t.mode WHEN 'buy' THEN 'B' ELSE 'S' END) END) AS eqp
 FROM ports p
 LEFT JOIN port_trade t ON t.port_id = p.port_id
 GROUP BY p.port_id
)
SELECT p.port_id AS id, p.number, p.name, p.sector_id AS sector_id, p.size, p.techlevel, p.petty_cash,
 COALESCE(m.ore,'-') || COALESCE(m.org,'-') || COALESCE(m.eqp,'-') AS trade_code
FROM ports p
LEFT JOIN m ON m.port_id = p.port_id;

CREATE OR REPLACE VIEW sector_ports AS
SELECT s.sector_id AS sector_id,
 COUNT(p.port_id) AS port_count,
 COALESCE(string_agg(p.name || ':' || pt.trade_code, ' | '), '') AS ports
FROM sectors s
LEFT JOIN port_trade_code pt ON pt.sector_id = s.sector_id
LEFT JOIN ports p ON p.port_id = pt.id
GROUP BY s.sector_id;

CREATE OR REPLACE VIEW stardock_location AS
SELECT port_id AS port_id, number, name, sector_id AS sector_id
FROM ports
WHERE type = 9 OR name LIKE '%Stardock%';

CREATE OR REPLACE VIEW planet_citadels AS
SELECT c.citadel_id AS citadel_id,
 c.level AS citadel_level,
 p.planet_id AS planet_id,
 p.name AS planet_name,
 p.sector_id AS sector_id,
 c.owner_id AS owner_id,
 pl.name AS owner_name
FROM citadels c
JOIN planets p ON p.planet_id = c.planet_id
LEFT JOIN players pl ON pl.player_id = c.owner_id;

CREATE OR REPLACE VIEW sector_planets AS
SELECT s.sector_id AS sector_id,
 COUNT(p.planet_id) AS planet_count,
 COALESCE(string_agg(p.name, ', '), '') AS planets
FROM sectors s
LEFT JOIN planets p ON p.sector_id = s.sector_id
GROUP BY s.sector_id;

CREATE OR REPLACE VIEW player_locations AS
 SELECT
 p.player_id AS player_id,
 p.name AS player_name,
 sh.sector_id AS sector_id,
 sh.ship_id AS ship_id,
 CASE
 WHEN sh.ported > 0 THEN 'docked_at_port'
 WHEN sh.onplanet = TRUE THEN 'landed_on_planet'
 WHEN sh.sector_id IS NOT NULL THEN 'in_space'
 ELSE 'unknown'
 END AS location_kind,
 (sh.ported > 0) AS is_ported,
 sh.onplanet AS is_onplanet
FROM players p
LEFT JOIN ships sh ON sh.ship_id = p.ship_id;

CREATE OR REPLACE VIEW ships_by_sector AS
SELECT s.sector_id AS sector_id,
 COUNT(sh.ship_id) AS ship_count
FROM sectors s
LEFT JOIN ships sh ON sh.sector_id = s.sector_id
GROUP BY s.sector_id;

CREATE OR REPLACE VIEW sector_ops AS WITH weighted_assets AS ( SELECT sector_id AS sector_id, COALESCE(SUM( quantity * CASE asset_type WHEN 1 THEN 10 WHEN 2 THEN 5 WHEN 3 THEN 1 WHEN 4 THEN 10 ELSE 0 END ), 0) AS asset_score FROM sector_assets GROUP BY sector_id ) SELECT ss.sector_id, ss.outdeg, ss.indeg, sp.port_count, spp.planet_count, sbs.ship_count, ( (COALESCE(spp.planet_count, 0) * 500) + (COALESCE(sp.port_count, 0) * 100) + (COALESCE(sbs.ship_count, 0) * 40) + (COALESCE(wa.asset_score, 0)) ) AS total_density_score, wa.asset_score AS weighted_asset_score FROM sector_summary ss LEFT JOIN sector_ports sp ON sp.sector_id = ss.sector_id LEFT JOIN sector_planets spp ON spp.sector_id = ss.sector_id LEFT JOIN ships_by_sector sbs ON sbs.sector_id = ss.sector_id LEFT JOIN weighted_assets wa ON wa.sector_id = ss.sector_id;

CREATE OR REPLACE VIEW world_summary AS
WITH a AS (
    SELECT
        COUNT(*) AS sectors
    FROM
        sectors
),
b AS (
    SELECT
        COUNT(*) AS warps
    FROM
        sector_warps
),
c AS (
    SELECT
        COUNT(*) AS ports
    FROM
        ports
),
d AS (
    SELECT
        COUNT(*) AS planets
    FROM
        planets
),
e AS (
    SELECT
        COUNT(*) AS players
    FROM
        players
),
f AS (
    SELECT
        COUNT(*) AS ships
    FROM
        ships
)
SELECT
    a.sectors,
    b.warps,
    c.ports,
    d.planets,
    e.players,
    f.ships
FROM
    a,
    b,
    c,
    d,
    e,
    f;

CREATE OR REPLACE VIEW v_bidirectional_warps AS
SELECT
 CASE WHEN w1.from_sector < w1.to_sector THEN w1.from_sector ELSE w1.to_sector END AS a,
 CASE WHEN w1.from_sector < w1.to_sector THEN w1.to_sector ELSE w1.from_sector END AS b
FROM sector_warps AS w1
JOIN sector_warps AS w2
 ON w1.from_sector = w2.to_sector
 AND w1.to_sector = w2.from_sector
GROUP BY a, b;

CREATE OR REPLACE VIEW player_info_v1 AS SELECT p.player_id AS player_id, p.name AS player_name, p.number AS player_number, sh.sector_id AS sector_id, sctr.name AS sector_name, p.credits AS petty_cash, p.alignment AS alignment, p.experience AS experience, p.ship_id AS ship_number, sh.ship_id AS ship_id, sh.name AS ship_name, sh.type_id AS ship_type_id, st.name AS ship_type_name, st.maxholds AS ship_holds_capacity, sh.holds AS ship_holds_current, sh.fighters AS ship_fighters, sh.mines AS ship_mines, sh.limpets AS ship_limpets, sh.genesis AS ship_genesis, sh.photons AS ship_photons, sh.beacons AS ship_beacons, sh.colonists AS ship_colonists, sh.equipment AS ship_equipment, sh.organics AS ship_organics, sh.ore AS ship_ore, (sh.ported > 0) AS ship_ported, sh.onplanet AS ship_onplanet, (COALESCE(p.credits,0) + COALESCE(sh.fighters,0)*2) AS approx_worth FROM players p LEFT JOIN ships sh ON sh.ship_id = p.ship_id LEFT JOIN shiptypes st ON st.shiptypes_id = sh.type_id LEFT JOIN sectors sctr ON sctr.sector_id = sh.sector_id;

CREATE OR REPLACE VIEW sector_search_index AS SELECT 'sector' AS kind, s.sector_id AS id, s.name AS name, s.sector_id AS sector_id, s.name AS sector_name, s.name AS search_term_1 FROM sectors s UNION ALL SELECT 'port' AS kind, p.port_id AS id, p.name AS name, p.sector_id AS sector_id, s.name AS sector_name, p.name AS search_term_1 FROM ports p JOIN sectors s ON s.sector_id = p.sector_id;

CREATE OR REPLACE VIEW v_player_networth AS SELECT p.player_id AS player_id, p.name AS player_name, COALESCE(ba.balance,0) AS bank_balance FROM players p LEFT JOIN bank_accounts ba ON ba.owner_type = 'player' AND ba.owner_id = p.player_id;

CREATE OR REPLACE VIEW v_corp_treasury AS SELECT c.corporation_id AS corp_id, c.name AS corp_name, COALESCE(ca.balance,0) AS bank_balance FROM corporations c LEFT JOIN corp_accounts ca ON ca.corp_id = c.corporation_id;

CREATE OR REPLACE VIEW v_bounty_board AS SELECT b.bounties_id AS id, b.target_type, b.target_id, p_target.name AS target_name, b.reward, b.status, b.posted_by_type, b.posted_by_id, CASE b.posted_by_type WHEN 'player' THEN p_poster.name WHEN 'corp' THEN c_poster.name ELSE b.posted_by_type END AS poster_name, b.posted_ts FROM bounties b LEFT JOIN players p_target ON b.target_type = 'player' AND b.target_id = p_target.player_id LEFT JOIN players p_poster ON b.posted_by_type = 'player' AND b.posted_by_id = p_poster.player_id LEFT JOIN corporations c_poster ON b.posted_by_type = 'corp' AND b.posted_by_id = c_poster.corporation_id WHERE b.status = 'open';

CREATE OR REPLACE VIEW v_bank_leaderboard AS SELECT ba.owner_id AS player_id, p.name, ba.balance FROM bank_accounts ba JOIN players p ON ba.owner_type = 'player' AND ba.owner_id = p.player_id LEFT JOIN player_prefs pp ON ba.owner_id = pp.player_prefs_id AND pp.key = 'privacy.show_leaderboard' WHERE COALESCE(pp.value, 'true') = 'true' ORDER BY ba.balance DESC;

CREATE OR REPLACE VIEW cronjobs AS SELECT cron_tasks_id AS id, name, next_due_at AS next_due_utc, last_run_at AS last_run_utc FROM cron_tasks ORDER BY next_due_at;
