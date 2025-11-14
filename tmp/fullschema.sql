CREATE TABLE sessions (  token      TEXT PRIMARY KEY,   player_id  INTEGER NOT NULL,   expires    INTEGER NOT NULL,   created_at INTEGER NOT NULL);
CREATE INDEX idx_sessions_player  ON sessions(player_id);
CREATE INDEX idx_sessions_expires ON sessions(expires);
CREATE TABLE idempotency (  key       TEXT PRIMARY KEY,   cmd       TEXT NOT NULL,   req_fp    TEXT NOT NULL,   response  TEXT,   created_at  INTEGER NOT NULL,   updated_at  INTEGER);
CREATE INDEX idx_idemp_cmd ON idempotency(cmd);
CREATE TABLE locks (  lock_name TEXT PRIMARY KEY,  owner TEXT,  until_ms INTEGER);
CREATE INDEX idx_locks_until ON locks(until_ms);
CREATE TABLE engine_state (  state_key TEXT PRIMARY KEY,  state_val TEXT NOT NULL);
CREATE TABLE config (   id INTEGER PRIMARY KEY CHECK (id = 1),   turnsperday INTEGER,   maxwarps_per_sector INTEGER,   startingcredits INTEGER,   startingfighters INTEGER,   startingholds INTEGER,   processinterval INTEGER,   autosave INTEGER,   max_ports INTEGER,   max_planets_per_sector INTEGER,   max_total_planets INTEGER,   max_citadel_level INTEGER,   number_of_planet_types INTEGER,   max_ship_name_length INTEGER,   ship_type_count INTEGER,   hash_length INTEGER,   default_nodes INTEGER,   buff_size INTEGER,   max_name_length INTEGER,   max_cloak_duration INTEGER DEFAULT 24,   planet_type_count INTEGER,   server_port INTEGER,   s2s_port INTEGER  );
CREATE TABLE trade_idempotency (  key          TEXT PRIMARY KEY,  player_id    INTEGER NOT NULL,  sector_id    INTEGER NOT NULL,  request_json TEXT NOT NULL,  response_json TEXT NOT NULL,  created_at   INTEGER NOT NULL );
CREATE TABLE used_sectors (used INTEGER);
CREATE TABLE npc_shipnames (id INTEGER, name TEXT);
CREATE TABLE planettypes (id INTEGER PRIMARY KEY AUTOINCREMENT, code TEXT UNIQUE, typeDescription TEXT, typeName TEXT, citadelUpgradeTime_lvl1 INTEGER, citadelUpgradeTime_lvl2 INTEGER, citadelUpgradeTime_lvl3 INTEGER, citadelUpgradeTime_lvl4 INTEGER, citadelUpgradeTime_lvl5 INTEGER, citadelUpgradeTime_lvl6 INTEGER, citadelUpgradeOre_lvl1 INTEGER, citadelUpgradeOre_lvl2 INTEGER, citadelUpgradeOre_lvl3 INTEGER, citadelUpgradeOre_lvl4 INTEGER, citadelUpgradeOre_lvl5 INTEGER, citadelUpgradeOre_lvl6 INTEGER, citadelUpgradeOrganics_lvl1 INTEGER, citadelUpgradeOrganics_lvl2 INTEGER, citadelUpgradeOrganics_lvl3 INTEGER, citadelUpgradeOrganics_lvl4 INTEGER, citadelUpgradeOrganics_lvl5 INTEGER, citadelUpgradeOrganics_lvl6 INTEGER, citadelUpgradeEquipment_lvl1 INTEGER, citadelUpgradeEquipment_lvl2 INTEGER, citadelUpgradeEquipment_lvl3 INTEGER, citadelUpgradeEquipment_lvl4 INTEGER, citadelUpgradeEquipment_lvl5 INTEGER, citadelUpgradeEquipment_lvl6 INTEGER, citadelUpgradeColonist_lvl1 INTEGER, citadelUpgradeColonist_lvl2 INTEGER, citadelUpgradeColonist_lvl3 INTEGER, citadelUpgradeColonist_lvl4 INTEGER, citadelUpgradeColonist_lvl5 INTEGER, citadelUpgradeColonist_lvl6 INTEGER, maxColonist_ore INTEGER, maxColonist_organics INTEGER, maxColonist_equipment INTEGER, fighters INTEGER, fuelProduction INTEGER, organicsProduction INTEGER, equipmentProduction INTEGER, fighterProduction INTEGER, maxore INTEGER, maxorganics INTEGER, maxequipment INTEGER, maxfighters INTEGER, breeding REAL);
CREATE TABLE ports (  id INTEGER PRIMARY KEY AUTOINCREMENT,  number INTEGER,  name TEXT NOT NULL,  sector INTEGER NOT NULL,  size INTEGER,  techlevel INTEGER,  ore_on_hand INTEGER NOT NULL DEFAULT 0,  organics_on_hand INTEGER NOT NULL DEFAULT 0,  equipment_on_hand INTEGER NOT NULL DEFAULT 0,  petty_cash INTEGER NOT NULL DEFAULT 0,  invisible INTEGER DEFAULT 0,  type INTEGER DEFAULT 1,  FOREIGN KEY (sector) REFERENCES sectors(id));
CREATE TABLE port_trade (  id INTEGER PRIMARY KEY AUTOINCREMENT,   port_id INTEGER NOT NULL,   maxproduct INTEGER,   commodity TEXT CHECK(commodity IN ('ore','organics','equipment')),   mode TEXT CHECK(mode IN ('buy','sell')),   FOREIGN KEY (port_id) REFERENCES ports(id));
CREATE TABLE players (  id INTEGER PRIMARY KEY AUTOINCREMENT,   type INTEGER DEFAULT 2,   number INTEGER,   name TEXT NOT NULL,   passwd TEXT NOT NULL,   sector INTEGER,   ship INTEGER,   experience INTEGER,   alignment INTEGER,   credits INTEGER,   flags INTEGER,   login_time INTEGER,   last_update INTEGER,   intransit INTEGER,   beginmove INTEGER,   movingto INTEGER,   loggedin INTEGER,   lastplanet INTEGER,   score INTEGER,   last_news_read_timestamp INTEGER DEFAULT 0);
CREATE TABLE player_types (type INTEGER PRIMARY KEY AUTOINCREMENT, description TEXT);
CREATE TABLE sectors (id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT, beacon TEXT, nebulae TEXT);
CREATE TABLE sector_warps (from_sector INTEGER, to_sector INTEGER, PRIMARY KEY (from_sector, to_sector), FOREIGN KEY (from_sector) REFERENCES sectors(id) ON DELETE CASCADE, FOREIGN KEY (to_sector) REFERENCES sectors(id) ON DELETE CASCADE);
CREATE TABLE shiptypes (     id INTEGER PRIMARY KEY AUTOINCREMENT,     name TEXT NOT NULL UNIQUE,     basecost INTEGER,     maxattack INTEGER,     initialholds INTEGER,     maxholds INTEGER,     maxfighters INTEGER,     turns INTEGER,     maxmines INTEGER,     maxlimpets INTEGER,     maxgenesis INTEGER,     twarp INTEGER, /* Transwarp capability (0/1) */     transportrange INTEGER,     maxshields INTEGER,     offense INTEGER,     defense INTEGER,     maxbeacons INTEGER,     holo INTEGER, /* Holo scanner (0/1) */     planet INTEGER, /* Can land on planets (0/1) */     maxphotons INTEGER, /* Photon torpedo count */     can_purchase INTEGER /* Can be bought at a port (0/1) */   );
CREATE TABLE ships (     id INTEGER PRIMARY KEY AUTOINCREMENT,     name TEXT NOT NULL,     type_id INTEGER, /* Foreign Key to shiptypes.id */     attack INTEGER,     holds INTEGER,     mines INTEGER, /* Current quantity carried */     limpets INTEGER, /* Current quantity carried */     fighters INTEGER, /* Current quantity carried */     genesis INTEGER, /* Current quantity carried */     photons INTEGER, /* Current quantity carried */     sector INTEGER, /* Foreign Key to sectors.id */     shields INTEGER,     beacons INTEGER, /* Current quantity carried */     colonists INTEGER,     equipment INTEGER,     organics INTEGER,     ore INTEGER,     flags INTEGER,     cloaking_devices INTEGER,     cloaked TIMESTAMP,     ported INTEGER,     onplanet INTEGER,     destroyed INTEGER DEFAULT 0,     CONSTRAINT check_current_cargo_limit CHECK ( (colonists + equipment + organics + ore) <= holds ),    FOREIGN KEY(type_id) REFERENCES shiptypes(id),     FOREIGN KEY(sector) REFERENCES sectors(id)   );
CREATE TABLE ship_roles ( role_id INTEGER PRIMARY KEY, role INTEGER DEFAULT 1, role_description TEXT DEFAULT 1);
CREATE TABLE ship_ownership (  ship_id     INTEGER NOT NULL,  player_id   INTEGER NOT NULL,  role_id     INTEGER NOT NULL,  is_primary  INTEGER NOT NULL DEFAULT 0,  acquired_at INTEGER NOT NULL DEFAULT (strftime('%s','now')),  PRIMARY KEY (ship_id, player_id, role_id),  FOREIGN KEY(ship_id)  REFERENCES ships(id),  FOREIGN KEY(player_id) REFERENCES players(id));
CREATE TABLE planets (  id INTEGER PRIMARY KEY AUTOINCREMENT,   num INTEGER,   sector INTEGER NOT NULL,   name TEXT NOT NULL,   owner INTEGER,   owner_player_id INTEGER REFERENCES players(id) ON DELETE SET NULL,  population INTEGER,   type INTEGER,   creator TEXT,   colonist INTEGER,   fighters INTEGER,   citadel_level INTEGER DEFAULT 0,   ore_on_hand INTEGER NOT NULL DEFAULT 0,  organics_on_hand INTEGER NOT NULL DEFAULT 0,  equipment_on_hand INTEGER NOT NULL DEFAULT 0, terraform_turns_left INTEGER NOT NULL DEFAULT 1,  FOREIGN KEY (sector) REFERENCES sectors(id),   FOREIGN KEY (owner) REFERENCES players(id),   FOREIGN KEY (type) REFERENCES planettypes(id)  );
CREATE TABLE citadel_requirements (    planet_type_id INTEGER NOT NULL REFERENCES planettypes(id) ON DELETE CASCADE,    citadel_level INTEGER NOT NULL,    ore_cost INTEGER NOT NULL DEFAULT 0,    organics_cost INTEGER NOT NULL DEFAULT 0,    equipment_cost INTEGER NOT NULL DEFAULT 0,    colonist_cost INTEGER NOT NULL DEFAULT 0,    time_cost_days INTEGER NOT NULL DEFAULT 0,    PRIMARY KEY (planet_type_id, citadel_level)  );
CREATE TABLE planet_goods (     planet_id INTEGER NOT NULL,     commodity TEXT NOT NULL CHECK(commodity IN ('ore', 'organics', 'equipment')),     quantity INTEGER NOT NULL DEFAULT 0,     max_capacity INTEGER NOT NULL,     production_rate INTEGER NOT NULL,     PRIMARY KEY (planet_id, commodity),     FOREIGN KEY (planet_id) REFERENCES planets(id)  );
CREATE TABLE citadels (  id INTEGER PRIMARY KEY AUTOINCREMENT,   planet_id INTEGER UNIQUE NOT NULL,   level INTEGER,   treasury INTEGER,   militaryReactionLevel INTEGER,   qCannonAtmosphere INTEGER,   qCannonSector INTEGER,   planetaryShields INTEGER,   transporterlvl INTEGER,   interdictor INTEGER,   upgradePercent REAL,   upgradestart INTEGER,   owner INTEGER,   shields INTEGER,   torps INTEGER,   fighters INTEGER,   qtorps INTEGER,   qcannon INTEGER,   qcannontype INTEGER,   qtorpstype INTEGER,   military INTEGER,   construction_start_time INTEGER DEFAULT 0,   construction_end_time INTEGER DEFAULT 0,   target_level INTEGER DEFAULT 0,   construction_status TEXT DEFAULT 'idle',   FOREIGN KEY (planet_id) REFERENCES planets(id) ON DELETE CASCADE,   FOREIGN KEY (owner) REFERENCES players(id)  );
CREATE TABLE turns(    player INTEGER NOT NULL,    turns_remaining INTEGER NOT NULL,    last_update TIMESTAMP NOT NULL,    PRIMARY KEY (player),    FOREIGN KEY (player) REFERENCES players(id) ON DELETE CASCADE );
CREATE TABLE mail (    id INTEGER PRIMARY KEY AUTOINCREMENT,     thread_id INTEGER,     sender_id INTEGER NOT NULL,     recipient_id INTEGER NOT NULL,     subject TEXT,     body TEXT NOT NULL,     sent_at DATETIME NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ','now')),     read_at DATETIME,     archived INTEGER NOT NULL DEFAULT 0,     deleted INTEGER NOT NULL DEFAULT 0,     idempotency_key TEXT,     FOREIGN KEY(sender_id) REFERENCES players(id) ON DELETE CASCADE,     FOREIGN KEY(recipient_id) REFERENCES players(id) ON DELETE CASCADE  );
CREATE TABLE subspace (    id INTEGER PRIMARY KEY AUTOINCREMENT,     sender_id INTEGER,     message TEXT NOT NULL,     kind TEXT NOT NULL DEFAULT 'chat',     posted_at DATETIME NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ','now')),     FOREIGN KEY(sender_id) REFERENCES players(id) ON DELETE SET NULL  );
CREATE TABLE subspace_cursors (    player_id INTEGER PRIMARY KEY,     last_seen_id INTEGER NOT NULL DEFAULT 0,     FOREIGN KEY(player_id) REFERENCES players(id) ON DELETE CASCADE  );
CREATE TABLE corporations (    id INTEGER PRIMARY KEY,     name TEXT NOT NULL COLLATE NOCASE,     owner_id INTEGER,     tag TEXT COLLATE NOCASE,     description TEXT,     created_at DATETIME NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ','now')),     updated_at DATETIME NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ','now')),     FOREIGN KEY(owner_id) REFERENCES players(id) ON DELETE SET NULL ON UPDATE CASCADE,     CHECK (tag IS NULL OR (length(tag) BETWEEN 2 AND 5 AND tag GLOB '[A-Za-z0-9]*'))  );
CREATE TABLE corp_members (    corp_id INTEGER NOT NULL,     player_id INTEGER NOT NULL,     role TEXT NOT NULL DEFAULT 'Member',     join_date DATETIME NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ','now')),     PRIMARY KEY (corp_id, player_id),     FOREIGN KEY(corp_id) REFERENCES corporations(id) ON DELETE CASCADE ON UPDATE CASCADE,     FOREIGN KEY(player_id) REFERENCES players(id) ON DELETE CASCADE ON UPDATE CASCADE,     CHECK (role IN ('Leader','Officer','Member'))  );
CREATE TABLE corp_mail (    id INTEGER PRIMARY KEY AUTOINCREMENT,     corp_id INTEGER NOT NULL,     sender_id INTEGER,     subject TEXT,     body TEXT NOT NULL,     posted_at DATETIME NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ','now')),     FOREIGN KEY(corp_id) REFERENCES corporations(id) ON DELETE CASCADE,     FOREIGN KEY(sender_id) REFERENCES players(id) ON DELETE SET NULL  );
CREATE TABLE corp_mail_cursors (    corp_id INTEGER NOT NULL,     player_id INTEGER NOT NULL,     last_seen_id INTEGER NOT NULL DEFAULT 0,     PRIMARY KEY (corp_id, player_id),     FOREIGN KEY(corp_id) REFERENCES corporations(id) ON DELETE CASCADE,     FOREIGN KEY(player_id) REFERENCES players(id) ON DELETE CASCADE  );
CREATE TABLE corp_log (    id INTEGER PRIMARY KEY AUTOINCREMENT,     corp_id INTEGER NOT NULL,     actor_id INTEGER,     event_type TEXT NOT NULL,     payload TEXT NOT NULL,     created_at DATETIME NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ','now')),     FOREIGN KEY(corp_id) REFERENCES corporations(id) ON DELETE CASCADE,     FOREIGN KEY(actor_id) REFERENCES players(id) ON DELETE SET NULL  );
CREATE TABLE system_events (    id INTEGER PRIMARY KEY AUTOINCREMENT,     scope TEXT NOT NULL,     event_type TEXT NOT NULL,     payload TEXT NOT NULL,     created_at DATETIME NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ','now'))  );
CREATE TABLE subscriptions (    id INTEGER PRIMARY KEY AUTOINCREMENT,     player_id INTEGER NOT NULL,     event_type TEXT NOT NULL,     delivery TEXT NOT NULL,     filter_json TEXT,     ephemeral INTEGER NOT NULL DEFAULT 0,     locked INTEGER NOT NULL DEFAULT 0,    enabled INTEGER NOT NULL DEFAULT 1,     UNIQUE(player_id, event_type),     FOREIGN KEY(player_id) REFERENCES players(id) ON DELETE CASCADE  );
CREATE TABLE player_block (  blocker_id   INTEGER NOT NULL,  blocked_id   INTEGER NOT NULL,  created_at   INTEGER NOT NULL,  PRIMARY KEY (blocker_id, blocked_id));
CREATE TABLE notice_seen (  notice_id  INTEGER NOT NULL,  player_id  INTEGER NOT NULL,  seen_at    INTEGER NOT NULL,  PRIMARY KEY (notice_id, player_id));
CREATE TABLE system_notice (  id         INTEGER PRIMARY KEY,  created_at INTEGER NOT NULL,  title      TEXT NOT NULL,  body       TEXT NOT NULL,  severity   TEXT NOT NULL CHECK(severity IN ('info','warn','error')),  expires_at INTEGER);
CREATE TABLE player_prefs   (    player_id  INTEGER NOT NULL  ,    key        TEXT    NOT NULL,         type       TEXT    NOT NULL CHECK (type IN ('bool','int','string','json'))  ,    value      TEXT    NOT NULL,         updated_at INTEGER NOT NULL DEFAULT (strftime('%s','now'))  ,    PRIMARY KEY (player_id, key)  ,    FOREIGN KEY (player_id) REFERENCES players(id) ON DELETE CASCADE );
CREATE TABLE player_bookmarks   (    id         INTEGER PRIMARY KEY AUTOINCREMENT  ,    player_id  INTEGER NOT NULL  ,    name       TEXT    NOT NULL,        sector_id  INTEGER NOT NULL  ,    updated_at INTEGER NOT NULL DEFAULT (strftime('%s','now'))  ,    UNIQUE(player_id, name)  ,    FOREIGN KEY (player_id) REFERENCES players(id) ON DELETE CASCADE  ,    FOREIGN KEY (sector_id) REFERENCES sectors(id) ON DELETE CASCADE );
CREATE TABLE player_avoid  (    player_id  INTEGER NOT NULL  ,    sector_id  INTEGER NOT NULL  ,    updated_at INTEGER NOT NULL DEFAULT (strftime('%s','now'))  ,    PRIMARY KEY (player_id, sector_id)  ,    FOREIGN KEY (player_id) REFERENCES players(id) ON DELETE CASCADE  ,    FOREIGN KEY (sector_id) REFERENCES sectors(id) ON DELETE CASCADE );
CREATE TABLE player_notes   (    id         INTEGER PRIMARY KEY AUTOINCREMENT  ,    player_id  INTEGER NOT NULL  ,    scope      TEXT    NOT NULL,      key        TEXT    NOT NULL,      note       TEXT    NOT NULL  ,    updated_at INTEGER NOT NULL DEFAULT (strftime('%s','now'))  ,    UNIQUE(player_id, scope, key)  ,    FOREIGN KEY (player_id) REFERENCES players(id) ON DELETE CASCADE );
CREATE TABLE sector_assets  (     id INTEGER PRIMARY KEY,      sector INTEGER NOT NULL REFERENCES sectors(id),     player INTEGER REFERENCES players(id),      corporation INTEGER NOT NULL DEFAULT 0,      asset_type INTEGER NOT NULL,      offensive_setting INTEGER DEFAULT 0,      quantity INTEGER,     ttl INTEGER,      deployed_at INTEGER NOT NULL  );
CREATE TABLE msl_sectors (  sector_id INTEGER PRIMARY KEY REFERENCES sectors(id));
CREATE TABLE trade_log (    id            INTEGER PRIMARY KEY AUTOINCREMENT,    player_id     INTEGER NOT NULL,    port_id       INTEGER NOT NULL,    sector_id     INTEGER NOT NULL,    commodity     TEXT NOT NULL,    units         INTEGER NOT NULL,    price_per_unit REAL NOT NULL,    action        TEXT CHECK(action IN ('buy', 'sell')) NOT NULL,    timestamp     INTEGER NOT NULL,    FOREIGN KEY (player_id) REFERENCES players(id),    FOREIGN KEY (port_id) REFERENCES ports(id),    FOREIGN KEY (sector_id) REFERENCES sectors(id));
CREATE INDEX ix_trade_log_ts ON trade_log(timestamp);
CREATE TABLE banks (    owner_type        TEXT NOT NULL,    owner_id          INTEGER NOT NULL,    credits           INTEGER NOT NULL DEFAULT 0,    last_deposit_at   INTEGER NOT NULL,    last_interest_run INTEGER NOT NULL,    PRIMARY KEY (owner_type, owner_id));
CREATE TABLE stardock_assets (    sector_id      INTEGER PRIMARY KEY,    owner_id       INTEGER NOT NULL,    fighters       INTEGER NOT NULL DEFAULT 0,    defenses       INTEGER NOT NULL DEFAULT 0,    ship_capacity  INTEGER NOT NULL DEFAULT 1,    created_at     INTEGER NOT NULL,    FOREIGN KEY (sector_id) REFERENCES sectors(id),    FOREIGN KEY (owner_id) REFERENCES players(id));
CREATE INDEX ix_stardock_owner ON stardock_assets(owner_id);
CREATE VIEW longest_tunnels AS
  WITH
  all_sectors AS (
    SELECT from_sector AS id FROM sector_warps
    UNION
    SELECT to_sector   AS id FROM sector_warps
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
  JOIN outdeg dn ON dn.id = e.to_sector  AND dn.deg = 1
),
rec(entry, curr, path, steps) AS (
  SELECT entry, next, printf('%d->%d', entry, next), 1
  FROM entry
  UNION ALL
  SELECT r.entry, e.to_sector,
         r.path || '->' || printf('%d', e.to_sector),
         r.steps + 1
  FROM rec r
  JOIN edges e  ON e.from_sector = r.curr
  JOIN outdeg d ON d.id = r.curr AND d.deg = 1
  WHERE instr(r.path, '->' || printf('%d', e.to_sector) || '->') = 0
)
SELECT
  r.entry                 AS entry_sector,
  r.curr                  AS exit_sector,
  r.path                  AS tunnel_path,
  r.steps                 AS tunnel_length_edges
FROM rec r
JOIN outdeg d_exit ON d_exit.id = r.curr
WHERE d_exit.deg <> 1 AND r.steps >= 2
ORDER BY r.steps DESC, r.entry, r.curr;
CREATE VIEW sector_degrees AS
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
CREATE VIEW sectors_dead_out AS
SELECT sector_id FROM sector_degrees WHERE outdeg = 0;
CREATE VIEW sectors_dead_in AS
SELECT sector_id FROM sector_degrees WHERE indeg = 0;
CREATE VIEW sectors_isolated AS
SELECT sector_id FROM sector_degrees WHERE outdeg = 0 AND indeg = 0;
CREATE VIEW one_way_edges AS
SELECT s.from_sector, s.to_sector
FROM sector_warps s
LEFT JOIN sector_warps r
  ON r.from_sector = s.to_sector AND r.to_sector = s.from_sector
WHERE r.from_sector IS NULL;
CREATE VIEW bidirectional_edges AS
SELECT s.from_sector, s.to_sector
FROM sector_warps s
JOIN sector_warps r
  ON r.from_sector = s.to_sector AND r.to_sector = s.from_sector
WHERE s.from_sector < s.to_sector;
CREATE VIEW sector_adjacency AS
SELECT s.id AS sector_id,
       COALESCE(GROUP_CONCAT(w.to_sector, ','), '') AS neighbors
FROM sectors s
LEFT JOIN sector_warps w ON w.from_sector = s.id
GROUP BY s.id;
CREATE VIEW sector_summary AS
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
LEFT JOIN pc  ON pc.sector_id  = s.id;
CREATE VIEW port_trade_code AS
WITH m AS (
  SELECT p.id AS port_id,
         MAX(CASE WHEN t.commodity='ore'       THEN (CASE t.mode WHEN 'buy' THEN 'B' ELSE 'S' END) END) AS ore,
         MAX(CASE WHEN t.commodity='organics'  THEN (CASE t.mode WHEN 'buy' THEN 'B' ELSE 'S' END) END) AS org,
         MAX(CASE WHEN t.commodity='equipment' THEN (CASE t.mode WHEN 'buy' THEN 'B' ELSE 'S' END) END) AS eqp
  FROM ports p
  LEFT JOIN port_trade t ON t.port_id = p.id
  GROUP BY p.id
)
SELECT p.id, p.number, p.name, p.sector AS sector_id, p.size, p.techlevel, p.petty_cash,
       COALESCE(m.ore,'-') || COALESCE(m.org,'-') || COALESCE(m.eqp,'-') AS trade_code
FROM ports p
LEFT JOIN m ON m.port_id = p.id;
CREATE VIEW sector_ports AS
SELECT s.id AS sector_id,
       COUNT(p.id) AS port_count,
       COALESCE(GROUP_CONCAT(p.name || ':' || pt.trade_code, ' | '), '') AS ports
FROM sectors s
LEFT JOIN port_trade_code pt ON pt.sector_id = s.id
LEFT JOIN ports p ON p.id = pt.id
GROUP BY s.id;
CREATE VIEW stardock_location AS
SELECT id AS port_id, number, name, sector AS sector_id
FROM ports
WHERE type = 9 OR name LIKE '%Stardock%';
CREATE VIEW planet_citadels AS
SELECT c.id AS citadel_id,
       c.level AS citadel_level,
       p.id AS planet_id,
       p.name AS planet_name,
       p.sector AS sector_id,
       c.owner AS owner_id,
       pl.name AS owner_name
FROM citadels c
JOIN planets  p  ON p.id = c.planet_id
LEFT JOIN players pl ON pl.id = c.owner;
CREATE VIEW sector_planets AS
SELECT s.id AS sector_id,
       COUNT(p.id) AS planet_count,
       COALESCE(GROUP_CONCAT(p.name, ', '), '') AS planets
FROM sectors s
LEFT JOIN planets p ON p.sector = s.id
GROUP BY s.id;
CREATE VIEW player_locations AS
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
CREATE VIEW ships_by_sector AS
SELECT s.id AS sector_id,
       COUNT(sh.id) AS ship_count
FROM sectors s
LEFT JOIN ships sh ON sh.sector = s.id
GROUP BY s.id;
CREATE VIEW sector_ops AS    WITH weighted_assets AS (       SELECT           sector AS sector_id,           COALESCE(SUM(               quantity * CASE asset_type                   WHEN 1 THEN 10                   WHEN 2 THEN 5                   WHEN 3 THEN 1                   WHEN 4 THEN 10                   ELSE 0               END           ), 0) AS asset_score       FROM sector_assets       GROUP BY sector    )    SELECT       ss.sector_id,       ss.outdeg,       ss.indeg,       sp.port_count,       spp.planet_count,       sbs.ship_count,       (           (COALESCE(spp.planet_count, 0) * 500)         + (COALESCE(sp.port_count, 0) * 100)         + (COALESCE(sbs.ship_count, 0) * 40)         + (COALESCE(wa.asset_score, 0))       ) AS total_density_score,       wa.asset_score AS weighted_asset_score    FROM sector_summary ss    LEFT JOIN sector_ports    sp  ON sp.sector_id  = ss.sector_id    LEFT JOIN sector_planets  spp ON spp.sector_id = ss.sector_id    LEFT JOIN ships_by_sector sbs ON sbs.sector_id = ss.sector_id    LEFT JOIN weighted_assets wa ON wa.sector_id = ss.sector_id;
CREATE VIEW world_summary AS
WITH a AS (SELECT COUNT(*) AS sectors FROM sectors),
     b AS (SELECT COUNT(*) AS warps   FROM sector_warps),
     c AS (SELECT COUNT(*) AS ports   FROM ports),
     d AS (SELECT COUNT(*) AS planets FROM planets),
     e AS (SELECT COUNT(*) AS players FROM players),
     f AS (SELECT COUNT(*) AS ships   FROM ships)
SELECT a.sectors, b.warps, c.ports, d.planets, e.players, f.ships
FROM a,b,c,d,e,f;
CREATE VIEW v_bidirectional_warps AS
SELECT
  CASE WHEN w1.from_sector < w1.to_sector THEN w1.from_sector ELSE w1.to_sector END AS a,
  CASE WHEN w1.from_sector < w1.to_sector THEN w1.to_sector ELSE w1.from_sector END AS b
FROM sector_warps AS w1
JOIN sector_warps AS w2
  ON w1.from_sector = w2.to_sector
 AND w1.to_sector   = w2.from_sector
GROUP BY a, b;
CREATE VIEW player_info_v1  AS    SELECT      p.id         AS player_id,      p.name       AS player_name,      p.number     AS player_number,      sh.sector    AS sector_id,       sctr.name    AS sector_name,      p.credits    AS petty_cash,      p.alignment  AS alignment,      p.experience AS experience,      p.ship       AS ship_number,      sh.id        AS ship_id,      sh.name      AS ship_name,      sh.type_id   AS ship_type_id,      st.name      AS ship_type_name,      st.maxholds  AS ship_holds_capacity,       sh.holds     AS ship_holds_current,       sh.fighters  AS ship_fighters,      sh.mines     AS ship_mines,               sh.limpets   AS ship_limpets,             sh.genesis   AS ship_genesis,             sh.photons   AS ship_photons,             sh.beacons   AS ship_beacons,             sh.colonists AS ship_colonists,           sh.equipment AS ship_equipment,           sh.organics  AS ship_organics,            sh.ore       AS ship_ore,                 sh.ported    AS ship_ported,              sh.onplanet  AS ship_onplanet,            (COALESCE(p.credits,0) + COALESCE(sh.fighters,0)*2) AS approx_worth    FROM players p    LEFT JOIN ships      sh   ON sh.id = p.ship    LEFT JOIN shiptypes  st   ON st.id = sh.type_id    LEFT JOIN sectors    sctr ON sctr.id = sh.sector;
CREATE VIEW sector_search_index  AS   SELECT        'sector' AS kind,       s.id AS id,       s.name AS name,       s.id AS sector_id,       s.name AS sector_name,       s.name AS search_term_1   FROM sectors s   UNION ALL   SELECT        'port' AS kind,       p.id AS id,       p.name AS name,       p.sector AS sector_id,       s.name AS sector_name,       p.name AS search_term_1   FROM ports p   JOIN sectors s ON s.id = p.sector;
CREATE INDEX idx_player_block_blocked ON player_block (blocked_id);
CREATE INDEX idx_notice_seen_player ON notice_seen (player_id, seen_at DESC);
CREATE INDEX idx_system_notice_active ON system_notice (expires_at, created_at DESC);
CREATE INDEX idx_warps_from ON sector_warps(from_sector);
CREATE INDEX idx_warps_to   ON sector_warps(to_sector);
CREATE INDEX idx_ports_loc  ON ports(sector);
CREATE INDEX idx_planets_sector ON planets(sector);
CREATE INDEX idx_citadels_planet ON citadels(planet_id);
CREATE INDEX ix_warps_from_to ON sector_warps(from_sector, to_sector);
CREATE INDEX idx_players_sector   ON players(sector);
CREATE INDEX idx_players_ship     ON players(ship);
CREATE INDEX idx_ship_own_ship   ON ship_ownership(ship_id);
CREATE UNIQUE INDEX idx_ports_loc_number ON ports(sector, number);
CREATE UNIQUE INDEX idx_mail_idem_recipient   ON mail(idempotency_key, recipient_id)   WHERE idempotency_key IS NOT NULL;
CREATE INDEX idx_mail_inbox    ON mail(recipient_id, deleted, archived, sent_at DESC);
CREATE INDEX idx_mail_unread   ON mail(recipient_id, read_at);
CREATE INDEX idx_mail_sender   ON mail(sender_id, sent_at DESC);
CREATE INDEX idx_subspace_time   ON subspace(posted_at DESC);
CREATE UNIQUE INDEX ux_corp_name_uc   ON corporations(upper(name));
CREATE UNIQUE INDEX ux_corp_tag_uc   ON corporations(upper(tag)) WHERE tag IS NOT NULL;
CREATE INDEX ix_corporations_owner   ON corporations(owner_id);
CREATE INDEX idx_ship_own_player ON ship_ownership(player_id);
CREATE INDEX ix_corp_members_player   ON corp_members(player_id);
CREATE INDEX ix_corp_members_role   ON corp_members(corp_id, role);
CREATE INDEX idx_corp_mail_corp   ON corp_mail(corp_id, posted_at DESC);
CREATE INDEX idx_corp_log_corp_time   ON corp_log(corp_id, created_at DESC);
CREATE INDEX idx_corp_log_type   ON corp_log(event_type, created_at DESC);
CREATE INDEX idx_sys_events_time   ON system_events(created_at DESC);
CREATE INDEX idx_sys_events_scope   ON system_events(scope);
CREATE INDEX idx_subscriptions_player   ON subscriptions(player_id, enabled);
CREATE INDEX idx_subs_enabled   ON subscriptions(enabled);
CREATE INDEX idx_subs_event   ON subscriptions(event_type);
CREATE INDEX idx_player_prefs_player ON player_prefs(player_id)  ;
CREATE INDEX idx_bookmarks_player ON player_bookmarks(player_id)  ;
CREATE INDEX idx_avoid_player ON player_avoid(player_id)  ;
CREATE INDEX idx_notes_player ON player_notes(player_id)  ;
CREATE TRIGGER corporations_touch_updated   AFTER UPDATE ON corporations   FOR EACH ROW   BEGIN     UPDATE corporations       SET updated_at = strftime('%Y-%m-%dT%H:%M:%SZ','now')     WHERE id = NEW.id;  END;
CREATE TRIGGER corp_owner_must_be_member_insert   AFTER INSERT ON corporations   FOR EACH ROW   WHEN NEW.owner_id IS NOT NULL   AND NOT EXISTS (SELECT 1 FROM corp_members WHERE corp_id=NEW.id AND player_id=NEW.owner_id)   BEGIN     INSERT INTO corp_members(corp_id, player_id, role) VALUES(NEW.id, NEW.owner_id, 'Leader');  END;
CREATE TRIGGER corp_one_leader_guard   BEFORE INSERT ON corp_members   FOR EACH ROW   WHEN NEW.role='Leader'   AND EXISTS (SELECT 1 FROM corp_members WHERE corp_id=NEW.corp_id AND role='Leader')   BEGIN     SELECT RAISE(ABORT, 'corp may have only one Leader');  END;
CREATE TRIGGER corp_owner_leader_sync   AFTER UPDATE OF owner_id ON corporations   FOR EACH ROW   BEGIN     UPDATE corp_members SET role='Officer'      WHERE corp_id=NEW.id AND role='Leader' AND player_id<>NEW.owner_id;     INSERT INTO corp_members(corp_id, player_id, role)     VALUES(NEW.id, NEW.owner_id, 'Leader')     ON CONFLICT(corp_id, player_id) DO UPDATE SET role='Leader';  END;
CREATE TABLE currencies (     code TEXT PRIMARY KEY,     name TEXT NOT NULL,     minor_unit INTEGER NOT NULL DEFAULT 1 CHECK (minor_unit > 0),     is_default INTEGER NOT NULL DEFAULT 0 CHECK (is_default IN (0,1))   );
CREATE TABLE commodities (     id INTEGER PRIMARY KEY,     code TEXT UNIQUE NOT NULL,     name TEXT NOT NULL,     illegal INTEGER NOT NULL DEFAULT 0,     base_price INTEGER NOT NULL DEFAULT 0 CHECK (base_price >= 0),     volatility INTEGER NOT NULL DEFAULT 0 CHECK (volatility >= 0)   );
CREATE TABLE commodity_orders (     id INTEGER PRIMARY KEY,     actor_type TEXT NOT NULL CHECK (actor_type IN ('player','corp','npc_planet','port')),     actor_id INTEGER NOT NULL,     location_type TEXT NOT NULL CHECK (location_type IN ('planet','port')),     location_id INTEGER NOT NULL,     commodity_id INTEGER NOT NULL REFERENCES commodities(id) ON DELETE CASCADE,     side TEXT NOT NULL CHECK (side IN ('buy','sell')),     quantity INTEGER NOT NULL CHECK (quantity > 0),     price INTEGER NOT NULL CHECK (price >= 0),     status TEXT NOT NULL DEFAULT 'open' CHECK (status IN ('open','filled','cancelled','expired')),     ts TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now'))   );
CREATE INDEX idx_commodity_orders_comm ON commodity_orders(commodity_id, status);
CREATE TABLE commodity_trades (     id INTEGER PRIMARY KEY,     commodity_id INTEGER NOT NULL REFERENCES commodities(id) ON DELETE CASCADE,     buyer_actor_type TEXT NOT NULL CHECK (buyer_actor_type IN ('player','corp','npc_planet','port')),     buyer_actor_id INTEGER NOT NULL,     buyer_location_type TEXT NOT NULL CHECK (buyer_location_type IN ('planet','port')),     buyer_location_id INTEGER NOT NULL,     seller_actor_type TEXT NOT NULL CHECK (seller_actor_type IN ('player','corp','npc_planet','port')),     seller_actor_id INTEGER NOT NULL,     seller_location_type TEXT NOT NULL CHECK (seller_location_type IN ('planet','port')),     seller_location_id INTEGER NOT NULL,     quantity INTEGER NOT NULL CHECK (quantity > 0),     price INTEGER NOT NULL CHECK (price >= 0),     ts TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now')),     settlement_tx_buy INTEGER,     settlement_tx_sell INTEGER   );
CREATE TABLE bank_accounts (     owner_type TEXT NOT NULL,     owner_id INTEGER NOT NULL,     currency TEXT NOT NULL DEFAULT 'CRD' REFERENCES currencies(code),     balance INTEGER NOT NULL DEFAULT 0 CHECK (balance >= 0),     last_interest_at TEXT,     PRIMARY KEY (owner_type, owner_id)   );
CREATE TABLE bank_tx (     id INTEGER PRIMARY KEY,     owner_type TEXT NOT NULL,     owner_id INTEGER NOT NULL,     ts TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now')),     kind TEXT NOT NULL CHECK (kind IN (       'deposit',       'withdraw',       'transfer_in',       'transfer_out',       'interest',       'adjustment'     )),     amount INTEGER NOT NULL CHECK (amount > 0),     balance_after INTEGER,     currency TEXT NOT NULL DEFAULT 'CRD' REFERENCES currencies(code),     memo TEXT,     idempotency_key TEXT UNIQUE,     FOREIGN KEY (owner_type, owner_id) REFERENCES bank_accounts(owner_type, owner_id) ON DELETE CASCADE   );
CREATE INDEX idx_bank_tx_owner_ts ON bank_tx(owner_type, owner_id, ts);
CREATE INDEX idx_bank_tx_kind_ts   ON bank_tx(kind, ts);
CREATE TABLE bank_interest_policy (     id INTEGER PRIMARY KEY CHECK (id = 1),     apr_bps INTEGER NOT NULL DEFAULT 0 CHECK (apr_bps >= 0),     compounding TEXT NOT NULL DEFAULT 'daily' CHECK (compounding IN ('none','daily','weekly','monthly')),     min_balance INTEGER NOT NULL DEFAULT 0 CHECK (min_balance >= 0),     max_balance INTEGER NOT NULL DEFAULT 9223372036854775807,     last_run_at TEXT,     currency TEXT NOT NULL DEFAULT 'CRD' REFERENCES currencies(code)   );
CREATE TABLE bank_orders (     id INTEGER PRIMARY KEY,     player_id INTEGER NOT NULL REFERENCES players(id) ON DELETE CASCADE,     kind TEXT NOT NULL CHECK (kind IN ('recurring','once')),     schedule TEXT NOT NULL,     next_run_at TEXT,     enabled INTEGER NOT NULL DEFAULT 1 CHECK (enabled IN (0,1)),     amount INTEGER NOT NULL CHECK (amount > 0),     currency TEXT NOT NULL DEFAULT 'CRD' REFERENCES currencies(code),     to_entity TEXT NOT NULL CHECK (to_entity IN ('player','corp','gov','npc')),     to_id INTEGER NOT NULL,     memo TEXT   );
CREATE TABLE bank_flags (     player_id INTEGER PRIMARY KEY REFERENCES players(id) ON DELETE CASCADE,     is_frozen INTEGER NOT NULL DEFAULT 0 CHECK (is_frozen IN (0,1)),     risk_tier TEXT NOT NULL DEFAULT 'normal' CHECK (risk_tier IN ('normal','elevated','high','blocked'))   );
CREATE TRIGGER trg_bank_tx_before_insert   BEFORE INSERT ON bank_tx   FOR EACH ROW   BEGIN     INSERT OR IGNORE INTO bank_accounts(owner_type, owner_id, currency, balance, last_interest_at)     VALUES (NEW.owner_type, NEW.owner_id, COALESCE(NEW.currency,'CRD'), 0, NULL);     SELECT CASE       WHEN NEW.kind IN ('withdraw','transfer_out')         AND (SELECT balance FROM bank_accounts WHERE owner_type = NEW.owner_type AND owner_id = NEW.owner_id) - NEW.amount < 0       THEN RAISE(ABORT, 'BANK_INSUFFICIENT_FUNDS')       ELSE 1     END;   END;
CREATE TRIGGER trg_bank_tx_after_insert   AFTER INSERT ON bank_tx   FOR EACH ROW   BEGIN     UPDATE bank_accounts     SET balance = CASE NEW.kind                     WHEN 'withdraw'     THEN balance - NEW.amount                     WHEN 'transfer_out' THEN balance - NEW.amount                     ELSE balance + NEW.amount                   END     WHERE owner_type = NEW.owner_type AND owner_id = NEW.owner_id;     UPDATE bank_tx     SET balance_after = (SELECT balance FROM bank_accounts WHERE owner_type = NEW.owner_type AND owner_id = NEW.owner_id)     WHERE id = NEW.id;   END;
CREATE TRIGGER trg_bank_tx_before_delete   BEFORE DELETE ON bank_tx   FOR EACH ROW   BEGIN     SELECT RAISE(ABORT, 'BANK_LEDGER_APPEND_ONLY');   END;
CREATE TABLE corp_accounts (     corp_id INTEGER PRIMARY KEY REFERENCES corps(id) ON DELETE CASCADE,     currency TEXT NOT NULL DEFAULT 'CRD' REFERENCES currencies(code),     balance INTEGER NOT NULL DEFAULT 0 CHECK (balance >= 0),     last_interest_at TEXT   );
CREATE TABLE corp_tx (     id INTEGER PRIMARY KEY,     corp_id INTEGER NOT NULL REFERENCES corps(id) ON DELETE CASCADE,     ts TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now')),     kind TEXT NOT NULL CHECK (kind IN (       'deposit',       'withdraw',       'transfer_in',       'transfer_out',       'interest',       'dividend',       'salary',       'adjustment'     )),     amount INTEGER NOT NULL CHECK (amount > 0),     balance_after INTEGER,     currency TEXT NOT NULL DEFAULT 'CRD' REFERENCES currencies(code),     memo TEXT,     idempotency_key TEXT UNIQUE   );
CREATE INDEX idx_corp_tx_corp_ts ON corp_tx(corp_id, ts);
CREATE TABLE corp_interest_policy (     id INTEGER PRIMARY KEY CHECK (id = 1),     apr_bps INTEGER NOT NULL DEFAULT 0 CHECK (apr_bps >= 0),     compounding TEXT NOT NULL DEFAULT 'none' CHECK (compounding IN ('none','daily','weekly','monthly')),     last_run_at TEXT,     currency TEXT NOT NULL DEFAULT 'CRD' REFERENCES currencies(code)   );
CREATE TRIGGER trg_corp_tx_before_insert   BEFORE INSERT ON corp_tx   FOR EACH ROW   BEGIN     INSERT OR IGNORE INTO corp_accounts(corp_id, currency, balance, last_interest_at)     VALUES (NEW.corp_id, COALESCE(NEW.currency,'CRD'), 0, NULL);     SELECT CASE       WHEN NEW.kind IN ('withdraw','transfer_out','dividend','salary')         AND (SELECT balance FROM corp_accounts WHERE corp_id = NEW.corp_id) - NEW.amount < 0       THEN RAISE(ABORT, 'CORP_INSUFFICIENT_FUNDS')       ELSE 1     END;   END;
CREATE TRIGGER trg_corp_tx_after_insert   AFTER INSERT ON corp_tx   FOR EACH ROW   BEGIN     UPDATE corp_accounts     SET balance = CASE NEW.kind                     WHEN 'withdraw'     THEN balance - NEW.amount                     WHEN 'transfer_out' THEN balance - NEW.amount                     WHEN 'dividend'     THEN balance - NEW.amount                     WHEN 'salary'       THEN balance - NEW.amount                     ELSE balance + NEW.amount                   END     WHERE corp_id = NEW.corp_id;     UPDATE corp_tx     SET balance_after = (SELECT balance FROM corp_accounts WHERE corp_id = NEW.corp_id)     WHERE id = NEW.id;   END;
CREATE TABLE stocks (     id INTEGER PRIMARY KEY,     corp_id INTEGER NOT NULL REFERENCES corps(id) ON DELETE CASCADE,     ticker TEXT NOT NULL UNIQUE,     total_shares INTEGER NOT NULL CHECK (total_shares > 0),     par_value INTEGER NOT NULL DEFAULT 0 CHECK (par_value >= 0),     current_price INTEGER NOT NULL DEFAULT 0 CHECK (current_price >= 0),     last_dividend_ts TEXT   );
CREATE TABLE stock_orders (     id INTEGER PRIMARY KEY,     player_id INTEGER NOT NULL REFERENCES players(id) ON DELETE CASCADE,     stock_id INTEGER NOT NULL REFERENCES stocks(id) ON DELETE CASCADE,     type TEXT NOT NULL CHECK (type IN ('buy','sell')),     quantity INTEGER NOT NULL CHECK (quantity > 0),     price INTEGER NOT NULL CHECK (price >= 0),     status TEXT NOT NULL DEFAULT 'open' CHECK (status IN ('open','filled','cancelled','expired')),     ts TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now'))   );
CREATE INDEX idx_stock_orders_stock ON stock_orders(stock_id, status);
CREATE TABLE stock_trades (     id INTEGER PRIMARY KEY,     stock_id INTEGER NOT NULL REFERENCES stocks(id) ON DELETE CASCADE,     buyer_id INTEGER NOT NULL REFERENCES players(id) ON DELETE CASCADE,     seller_id INTEGER NOT NULL REFERENCES players(id) ON DELETE CASCADE,     quantity INTEGER NOT NULL CHECK (quantity > 0),     price INTEGER NOT NULL CHECK (price >= 0),     ts TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now')),     settlement_tx_buy INTEGER,     settlement_tx_sell INTEGER   );
CREATE TABLE stock_dividends (     id INTEGER PRIMARY KEY,     stock_id INTEGER NOT NULL REFERENCES stocks(id) ON DELETE CASCADE,     amount_per_share INTEGER NOT NULL CHECK (amount_per_share >= 0),     declared_ts TEXT NOT NULL,     paid_ts TEXT   );
CREATE TABLE stock_indices (     id INTEGER PRIMARY KEY,     name TEXT UNIQUE NOT NULL   );
CREATE TABLE stock_index_members (     index_id INTEGER NOT NULL REFERENCES stock_indices(id) ON DELETE CASCADE,     stock_id INTEGER NOT NULL REFERENCES stocks(id) ON DELETE CASCADE,     weight REAL NOT NULL DEFAULT 1.0,     PRIMARY KEY (index_id, stock_id)   );
CREATE TABLE insurance_funds (     id INTEGER PRIMARY KEY,     owner_type TEXT NOT NULL CHECK (owner_type IN ('system','corp','player')),     owner_id INTEGER,     balance INTEGER NOT NULL DEFAULT 0 CHECK (balance >= 0)   );
CREATE TABLE insurance_policies (     id INTEGER PRIMARY KEY,     holder_type TEXT NOT NULL CHECK (holder_type IN ('player','corp')),     holder_id INTEGER NOT NULL,     subject_type TEXT NOT NULL CHECK (subject_type IN ('ship','cargo','planet')),     subject_id INTEGER NOT NULL,     premium INTEGER NOT NULL CHECK (premium >= 0),     payout INTEGER NOT NULL CHECK (payout >= 0),     fund_id INTEGER REFERENCES insurance_funds(id) ON DELETE SET NULL,     start_ts TEXT NOT NULL,     expiry_ts TEXT,     active INTEGER NOT NULL DEFAULT 1 CHECK (active IN (0,1))   );
CREATE INDEX idx_policies_holder ON insurance_policies(holder_type, holder_id);
CREATE TABLE insurance_claims (     id INTEGER PRIMARY KEY,     policy_id INTEGER NOT NULL REFERENCES insurance_policies(id) ON DELETE CASCADE,     event_id TEXT,     amount INTEGER NOT NULL CHECK (amount >= 0),     status TEXT NOT NULL DEFAULT 'open' CHECK (status IN ('open','paid','denied')),     ts TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now')),     paid_bank_tx INTEGER   );
CREATE TABLE risk_profiles (     id INTEGER PRIMARY KEY,     entity_type TEXT NOT NULL CHECK (entity_type IN ('player','corp')),     entity_id INTEGER NOT NULL,     risk_score INTEGER NOT NULL DEFAULT 0   );
CREATE TABLE loans (     id INTEGER PRIMARY KEY,     lender_type TEXT NOT NULL CHECK (lender_type IN ('player','corp','bank')),     lender_id INTEGER,     borrower_type TEXT NOT NULL CHECK (borrower_type IN ('player','corp')),     borrower_id INTEGER NOT NULL,     principal INTEGER NOT NULL CHECK (principal > 0),     rate_bps INTEGER NOT NULL DEFAULT 0 CHECK (rate_bps >= 0),     term_days INTEGER NOT NULL CHECK (term_days > 0),     next_due TEXT,     status TEXT NOT NULL DEFAULT 'active' CHECK (status IN ('active','paid','defaulted','written_off')),     created_ts TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now'))   );
CREATE TABLE loan_payments (     id INTEGER PRIMARY KEY,     loan_id INTEGER NOT NULL REFERENCES loans(id) ON DELETE CASCADE,     ts TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now')),     amount INTEGER NOT NULL CHECK (amount > 0),     status TEXT NOT NULL DEFAULT 'posted' CHECK (status IN ('posted','reversed')),     bank_tx_id INTEGER   );
CREATE TABLE collateral (     id INTEGER PRIMARY KEY,     loan_id INTEGER NOT NULL REFERENCES loans(id) ON DELETE CASCADE,     asset_type TEXT NOT NULL CHECK (asset_type IN ('ship','planet','cargo','stock','other')),     asset_id INTEGER NOT NULL,     appraised_value INTEGER NOT NULL DEFAULT 0 CHECK (appraised_value >= 0)   );
CREATE TABLE credit_ratings (     entity_type TEXT NOT NULL CHECK (entity_type IN ('player','corp')),     entity_id INTEGER NOT NULL,     score INTEGER NOT NULL DEFAULT 600 CHECK (score BETWEEN 300 AND 900),     last_update TEXT,     PRIMARY KEY (entity_type, entity_id)   );
CREATE TABLE charters (     id INTEGER PRIMARY KEY,     name TEXT NOT NULL UNIQUE,     granted_by TEXT NOT NULL DEFAULT 'federation',     monopoly_scope TEXT,     start_ts TEXT NOT NULL,     expiry_ts TEXT   );
CREATE TABLE expeditions (     id INTEGER PRIMARY KEY,     leader_player_id INTEGER NOT NULL REFERENCES players(id) ON DELETE CASCADE,     charter_id INTEGER REFERENCES charters(id) ON DELETE SET NULL,     goal TEXT NOT NULL,     target_region TEXT,     pledged_total INTEGER NOT NULL DEFAULT 0 CHECK (pledged_total >= 0),     duration_days INTEGER NOT NULL DEFAULT 7 CHECK (duration_days > 0),     status TEXT NOT NULL DEFAULT 'planning' CHECK (status IN ('planning','launched','complete','failed','aborted')),     created_ts TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now'))   );
CREATE TABLE expedition_backers (     expedition_id INTEGER NOT NULL REFERENCES expeditions(id) ON DELETE CASCADE,     backer_type TEXT NOT NULL CHECK (backer_type IN ('player','corp')),     backer_id INTEGER NOT NULL,     pledged_amount INTEGER NOT NULL CHECK (pledged_amount >= 0),     share_pct REAL NOT NULL CHECK (share_pct >= 0),     PRIMARY KEY (expedition_id, backer_type, backer_id)   );
CREATE TABLE expedition_returns (     id INTEGER PRIMARY KEY,     expedition_id INTEGER NOT NULL REFERENCES expeditions(id) ON DELETE CASCADE,     ts TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now')),     amount INTEGER NOT NULL CHECK (amount >= 0),     bank_tx_id INTEGER   );
CREATE TABLE futures_contracts (     id INTEGER PRIMARY KEY,     commodity_id INTEGER NOT NULL REFERENCES commodities(id) ON DELETE CASCADE,     buyer_type TEXT NOT NULL CHECK (buyer_type IN ('player','corp')),     buyer_id INTEGER NOT NULL,     seller_type TEXT NOT NULL CHECK (seller_type IN ('player','corp')),     seller_id INTEGER NOT NULL,     strike_price INTEGER NOT NULL CHECK (strike_price >= 0),     expiry_ts TEXT NOT NULL,     status TEXT NOT NULL DEFAULT 'open' CHECK (status IN ('open','settled','defaulted','cancelled'))   );
CREATE TABLE warehouses (     id INTEGER PRIMARY KEY,     location_type TEXT NOT NULL CHECK (location_type IN ('sector','planet','port')),     location_id INTEGER NOT NULL,     owner_type TEXT NOT NULL CHECK (owner_type IN ('player','corp')),     owner_id INTEGER NOT NULL   );
CREATE TABLE gov_accounts (     id INTEGER PRIMARY KEY,     name TEXT NOT NULL UNIQUE,     balance INTEGER NOT NULL DEFAULT 0 CHECK (balance >= 0)   );
CREATE TABLE tax_policies (     id INTEGER PRIMARY KEY,     name TEXT NOT NULL,     tax_type TEXT NOT NULL CHECK (tax_type IN ('trade','income','corp','wealth','transfer')),     rate_bps INTEGER NOT NULL DEFAULT 0 CHECK (rate_bps >= 0),     active INTEGER NOT NULL DEFAULT 1 CHECK (active IN (0,1))   );
CREATE TABLE tax_ledgers (     id INTEGER PRIMARY KEY,     policy_id INTEGER NOT NULL REFERENCES tax_policies(id) ON DELETE CASCADE,     payer_type TEXT NOT NULL CHECK (payer_type IN ('player','corp')),     payer_id INTEGER NOT NULL,     amount INTEGER NOT NULL CHECK (amount >= 0),     ts TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now')),     bank_tx_id INTEGER   );
CREATE TABLE fines (     id INTEGER PRIMARY KEY,     issued_by TEXT NOT NULL DEFAULT 'federation',     recipient_type TEXT NOT NULL CHECK (recipient_type IN ('player','corp')),     recipient_id INTEGER NOT NULL,     reason TEXT,     amount INTEGER NOT NULL CHECK (amount >= 0),     status TEXT NOT NULL DEFAULT 'unpaid' CHECK (status IN ('unpaid','paid','void')),     issued_ts TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now')),     paid_bank_tx INTEGER   );
CREATE TABLE bounties (     id INTEGER PRIMARY KEY,     posted_by_type TEXT NOT NULL CHECK (posted_by_type IN ('player','corp','gov','npc')),     posted_by_id INTEGER,     target_type TEXT NOT NULL CHECK (target_type IN ('player','corp','npc')),     target_id INTEGER NOT NULL,     reward INTEGER NOT NULL CHECK (reward >= 0),     escrow_bank_tx INTEGER,     status TEXT NOT NULL DEFAULT 'open' CHECK (status IN ('open','claimed','cancelled','expired')),     posted_ts TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now')),     claimed_by INTEGER,     paid_bank_tx INTEGER   );
CREATE TABLE grants (     id INTEGER PRIMARY KEY,     name TEXT NOT NULL,     recipient_type TEXT NOT NULL CHECK (recipient_type IN ('player','corp')),     recipient_id INTEGER NOT NULL,     amount INTEGER NOT NULL CHECK (amount >= 0),     awarded_ts TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now')),     bank_tx_id INTEGER   );
CREATE TABLE research_projects (     id INTEGER PRIMARY KEY,     sponsor_type TEXT NOT NULL CHECK (sponsor_type IN ('player','corp','gov')),     sponsor_id INTEGER,     title TEXT NOT NULL,     field TEXT NOT NULL,     cost INTEGER NOT NULL CHECK (cost >= 0),     progress INTEGER NOT NULL DEFAULT 0 CHECK (progress BETWEEN 0 AND 100),     status TEXT NOT NULL DEFAULT 'active' CHECK (status IN ('active','paused','complete','failed')),     created_ts TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now'))   );
CREATE TABLE research_contributors (     project_id INTEGER NOT NULL REFERENCES research_projects(id) ON DELETE CASCADE,     actor_type TEXT NOT NULL CHECK (actor_type IN ('player','corp')),     actor_id INTEGER NOT NULL,     amount INTEGER NOT NULL CHECK (amount >= 0),     PRIMARY KEY (project_id, actor_type, actor_id)   );
CREATE TABLE research_results (     id INTEGER PRIMARY KEY,     project_id INTEGER NOT NULL REFERENCES research_projects(id) ON DELETE CASCADE,     blueprint_code TEXT NOT NULL,     unlocked_ts TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now'))   );
CREATE TABLE black_accounts (     id INTEGER PRIMARY KEY,     owner_type TEXT NOT NULL CHECK (owner_type IN ('player','corp','npc')),     owner_id INTEGER NOT NULL,     balance INTEGER NOT NULL DEFAULT 0 CHECK (balance >= 0)   );
CREATE TABLE laundering_ops (     id INTEGER PRIMARY KEY,     from_black_id INTEGER REFERENCES black_accounts(id) ON DELETE SET NULL,     to_player_id INTEGER REFERENCES players(id) ON DELETE SET NULL,     amount INTEGER NOT NULL CHECK (amount > 0),     risk_pct INTEGER NOT NULL DEFAULT 25 CHECK (risk_pct BETWEEN 0 AND 100),     status TEXT NOT NULL DEFAULT 'pending' CHECK (status IN ('pending','cleaned','seized','failed')),     ts TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now'))   );
CREATE TABLE contracts_illicit (     id INTEGER PRIMARY KEY,     contractor_type TEXT NOT NULL CHECK (contractor_type IN ('player','corp','npc')),     contractor_id INTEGER NOT NULL,     target_type TEXT NOT NULL CHECK (target_type IN ('player','corp','npc')),     target_id INTEGER NOT NULL,     reward INTEGER NOT NULL CHECK (reward >= 0),     escrow_black_id INTEGER REFERENCES black_accounts(id) ON DELETE SET NULL,     status TEXT NOT NULL DEFAULT 'open' CHECK (status IN ('open','fulfilled','failed','cancelled')),     ts TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now'))   );
CREATE TABLE fences (     id INTEGER PRIMARY KEY,     npc_id INTEGER,     sector_id INTEGER,     reputation INTEGER NOT NULL DEFAULT 0   );
CREATE TABLE economic_indicators (     id INTEGER PRIMARY KEY,     ts TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now')),     inflation_bps INTEGER NOT NULL DEFAULT 0,     liquidity INTEGER NOT NULL DEFAULT 0,     credit_velocity REAL NOT NULL DEFAULT 0.0   );
CREATE TABLE sector_gdp (     sector_id INTEGER PRIMARY KEY,     gdp INTEGER NOT NULL DEFAULT 0,     last_update TEXT   );
CREATE TABLE event_triggers (     id INTEGER PRIMARY KEY,     name TEXT NOT NULL,     condition_json TEXT NOT NULL,     action_json TEXT NOT NULL   );
CREATE TABLE charities (     id INTEGER PRIMARY KEY,     name TEXT NOT NULL UNIQUE,     description TEXT   );
CREATE TABLE donations (     id INTEGER PRIMARY KEY,     charity_id INTEGER NOT NULL REFERENCES charities(id) ON DELETE CASCADE,     donor_type TEXT NOT NULL CHECK (donor_type IN ('player','corp')),     donor_id INTEGER NOT NULL,     amount INTEGER NOT NULL CHECK (amount >= 0),     ts TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now')),     bank_tx_id INTEGER   );
CREATE TABLE temples (     id INTEGER PRIMARY KEY,     name TEXT NOT NULL UNIQUE,     sector_id INTEGER,     favour INTEGER NOT NULL DEFAULT 0   );
CREATE TABLE guilds (     id INTEGER PRIMARY KEY,     name TEXT NOT NULL UNIQUE,     description TEXT   );
CREATE TABLE guild_memberships (     guild_id INTEGER NOT NULL REFERENCES guilds(id) ON DELETE CASCADE,     member_type TEXT NOT NULL CHECK (member_type IN ('player','corp')),     member_id INTEGER NOT NULL,     role TEXT NOT NULL DEFAULT 'member',     PRIMARY KEY (guild_id, member_type, member_id)   );
CREATE TABLE guild_dues (     id INTEGER PRIMARY KEY,     guild_id INTEGER NOT NULL REFERENCES guilds(id) ON DELETE CASCADE,     amount INTEGER NOT NULL CHECK (amount >= 0),     period TEXT NOT NULL DEFAULT 'monthly' CHECK (period IN ('weekly','monthly','quarterly','yearly'))   );
CREATE TABLE economy_snapshots (     id INTEGER PRIMARY KEY,     ts TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now')),     money_supply INTEGER NOT NULL DEFAULT 0,     total_deposits INTEGER NOT NULL DEFAULT 0,     total_loans INTEGER NOT NULL DEFAULT 0,     total_insured INTEGER NOT NULL DEFAULT 0,     notes TEXT   );
CREATE TABLE ai_economy_agents (     id INTEGER PRIMARY KEY,     name TEXT NOT NULL,     role TEXT NOT NULL,     config_json TEXT NOT NULL   );
CREATE TABLE anomaly_reports (     id INTEGER PRIMARY KEY,     ts TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now')),     severity TEXT NOT NULL CHECK (severity IN ('low','medium','high','critical')),     subject TEXT NOT NULL,     details TEXT NOT NULL,     resolved INTEGER NOT NULL DEFAULT 0 CHECK (resolved IN (0,1))   );
CREATE TABLE economy_policies (     id INTEGER PRIMARY KEY,     name TEXT NOT NULL UNIQUE,     config_json TEXT NOT NULL,     active INTEGER NOT NULL DEFAULT 1 CHECK (active IN (0,1))   );
CREATE VIEW v_player_networth AS   SELECT     p.id AS player_id,     p.name AS player_name,     COALESCE(ba.balance,0) AS bank_balance   FROM players p   LEFT JOIN bank_accounts ba ON ba.owner_type = 'player' AND ba.owner_id = p.id;
CREATE VIEW v_corp_treasury AS   SELECT     c.id AS corp_id,     c.name AS corp_name,     COALESCE(ca.balance,0) AS bank_balance   FROM corps c   LEFT JOIN corp_accounts ca ON ca.corp_id = c.id;
CREATE VIEW v_bounty_board AS   SELECT     b.id,     b.target_type,     b.target_id,     p_target.name AS target_name,     b.reward,     b.status,     b.posted_by_type,     b.posted_by_id,     CASE b.posted_by_type       WHEN 'player' THEN p_poster.name       WHEN 'corp' THEN c_poster.name       ELSE b.posted_by_type     END AS poster_name,     b.posted_ts   FROM bounties b   LEFT JOIN players p_target ON b.target_type = 'player' AND b.target_id = p_target.id   LEFT JOIN players p_poster ON b.posted_by_type = 'player' AND b.posted_by_id = p_poster.id   LEFT JOIN corps c_poster ON b.posted_by_type = 'corp' AND b.posted_by_id = c_poster.id   WHERE b.status = 'open';
CREATE VIEW v_bank_leaderboard AS   SELECT     ba.owner_id AS player_id,     p.name,     ba.balance   FROM bank_accounts ba   JOIN players p ON ba.owner_type = 'player' AND ba.owner_id = p.id   LEFT JOIN player_prefs pp ON ba.owner_id = pp.player_id AND pp.key = 'privacy.show_leaderboard'   WHERE COALESCE(pp.value, 'true') = 'true'   ORDER BY ba.balance DESC;
CREATE TABLE s2s_keys(  key_id TEXT PRIMARY KEY,  key_b64 TEXT NOT NULL,  is_default_tx INTEGER NOT NULL DEFAULT 0,  active INTEGER NOT NULL DEFAULT 1,  created_ts INTEGER NOT NULL);
CREATE TABLE cron_tasks(  id INTEGER PRIMARY KEY,  name TEXT UNIQUE NOT NULL,  schedule TEXT NOT NULL,  last_run_at INTEGER,  next_due_at INTEGER NOT NULL,  enabled INTEGER NOT NULL DEFAULT 1,  payload TEXT);
CREATE TABLE engine_events(  id INTEGER PRIMARY KEY,  ts INTEGER NOT NULL,  type TEXT NOT NULL,  actor_player_id INTEGER,  sector_id INTEGER,  payload TEXT NOT NULL,  idem_key TEXT,  processed_at INTEGER);
CREATE UNIQUE INDEX idx_engine_events_idem ON engine_events(idem_key) WHERE idem_key IS NOT NULL;
CREATE INDEX idx_engine_events_ts ON engine_events(ts);
CREATE INDEX idx_engine_events_actor_ts ON engine_events(actor_player_id, ts);
CREATE INDEX idx_engine_events_sector_ts ON engine_events(sector_id, ts);
CREATE TABLE engine_offset(  key TEXT PRIMARY KEY,  last_event_id INTEGER NOT NULL,  last_event_ts INTEGER NOT NULL);
CREATE TABLE engine_events_deadletter(  id INTEGER PRIMARY KEY,  ts INTEGER NOT NULL,  type TEXT NOT NULL,  payload TEXT NOT NULL,  error TEXT NOT NULL,  moved_at INTEGER NOT NULL);
CREATE TABLE engine_commands(  id INTEGER PRIMARY KEY,  type TEXT NOT NULL,  payload TEXT NOT NULL,  status TEXT NOT NULL DEFAULT 'ready',  priority INTEGER NOT NULL DEFAULT 100,  attempts INTEGER NOT NULL DEFAULT 0,  created_at INTEGER NOT NULL,  due_at INTEGER NOT NULL,  started_at INTEGER,  finished_at INTEGER,  worker TEXT,  idem_key TEXT);
CREATE UNIQUE INDEX idx_engine_cmds_idem ON engine_commands(idem_key) WHERE idem_key IS NOT NULL;
CREATE INDEX idx_engine_cmds_status_due ON engine_commands(status, due_at);
CREATE INDEX idx_engine_cmds_prio_due ON engine_commands(priority, due_at);
CREATE TABLE engine_audit(  id INTEGER PRIMARY KEY,  ts INTEGER NOT NULL,  cmd_type TEXT NOT NULL,  correlation_id TEXT,  actor_player_id INTEGER,  details TEXT);
CREATE TABLE news_feed(     news_id INTEGER PRIMARY KEY,     published_ts INTEGER NOT NULL,     expiration_ts INTEGER NOT NULL,     news_category TEXT NOT NULL,     article_text TEXT NOT NULL,     source_ids TEXT);
CREATE INDEX ix_news_feed_pub_ts ON news_feed(published_ts);
CREATE INDEX ix_news_feed_exp_ts ON news_feed(expiration_ts);
CREATE VIEW cronjobs AS SELECT     id,     name,     datetime(next_due_at, 'unixepoch') AS next_due_utc,     datetime(last_run_at, 'unixepoch') AS last_run_utc FROM     cron_tasks ORDER BY     next_due_at;
CREATE TABLE eligible_tows (ship_id INTEGER PRIMARY KEY, sector_id INTEGER, owner_id INTEGER, fighters INTEGER, alignment INTEGER, experience INTEGER);
/* No STAT tables available */
