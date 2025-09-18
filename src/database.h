#ifndef DATABASE_H
#define DATABASE_H

#include <jansson.h>		/* for json_t */
#include <sqlite3.h>		/* for sqlite3 */


/* Initialise database (creates file if not exists) */
int db_init ();

/* Create required tables */
int db_create_tables (void);

/* Insert default data (config rows, etc.) */
int db_insert_defaults (void);

/* CRUD operations */
int db_create (const char *table, json_t * row);
json_t *db_read (const char *table, int id);
int db_update (const char *table, int id, json_t * row);
int db_delete (const char *table, int id);

/* Cleanup */
void db_close (void);

////////////////////

static const char *create_table_sql[] = {
  "CREATE TABLE IF NOT EXISTS config (id INTEGER PRIMARY KEY CHECK (id = 1), turnsperday INTEGER, maxwarps_per_sector INTEGER, startingcredits INTEGER, startingfighters INTEGER, startingholds INTEGER, processinterval INTEGER, autosave INTEGER, max_ports INTEGER, max_planets_per_sector INTEGER, max_total_planets INTEGER, max_citadel_level INTEGER, number_of_planet_types INTEGER, max_ship_name_length INTEGER, ship_type_count INTEGER, hash_length INTEGER, default_nodes INTEGER, buff_size INTEGER, max_name_length INTEGER);",

  "CREATE TABLE IF NOT EXISTS planettypes (id INTEGER PRIMARY KEY AUTOINCREMENT, code TEXT UNIQUE, typeDescription TEXT, typeName TEXT, citadelUpgradeTime_lvl1 INTEGER, citadelUpgradeTime_lvl2 INTEGER, citadelUpgradeTime_lvl3 INTEGER, citadelUpgradeTime_lvl4 INTEGER, citadelUpgradeTime_lvl5 INTEGER, citadelUpgradeTime_lvl6 INTEGER, citadelUpgradeOre_lvl1 INTEGER, citadelUpgradeOre_lvl2 INTEGER, citadelUpgradeOre_lvl3 INTEGER, citadelUpgradeOre_lvl4 INTEGER, citadelUpgradeOre_lvl5 INTEGER, citadelUpgradeOre_lvl6 INTEGER, citadelUpgradeOrganics_lvl1 INTEGER, citadelUpgradeOrganics_lvl2 INTEGER, citadelUpgradeOrganics_lvl3 INTEGER, citadelUpgradeOrganics_lvl4 INTEGER, citadelUpgradeOrganics_lvl5 INTEGER, citadelUpgradeOrganics_lvl6 INTEGER, citadelUpgradeEquipment_lvl1 INTEGER, citadelUpgradeEquipment_lvl2 INTEGER, citadelUpgradeEquipment_lvl3 INTEGER, citadelUpgradeEquipment_lvl4 INTEGER, citadelUpgradeEquipment_lvl5 INTEGER, citadelUpgradeEquipment_lvl6 INTEGER, citadelUpgradeColonist_lvl1 INTEGER, citadelUpgradeColonist_lvl2 INTEGER, citadelUpgradeColonist_lvl3 INTEGER, citadelUpgradeColonist_lvl4 INTEGER, citadelUpgradeColonist_lvl5 INTEGER, citadelUpgradeColonist_lvl6 INTEGER, maxColonist_ore INTEGER, maxColonist_organics INTEGER, maxColonist_equipment INTEGER, fighters INTEGER, fuelProduction INTEGER, organicsProduction INTEGER, equipmentProduction INTEGER, fighterProduction INTEGER, maxore INTEGER, maxorganics INTEGER, maxequipment INTEGER, maxfighters INTEGER, breeding REAL);",

  "CREATE TABLE IF NOT EXISTS ports ("
  "id INTEGER PRIMARY KEY AUTOINCREMENT, "
  "number INTEGER, "
  "name TEXT NOT NULL, "
  "location INTEGER NOT NULL, "  /* FK to sectors.id */
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
  "FOREIGN KEY (location) REFERENCES sectors(id));",

  "CREATE TABLE IF NOT EXISTS port_trade ("
  "id INTEGER PRIMARY KEY AUTOINCREMENT, "
  "port_id INTEGER NOT NULL, "
  "commodity TEXT CHECK(commodity IN ('ore','organics','equipment')), "
  "mode TEXT CHECK(mode IN ('buy','sell')), "
  "FOREIGN KEY (port_id) REFERENCES ports(id));",


  "CREATE TABLE IF NOT EXISTS players ("
  "id INTEGER PRIMARY KEY AUTOINCREMENT, "
  "number INTEGER, "                     /* legacy player ID */
  "name TEXT NOT NULL, "
  "passwd TEXT NOT NULL, "               /* hashed password */
  "sector INTEGER, "                     /* 0 if in a ship */
  "ship INTEGER, "                       /* ship number */
  "experience INTEGER, "
  "alignment INTEGER, "
  "turns INTEGER, "
  "credits INTEGER, "
  "bank_balance INTEGER, "
  "flags INTEGER, "                      /* bitfield: P_LOGGEDIN, P_STARDOCK, etc. */
  "lastprice INTEGER, "
  "firstprice INTEGER, "
  "integrity INTEGER, "
  "login_time INTEGER, "
  "last_update INTEGER, "
  "intransit INTEGER, "                  /* 0/1 boolean */
  "beginmove INTEGER, "                  /* timestamp */
  "movingto INTEGER, "                   /* sector destination */
  "loggedin INTEGER, "                   /* runtime only, but persisted if desired */
  "lastplanet INTEGER, "                 /* last planet created */
  "score INTEGER, "
  "kills INTEGER, "
  "cloaked INTEGER, "
  "remote INTEGER, "
  "fighters INTEGER, "
  "holds INTEGER"
  ");",

  
  "CREATE TABLE IF NOT EXISTS sectors (id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT, beacon TEXT, nebulae TEXT);",

  "CREATE TABLE IF NOT EXISTS sector_warps (from_sector INTEGER, to_sector INTEGER, PRIMARY KEY (from_sector, to_sector), FOREIGN KEY (from_sector) REFERENCES sectors(id) ON DELETE CASCADE, FOREIGN KEY (to_sector) REFERENCES sectors(id) ON DELETE CASCADE);",
  

  "CREATE TABLE IF NOT EXISTS ships ("
  "id INTEGER PRIMARY KEY AUTOINCREMENT, "
  "number INTEGER, "              /* legacy ID */
  "name TEXT NOT NULL, "
  "type INTEGER, "                /* FK to shiptypes.id (Index+1 from shipinfo array) */
  "attack INTEGER, "
  "holds_used INTEGER, "
  "mines INTEGER, "
  "fighters_used INTEGER, "
  "genesis INTEGER, "
  "photons INTEGER, "
  "location INTEGER, "            /* FK to sectors.id */
  "fighters INTEGER, "
  "shields INTEGER, "
  "holds INTEGER, "
  "colonists INTEGER, "
  "equipment INTEGER, "
  "organics INTEGER, "
  "ore INTEGER, "
  "owner INTEGER, "               /* FK to players.id */
  "flags INTEGER, "
  "ported INTEGER, "
  "onplanet INTEGER"
  ");",


  "CREATE TABLE IF NOT EXISTS shiptypes ("
  "id INTEGER PRIMARY KEY AUTOINCREMENT, "
  "name TEXT NOT NULL, "             /* Coloured name string */
  "basecost INTEGER, "
  "maxattack INTEGER, "
  "initialholds INTEGER, "
  "maxholds INTEGER, "
  "maxfighters INTEGER, "
  "turns INTEGER, "
  "mines INTEGER, "
  "genesis INTEGER, "
  "twarp INTEGER, "                  /* Transwarp capability (0/1) */
  "transportrange INTEGER, "
  "maxshields INTEGER, "
  "offense INTEGER, "
  "defense INTEGER, "
  "beacons INTEGER, "
  "holo INTEGER, "                   /* Holo scanner (0/1) */
  "planet INTEGER, "                 /* Can land on planets (0/1) */
  "photons INTEGER"                  /* Photon torpedo count */
  ");",

  "CREATE TABLE IF NOT EXISTS planets ("
  "id INTEGER PRIMARY KEY AUTOINCREMENT, "
  "num INTEGER, "                      /* legacy planet ID */
  "sector INTEGER NOT NULL, "          /* FK to sectors.id */
  "name TEXT NOT NULL, "
  "owner INTEGER, "                    /* FK to players.id */
  "population INTEGER, "
  "minerals INTEGER, "
  "ore INTEGER, "
  "energy INTEGER, "
  "type INTEGER, "                     /* FK to planettypes.id */
  "creator TEXT, "
  "fuelColonist INTEGER, "
  "organicsColonist INTEGER, "
  "equipmentColonist INTEGER, "
  "fuel INTEGER, "
  "organics INTEGER, "
  "equipment INTEGER, "
  "fighters INTEGER, "
  "citadel_level INTEGER DEFAULT 0, "  /* replaces pointer to citadel struct */
  "FOREIGN KEY (sector) REFERENCES sectors(id), "
  "FOREIGN KEY (owner) REFERENCES players(id), "
  "FOREIGN KEY (type) REFERENCES planettypes(id)"
  ");",

  "CREATE TABLE IF NOT EXISTS citadels ("
  "id INTEGER PRIMARY KEY AUTOINCREMENT, "
  "planet_id INTEGER UNIQUE NOT NULL, "   /* 1:1 link to planets.id */
  "level INTEGER, "
  "treasury INTEGER, "
  "militaryReactionLevel INTEGER, "
  "qCannonAtmosphere INTEGER, "
  "qCannonSector INTEGER, "
  "planetaryShields INTEGER, "
  "transporterlvl INTEGER, "
  "interdictor INTEGER, "
  "upgradePercent REAL, "
  "upgradestart INTEGER, "
  "owner INTEGER, "                       /* FK to players.id */
  "shields INTEGER, "
  "torps INTEGER, "
  "fighters INTEGER, "
  "qtorps INTEGER, "
  "qcannon INTEGER, "
  "qcannontype INTEGER, "
  "qtorpstype INTEGER, "
  "military INTEGER, "
  "FOREIGN KEY (planet_id) REFERENCES planets(id) ON DELETE CASCADE, "
  "FOREIGN KEY (owner) REFERENCES players(id)"
  ");"



};

//////////////////

/* Default planet types (migrated from planettypes.data) */
static const char *insert_default_sql[] = {
  /* Config defaults */
  "INSERT INTO config (id, turnsperday, maxwarps_per_sector, startingcredits, startingfighters, startingholds, processinterval, autosave, max_ports, max_planets_per_sector, max_total_planets, max_citadel_level, number_of_planet_types, max_ship_name_length, ship_type_count, hash_length, default_nodes, buff_size, max_name_length) "
  "VALUES (1, 120, 6, 1000, 10, 20, 1, 5, 200, 6, 300, 6, 8, 50, 8, 128, 500, 1024, 50);",

  /* Shiptypes */
  "INSERT INTO shiptypes (name, basecost, maxattack, initialholds, maxholds, maxfighters, turns, mines, genesis, twarp, transportrange, maxshields, offense, defense, beacons, holo, planet, photons) "
  "VALUES ('Merchant Cruiser', 41300, 750, 20, 75, 2500, 3, 50, 5, 0, 5, 400, 10, 10, 0, 1, 1, 0);",

  "INSERT INTO shiptypes (name, basecost, maxattack, initialholds, maxholds, maxfighters, turns, mines, genesis, twarp, transportrange, maxshields, offense, defense, beacons, holo, planet, photons) "
  "VALUES ('Scout Marauder', 15950, 250, 10, 25, 250, 2, 0, 0, 0, 0, 100, 20, 20, 0, 1, 1, 0);",

  "INSERT INTO shiptypes (name, basecost, maxattack, initialholds, maxholds, maxfighters, turns, mines, genesis, twarp, transportrange, maxshields, offense, defense, beacons, holo, planet, photons) "
  "VALUES ('Missile Frigate', 100000, 2000, 12, 60, 5000, 3, 5, 0, 0, 2, 400, 13, 13, 5, 0, 0, 1);",

  "INSERT INTO shiptypes (name, basecost, maxattack, initialholds, maxholds, maxfighters, turns, mines, genesis, twarp, transportrange, maxshields, offense, defense, beacons, holo, planet, photons) "
  "VALUES ('Battleship', 88500, 3000, 16, 80, 10000, 4, 25, 1, 0, 8, 750, 16, 16, 50, 1, 1, 0);",

  "INSERT INTO shiptypes (name, basecost, maxattack, initialholds, maxholds, maxfighters, turns, mines, genesis, twarp, transportrange, maxshields, offense, defense, beacons, holo, planet, photons) "
  "VALUES ('Corporate Flagship', 163500, 6000, 20, 85, 20000, 3, 100, 10, 1, 10, 1500, 12, 12, 100, 1, 1, 0);",

  "INSERT INTO shiptypes (name, basecost, maxattack, initialholds, maxholds, maxfighters, turns, mines, genesis, twarp, transportrange, maxshields, offense, defense, beacons, holo, planet, photons) "
  "VALUES ('Colonial Transport', 63600, 100, 50, 250, 200, 6, 0, 5, 0, 7, 500, 6, 6, 10, 0, 1, 0);",

  "INSERT INTO shiptypes (name, basecost, maxattack, initialholds, maxholds, maxfighters, turns, mines, genesis, twarp, transportrange, maxshields, offense, defense, beacons, holo, planet, photons) "
  "VALUES ('Cargo Transport', 51950, 125, 50, 125, 400, 4, 1, 2, 0, 5, 1000, 8, 8, 20, 1, 1, 0);",

  "INSERT INTO shiptypes (name, basecost, maxattack, initialholds, maxholds, maxfighters, turns, mines, genesis, twarp, transportrange, maxshields, offense, defense, beacons, holo, planet, photons) "
  "VALUES ('Merchant Freighter', 33400, 100, 30, 65, 300, 2, 2, 2, 0, 5, 500, 8, 8, 20, 1, 1, 0);",

  "INSERT INTO shiptypes (name, basecost, maxattack, initialholds, maxholds, maxfighters, turns, mines, genesis, twarp, transportrange, maxshields, offense, defense, beacons, holo, planet, photons) "
  "VALUES ('Imperial Starship', 329000, 10000, 40, 150, 50000, 4, 125, 10, 1, 15, 2000, 15, 15, 150, 1, 1, 1);",

  "INSERT INTO shiptypes (name, basecost, maxattack, initialholds, maxholds, maxfighters, turns, mines, genesis, twarp, transportrange, maxshields, offense, defense, beacons, holo, planet, photons) "
  "VALUES ('Havoc Gunstar', 79000, 1000, 12, 50, 10000, 3, 5, 1, 1, 6, 3000, 13, 13, 5, 1, 0, 0);",

  "INSERT INTO shiptypes (name, basecost, maxattack, initialholds, maxholds, maxfighters, turns, mines, genesis, twarp, transportrange, maxshields, offense, defense, beacons, holo, planet, photons) "
  "VALUES ('Constellation', 72500, 2000, 20, 80, 5000, 3, 25, 2, 0, 6, 750, 14, 14, 50, 1, 1, 0);",

  "INSERT INTO shiptypes (name, basecost, maxattack, initialholds, maxholds, maxfighters, turns, mines, genesis, twarp, transportrange, maxshields, offense, defense, beacons, holo, planet, photons) "
  "VALUES ('T''khasi Orion', 42500, 250, 30, 60, 750, 2, 5, 1, 0, 3, 750, 11, 11, 20, 1, 1, 0);",

  "INSERT INTO shiptypes (name, basecost, maxattack, initialholds, maxholds, maxfighters, turns, mines, genesis, twarp, transportrange, maxshields, offense, defense, beacons, holo, planet, photons) "
  "VALUES ('Tholian Sentinel', 47500, 800, 10, 50, 2500, 4, 50, 1, 0, 3, 4000, 1, 1, 10, 1, 0, 0);",

  "INSERT INTO shiptypes (name, basecost, maxattack, initialholds, maxholds, maxfighters, turns, mines, genesis, twarp, transportrange, maxshields, offense, defense, beacons, holo, planet, photons) "
  "VALUES ('Taurean Mule', 63600, 150, 50, 150, 300, 4, 0, 1, 0, 5, 600, 5, 5, 20, 1, 1, 0);",

  "INSERT INTO shiptypes (name, basecost, maxattack, initialholds, maxholds, maxfighters, turns, mines, genesis, twarp, transportrange, maxshields, offense, defense, beacons, holo, planet, photons) "
  "VALUES ('Interdictor Cruiser', 539000, 15000, 10, 40, 100000, 15, 200, 20, 0, 20, 4000, 12, 12, 100, 1, 1, 0);",
  
  
  /* ---------- PORTS ---------- */
  "INSERT INTO ports (id, number, name, location, size, techlevel, max_ore, max_organics, max_equipment, product_ore, product_organics, product_equipment, credits, invisible) "
  "VALUES (1, 1, 'Port Type 1 (BBS)', 1, 5, 3, 10000, 10000, 10000, 5000, 5000, 5000, 500000, 0);",

  "INSERT INTO ports (id, number, name, location, size, techlevel, max_ore, max_organics, max_equipment, product_ore, product_organics, product_equipment, credits, invisible) "
  "VALUES (2, 2, 'Port Type 2 (BSB)', 2, 5, 3, 10000, 10000, 10000, 5000, 5000, 5000, 500000, 0);",

  "INSERT INTO ports (id, number, name, location, size, techlevel, max_ore, max_organics, max_equipment, product_ore, product_organics, product_equipment, credits, invisible) "
  "VALUES (3, 3, 'Port Type 3 (BSS)', 3, 5, 3, 10000, 10000, 10000, 5000, 5000, 5000, 500000, 0);",

  "INSERT INTO ports (id, number, name, location, size, techlevel, max_ore, max_organics, max_equipment, product_ore, product_organics, product_equipment, credits, invisible) "
  "VALUES (4, 4, 'Port Type 4 (SBB)', 4, 5, 3, 10000, 10000, 10000, 5000, 5000, 5000, 500000, 0);",

  "INSERT INTO ports (id, number, name, location, size, techlevel, max_ore, max_organics, max_equipment, product_ore, product_organics, product_equipment, credits, invisible) "
  "VALUES (5, 5, 'Port Type 5 (SBS)', 5, 5, 3, 10000, 10000, 10000, 5000, 5000, 5000, 500000, 0);",

  "INSERT INTO ports (id, number, name, location, size, techlevel, max_ore, max_organics, max_equipment, product_ore, product_organics, product_equipment, credits, invisible) "
  "VALUES (6, 6, 'Port Type 6 (SSB)', 6, 5, 3, 10000, 10000, 10000, 5000, 5000, 5000, 500000, 0);",

  "INSERT INTO ports (id, number, name, location, size, techlevel, max_ore, max_organics, max_equipment, product_ore, product_organics, product_equipment, credits, invisible) "
  "VALUES (7, 7, 'Port Type 7 (SSS)', 7, 5, 3, 10000, 10000, 10000, 5000, 5000, 5000, 500000, 0);",

  "INSERT INTO ports (id, number, name, location, size, techlevel, max_ore, max_organics, max_equipment, product_ore, product_organics, product_equipment, credits, invisible) "
  "VALUES (8, 8, 'Port Type 8 (BBB)', 8, 5, 3, 10000, 10000, 10000, 5000, 5000, 5000, 500000, 0);",

  "INSERT INTO ports (id, number, name, location, size, techlevel, max_ore, max_organics, max_equipment, product_ore, product_organics, product_equipment, credits, invisible) "
  "VALUES (9, 9, 'Port Type 9 (Stardock)', 9, 10, 5, 20000, 20000, 20000, 10000, 10000, 10000, 1000000, 0);",

  /* ---------- TRADE RULES ---------- */
  /* Type 1: BBS (buys ore, buys organics, sells equipment) */
  "INSERT INTO port_trade (port_id, commodity, mode) VALUES (1, 'ore', 'buy');",
  "INSERT INTO port_trade (port_id, commodity, mode) VALUES (1, 'organics', 'buy');",
  "INSERT INTO port_trade (port_id, commodity, mode) VALUES (1, 'equipment', 'sell');",

  /* Type 2: BSB (buys ore, sells organics, buys equipment) */
  "INSERT INTO port_trade (port_id, commodity, mode) VALUES (2, 'ore', 'buy');",
  "INSERT INTO port_trade (port_id, commodity, mode) VALUES (2, 'organics', 'sell');",
  "INSERT INTO port_trade (port_id, commodity, mode) VALUES (2, 'equipment', 'buy');",

  /* Type 3: BSS (buys ore, sells organics, sells equipment) */
  "INSERT INTO port_trade (port_id, commodity, mode) VALUES (3, 'ore', 'buy');",
  "INSERT INTO port_trade (port_id, commodity, mode) VALUES (3, 'organics', 'sell');",
  "INSERT INTO port_trade (port_id, commodity, mode) VALUES (3, 'equipment', 'sell');",

  /* Type 4: SBB (sells ore, buys organics, buys equipment) */
  "INSERT INTO port_trade (port_id, commodity, mode) VALUES (4, 'ore', 'sell');",
  "INSERT INTO port_trade (port_id, commodity, mode) VALUES (4, 'organics', 'buy');",
  "INSERT INTO port_trade (port_id, commodity, mode) VALUES (4, 'equipment', 'buy');",

  /* Type 5: SBS (sells ore, buys organics, sells equipment) */
  "INSERT INTO port_trade (port_id, commodity, mode) VALUES (5, 'ore', 'sell');",
  "INSERT INTO port_trade (port_id, commodity, mode) VALUES (5, 'organics', 'buy');",
  "INSERT INTO port_trade (port_id, commodity, mode) VALUES (5, 'equipment', 'sell');",

  /* Type 6: SSB (sells ore, sells organics, buys equipment) */
  "INSERT INTO port_trade (port_id, commodity, mode) VALUES (6, 'ore', 'sell');",
  "INSERT INTO port_trade (port_id, commodity, mode) VALUES (6, 'organics', 'sell');",
  "INSERT INTO port_trade (port_id, commodity, mode) VALUES (6, 'equipment', 'buy');",

  /* Type 7: SSS (sells all) */
  "INSERT INTO port_trade (port_id, commodity, mode) VALUES (7, 'ore', 'sell');",
  "INSERT INTO port_trade (port_id, commodity, mode) VALUES (7, 'organics', 'sell');",
  "INSERT INTO port_trade (port_id, commodity, mode) VALUES (7, 'equipment', 'sell');",

  /* Type 8: BBB (buys all) */
  "INSERT INTO port_trade (port_id, commodity, mode) VALUES (8, 'ore', 'buy');",
  "INSERT INTO port_trade (port_id, commodity, mode) VALUES (8, 'organics', 'buy');",
  "INSERT INTO port_trade (port_id, commodity, mode) VALUES (8, 'equipment', 'buy');",

  /* Type 9: Stardock (BBB + upgrades/shipyard) */
  "INSERT INTO port_trade (port_id, commodity, mode) VALUES (9, 'ore', 'buy');",
  "INSERT INTO port_trade (port_id, commodity, mode) VALUES (9, 'organics', 'buy');",
  "INSERT INTO port_trade (port_id, commodity, mode) VALUES (9, 'equipment', 'buy');"

  
  /************ Planet types ******************/
  /* Earth type (M) */
  "INSERT INTO planettypes (code, typeDescription, typeName, "
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
  "0,0,0,0,0,"
  "10000000,100000,100000,1000000,0.75);",

  /* Mountainous (L) */
  "INSERT INTO planettypes (code, typeDescription, typeName, "
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
  "0,0,0,0,0,"
  "200000,200000,100000,1000000,0.24);",

  /* Oceanic (O) */
  "INSERT INTO planettypes (code, typeDescription, typeName, "
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
  "0,0,0,0,0,"
  "50000,1000000,50000,1000000,0.30);",

  /* Desert Wasteland (K) */
  "INSERT INTO planettypes (code, typeDescription, typeName, "
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
  "200000,50000,50000,"
  "0,0,0,0,0,"
  "200000,50000,10000,1000000,0.50);",

  /* Volcanic (H) */
  "INSERT INTO planettypes (code, typeDescription, typeName, "
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
  "1000000,10000,10000,"
  "0,0,0,0,0,"
  "1000000,10000,100000,1000000,0.30);",

  /* Gaseous (U) */
  "INSERT INTO planettypes (code, typeDescription, typeName, "
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
  "10000,10000,10000,"
  "0,0,0,0,0,"
  "10000,10000,10000,1000000,-0.10);",

  /* Glacial/Ice (C) */
  "INSERT INTO planettypes (code, typeDescription, typeName, "
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
  "20000,50000,20000,"
  "0,0,0,0,0,"
  "20000,50000,10000,1000000,-0.10);"
};

















#endif /* DATABASE_H */
