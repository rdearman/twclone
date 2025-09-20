#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sqlite3.h>
#include "database.h"
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <jansson.h>


static sqlite3 *db_handle = NULL;
const char *DEFAULT_DB_NAME = "twconfig.db";
/* Forward declaration so we can call it before the definition */
static json_t *parse_neighbors_csv(const unsigned char *txt);

////////////////////

const char *create_table_sql[] = {
  "CREATE TABLE IF NOT EXISTS config ("
    " id INTEGER PRIMARY KEY CHECK (id = 1),"
    " turnsperday INTEGER,"
    " maxwarps_per_sector INTEGER,"
    " startingcredits INTEGER,"
    " startingfighters INTEGER,"
    " startingholds INTEGER,"
    " processinterval INTEGER,"
    " autosave INTEGER,"
    " max_ports INTEGER,"
    " max_planets_per_sector INTEGER,"
    " max_total_planets INTEGER,"
    " max_citadel_level INTEGER,"
    " number_of_planet_types INTEGER,"
    " max_ship_name_length INTEGER,"
    " ship_type_count INTEGER,"
    " hash_length INTEGER,"
    " default_nodes INTEGER,"
    " buff_size INTEGER,"
    " max_name_length INTEGER," " planet_type_count INTEGER" ");",

  "CREATE TABLE IF NOT EXISTS planettypes (id INTEGER PRIMARY KEY AUTOINCREMENT, code TEXT UNIQUE, typeDescription TEXT, typeName TEXT, citadelUpgradeTime_lvl1 INTEGER, citadelUpgradeTime_lvl2 INTEGER, citadelUpgradeTime_lvl3 INTEGER, citadelUpgradeTime_lvl4 INTEGER, citadelUpgradeTime_lvl5 INTEGER, citadelUpgradeTime_lvl6 INTEGER, citadelUpgradeOre_lvl1 INTEGER, citadelUpgradeOre_lvl2 INTEGER, citadelUpgradeOre_lvl3 INTEGER, citadelUpgradeOre_lvl4 INTEGER, citadelUpgradeOre_lvl5 INTEGER, citadelUpgradeOre_lvl6 INTEGER, citadelUpgradeOrganics_lvl1 INTEGER, citadelUpgradeOrganics_lvl2 INTEGER, citadelUpgradeOrganics_lvl3 INTEGER, citadelUpgradeOrganics_lvl4 INTEGER, citadelUpgradeOrganics_lvl5 INTEGER, citadelUpgradeOrganics_lvl6 INTEGER, citadelUpgradeEquipment_lvl1 INTEGER, citadelUpgradeEquipment_lvl2 INTEGER, citadelUpgradeEquipment_lvl3 INTEGER, citadelUpgradeEquipment_lvl4 INTEGER, citadelUpgradeEquipment_lvl5 INTEGER, citadelUpgradeEquipment_lvl6 INTEGER, citadelUpgradeColonist_lvl1 INTEGER, citadelUpgradeColonist_lvl2 INTEGER, citadelUpgradeColonist_lvl3 INTEGER, citadelUpgradeColonist_lvl4 INTEGER, citadelUpgradeColonist_lvl5 INTEGER, citadelUpgradeColonist_lvl6 INTEGER, maxColonist_ore INTEGER, maxColonist_organics INTEGER, maxColonist_equipment INTEGER, fighters INTEGER, fuelProduction INTEGER, organicsProduction INTEGER, equipmentProduction INTEGER, fighterProduction INTEGER, maxore INTEGER, maxorganics INTEGER, maxequipment INTEGER, maxfighters INTEGER, breeding REAL);",

  "CREATE TABLE IF NOT EXISTS ports (" "id INTEGER PRIMARY KEY AUTOINCREMENT, " "number INTEGER, " "name TEXT NOT NULL, " "location INTEGER NOT NULL, "	/* FK to sectors.id */
    "size INTEGER, "
    "techlevel INTEGER, "
    "max_ore INTEGER, "
    "max_organics INTEGER, "
    "max_equipment INTEGER, "
    "product_ore INTEGER, "
    "product_organics INTEGER, "
    "product_equipment INTEGER, "
    "credits INTEGER, "
    "invisible INTEGER DEFAULT 0, "
    "type INTEGER DEFAULT 1, "
    "FOREIGN KEY (location) REFERENCES sectors(id));",

  "CREATE TABLE IF NOT EXISTS port_trade ("
    "id INTEGER PRIMARY KEY AUTOINCREMENT, "
    "port_id INTEGER NOT NULL, "
    "maxproduct INTEGER, "
    "commodity TEXT CHECK(commodity IN ('ore','organics','equipment')), "
    "mode TEXT CHECK(mode IN ('buy','sell')), "
    "FOREIGN KEY (port_id) REFERENCES ports(id));",


  "CREATE TABLE IF NOT EXISTS players (" "id INTEGER PRIMARY KEY AUTOINCREMENT, " "number INTEGER, "	/* legacy player ID */
    "name TEXT NOT NULL, " "passwd TEXT NOT NULL, "	/* hashed password */
    "sector INTEGER, "		/* 0 if in a ship */
    "ship INTEGER, "		/* ship number */
    "experience INTEGER, " "alignment INTEGER, " "turns INTEGER, " "credits INTEGER, " "bank_balance INTEGER, " "flags INTEGER, "	/* bitfield: P_LOGGEDIN, P_STARDOCK, etc. */
    "lastprice INTEGER, " "firstprice INTEGER, " "integrity INTEGER, " "login_time INTEGER, " "last_update INTEGER, " "intransit INTEGER, "	/* 0/1 boolean */
    "beginmove INTEGER, "	/* timestamp */
    "movingto INTEGER, "	/* sector destination */
    "loggedin INTEGER, "	/* runtime only, but persisted if desired */
    "lastplanet INTEGER, "	/* last planet created */
    "score INTEGER, "
    "kills INTEGER, "
    "cloaked INTEGER, "
    "remote INTEGER, " "fighters INTEGER, " "holds INTEGER" ");",


  "CREATE TABLE IF NOT EXISTS sectors (id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT, beacon TEXT, nebulae TEXT);",

  "CREATE TABLE IF NOT EXISTS sector_warps (from_sector INTEGER, to_sector INTEGER, PRIMARY KEY (from_sector, to_sector), FOREIGN KEY (from_sector) REFERENCES sectors(id) ON DELETE CASCADE, FOREIGN KEY (to_sector) REFERENCES sectors(id) ON DELETE CASCADE);",


  "CREATE TABLE IF NOT EXISTS ships (" "id INTEGER PRIMARY KEY AUTOINCREMENT, " "number INTEGER, "	/* legacy ID */
    "name TEXT NOT NULL, " "type INTEGER, "	/* FK to shiptypes.id (Index+1 from shipinfo array) */
    "attack INTEGER, " "holds_used INTEGER, " "mines INTEGER, " "fighters_used INTEGER, " "genesis INTEGER, " "photons INTEGER, " "location INTEGER, "	/* FK to sectors.id */
    "fighters INTEGER, " "shields INTEGER, " "holds INTEGER, " "colonists INTEGER, " "equipment INTEGER, " "organics INTEGER, " "ore INTEGER, " "owner INTEGER, "	/* FK to players.id */
    "flags INTEGER, " "ported INTEGER, " "onplanet INTEGER" ");",


  "CREATE TABLE IF NOT EXISTS shiptypes (" "id INTEGER PRIMARY KEY AUTOINCREMENT, " "name TEXT NOT NULL, "	/* Coloured name string */
    "basecost INTEGER, " "maxattack INTEGER, " "initialholds INTEGER, " "maxholds INTEGER, " "maxfighters INTEGER, " "turns INTEGER, " "mines INTEGER, " "genesis INTEGER, " "twarp INTEGER, "	/* Transwarp capability (0/1) */
    "transportrange INTEGER, " "maxshields INTEGER, " "offense INTEGER, " "defense INTEGER, " "beacons INTEGER, " "holo INTEGER, "	/* Holo scanner (0/1) */
    "planet INTEGER, "		/* Can land on planets (0/1) */
    "photons INTEGER,"		/* Photon torpedo count */
    "can_purchase INTEGER"	/* Can be bought at a port */
    ");",

  "CREATE TABLE IF NOT EXISTS planets (" "id INTEGER PRIMARY KEY AUTOINCREMENT, " "num INTEGER, "	/* legacy planet ID */
    "sector INTEGER NOT NULL, "	/* FK to sectors.id */
    "name TEXT NOT NULL, " "owner INTEGER, "	/* FK to players.id */
    "population INTEGER, " "minerals INTEGER, " "ore INTEGER, " "energy INTEGER, " "type INTEGER, "	/* FK to planettypes.id */
    "creator TEXT, " "fuelColonist INTEGER, " "organicsColonist INTEGER, " "equipmentColonist INTEGER, " "fuel INTEGER, " "organics INTEGER, " "equipment INTEGER, " "fighters INTEGER, " "citadel_level INTEGER DEFAULT 0, "	/* replaces pointer to citadel struct */
    "FOREIGN KEY (sector) REFERENCES sectors(id), "
    "FOREIGN KEY (owner) REFERENCES players(id), "
    "FOREIGN KEY (type) REFERENCES planettypes(id)" ");",

  /* --- citadels table (fixed, closed properly) --- */
  "CREATE TABLE IF NOT EXISTS citadels (" "id INTEGER PRIMARY KEY AUTOINCREMENT, " "planet_id INTEGER UNIQUE NOT NULL, "	/* 1:1 link to planets.id */
    "level INTEGER, " "treasury INTEGER, " "militaryReactionLevel INTEGER, " "qCannonAtmosphere INTEGER, " "qCannonSector INTEGER, " "planetaryShields INTEGER, " "transporterlvl INTEGER, " "interdictor INTEGER, " "upgradePercent REAL, " "upgradestart INTEGER, " "owner INTEGER, "	/* FK to players.id */
    "shields INTEGER, "
    "torps INTEGER, "
    "fighters INTEGER, "
    "qtorps INTEGER, "
    "qcannon INTEGER, "
    "qcannontype INTEGER, "
    "qtorpstype INTEGER, "
    "military INTEGER, "
    "FOREIGN KEY (planet_id) REFERENCES planets(id) ON DELETE CASCADE, "
    "FOREIGN KEY (owner) REFERENCES players(id)" ");",

  "CREATE TABLE IF NOT EXISTS sessions (" "  token       TEXT PRIMARY KEY,"	/* 64-hex opaque */
    "  player_id   INTEGER NOT NULL," "  expires     INTEGER NOT NULL,"	/* epoch seconds (UTC) */
    "  created_at  INTEGER NOT NULL"	/* epoch seconds */
    ");"
    "CREATE INDEX IF NOT EXISTS idx_sessions_player ON sessions(player_id);"
    "CREATE INDEX IF NOT EXISTS idx_sessions_expires ON sessions(expires);",


  //////////////////////////////////////////////////////////////////////
/// CREATE VIEWS 
//////////////////////////////////////////////////////////////////////


  /* --- longest_tunnels view (array item ends with a comma, not semicolon) --- */
  "CREATE VIEW IF NOT EXISTS longest_tunnels AS\n"
    "WITH\n"
    "all_sectors AS (\n"
    "  SELECT from_sector AS id FROM sector_warps\n"
    "  UNION\n"
    "  SELECT to_sector   AS id FROM sector_warps\n"
    "),\n"
    "outdeg AS (\n"
    "  SELECT a.id, COALESCE(COUNT(w.to_sector),0) AS deg\n"
    "  FROM all_sectors a\n"
    "  LEFT JOIN sector_warps w ON w.from_sector = a.id\n"
    "  GROUP BY a.id\n"
    "),\n"
    "edges AS (\n"
    "  SELECT from_sector, to_sector FROM sector_warps\n"
    "),\n"
    "entry AS (\n"
    "  SELECT e.from_sector AS entry, e.to_sector AS next\n"
    "  FROM edges e\n"
    "  JOIN outdeg df ON df.id = e.from_sector AND df.deg > 1\n"
    "  JOIN outdeg dn ON dn.id = e.to_sector  AND dn.deg = 1\n"
    "),\n"
    "rec(entry, curr, path, steps) AS (\n"
    "  SELECT entry, next, printf('%d->%d', entry, next), 1\n"
    "  FROM entry\n"
    "  UNION ALL\n"
    "  SELECT r.entry, e.to_sector,\n"
    "         r.path || '->' || printf('%d', e.to_sector),\n"
    "         r.steps + 1\n"
    "  FROM rec r\n"
    "  JOIN edges e  ON e.from_sector = r.curr\n"
    "  JOIN outdeg d ON d.id = r.curr AND d.deg = 1\n"
    "  WHERE instr(r.path, '->' || printf('%d', e.to_sector) || '->') = 0\n"
    ")\n"
    "SELECT\n"
    "  r.entry                 AS entry_sector,\n"
    "  r.curr                  AS exit_sector,\n"
    "  r.path                  AS tunnel_path,\n"
    "  r.steps                 AS tunnel_length_edges\n"
    "FROM rec r\n"
    "JOIN outdeg d_exit ON d_exit.id = r.curr\n"
    "WHERE d_exit.deg <> 1 AND r.steps >= 2\n"
    "ORDER BY r.steps DESC, r.entry, r.curr;"
    /* ===================== GRAPH / TOPOLOGY ===================== */
/* 1) Degrees per sector (base for several views) */
    "CREATE VIEW IF NOT EXISTS sector_degrees AS\n"
    "WITH outdeg AS (\n"
    "  SELECT s.id, COUNT(w.to_sector) AS outdeg\n"
    "  FROM sectors s\n"
    "  LEFT JOIN sector_warps w ON w.from_sector = s.id\n"
    "  GROUP BY s.id\n"
    "), indeg AS (\n"
    "  SELECT s.id, COUNT(w.from_sector) AS indeg\n"
    "  FROM sectors s\n"
    "  LEFT JOIN sector_warps w ON w.to_sector = s.id\n"
    "  GROUP BY s.id\n"
    ")\n"
    "SELECT o.id AS sector_id, o.outdeg, i.indeg\n"
    "FROM outdeg o JOIN indeg i USING(id);",

/* 2) Dead-out (no outgoing) */
  "CREATE VIEW IF NOT EXISTS sectors_dead_out AS\n"
    "SELECT sector_id FROM sector_degrees WHERE outdeg = 0;",

/* 3) Dead-in (no incoming) */
  "CREATE VIEW IF NOT EXISTS sectors_dead_in AS\n"
    "SELECT sector_id FROM sector_degrees WHERE indeg = 0;",

/* 4) Isolated (no in, no out) */
  "CREATE VIEW IF NOT EXISTS sectors_isolated AS\n"
    "SELECT sector_id FROM sector_degrees WHERE outdeg = 0 AND indeg = 0;",

/* 5) One-way edges (no reverse) */
  "CREATE VIEW IF NOT EXISTS one_way_edges AS\n"
    "SELECT s.from_sector, s.to_sector\n"
    "FROM sector_warps s\n"
    "LEFT JOIN sector_warps r\n"
    "  ON r.from_sector = s.to_sector AND r.to_sector = s.from_sector\n"
    "WHERE r.from_sector IS NULL;",

/* 6) Bidirectional edges (dedup pairs) */
  "CREATE VIEW IF NOT EXISTS bidirectional_edges AS\n"
    "SELECT s.from_sector, s.to_sector\n"
    "FROM sector_warps s\n"
    "JOIN sector_warps r\n"
    "  ON r.from_sector = s.to_sector AND r.to_sector = s.from_sector\n"
    "WHERE s.from_sector < s.to_sector;",

/* 7) Sector adjacency list (CSV) */
  "CREATE VIEW IF NOT EXISTS sector_adjacency AS\n"
    "SELECT s.id AS sector_id,\n"
    "       COALESCE(GROUP_CONCAT(w.to_sector, ','), '') AS neighbors\n"
    "FROM sectors s\n"
    "LEFT JOIN sector_warps w ON w.from_sector = s.id\n" "GROUP BY s.id;",

/* 8) Sector summary (depends on sector_degrees) */
  "CREATE VIEW IF NOT EXISTS sector_summary AS\n"
    "WITH pc AS (\n"
    "  SELECT sector AS sector_id, COUNT(*) AS planet_count\n"
    "  FROM planets GROUP BY sector\n"
    "), prt AS (\n"
    "  SELECT location AS sector_id, COUNT(*) AS port_count\n"
    "  FROM ports GROUP BY location\n"
    ")\n"
    "SELECT s.id AS sector_id,\n"
    "       COALESCE(d.outdeg,0) AS outdeg,\n"
    "       COALESCE(d.indeg,0) AS indeg,\n"
    "       COALESCE(prt.port_count,0) AS ports,\n"
    "       COALESCE(pc.planet_count,0) AS planets\n"
    "FROM sectors s\n"
    "LEFT JOIN sector_degrees d ON d.sector_id = s.id\n"
    "LEFT JOIN prt ON prt.sector_id = s.id\n"
    "LEFT JOIN pc  ON pc.sector_id  = s.id;",

/* ===================== PORTS & TRADE ===================== */

/* 9) Compact trade code per port (B/S over ore|organics|equipment) */
  "CREATE VIEW IF NOT EXISTS port_trade_code AS\n"
    "WITH m AS (\n"
    "  SELECT p.id AS port_id,\n"
    "         MAX(CASE WHEN t.commodity='ore'       THEN (CASE t.mode WHEN 'buy' THEN 'B' ELSE 'S' END) END) AS ore,\n"
    "         MAX(CASE WHEN t.commodity='organics'  THEN (CASE t.mode WHEN 'buy' THEN 'B' ELSE 'S' END) END) AS org,\n"
    "         MAX(CASE WHEN t.commodity='equipment' THEN (CASE t.mode WHEN 'buy' THEN 'B' ELSE 'S' END) END) AS eqp\n"
    "  FROM ports p\n"
    "  LEFT JOIN port_trade t ON t.port_id = p.id\n"
    "  GROUP BY p.id\n"
    ")\n"
    "SELECT p.id, p.number, p.name, p.location AS sector_id, p.size, p.techlevel, p.credits,\n"
    "       COALESCE(m.ore,'-') || COALESCE(m.org,'-') || COALESCE(m.eqp,'-') AS trade_code\n"
    "FROM ports p\n" "LEFT JOIN m ON m.port_id = p.id;",

/* 10) Ports grouped by sector (depends on port_trade_code) */
  "CREATE VIEW IF NOT EXISTS sector_ports AS\n"
    "SELECT s.id AS sector_id,\n"
    "       COUNT(p.id) AS port_count,\n"
    "       COALESCE(GROUP_CONCAT(p.name || ':' || pt.trade_code, ' | '), '') AS ports\n"
    "FROM sectors s\n"
    "LEFT JOIN port_trade_code pt ON pt.sector_id = s.id\n"
    "LEFT JOIN ports p ON p.id = pt.id\n" "GROUP BY s.id;",

/* 11) Stardock location (by type=9 or name) */
  "CREATE VIEW IF NOT EXISTS stardock_location AS\n"
    "SELECT id AS port_id, number, name, location AS sector_id\n"
    "FROM ports\n" "WHERE type = 9 OR name LIKE '%Stardock%';",

/* ===================== PLANETS / CITADELS ===================== */

/* 12) Planet + citadel (with optional owner) */
  "CREATE VIEW IF NOT EXISTS planet_citadels AS\n"
    "SELECT c.id AS citadel_id,\n"
    "       c.level AS citadel_level,\n"
    "       p.id AS planet_id,\n"
    "       p.name AS planet_name,\n"
    "       p.sector AS sector_id,\n"
    "       c.owner AS owner_id,\n"
    "       pl.name AS owner_name\n"
    "FROM citadels c\n"
    "JOIN planets  p  ON p.id = c.planet_id\n"
    "LEFT JOIN players pl ON pl.id = c.owner;",

/* 13) Planets grouped by sector */
  "CREATE VIEW IF NOT EXISTS sector_planets AS\n"
    "SELECT s.id AS sector_id,\n"
    "       COUNT(p.id) AS planet_count,\n"
    "       COALESCE(GROUP_CONCAT(p.name, ', '), '') AS planets\n"
    "FROM sectors s\n"
    "LEFT JOIN planets p ON p.sector = s.id\n" "GROUP BY s.id;",

/* ===================== RUNTIME SNAPSHOTS ===================== */

/* 14) Player locations */
  "CREATE VIEW IF NOT EXISTS player_locations AS\n"
    "SELECT id AS player_id, name AS player_name,\n"
    "       sector AS sector_id,\n"
    "       ship   AS ship_number,\n"
    "       CASE WHEN sector IS NULL OR sector=0 THEN 'in_ship' ELSE 'in_sector' END AS location_kind\n"
    "FROM players;",

/* 15) Ships by sector */
  "CREATE VIEW IF NOT EXISTS ships_by_sector AS\n"
    "SELECT s.id AS sector_id,\n"
    "       COALESCE(GROUP_CONCAT(sh.name || '#' || sh.number, ', '), '') AS ships,\n"
    "       COUNT(sh.id) AS ship_count\n"
    "FROM sectors s\n"
    "LEFT JOIN ships sh ON sh.location = s.id\n" "GROUP BY s.id;",

/* ===================== OPS DASHBOARDS ===================== */

/* 16) Sector ops (depends on sector_summary, sector_ports, sector_planets, ships_by_sector) */
  "CREATE VIEW IF NOT EXISTS sector_ops AS\n"
    "SELECT ss.sector_id,\n"
    "       ss.outdeg, ss.indeg,\n"
    "       sp.port_count,\n"
    "       spp.planet_count,\n"
    "       sbs.ship_count\n"
    "FROM sector_summary ss\n"
    "LEFT JOIN sector_ports    sp  ON sp.sector_id  = ss.sector_id\n"
    "LEFT JOIN sector_planets  spp ON spp.sector_id = ss.sector_id\n"
    "LEFT JOIN ships_by_sector sbs ON sbs.sector_id = ss.sector_id;",

/* 17) World summary (one row) */
  "CREATE VIEW IF NOT EXISTS world_summary AS\n"
    "WITH a AS (SELECT COUNT(*) AS sectors FROM sectors),\n"
    "     b AS (SELECT COUNT(*) AS warps   FROM sector_warps),\n"
    "     c AS (SELECT COUNT(*) AS ports   FROM ports),\n"
    "     d AS (SELECT COUNT(*) AS planets FROM planets),\n"
    "     e AS (SELECT COUNT(*) AS players FROM players),\n"
    "     f AS (SELECT COUNT(*) AS ships   FROM ships)\n"
    "SELECT a.sectors, b.warps, c.ports, d.planets, e.players, f.ships\n"
    "FROM a,b,c,d,e,f;",

  "CREATE VIEW IF NOT EXISTS v_bidirectional_warps AS\n"
    "SELECT\n"
    "  CASE WHEN w1.from_sector < w1.to_sector THEN w1.from_sector ELSE w1.to_sector END AS a,\n"
    "  CASE WHEN w1.from_sector < w1.to_sector THEN w1.to_sector ELSE w1.from_sector END AS b\n"
    "FROM sector_warps AS w1\n"
    "JOIN sector_warps AS w2\n"
    "  ON w1.from_sector = w2.to_sector\n"
    " AND w1.to_sector   = w2.from_sector\n" "GROUP BY a, b;",


  /* --- player_info_v1 view and indexes --- */
  "CREATE VIEW IF NOT EXISTS player_info_v1 AS\n"
    "SELECT\n"
    "  p.id         AS player_id,\n"
    "  p.name       AS player_name,\n"
    "  p.number     AS player_number,\n"
    "  p.sector     AS sector_id,\n"
    "  sctr.name    AS sector_name,\n"
    "  p.credits    AS credits,\n"
    "  p.alignment  AS alignment,\n"
    "  p.experience AS experience,\n"
    "  p.ship       AS ship_number,\n"
    "  sh.id        AS ship_id,\n"
    "  sh.name      AS ship_name,\n"
    "  sh.type      AS ship_type_id,\n"
    "  st.name      AS ship_type_name,\n"
    "  sh.holds     AS ship_holds,\n"
    "  sh.fighters  AS ship_fighters,\n"
    "  (COALESCE(p.credits,0) + COALESCE(sh.fighters,0)*2) AS approx_worth\n"
    "FROM players p\n"
    "LEFT JOIN ships      sh   ON sh.number = p.ship\n"
    "LEFT JOIN shiptypes  st   ON st.id     = sh.type\n"
    "LEFT JOIN sectors    sctr ON sctr.id   = p.sector;",

//////////////////////////////////////////////////////////////////////
/// CREATE INDEX
//////////////////////////////////////////////////////////////////////
/* ===================== INDEXES ===================== */

  "CREATE INDEX IF NOT EXISTS idx_warps_from ON sector_warps(from_sector);",
  "CREATE INDEX IF NOT EXISTS idx_warps_to   ON sector_warps(to_sector);",
  "CREATE INDEX IF NOT EXISTS idx_ports_loc  ON ports(location);",
  "CREATE INDEX IF NOT EXISTS idx_planets_sector ON planets(sector);",
  "CREATE INDEX IF NOT EXISTS idx_citadels_planet ON citadels(planet_id);",
  "CREATE INDEX IF NOT EXISTS ix_warps_from_to ON sector_warps(from_sector, to_sector);",
  "CREATE INDEX IF NOT EXISTS idx_players_name     ON players(name);",
  "CREATE INDEX IF NOT EXISTS idx_players_sector   ON players(sector);",
  "CREATE INDEX IF NOT EXISTS idx_players_ship     ON players(ship);",
  "CREATE INDEX IF NOT EXISTS idx_ships_number     ON ships(number);",
  "CREATE INDEX IF NOT EXISTS idx_ships_id         ON ships(id);",
  "CREATE INDEX IF NOT EXISTS idx_sectors_id       ON sectors(id);",

};


const char *insert_default_sql[] = {
  /* Config defaults */
  "INSERT OR IGNORE INTO config (id, turnsperday, maxwarps_per_sector, startingcredits, startingfighters, startingholds, processinterval, autosave, max_ports, max_planets_per_sector, max_total_planets, max_citadel_level, number_of_planet_types, max_ship_name_length, ship_type_count, hash_length, default_nodes, buff_size, max_name_length, planet_type_count) "
    "VALUES (1, 120, 6, 1000, 10, 20, 1, 5, 200, 6, 300, 6, 8, 50, 8, 128, 500, 1024, 50, 8);",

  /* Shiptypes */
  "INSERT OR IGNORE INTO shiptypes (name, basecost, maxattack, initialholds, maxholds, maxfighters, turns, mines, genesis, twarp, transportrange, maxshields, offense, defense, beacons, holo, planet, photons, can_purchase) "
    "VALUES ('Merchant Cruiser', 41300, 750, 20, 75, 2500, 3, 50, 5, 0, 5, 400, 10, 10, 0, 1, 1, 0, 1);",

  "INSERT OR IGNORE INTO shiptypes (name, basecost, maxattack, initialholds, maxholds, maxfighters, turns, mines, genesis, twarp, transportrange, maxshields, offense, defense, beacons, holo, planet, photons, can_purchase) "
    "VALUES ('Scout Marauder', 15950, 250, 10, 25, 250, 2, 0, 0, 0, 0, 100, 20, 20, 0, 1, 1, 0, 1);",

  "INSERT OR IGNORE INTO shiptypes (name, basecost, maxattack, initialholds, maxholds, maxfighters, turns, mines, genesis, twarp, transportrange, maxshields, offense, defense, beacons, holo, planet, photons, can_purchase) "
    "VALUES ('Missile Frigate', 100000, 2000, 12, 60, 5000, 3, 5, 0, 0, 2, 400, 13, 13, 5, 0, 0, 1, 1);",

  "INSERT OR IGNORE INTO shiptypes (name, basecost, maxattack, initialholds, maxholds, maxfighters, turns, mines, genesis, twarp, transportrange, maxshields, offense, defense, beacons, holo, planet, photons, can_purchase) "
    "VALUES ('Battleship', 88500, 3000, 16, 80, 10000, 4, 25, 1, 0, 8, 750, 16, 16, 50, 1, 1, 0, 1);",

  "INSERT OR IGNORE INTO shiptypes (name, basecost, maxattack, initialholds, maxholds, maxfighters, turns, mines, genesis, twarp, transportrange, maxshields, offense, defense, beacons, holo, planet, photons, can_purchase) "
    "VALUES ('Corporate Flagship', 163500, 6000, 20, 85, 20000, 3, 100, 10, 1, 10, 1500, 12, 12, 100, 1, 1, 1, 1);",

  "INSERT OR IGNORE INTO shiptypes (name, basecost, maxattack, initialholds, maxholds, maxfighters, turns, mines, genesis, twarp, transportrange, maxshields, offense, defense, beacons, holo, planet, photons, can_purchase) "
    "VALUES ('Colonial Transport', 63600, 100, 50, 250, 200, 6, 0, 5, 0, 7, 500, 6, 6, 10, 0, 1, 0, 1);",

  "INSERT OR IGNORE INTO shiptypes (name, basecost, maxattack, initialholds, maxholds, maxfighters, turns, mines, genesis, twarp, transportrange, maxshields, offense, defense, beacons, holo, planet, photons, can_purchase) "
    "VALUES ('Cargo Transport', 51950, 125, 50, 125, 400, 4, 1, 2, 0, 5, 1000, 8, 8, 20, 1, 1, 0, 1);",

  "INSERT OR IGNORE INTO shiptypes (name, basecost, maxattack, initialholds, maxholds, maxfighters, turns, mines, genesis, twarp, transportrange, maxshields, offense, defense, beacons, holo, planet, photons, can_purchase) "
    "VALUES ('Merchant Freighter', 33400, 100, 30, 65, 300, 2, 2, 2, 0, 5, 500, 8, 8, 20, 1, 1, 0, 1);",

  "INSERT OR IGNORE INTO shiptypes (name, basecost, maxattack, initialholds, maxholds, maxfighters, turns, mines, genesis, twarp, transportrange, maxshields, offense, defense, beacons, holo, planet, photons, can_purchase) "
    "VALUES ('Imperial Starship', 329000, 10000, 40, 150, 50000, 4, 125, 10, 1, 15, 2000, 15, 15, 150, 1, 1, 1, 1);",

  "INSERT OR IGNORE INTO shiptypes (name, basecost, maxattack, initialholds, maxholds, maxfighters, turns, mines, genesis, twarp, transportrange, maxshields, offense, defense, beacons, holo, planet, photons, can_purchase) "
    "VALUES ('Havoc Gunstar', 79000, 1000, 12, 50, 10000, 3, 5, 1, 1, 6, 3000, 13, 13, 5, 1, 0, 0, 1);",

  "INSERT OR IGNORE INTO shiptypes (name, basecost, maxattack, initialholds, maxholds, maxfighters, turns, mines, genesis, twarp, transportrange, maxshields, offense, defense, beacons, holo, planet, photons, can_purchase) "
    "VALUES ('Constellation', 72500, 2000, 20, 80, 5000, 3, 25, 2, 0, 6, 750, 14, 14, 50, 1, 1, 0, 1);",

  "INSERT OR IGNORE INTO shiptypes (name, basecost, maxattack, initialholds, maxholds, maxfighters, turns, mines, genesis, twarp, transportrange, maxshields, offense, defense, beacons, holo, planet, photons, can_purchase) "
    "VALUES ('T''khasi Orion', 42500, 250, 30, 60, 750, 2, 5, 1, 0, 3, 750, 11, 11, 20, 1, 1, 0, 1);",

  "INSERT OR IGNORE INTO shiptypes (name, basecost, maxattack, initialholds, maxholds, maxfighters, turns, mines, genesis, twarp, transportrange, maxshields, offense, defense, beacons, holo, planet, photons, can_purchase) "
    "VALUES ('Tholian Sentinel', 47500, 800, 10, 50, 2500, 4, 50, 1, 0, 3, 4000, 1, 1, 10, 1, 0, 0, 1);",

  "INSERT OR IGNORE INTO shiptypes (name, basecost, maxattack, initialholds, maxholds, maxfighters, turns, mines, genesis, twarp, transportrange, maxshields, offense, defense, beacons, holo, planet, photons, can_purchase) "
    "VALUES ('Taurean Mule', 63600, 150, 50, 150, 300, 4, 0, 1, 0, 5, 600, 5, 5, 20, 1, 1, 0, 1);",

  "INSERT OR IGNORE INTO shiptypes (name, basecost, maxattack, initialholds, maxholds, maxfighters, turns, mines, genesis, twarp, transportrange, maxshields, offense, defense, beacons, holo, planet, photons, can_purchase) "
    "VALUES ('Interdictor Cruiser', 539000, 15000, 10, 40, 100000, 15, 200, 20, 0, 20, 4000, 12, 12, 100, 1, 1, 0, 1);",
  "INSERT OR IGNORE INTO shiptypes (name, basecost, maxattack, initialholds, maxholds, maxfighters, turns, mines, genesis, twarp, transportrange, maxshields, offense, defense, beacons, holo, planet, photons, can_purchase) "
    "VALUES ('Ferrengi Warship', 150000, 5000, 20, 100, 15000, 5, 20, 5, 0, 10, 5000, 15, 15, 50, 1, 1, 1, 0);",
  "INSERT OR IGNORE INTO shiptypes (name, basecost, maxattack, initialholds, maxholds, maxfighters, turns, mines, genesis, twarp, transportrange, maxshields, offense, defense, beacons, holo, planet, photons, can_purchase) "
    "VALUES ('Imperial Starship (NPC)', 329000, 10000, 40, 150, 50000, 4, 125, 10, 1, 15, 2000, 15, 15, 150, 1, 1, 1, 0);",

  /* ---------- PORTS ---------- */
  "INSERT OR IGNORE INTO ports (id, number, name, location, size, techlevel, max_ore, max_organics, max_equipment, product_ore, product_organics, product_equipment, credits, invisible) "
    "VALUES (1, 1, 'Port Type 1 (BBS)', 1, 5, 3, 10000, 10000, 10000, 5000, 5000, 5000, 500000, 0);",

  "INSERT OR IGNORE INTO ports (id, number, name, location, size, techlevel, max_ore, max_organics, max_equipment, product_ore, product_organics, product_equipment, credits, invisible) "
    "VALUES (2, 2, 'Port Type 2 (BSB)', 2, 5, 3, 10000, 10000, 10000, 5000, 5000, 5000, 500000, 0);",

  "INSERT OR IGNORE INTO ports (id, number, name, location, size, techlevel, max_ore, max_organics, max_equipment, product_ore, product_organics, product_equipment, credits, invisible) "
    "VALUES (3, 3, 'Port Type 3 (BSS)', 3, 5, 3, 10000, 10000, 10000, 5000, 5000, 5000, 500000, 0);",

  "INSERT OR IGNORE INTO ports (id, number, name, location, size, techlevel, max_ore, max_organics, max_equipment, product_ore, product_organics, product_equipment, credits, invisible) "
    "VALUES (4, 4, 'Port Type 4 (SBB)', 4, 5, 3, 10000, 10000, 10000, 5000, 5000, 5000, 500000, 0);",

  "INSERT OR IGNORE INTO ports (id, number, name, location, size, techlevel, max_ore, max_organics, max_equipment, product_ore, product_organics, product_equipment, credits, invisible) "
    "VALUES (5, 5, 'Port Type 5 (SBS)', 5, 5, 3, 10000, 10000, 10000, 5000, 5000, 5000, 500000, 0);",

  "INSERT OR IGNORE INTO ports (id, number, name, location, size, techlevel, max_ore, max_organics, max_equipment, product_ore, product_organics, product_equipment, credits, invisible) "
    "VALUES (6, 6, 'Port Type 6 (SSB)', 6, 5, 3, 10000, 10000, 10000, 5000, 5000, 5000, 500000, 0);",

  "INSERT OR IGNORE INTO ports (id, number, name, location, size, techlevel, max_ore, max_organics, max_equipment, product_ore, product_organics, product_equipment, credits, invisible) "
    "VALUES (7, 7, 'Port Type 7 (SSS)', 7, 5, 3, 10000, 10000, 10000, 5000, 5000, 5000, 500000, 0);",

  "INSERT OR IGNORE INTO ports (id, number, name, location, size, techlevel, max_ore, max_organics, max_equipment, product_ore, product_organics, product_equipment, credits, invisible) "
    "VALUES (8, 8, 'Port Type 8 (BBB)', 8, 5, 3, 10000, 10000, 10000, 5000, 5000, 5000, 500000, 0);",

  "INSERT OR IGNORE INTO ports (id, number, name, location, size, techlevel, max_ore, max_organics, max_equipment, product_ore, product_organics, product_equipment, credits, invisible) "
    "VALUES (9, 9, 'Port Type 9 (Stardock)', 9, 10, 5, 20000, 20000, 20000, 10000, 10000, 10000, 1000000, 0);",

  /* ---------- TRADE RULES ---------- */
  /* Type 1: BBS (buys ore, buys organics, sells equipment) */
  "INSERT OR IGNORE INTO port_trade (port_id, commodity, mode) VALUES (1, 'ore', 'buy');",
  "INSERT OR IGNORE INTO port_trade (port_id, commodity, mode) VALUES (1, 'organics', 'buy');",
  "INSERT OR IGNORE INTO port_trade (port_id, commodity, mode) VALUES (1, 'equipment', 'sell');",

  /* Type 2: BSB (buys ore, sells organics, buys equipment) */
  "INSERT OR IGNORE INTO port_trade (port_id, commodity, mode) VALUES (2, 'ore', 'buy');",
  "INSERT OR IGNORE INTO port_trade (port_id, commodity, mode) VALUES (2, 'organics', 'sell');",
  "INSERT OR IGNORE INTO port_trade (port_id, commodity, mode) VALUES (2, 'equipment', 'buy');",

  /* Type 3: BSS (buys ore, sells organics, sells equipment) */
  "INSERT OR IGNORE INTO port_trade (port_id, commodity, mode) VALUES (3, 'ore', 'buy');",
  "INSERT OR IGNORE INTO port_trade (port_id, commodity, mode) VALUES (3, 'organics', 'sell');",
  "INSERT OR IGNORE INTO port_trade (port_id, commodity, mode) VALUES (3, 'equipment', 'sell');",

  /* Type 4: SBB (sells ore, buys organics, buys equipment) */
  "INSERT OR IGNORE INTO port_trade (port_id, commodity, mode) VALUES (4, 'ore', 'sell');",
  "INSERT OR IGNORE INTO port_trade (port_id, commodity, mode) VALUES (4, 'organics', 'buy');",
  "INSERT OR IGNORE INTO port_trade (port_id, commodity, mode) VALUES (4, 'equipment', 'buy');",

  /* Type 5: SBS (sells ore, buys organics, sells equipment) */
  "INSERT OR IGNORE INTO port_trade (port_id, commodity, mode) VALUES (5, 'ore', 'sell');",
  "INSERT OR IGNORE INTO port_trade (port_id, commodity, mode) VALUES (5, 'organics', 'buy');",
  "INSERT OR IGNORE INTO port_trade (port_id, commodity, mode) VALUES (5, 'equipment', 'sell');",

  /* Type 6: SSB (sells ore, sells organics, buys equipment) */
  "INSERT OR IGNORE INTO port_trade (port_id, commodity, mode) VALUES (6, 'ore', 'sell');",
  "INSERT OR IGNORE INTO port_trade (port_id, commodity, mode) VALUES (6, 'organics', 'sell');",
  "INSERT OR IGNORE INTO port_trade (port_id, commodity, mode) VALUES (6, 'equipment', 'buy');",

  /* Type 7: SSS (sells all) */
  "INSERT OR IGNORE INTO port_trade (port_id, commodity, mode) VALUES (7, 'ore', 'sell');",
  "INSERT OR IGNORE INTO port_trade (port_id, commodity, mode) VALUES (7, 'organics', 'sell');",
  "INSERT OR IGNORE INTO port_trade (port_id, commodity, mode) VALUES (7, 'equipment', 'sell');",

  /* Type 8: BBB (buys all) */
  "INSERT OR IGNORE INTO port_trade (port_id, commodity, mode) VALUES (8, 'ore', 'buy');",
  "INSERT OR IGNORE INTO port_trade (port_id, commodity, mode) VALUES (8, 'organics', 'buy');",
  "INSERT OR IGNORE INTO port_trade (port_id, commodity, mode) VALUES (8, 'equipment', 'buy');",

  /* Type 9: Stardock (BBB + upgrades/shipyard) */
  "INSERT OR IGNORE INTO port_trade (port_id, commodity, mode) VALUES (9, 'ore', 'buy');",
  "INSERT OR IGNORE INTO port_trade (port_id, commodity, mode) VALUES (9, 'organics', 'buy');",
  "INSERT OR IGNORE INTO port_trade (port_id, commodity, mode) VALUES (9, 'equipment', 'buy');",

  "INSERT OR IGNORE INTO planettypes (code, typeDescription, typeName, "
    "citadelUpgradeTime_lvl1, citadelUpgradeTime_lvl2, citadelUpgradeTime_lvl3, citadelUpgradeTime_lvl4, citadelUpgradeTime_lvl5, citadelUpgradeTime_lvl6, "
    "citadelUpgradeOre_lvl1, citadelUpgradeOre_lvl2, citadelUpgradeOre_lvl3, citadelUpgradeOre_lvl4, citadelUpgradeOre_lvl5, citadelUpgradeOre_lvl6, "
    "citadelUpgradeOrganics_lvl1, citadelUpgradeOrganics_lvl2, citadelUpgradeOrganics_lvl3, citadelUpgradeOrganics_lvl4, citadelUpgradeOrganics_lvl5, citadelUpgradeOrganics_lvl6, "
    "citadelUpgradeEquipment_lvl1, citadelUpgradeEquipment_lvl2, citadelUpgradeEquipment_lvl3, citadelUpgradeEquipment_lvl4, citadelUpgradeEquipment_lvl5, citadelUpgradeEquipment_lvl6, "
    "citadelUpgradeColonist_lvl1, citadelUpgradeColonist_lvl2, citadelUpgradeColonist_lvl3, citadelUpgradeColonist_lvl4, citadelUpgradeColonist_lvl5, citadelUpgradeColonist_lvl6, "
    "maxColonist_ore, maxColonist_organics, maxColonist_equipment, "
    "fighters, fuelProduction, organicsProduction, equipmentProduction, fighterProduction, "
    "maxore, maxorganics, maxequipment, maxfighters, breeding) "
    "VALUES ('M','Earth type','Earth',"
    "4,4,5,10,5,15,"
    "300,200,500,1000,300,1000,"
    "200,50,250,1200,400,1200,"
    "250,250,500,1000,1000,2000,"
    "1000000,2000000,4000000,6000000,6000000,6000000,"
    "100000,100000,100000,"
    "0,0,0,0,0," "10000000,100000,100000,1000000,0.75);",

  /* Mountainous (L) */
  "INSERT OR IGNORE INTO planettypes (code, typeDescription, typeName, "
    "citadelUpgradeTime_lvl1, citadelUpgradeTime_lvl2, citadelUpgradeTime_lvl3, citadelUpgradeTime_lvl4, citadelUpgradeTime_lvl5, citadelUpgradeTime_lvl6, "
    "citadelUpgradeOre_lvl1, citadelUpgradeOre_lvl2, citadelUpgradeOre_lvl3, citadelUpgradeOre_lvl4, citadelUpgradeOre_lvl5, citadelUpgradeOre_lvl6, "
    "citadelUpgradeOrganics_lvl1, citadelUpgradeOrganics_lvl2, citadelUpgradeOrganics_lvl3, citadelUpgradeOrganics_lvl4, citadelUpgradeOrganics_lvl5, citadelUpgradeOrganics_lvl6, "
    "citadelUpgradeEquipment_lvl1, citadelUpgradeEquipment_lvl2, citadelUpgradeEquipment_lvl3, citadelUpgradeEquipment_lvl4, citadelUpgradeEquipment_lvl5, citadelUpgradeEquipment_lvl6, "
    "citadelUpgradeColonist_lvl1, citadelUpgradeColonist_lvl2, citadelUpgradeColonist_lvl3, citadelUpgradeColonist_lvl4, citadelUpgradeColonist_lvl5, citadelUpgradeColonist_lvl6, "
    "maxColonist_ore, maxColonist_organics, maxColonist_equipment, "
    "fighters, fuelProduction, organicsProduction, equipmentProduction, fighterProduction, "
    "maxore, maxorganics, maxequipment, maxfighters, breeding) "
    "VALUES ('L','Mountainous','Mountain',"
    "2,5,5,8,5,12,"
    "150,200,600,1000,300,1000,"
    "100,50,250,1200,400,1200,"
    "150,250,700,1000,1000,2000,"
    "400000,1400000,3600000,5600000,7000000,5600000,"
    "200000,200000,200000,"
    "0,0,0,0,0," "200000,200000,100000,1000000,0.24);",

  /* Oceanic (O) */
  "INSERT OR IGNORE INTO planettypes (code, typeDescription, typeName, "
    "citadelUpgradeTime_lvl1, citadelUpgradeTime_lvl2, citadelUpgradeTime_lvl3, citadelUpgradeTime_lvl4, citadelUpgradeTime_lvl5, citadelUpgradeTime_lvl6, "
    "citadelUpgradeOre_lvl1, citadelUpgradeOre_lvl2, citadelUpgradeOre_lvl3, citadelUpgradeOre_lvl4, citadelUpgradeOre_lvl5, citadelUpgradeOre_lvl6, "
    "citadelUpgradeOrganics_lvl1, citadelUpgradeOrganics_lvl2, citadelUpgradeOrganics_lvl3, citadelUpgradeOrganics_lvl4, citadelUpgradeOrganics_lvl5, citadelUpgradeOrganics_lvl6, "
    "citadelUpgradeEquipment_lvl1, citadelUpgradeEquipment_lvl2, citadelUpgradeEquipment_lvl3, citadelUpgradeEquipment_lvl4, citadelUpgradeEquipment_lvl5, citadelUpgradeEquipment_lvl6, "
    "citadelUpgradeColonist_lvl1, citadelUpgradeColonist_lvl2, citadelUpgradeColonist_lvl3, citadelUpgradeColonist_lvl4, citadelUpgradeColonist_lvl5, citadelUpgradeColonist_lvl6, "
    "maxColonist_ore, maxColonist_organics, maxColonist_equipment, "
    "fighters, fuelProduction, organicsProduction, equipmentProduction, fighterProduction, "
    "maxore, maxorganics, maxequipment, maxfighters, breeding) "
    "VALUES ('O','Oceanic','Ocean',"
    "6,5,8,5,4,8,"
    "500,200,600,700,300,700,"
    "200,50,400,900,400,900,"
    "400,300,650,800,1000,1600,"
    "1400000,2400000,4400000,7000000,8000000,7000000,"
    "100000,1000000,1000000,"
    "0,0,0,0,0," "50000,1000000,50000,1000000,0.30);",

  /* Desert Wasteland (K) */
  "INSERT OR IGNORE INTO planettypes (code, typeDescription, typeName, "
    "citadelUpgradeTime_lvl1, citadelUpgradeTime_lvl2, citadelUpgradeTime_lvl3, citadelUpgradeTime_lvl4, citadelUpgradeTime_lvl5, citadelUpgradeTime_lvl6, "
    "citadelUpgradeOre_lvl1, citadelUpgradeOre_lvl2, citadelUpgradeOre_lvl3, citadelUpgradeOre_lvl4, citadelUpgradeOre_lvl5, citadelUpgradeOre_lvl6, "
    "citadelUpgradeOrganics_lvl1, citadelUpgradeOrganics_lvl2, citadelUpgradeOrganics_lvl3, citadelUpgradeOrganics_lvl4, citadelUpgradeOrganics_lvl5, citadelUpgradeOrganics_lvl6, "
    "citadelUpgradeEquipment_lvl1, citadelUpgradeEquipment_lvl2, citadelUpgradeEquipment_lvl3, citadelUpgradeEquipment_lvl4, citadelUpgradeEquipment_lvl5, citadelUpgradeEquipment_lvl6, "
    "citadelUpgradeColonist_lvl1, citadelUpgradeColonist_lvl2, citadelUpgradeColonist_lvl3, citadelUpgradeColonist_lvl4, citadelUpgradeColonist_lvl5, citadelUpgradeColonist_lvl6, "
    "maxColonist_ore, maxColonist_organics, maxColonist_equipment, "
    "fighters, fuelProduction, organicsProduction, equipmentProduction, fighterProduction, "
    "maxore, maxorganics, maxequipment, maxfighters, breeding) "
    "VALUES ('K','Desert Wasteland','Desert',"
    "6,5,8,5,4,8,"
    "400,300,700,700,300,700,"
    "300,80,900,900,400,900,"
    "600,400,800,800,1000,1600,"
    "1000000,2400000,4000000,7000000,8000000,7000000,"
    "200000,50000,50000," "0,0,0,0,0," "200000,50000,10000,1000000,0.50);",

  /* Volcanic (H) */
  "INSERT OR IGNORE INTO planettypes (code, typeDescription, typeName, "
    "citadelUpgradeTime_lvl1, citadelUpgradeTime_lvl2, citadelUpgradeTime_lvl3, citadelUpgradeTime_lvl4, citadelUpgradeTime_lvl5, citadelUpgradeTime_lvl6, "
    "citadelUpgradeOre_lvl1, citadelUpgradeOre_lvl2, citadelUpgradeOre_lvl3, citadelUpgradeOre_lvl4, citadelUpgradeOre_lvl5, citadelUpgradeOre_lvl6, "
    "citadelUpgradeOrganics_lvl1, citadelUpgradeOrganics_lvl2, citadelUpgradeOrganics_lvl3, citadelUpgradeOrganics_lvl4, citadelUpgradeOrganics_lvl5, citadelUpgradeOrganics_lvl6, "
    "citadelUpgradeEquipment_lvl1, citadelUpgradeEquipment_lvl2, citadelUpgradeEquipment_lvl3, citadelUpgradeEquipment_lvl4, citadelUpgradeEquipment_lvl5, citadelUpgradeEquipment_lvl6, "
    "citadelUpgradeColonist_lvl1, citadelUpgradeColonist_lvl2, citadelUpgradeColonist_lvl3, citadelUpgradeColonist_lvl4, citadelUpgradeColonist_lvl5, citadelUpgradeColonist_lvl6, "
    "maxColonist_ore, maxColonist_organics, maxColonist_equipment, "
    "fighters, fuelProduction, organicsProduction, equipmentProduction, fighterProduction, "
    "maxore, maxorganics, maxequipment, maxfighters, breeding) "
    "VALUES ('H','Volcanic','Volcano',"
    "4,5,8,12,18,8,"
    "500,300,1200,2000,3000,2000,"
    "300,100,400,2000,1200,2000,"
    "600,400,1500,2500,2000,5000,"
    "800000,1600000,4400000,7000000,10000000,7000000,"
    "1000000,10000,10000," "0,0,0,0,0," "1000000,10000,100000,1000000,0.30);",

  /* Gaseous (U) */
  "INSERT OR IGNORE INTO planettypes (code, typeDescription, typeName, "
    "citadelUpgradeTime_lvl1, citadelUpgradeTime_lvl2, citadelUpgradeTime_lvl3, citadelUpgradeTime_lvl4, citadelUpgradeTime_lvl5, citadelUpgradeTime_lvl6, "
    "citadelUpgradeOre_lvl1, citadelUpgradeOre_lvl2, citadelUpgradeOre_lvl3, citadelUpgradeOre_lvl4, citadelUpgradeOre_lvl5, citadelUpgradeOre_lvl6, "
    "citadelUpgradeOrganics_lvl1, citadelUpgradeOrganics_lvl2, citadelUpgradeOrganics_lvl3, citadelUpgradeOrganics_lvl4, citadelUpgradeOrganics_lvl5, citadelUpgradeOrganics_lvl6, "
    "citadelUpgradeEquipment_lvl1, citadelUpgradeEquipment_lvl2, citadelUpgradeEquipment_lvl3, citadelUpgradeEquipment_lvl4, citadelUpgradeEquipment_lvl5, citadelUpgradeEquipment_lvl6, "
    "citadelUpgradeColonist_lvl1, citadelUpgradeColonist_lvl2, citadelUpgradeColonist_lvl3, citadelUpgradeColonist_lvl4, citadelUpgradeColonist_lvl5, citadelUpgradeColonist_lvl6, "
    "maxColonist_ore, maxColonist_organics, maxColonist_equipment, "
    "fighters, fuelProduction, organicsProduction, equipmentProduction, fighterProduction, "
    "maxore, maxorganics, maxequipment, maxfighters, breeding) "
    "VALUES ('U','Gaseous','Gas Giant',"
    "8,4,5,5,4,8,"
    "1200,300,500,500,200,500,"
    "400,100,500,200,200,200,"
    "2500,400,2000,600,600,1200,"
    "3000000,3000000,8000000,6000000,8000000,6000000,"
    "10000,10000,10000," "0,0,0,0,0," "10000,10000,10000,1000000,-0.10);",

  /* Glacial/Ice (C) */
  "INSERT OR IGNORE INTO planettypes (code, typeDescription, typeName, "
    "citadelUpgradeTime_lvl1, citadelUpgradeTime_lvl2, citadelUpgradeTime_lvl3, citadelUpgradeTime_lvl4, citadelUpgradeTime_lvl5, citadelUpgradeTime_lvl6, "
    "citadelUpgradeOre_lvl1, citadelUpgradeOre_lvl2, citadelUpgradeOre_lvl3, citadelUpgradeOre_lvl4, citadelUpgradeOre_lvl5, citadelUpgradeOre_lvl6, "
    "citadelUpgradeOrganics_lvl1, citadelUpgradeOrganics_lvl2, citadelUpgradeOrganics_lvl3, citadelUpgradeOrganics_lvl4, citadelUpgradeOrganics_lvl5, citadelUpgradeOrganics_lvl6, "
    "citadelUpgradeEquipment_lvl1, citadelUpgradeEquipment_lvl2, citadelUpgradeEquipment_lvl3, citadelUpgradeEquipment_lvl4, citadelUpgradeEquipment_lvl5, citadelUpgradeEquipment_lvl6, "
    "citadelUpgradeColonist_lvl1, citadelUpgradeColonist_lvl2, citadelUpgradeColonist_lvl3, citadelUpgradeColonist_lvl4, citadelUpgradeColonist_lvl5, citadelUpgradeColonist_lvl6, "
    "maxColonist_ore, maxColonist_organics, maxColonist_equipment, "
    "fighters, fuelProduction, organicsProduction, equipmentProduction, fighterProduction, "
    "maxore, maxorganics, maxequipment, maxfighters, breeding) "
    "VALUES ('C','Glacial/Ice','Ice World',"
    "5,5,7,5,4,8,"
    "400,300,600,700,300,700,"
    "300,80,400,900,400,900,"
    "600,400,650,700,1000,1400,"
    "1000000,24000000,4400000,6600000,9000000,6600000,"
    "20000,50000,20000," "0,0,0,0,0," "20000,50000,10000,1000000,-0.10);",

  /* Earth planet in sector 1 */
  "INSERT OR IGNORE INTO planets (num, sector, name, owner, population, minerals, ore, energy, type, creator, fuelColonist, organicsColonist, equipmentColonist, fuel, organics, equipment, fighters) "
    "VALUES (1,1,'Earth',0,8000000000,100000,100000,100000,1,'System',0,0,0,0,0,0,0);",

  /* Fedspace sectors 1–10 */
  "INSERT OR IGNORE INTO sectors (id, name, beacon, nebulae) VALUES (1, 'Fedspace 1', 'The Federation -- Do Not Dump!', 'The Federation');",
  "INSERT OR IGNORE INTO sectors (id, name, beacon, nebulae) VALUES (2, 'Fedspace 2', 'The Federation -- Do Not Dump!', 'The Federation');",
  "INSERT OR IGNORE INTO sectors (id, name, beacon, nebulae) VALUES (3, 'Fedspace 3', 'The Federation -- Do Not Dump!', 'The Federation');",
  "INSERT OR IGNORE INTO sectors (id, name, beacon, nebulae) VALUES (4, 'Fedspace 4', 'The Federation -- Do Not Dump!', 'The Federation');",
  "INSERT OR IGNORE INTO sectors (id, name, beacon, nebulae) VALUES (5, 'Fedspace 5', 'The Federation -- Do Not Dump!', 'The Federation');",
  "INSERT OR IGNORE INTO sectors (id, name, beacon, nebulae) VALUES (6, 'Fedspace 6', 'The Federation -- Do Not Dump!', 'The Federation');",
  "INSERT OR IGNORE INTO sectors (id, name, beacon, nebulae) VALUES (7, 'Fedspace 7', 'The Federation -- Do Not Dump!', 'The Federation');",
  "INSERT OR IGNORE INTO sectors (id, name, beacon, nebulae) VALUES (8, 'Fedspace 8', 'The Federation -- Do Not Dump!', 'The Federation');",
  "INSERT OR IGNORE INTO sectors (id, name, beacon, nebulae) VALUES (9, 'Fedspace 9', 'The Federation -- Do Not Dump!', 'The Federation');",
  "INSERT OR IGNORE INTO sectors (id, name, beacon, nebulae) VALUES (10, 'Fedspace 10','The Federation -- Do Not Dump!', 'The Federation');",

  /* Fedspace warps (hard-coded) */
  "INSERT OR IGNORE INTO sector_warps (from_sector, to_sector) VALUES (1,2);",
  "INSERT OR IGNORE INTO sector_warps (from_sector, to_sector) VALUES (1,3);",
  "INSERT OR IGNORE INTO sector_warps (from_sector, to_sector) VALUES (1,4);",
  "INSERT OR IGNORE INTO sector_warps (from_sector, to_sector) VALUES (1,5);",
  "INSERT OR IGNORE INTO sector_warps (from_sector, to_sector) VALUES (1,6);",
  "INSERT OR IGNORE INTO sector_warps (from_sector, to_sector) VALUES (1,7);",
  "INSERT OR IGNORE INTO sector_warps (from_sector, to_sector) VALUES (2,1);",
  "INSERT OR IGNORE INTO sector_warps (from_sector, to_sector) VALUES (2,3);",
  "INSERT OR IGNORE INTO sector_warps (from_sector, to_sector) VALUES (2,7);",
  "INSERT OR IGNORE INTO sector_warps (from_sector, to_sector) VALUES (2,8);",
  "INSERT OR IGNORE INTO sector_warps (from_sector, to_sector) VALUES (2,9);",
  "INSERT OR IGNORE INTO sector_warps (from_sector, to_sector) VALUES (2,10);",
  "INSERT OR IGNORE INTO sector_warps (from_sector, to_sector) VALUES (3,1);",
  "INSERT OR IGNORE INTO sector_warps (from_sector, to_sector) VALUES (3,2);",
  "INSERT OR IGNORE INTO sector_warps (from_sector, to_sector) VALUES (3,4);",
  "INSERT OR IGNORE INTO sector_warps (from_sector, to_sector) VALUES (4,1);",
  "INSERT OR IGNORE INTO sector_warps (from_sector, to_sector) VALUES (4,3);",
  "INSERT OR IGNORE INTO sector_warps (from_sector, to_sector) VALUES (4,5);",
  "INSERT OR IGNORE INTO sector_warps (from_sector, to_sector) VALUES (5,1);",
  "INSERT OR IGNORE INTO sector_warps (from_sector, to_sector) VALUES (5,4);",
  "INSERT OR IGNORE INTO sector_warps (from_sector, to_sector) VALUES (5,6);",
  "INSERT OR IGNORE INTO sector_warps (from_sector, to_sector) VALUES (6,1);",
  "INSERT OR IGNORE INTO sector_warps (from_sector, to_sector) VALUES (6,5);",
  "INSERT OR IGNORE INTO sector_warps (from_sector, to_sector) VALUES (6,7);",
  "INSERT OR IGNORE INTO sector_warps (from_sector, to_sector) VALUES (7,1);",
  "INSERT OR IGNORE INTO sector_warps (from_sector, to_sector) VALUES (7,2);",
  "INSERT OR IGNORE INTO sector_warps (from_sector, to_sector) VALUES (7,6);",
  "INSERT OR IGNORE INTO sector_warps (from_sector, to_sector) VALUES (7,8);",
  "INSERT OR IGNORE INTO sector_warps (from_sector, to_sector) VALUES (8,2);",
  "INSERT OR IGNORE INTO sector_warps (from_sector, to_sector) VALUES (8,7);",
  "INSERT OR IGNORE INTO sector_warps (from_sector, to_sector) VALUES (9,2);",
  "INSERT OR IGNORE INTO sector_warps (from_sector, to_sector) VALUES (9,10);",
  "INSERT OR IGNORE INTO sector_warps (from_sector, to_sector) VALUES (10,2);",
  "INSERT OR IGNORE INTO sector_warps (from_sector, to_sector) VALUES (10,9);"
};

/* Number of tables */
static const size_t create_table_count =
  sizeof (create_table_sql) / sizeof (create_table_sql[0]);
/* Number of default inserts */
static const size_t insert_default_count =
  sizeof (insert_default_sql) / sizeof (insert_default_sql[0]);

sqlite3 *
db_get_handle (void)
{
  return db_handle;
}


static int
urandom_bytes (void *buf, size_t n)
{
  int fd = open ("/dev/urandom", O_RDONLY);
  if (fd < 0)
    return -1;
  ssize_t rd = read (fd, buf, n);
  close (fd);
  return (rd == (ssize_t) n) ? 0 : -1;
}

static void
to_hex (const unsigned char *in, size_t n, char out_hex[ /*2n+1 */ ])
{
  static const char hexd[] = "0123456789abcdef";
  for (size_t i = 0; i < n; ++i)
    {
      out_hex[2 * i + 0] = hexd[(in[i] >> 4) & 0xF];
      out_hex[2 * i + 1] = hexd[(in[i]) & 0xF];
    }
  out_hex[2 * n] = '\0';
}

static int
gen_session_token (char out64[65])
{
  unsigned char rnd[32];
  if (urandom_bytes (rnd, sizeof (rnd)) != 0)
    return -1;
  to_hex (rnd, sizeof (rnd), out64);
  return 0;
}


int
db_init (void)
{
  /* Step 1: open or create DB file */
  if (sqlite3_open (DEFAULT_DB_NAME, &db_handle) != SQLITE_OK)
    {
      fprintf (stderr, "DB init error: %s\n", sqlite3_errmsg (db_handle));
      return -1;
    }

  (void) db_ensure_auth_schema ();
  (void) db_ensure_idempotency_schema ();

  /* Step 2: check if config table exists */
  const char *sql =
    "SELECT name FROM sqlite_master WHERE type='table' AND name='config';";
  sqlite3_stmt *stmt = NULL;
  int rc = sqlite3_prepare_v2 (db_handle, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK)
    {
      fprintf (stderr, "DB check error: %s\n", sqlite3_errmsg (db_handle));
      sqlite3_close (db_handle);
      db_handle = NULL;
      return -1;
    }

  rc = sqlite3_step (stmt);
  int table_exists = (rc == SQLITE_ROW);	/* row means table found */
  sqlite3_finalize (stmt);

  /* Step 3: if no config table, create schema + defaults */
  if (!table_exists)
    {
      fprintf (stderr,
	       "No schema detected – creating tables and inserting defaults...\n");

      if (db_create_tables () != 0)
	{
	  fprintf (stderr, "Failed to create tables\n");
	  return -1;
	}

      if (db_insert_defaults () != 0)
	{
	  fprintf (stderr, "Failed to insert default data\n");
	  return -1;
	}
    }

  return 0;
}

int
db_create_tables (void)
{
  char *errmsg = NULL;

  for (size_t i = 0; i < create_table_count; i++)
    {
      if (sqlite3_exec (db_handle, create_table_sql[i], NULL, NULL, &errmsg)
	  != SQLITE_OK)
	{
	  fprintf (stderr, "DB create_tables error (%zu): %s\n", i, errmsg);
	  sqlite3_free (errmsg);
	  return -1;
	}
    }

  return 0;
}



int
db_insert_defaults (void)
{
  char *errmsg = NULL;

  for (size_t i = 0; i < insert_default_count; i++)
    {
      if (sqlite3_exec (db_handle, insert_default_sql[i], NULL, NULL, &errmsg)
	  != SQLITE_OK)
	{
	  fprintf (stderr, "DB insert_defaults error (%zu): %s\n", i, errmsg);
	  sqlite3_free (errmsg);
	  return -1;
	}
    }

  return 0;
}



/* Close database */
void
db_close (void)
{
  if (db_handle)
    {
      sqlite3_close (db_handle);
      db_handle = NULL;
    }
}

////////////////////////////////////////////

/* Create row in table from JSON */
int
db_create (const char *table, json_t *row)
{
  /* TODO: Build INSERT SQL dynamically based on JSON keys/values */
  fprintf (stderr, "db_create(%s, row) called (not implemented)\n", table);
  return 0;
}

/* Read row by id into JSON */
json_t *
db_read (const char *table, int id)
{
  /* TODO: Prepare SELECT ... WHERE id=? and return json_t * */
  fprintf (stderr, "db_read(%s, %d) called (not implemented)\n", table, id);
  return NULL;
}

/* Update row by id with new JSON */
int
db_update (const char *table, int id, json_t *row)
{
  /* TODO: Build UPDATE SQL dynamically */
  fprintf (stderr, "db_update(%s, %d, row) called (not implemented)\n", table,
	   id);
  return 0;
}

/* Delete row by id */
int
db_delete (const char *table, int id)
{
  /* TODO: Prepare DELETE ... WHERE id=? */
  fprintf (stderr, "db_delete(%s, %d) called (not implemented)\n", table, id);
  return 0;
}

/* Helper: safe text access (returns "" if NULL) */
static const char *
col_text_or_empty (sqlite3_stmt *st, int col)
{
  const unsigned char *t = sqlite3_column_text (st, col);
  return t ? (const char *) t : "";
}

/* Returns:
 *   SQLITE_OK        -> *out is a JSON object with player info
 *   SQLITE_NOTFOUND  -> no such player_id (out == NULL)
 *   other sqlite code -> error (out == NULL)
 */
int
db_player_info_json (int player_id, json_t **out)
{
  if (!out)
    return SQLITE_MISUSE;
  *out = NULL;

  sqlite3 *db = db_get_handle ();
  if (!db)
    return SQLITE_ERROR;

  /* Must match the columns in your player_info_v1 view */
  static const char *SQL =
    "SELECT player_id, player_name, player_number, "
    "       sector_id, sector_name, credits, alignment, experience, "
    "       ship_number, ship_id, ship_name, ship_type_id, ship_type_name, "
    "       ship_holds, ship_fighters, approx_worth "
    "FROM player_info_v1 WHERE player_id=?1;";

  sqlite3_stmt *st = NULL;
  int rc = sqlite3_prepare_v2 (db, SQL, -1, &st, NULL);
  if (rc != SQLITE_OK)
    return rc;

  sqlite3_bind_int (st, 1, player_id);

  rc = sqlite3_step (st);
  if (rc == SQLITE_ROW)
    {
      json_t *j =
	json_pack
	("{s:i, s:s, s:i, s:i, s:s, s:i, s:i, s:i, s:i, s:i, s:s, s:i, s:s, s:i, s:i, s:i}",
	 "player_id", sqlite3_column_int (st, 0),
	 "player_name", col_text_or_empty (st, 1),
	 "player_number", sqlite3_column_int (st, 2),

	 "sector_id", sqlite3_column_int (st, 3),
	 "sector_name", col_text_or_empty (st, 4),

	 "credits", sqlite3_column_int (st, 5),
	 "alignment", sqlite3_column_int (st, 6),
	 "experience", sqlite3_column_int (st, 7),

	 "ship_number", sqlite3_column_int (st, 8),
	 "ship_id", sqlite3_column_int (st, 9),
	 "ship_name", col_text_or_empty (st, 10),
	 "ship_type_id", sqlite3_column_int (st, 11),
	 "ship_type_name", col_text_or_empty (st, 12),

	 "ship_holds", sqlite3_column_int (st, 13),
	 "ship_fighters", sqlite3_column_int (st, 14),

	 "approx_worth", sqlite3_column_int (st, 15));
      sqlite3_finalize (st);
      *out = j;
      return SQLITE_OK;
    }

  sqlite3_finalize (st);
  return (rc == SQLITE_DONE) ? SQLITE_NOTFOUND : rc;
}


int
db_ensure_auth_schema (void)
{
  sqlite3 *db = db_get_handle ();
  if (!db)
    return SQLITE_ERROR;

  char *errmsg = NULL;
  int rc = sqlite3_exec (db, "BEGIN IMMEDIATE;", NULL, NULL, &errmsg);
  if (rc != SQLITE_OK)
    goto fail;

  /* Table */
  rc = sqlite3_exec (db, "CREATE TABLE IF NOT EXISTS sessions (" "  token       TEXT PRIMARY KEY," "  player_id   INTEGER NOT NULL," "  expires     INTEGER NOT NULL,"	/* epoch seconds */
		     "  created_at  INTEGER NOT NULL"	/* epoch seconds */
		     ");", NULL, NULL, &errmsg);
  if (rc != SQLITE_OK)
    goto rollback;

  /* Indexes */
  rc = sqlite3_exec (db,
		     "CREATE INDEX IF NOT EXISTS idx_sessions_player  ON sessions(player_id);",
		     NULL, NULL, &errmsg);
  if (rc != SQLITE_OK)
    goto rollback;

  rc = sqlite3_exec (db,
		     "CREATE INDEX IF NOT EXISTS idx_sessions_expires ON sessions(expires);",
		     NULL, NULL, &errmsg);
  if (rc != SQLITE_OK)
    goto rollback;

  rc = sqlite3_exec (db, "COMMIT;", NULL, NULL, &errmsg);
  if (rc != SQLITE_OK)
    goto fail;

  return SQLITE_OK;

rollback:
  sqlite3_exec (db, "ROLLBACK;", NULL, NULL, NULL);
fail:
  if (errmsg)
    {
      fprintf (stderr, "[DB] auth schema: %s\n", errmsg);
      sqlite3_free (errmsg);
    }
  return rc;
}



/* Create */
int
db_session_create (int player_id, int ttl_seconds, char token_out[65])
{
  if (!token_out || player_id <= 0 || ttl_seconds <= 0)
    return SQLITE_MISUSE;
  if (gen_session_token (token_out) != 0)
    return SQLITE_ERROR;

  sqlite3 *db = db_get_handle ();
  if (!db)
    return SQLITE_ERROR;

  time_t now = time (NULL);
  long long exp = (long long) now + ttl_seconds;

  const char *sql =
    "INSERT INTO sessions(token, player_id, expires, created_at) VALUES(?1, ?2, ?3, ?4);";
  sqlite3_stmt *st = NULL;
  int rc = sqlite3_prepare_v2 (db, sql, -1, &st, NULL);
  if (rc != SQLITE_OK)
    return rc;

  sqlite3_bind_text (st, 1, token_out, -1, SQLITE_TRANSIENT);
  sqlite3_bind_int (st, 2, player_id);
  sqlite3_bind_int64 (st, 3, exp);
  sqlite3_bind_int64 (st, 4, (sqlite3_int64) now);

  rc = sqlite3_step (st);
  sqlite3_finalize (st);
  return (rc == SQLITE_DONE) ? SQLITE_OK : SQLITE_ERROR;
}

/* Lookup */
int
db_session_lookup (const char *token, int *out_player_id,
		   long long *out_expires_epoch)
{
  if (!token)
    return SQLITE_MISUSE;
  if (out_player_id)
    *out_player_id = 0;
  if (out_expires_epoch)
    *out_expires_epoch = 0;

  sqlite3 *db = db_get_handle ();
  if (!db)
    return SQLITE_ERROR;

  const char *sql = "SELECT player_id, expires FROM sessions WHERE token=?1;";
  sqlite3_stmt *st = NULL;
  int rc = sqlite3_prepare_v2 (db, sql, -1, &st, NULL);
  if (rc != SQLITE_OK)
    return rc;

  sqlite3_bind_text (st, 1, token, -1, SQLITE_TRANSIENT);
  rc = sqlite3_step (st);
  if (rc == SQLITE_ROW)
    {
      int pid = sqlite3_column_int (st, 0);
      long long exp = sqlite3_column_int64 (st, 1);
      sqlite3_finalize (st);
      time_t now = time (NULL);
      if (exp <= (long long) now)
	return SQLITE_NOTFOUND;	/* expired */
      if (out_player_id)
	*out_player_id = pid;
      if (out_expires_epoch)
	*out_expires_epoch = exp;
      return SQLITE_OK;
    }
  sqlite3_finalize (st);
  return SQLITE_NOTFOUND;
}

/* Revoke */
int
db_session_revoke (const char *token)
{
  if (!token)
    return SQLITE_MISUSE;
  sqlite3 *db = db_get_handle ();
  if (!db)
    return SQLITE_ERROR;
  const char *sql = "DELETE FROM sessions WHERE token=?1;";
  sqlite3_stmt *st = NULL;
  int rc = sqlite3_prepare_v2 (db, sql, -1, &st, NULL);
  if (rc != SQLITE_OK)
    return rc;
  sqlite3_bind_text (st, 1, token, -1, SQLITE_TRANSIENT);
  rc = sqlite3_step (st);
  sqlite3_finalize (st);
  return (rc == SQLITE_DONE) ? SQLITE_OK : rc;
}

/* Refresh (rotate token) */
int
db_session_refresh (const char *old_token, int ttl_seconds,
		    char token_out[65], int *out_player_id)
{
  if (!old_token || !token_out)
    return SQLITE_MISUSE;

  int pid = 0;
  long long exp = 0;
  int rc = db_session_lookup (old_token, &pid, &exp);
  if (rc != SQLITE_OK)
    return rc;

  rc = db_session_revoke (old_token);
  if (rc != SQLITE_OK)
    return rc;

  rc = db_session_create (pid, ttl_seconds, token_out);
  if (rc == SQLITE_OK && out_player_id)
    *out_player_id = pid;
  return rc;
}

int
db_ensure_idempotency_schema (void)
{
  sqlite3 *db = db_get_handle ();
  if (!db)
    return SQLITE_ERROR;

  char *errmsg = NULL;
  int rc = sqlite3_exec (db, "BEGIN IMMEDIATE;", NULL, NULL, &errmsg);
  if (rc != SQLITE_OK)
    goto fail;

  rc = sqlite3_exec (db, "CREATE TABLE IF NOT EXISTS idempotency (" "  key         TEXT PRIMARY KEY," "  cmd         TEXT NOT NULL," "  req_fp      TEXT NOT NULL," "  response    TEXT,"	/* JSON of full envelope we sent */
		     "  created_at  INTEGER NOT NULL,"	/* epoch seconds */
		     "  updated_at  INTEGER" ");", NULL, NULL, &errmsg);
  if (rc != SQLITE_OK)
    {
      sqlite3_exec (db, "ROLLBACK;", NULL, NULL, NULL);
      goto fail;
    }

  rc = sqlite3_exec (db,
		     "CREATE INDEX IF NOT EXISTS idx_idemp_cmd ON idempotency(cmd);",
		     NULL, NULL, &errmsg);
  if (rc != SQLITE_OK)
    {
      sqlite3_exec (db, "ROLLBACK;", NULL, NULL, NULL);
      goto fail;
    }

  rc = sqlite3_exec (db, "COMMIT;", NULL, NULL, &errmsg);
  if (rc != SQLITE_OK)
    goto fail;
  return SQLITE_OK;

fail:
  if (errmsg)
    {
      fprintf (stderr, "[DB] idempotency schema: %s\n", errmsg);
      sqlite3_free (errmsg);
    }
  return rc;
}

int
db_idemp_try_begin (const char *key, const char *cmd, const char *req_fp)
{
  if (!key || !cmd || !req_fp)
    return SQLITE_MISUSE;
  sqlite3 *db = db_get_handle ();
  if (!db)
    return SQLITE_ERROR;

  time_t now = time (NULL);
  const char *sql =
    "INSERT INTO idempotency(key, cmd, req_fp, created_at) VALUES(?1, ?2, ?3, ?4);";
  sqlite3_stmt *st = NULL;
  int rc = sqlite3_prepare_v2 (db, sql, -1, &st, NULL);
  if (rc != SQLITE_OK)
    return rc;

  sqlite3_bind_text (st, 1, key, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text (st, 2, cmd, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text (st, 3, req_fp, -1, SQLITE_TRANSIENT);
  sqlite3_bind_int64 (st, 4, (sqlite3_int64) now);

  rc = sqlite3_step (st);
  sqlite3_finalize (st);

  if (rc == SQLITE_DONE)
    return SQLITE_OK;
  if (rc == SQLITE_CONSTRAINT)
    return SQLITE_CONSTRAINT;
  return SQLITE_ERROR;
}

int
db_idemp_fetch (const char *key, char **out_cmd, char **out_req_fp,
		char **out_response_json)
{
  if (!key)
    return SQLITE_MISUSE;
  sqlite3 *db = db_get_handle ();
  if (!db)
    return SQLITE_ERROR;

  const char *sql =
    "SELECT cmd, req_fp, response FROM idempotency WHERE key=?1;";
  sqlite3_stmt *st = NULL;
  int rc = sqlite3_prepare_v2 (db, sql, -1, &st, NULL);
  if (rc != SQLITE_OK)
    return rc;

  sqlite3_bind_text (st, 1, key, -1, SQLITE_TRANSIENT);
  rc = sqlite3_step (st);
  if (rc == SQLITE_ROW)
    {
      if (out_cmd)
	{
	  const unsigned char *t = sqlite3_column_text (st, 0);
	  *out_cmd = t ? strdup ((const char *) t) : NULL;
	}
      if (out_req_fp)
	{
	  const unsigned char *t = sqlite3_column_text (st, 1);
	  *out_req_fp = t ? strdup ((const char *) t) : NULL;
	}
      if (out_response_json)
	{
	  const unsigned char *t = sqlite3_column_text (st, 2);
	  *out_response_json = t ? strdup ((const char *) t) : NULL;
	}
      sqlite3_finalize (st);
      return SQLITE_OK;
    }
  sqlite3_finalize (st);
  return SQLITE_NOTFOUND;
}

int
db_idemp_store_response (const char *key, const char *response_json)
{
  if (!key || !response_json)
    return SQLITE_MISUSE;
  sqlite3 *db = db_get_handle ();
  if (!db)
    return SQLITE_ERROR;

  time_t now = time (NULL);
  const char *sql =
    "UPDATE idempotency SET response=?1, updated_at=?2 WHERE key=?3;";
  sqlite3_stmt *st = NULL;
  int rc = sqlite3_prepare_v2 (db, sql, -1, &st, NULL);
  if (rc != SQLITE_OK)
    return rc;

  sqlite3_bind_text (st, 1, response_json, -1, SQLITE_TRANSIENT);
  sqlite3_bind_int64 (st, 2, (sqlite3_int64) now);
  sqlite3_bind_text (st, 3, key, -1, SQLITE_TRANSIENT);

  rc = sqlite3_step (st);
  sqlite3_finalize (st);
  return (rc == SQLITE_DONE) ? SQLITE_OK : SQLITE_ERROR;
}

/* Helper: prepare, bind one int, and return stmt or NULL */
static sqlite3_stmt* prep1i(sqlite3 *db, const char *sql, int v) {
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) != SQLITE_OK) return NULL;
    sqlite3_bind_int(st, 1, v);
    return st;
}

#if 0 /* old db_sector_info_json */
int db_sector_info_json(int sector_id, json_t **out)
{
    if (out) *out = NULL;
    sqlite3 *db = db_get_handle();
    if (!db) return SQLITE_ERROR;

    json_t *root = json_object();
    json_object_set_new(root, "sector_id", json_integer(sector_id));

    /* 1) Sector core (id, name, security level, safe flag, beacon text/author) */
    {
        /* Adjust column names to your actual schema:
           - sectors(id INTEGER PK, name TEXT, security_level INT, safe_zone INT, beacon_text TEXT, beacon_by INT)
           If some columns don’t exist, this SELECT still works for the ones that do. */
        const char *sql =
            "SELECT name,"
            "       COALESCE(security_level, 0),"
            "       COALESCE(safe_zone, 0),"
            "       COALESCE(beacon_text, ''),"
            "       COALESCE(beacon_by, 0)"
            "  FROM sectors WHERE id=?1;";
        sqlite3_stmt *st = prep1i(db, sql, sector_id);
        if (st) {
            if (sqlite3_step(st) == SQLITE_ROW) {
                const char *nm = (const char*)sqlite3_column_text(st, 0);
                int sec_level  = sqlite3_column_int(st, 1);
                int safe       = sqlite3_column_int(st, 2);
                const char *btxt = (const char*)sqlite3_column_text(st, 3);
                int bby        = sqlite3_column_int(st, 4);

                if (nm) json_object_set_new(root, "name", json_string(nm));

                json_t *sec = json_pack("{s:i, s:b}", "level", sec_level, "is_safe_zone", safe ? 1 : 0);
                json_object_set_new(root, "security", sec);

                if (btxt && btxt[0]) {
                    json_t *b = json_pack("{s:s, s:i}", "text", btxt, "by_player_id", bby);
                    json_object_set_new(root, "beacon", b);
                }
            }
            sqlite3_finalize(st);
        }
    }

    /* 2) Adjacency (warps): warps(from_sector, to_sector) */
    {
        const char *sql = "SELECT to_sector FROM warps WHERE from_sector=?1 ORDER BY to_sector;";
        sqlite3_stmt *st = prep1i(db, sql, sector_id);
        if (st) {
            json_t *adj = json_array();
            while (sqlite3_step(st) == SQLITE_ROW) {
                int to = sqlite3_column_int(st, 0);
                json_array_append_new(adj, json_integer(to));
            }
            sqlite3_finalize(st);
            if (json_array_size(adj) > 0) json_object_set_new(root, "adjacent", adj);
            else json_decref(adj);
        }
    }

    /* 3) Port (at most one per sector in classic TW; adapt if you support multiple) */
    {
        /* Example columns: ports(id, sector_id, name, type_code, is_open) */
        const char *sql =
            "SELECT id, name, COALESCE(type_code,''), COALESCE(is_open,1)"
            "  FROM ports WHERE sector_id=?1 LIMIT 1;";
        sqlite3_stmt *st = prep1i(db, sql, sector_id);
        if (st) {
            if (sqlite3_step(st) == SQLITE_ROW) {
                int pid = sqlite3_column_int(st, 0);
                const char *pname = (const char*)sqlite3_column_text(st, 1);
                const char *ptype = (const char*)sqlite3_column_text(st, 2);
                int is_open = sqlite3_column_int(st, 3);

                json_t *port = json_object();
                json_object_set_new(port, "id", json_integer(pid));
                if (pname) json_object_set_new(port, "name", json_string(pname));
                if (ptype) json_object_set_new(port, "type", json_string(ptype));
                json_object_set_new(port, "status", json_string(is_open ? "open" : "closed"));
                json_object_set_new(root, "port", port);
            }
            sqlite3_finalize(st);
        }
    }

    /* 4) Planets (optional) — planets(id, sector_id, name, owner_id) */
    {
        const char *sql =
            "SELECT id, name, COALESCE(owner_id,0)"
            "  FROM planets WHERE sector_id=?1 ORDER BY id;";
        sqlite3_stmt *st = prep1i(db, sql, sector_id);
        if (st) {
            json_t *arr = json_array();
            while (sqlite3_step(st) == SQLITE_ROW) {
                int id = sqlite3_column_int(st, 0);
                const char *nm = (const char*)sqlite3_column_text(st, 1);
                int owner = sqlite3_column_int(st, 2);
                json_t *pl = json_pack("{s:i, s:i}", "id", id, "owner_id", owner);
                if (nm) json_object_set_new(pl, "name", json_string(nm));
                json_array_append_new(arr, pl);
            }
            sqlite3_finalize(st);
            if (json_array_size(arr) > 0) json_object_set_new(root, "planets", arr);
            else json_decref(arr);
        }
    }

    /* 5) Entities (optional) — entities(id, sector_id, kind, name) */
    {
        const char *sql =
            "SELECT id, COALESCE(kind,''), COALESCE(name,'')"
            "  FROM entities WHERE sector_id=?1 ORDER BY id;";
        sqlite3_stmt *st = prep1i(db, sql, sector_id);
        if (st) {
            json_t *arr = json_array();
            while (sqlite3_step(st) == SQLITE_ROW) {
                int id = sqlite3_column_int(st, 0);
                const char *kind = (const char*)sqlite3_column_text(st, 1);
                const char *nm   = (const char*)sqlite3_column_text(st, 2);
                json_t *e = json_pack("{s:i}", "id", id);
                if (kind && *kind) json_object_set_new(e, "kind", json_string(kind));
                if (nm && *nm)   json_object_set_new(e, "name", json_string(nm));
                json_array_append_new(arr, e);
            }
            sqlite3_finalize(st);
            if (json_array_size(arr) > 0) json_object_set_new(root, "entities", arr);
            else json_decref(arr);
        }
    }

    if (out) *out = root; else json_decref(root);
    return SQLITE_OK;
}
#endif /* old db_sector_info_json */

int db_sector_info_json(int sector_id, json_t **out)
{
    if (out) *out = NULL;
    sqlite3 *db = db_get_handle();
    if (!db) return SQLITE_ERROR;

    json_t *root = json_object();
    json_object_set_new(root, "sector_id", json_integer(sector_id));

    /* 0) Sector core: name (+ optional beacon if present in schema) */
    {
        /* Adjust these column names if your sectors table differs; unknown columns will cause prepare to fail,
           so we try a minimal SELECT first (name only), then a richer one if available. */

        const char *sql_min = "SELECT name FROM sectors WHERE id=?1;";
        sqlite3_stmt *st = NULL;
        if (sqlite3_prepare_v2(db, sql_min, -1, &st, NULL) == SQLITE_OK) {
            sqlite3_bind_int(st, 1, sector_id);
            if (sqlite3_step(st) == SQLITE_ROW) {
                const char *nm = (const char*)sqlite3_column_text(st, 0);
                if (nm) json_object_set_new(root, "name", json_string(nm));
            }
            sqlite3_finalize(st);
        }

        /* Optional richer fields (beacon/security) – try only if columns exist.
           This SELECT will fail silently if your schema doesn’t have those columns, which is OK. */
        const char *sql_rich =
            "SELECT "
            "  COALESCE(beacon_text, ''), COALESCE(beacon_by, 0), "
            "  COALESCE(security_level, 0), COALESCE(safe_zone, 0) "
            "FROM sectors WHERE id=?1;";
        if (sqlite3_prepare_v2(db, sql_rich, -1, &st, NULL) == SQLITE_OK) {
            sqlite3_bind_int(st, 1, sector_id);
            if (sqlite3_step(st) == SQLITE_ROW) {
                const char *btxt = (const char*)sqlite3_column_text(st, 0);
                int bby          = sqlite3_column_int(st, 1);
                int sec_level    = sqlite3_column_int(st, 2);
                int safe         = sqlite3_column_int(st, 3);

                if (btxt && btxt[0]) {
                    json_t *b = json_pack("{s:s, s:i}", "text", btxt, "by_player_id", bby);
                    json_object_set_new(root, "beacon", b);
                }
                /* Only add security if at least one of these is non-default */
                if (sec_level != 0 || safe != 0) {
                    json_t *sec = json_pack("{s:i, s:b}", "level", sec_level, "is_safe_zone", safe ? 1 : 0);
                    json_object_set_new(root, "security", sec);
                }
            }
            sqlite3_finalize(st);
        }
    }

/* 1) Adjacency via sector_adjacency(neighbors CSV) */
{
    const char *sql = "SELECT neighbors FROM sector_adjacency WHERE sector_id=?1;";
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_int(st, 1, sector_id);
        if (sqlite3_step(st) == SQLITE_ROW) {
            const unsigned char *neighbors = sqlite3_column_text(st, 0);
            json_t *adj = parse_neighbors_csv(neighbors);
            if (json_array_size(adj) > 0)
                json_object_set_new(root, "adjacent", adj);
            else
                json_decref(adj);
        }
        sqlite3_finalize(st);
    }
}

    /* 2) Port (first port in sector, if any) via sector_ports */
    {
        const char *sql =
            "SELECT port_id, port_name, COALESCE(type_code,''), COALESCE(is_open,1) "
            "FROM sector_ports WHERE sector_id=?1 ORDER BY port_id LIMIT 1;";
        sqlite3_stmt *st = NULL;
        if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) == SQLITE_OK) {
            sqlite3_bind_int(st, 1, sector_id);
            if (sqlite3_step(st) == SQLITE_ROW) {
                int pid = sqlite3_column_int(st, 0);
                const char *pname = (const char*)sqlite3_column_text(st, 1);
                const char *ptype = (const char*)sqlite3_column_text(st, 2);
                int is_open = sqlite3_column_int(st, 3);

                json_t *port = json_object();
                json_object_set_new(port, "id", json_integer(pid));
                if (pname) json_object_set_new(port, "name", json_string(pname));
                if (ptype) json_object_set_new(port, "type", json_string(ptype));
                json_object_set_new(port, "status", json_string(is_open ? "open" : "closed"));
                json_object_set_new(root, "port", port);
            }
            sqlite3_finalize(st);
        }
    }

    /* 3) Planets via sector_planets */
    {
        const char *sql =
            "SELECT planet_id, planet_name, COALESCE(owner_id,0) "
            "FROM sector_planets WHERE sector_id=?1 ORDER BY planet_id;";
        sqlite3_stmt *st = NULL;
        if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) == SQLITE_OK) {
            sqlite3_bind_int(st, 1, sector_id);
            json_t *arr = json_array();
            while (sqlite3_step(st) == SQLITE_ROW) {
                int id = sqlite3_column_int(st, 0);
                const char *nm = (const char*)sqlite3_column_text(st, 1);
                int owner = sqlite3_column_int(st, 2);
                json_t *pl = json_pack("{s:i, s:i}", "id", id, "owner_id", owner);
                if (nm) json_object_set_new(pl, "name", json_string(nm));
                json_array_append_new(arr, pl);
            }
            sqlite3_finalize(st);
            if (json_array_size(arr) > 0) json_object_set_new(root, "planets", arr);
            else json_decref(arr);
        }
    }

    /* 4) Entities via ships_by_sector (treat them as 'ship' entities) */
    {
        const char *sql =
            "SELECT ship_id, COALESCE(ship_name,''), COALESCE(owner_player_id,0) "
            "FROM ships_by_sector WHERE sector_id=?1 ORDER BY ship_id;";
        sqlite3_stmt *st = NULL;
        if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) == SQLITE_OK) {
            sqlite3_bind_int(st, 1, sector_id);
            json_t *arr = json_array();
            while (sqlite3_step(st) == SQLITE_ROW) {
                int id = sqlite3_column_int(st, 0);
                const char *nm = (const char*)sqlite3_column_text(st, 1);
                int owner = sqlite3_column_int(st, 2);
                json_t *e = json_pack("{s:i, s:s, s:i}", "id", id, "kind", "ship", "owner_id", owner);
                if (nm && *nm) json_object_set_new(e, "name", json_string(nm));
                json_array_append_new(arr, e);
            }
            sqlite3_finalize(st);
            if (json_array_size(arr) > 0) json_object_set_new(root, "entities", arr);
            else json_decref(arr);
        }
    }

    /* 5) Security/topology flags via sector_summary (if present) */
    {
        /* sector_summary columns vary; this query safely tries a few common ones */
        const char *sql =
            "SELECT "
            "  COALESCE(degree, NULL), "
            "  COALESCE(dead_in, NULL), "
            "  COALESCE(dead_out, NULL), "
            "  COALESCE(is_isolated, NULL) "
            "FROM sector_summary WHERE sector_id=?1;";
        sqlite3_stmt *st = NULL;
        if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) == SQLITE_OK) {
            sqlite3_bind_int(st, 1, sector_id);
            if (sqlite3_step(st) == SQLITE_ROW) {
                int has_any = 0;
                json_t *sec = json_object();

                if (sqlite3_column_type(st, 0) != SQLITE_NULL) {
                    json_object_set_new(sec, "degree", json_integer(sqlite3_column_int(st, 0)));
                    has_any = 1;
                }
                if (sqlite3_column_type(st, 1) != SQLITE_NULL) {
                    json_object_set_new(sec, "dead_in", json_integer(sqlite3_column_int(st, 1)));
                    has_any = 1;
                }
                if (sqlite3_column_type(st, 2) != SQLITE_NULL) {
                    json_object_set_new(sec, "dead_out", json_integer(sqlite3_column_int(st, 2)));
                    has_any = 1;
                }
                if (sqlite3_column_type(st, 3) != SQLITE_NULL) {
                    json_object_set_new(sec, "is_isolated", sqlite3_column_int(st, 3) ? json_true() : json_false());
                    has_any = 1;
                }
                if (has_any) json_object_set_new(root, "security", sec);
                else json_decref(sec);
            }
            sqlite3_finalize(st);
        }
    }

    if (out) *out = root; else json_decref(root);
    return SQLITE_OK;
}

/* Parse "2,3,4,5" -> [2,3,4,5] */
static json_t *parse_neighbors_csv(const unsigned char *txt)
{
    json_t *arr = json_array();
    if (!txt) return arr;
    const char *p = (const char *)txt;

    while (*p) {
        while (*p == ' ' || *p == '\t') p++;           /* trim left */
        const char *start = p;
        while (*p && *p != ',') p++;
        int len = (int)(p - start);
        if (len > 0) {
            char buf[32];
            if (len >= (int)sizeof(buf)) len = (int)sizeof(buf) - 1; /* defensive */
            memcpy(buf, start, len);
            buf[len] = '\0';
            int id = atoi(buf);
            if (id > 0) json_array_append_new(arr, json_integer(id));
        }
        if (*p == ',') p++;                             /* skip comma */
    }
    return arr;
}
