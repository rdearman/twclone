#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sqlite3.h>
#include <string.h>
#include "database.h"
#include "server_config.h"
#include "server_bigbang.h"
#include "namegen.h"



/* Define constants for random warp generation */
#define DEFAULT_PERCENT_DEADEND 25
#define DEFAULT_PERCENT_ONEWAY 50
#define DEFAULT_PERCENT_JUMP 10

/* Internal helpers */
static int get_out_degree(sqlite3 *db, int sector);
static int insert_warp_unique(sqlite3 *db, int from, int to);
static int create_random_warps(sqlite3 *db, int numSectors, int maxWarps);
int create_imperial(void); // Declared here for the compiler

/* Returns the ID of an NPC shiptype by its name. Returns -1 on error. */
static int get_npc_shiptype_id_by_name(sqlite3 *db, const char *name) {
    const char *q = "SELECT id FROM shiptypes WHERE name = ? AND can_purchase = 0;";
    sqlite3_stmt *st = NULL;
    int id = -1;

    if (sqlite3_prepare_v2(db, q, -1, &st, NULL) != SQLITE_OK) {
        fprintf(stderr, "get_npc_shiptype_id_by_name prepare failed: %s\n", sqlite3_errmsg(db));
        return -1;
    }
    sqlite3_bind_text(st, 1, name, -1, SQLITE_STATIC);
    if (sqlite3_step(st) == SQLITE_ROW) {
        id = sqlite3_column_int(st, 0);
    } else {
        fprintf(stderr, "get_npc_shiptype_id_by_name: NPC Shiptype '%s' not found.\n", name);
    }
    sqlite3_finalize(st);
    return id;
}

/* Returns the ID of a purchasable shiptype by its name. Returns -1 on error. */
static int get_purchasable_shiptype_id_by_name(sqlite3 *db, const char *name) {
    const char *q = "SELECT id FROM shiptypes WHERE name = ? AND can_purchase = 1;";
    sqlite3_stmt *st = NULL;
    int id = -1;

    if (sqlite3_prepare_v2(db, q, -1, &st, NULL) != SQLITE_OK) {
        fprintf(stderr, "get_purchasable_shiptype_id_by_name prepare failed: %s\n", sqlite3_errmsg(db));
        return -1;
    }
    sqlite3_bind_text(st, 1, name, -1, SQLITE_STATIC);
    if (sqlite3_step(st) == SQLITE_ROW) {
        id = sqlite3_column_int(st, 0);
    } else {
        fprintf(stderr, "get_purchasable_shiptype_id_by_name: Purchasable Shiptype '%s' not found.\n", name);
    }
    sqlite3_finalize(st);
    return id;
}
/* Returns the number of existing warps from a sector. Returns -1 on error. */
static int get_out_degree(sqlite3 *db, int sector) {
    const char *q = "SELECT COUNT(*) FROM sector_warps WHERE from_sector=?;";
    sqlite3_stmt *st = NULL;

    if (sqlite3_prepare_v2(db, q, -1, &st, NULL) != SQLITE_OK) {
        fprintf(stderr, "get_out_degree prepare failed: %s\n", sqlite3_errmsg(db));
        return -1;
    }
    sqlite3_bind_int(st, 1, sector);
    int deg = -1;
    if (sqlite3_step(st) == SQLITE_ROW) {
        deg = sqlite3_column_int(st, 0);
    }
    sqlite3_finalize(st);
    return deg;
}

/* * Inserts a warp if it doesn't exist.
 * Returns: 1 = inserted, 0 = already existed, -1 = error.
 */
static int insert_warp_unique(sqlite3 *db, int from, int to) {
    char *errmsg = NULL;
    char sql[128];

    snprintf(sql, sizeof(sql),
             "INSERT OR IGNORE INTO sector_warps (from_sector, to_sector) VALUES (%d,%d);",
             from, to);

    int rc = sqlite3_exec(db, sql, NULL, NULL, &errmsg);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "insert_warp_unique exec failed (%d -> %d): %s\n", from, to, errmsg);
        sqlite3_free(errmsg);
        return -1;
    }

    return sqlite3_changes(db) > 0 ? 1 : 0;
}

/* ----------------------------------------------------
 * Random warp creation
 * ---------------------------------------------------- */
static int create_random_warps(sqlite3 *db, int numSectors, int maxWarps) {
    srand((unsigned)time(NULL));
    char *errmsg = NULL;
    int rc;

    if (sqlite3_exec(db, "BEGIN IMMEDIATE;", NULL, NULL, &errmsg) != SQLITE_OK) {
        fprintf(stderr, "create_random_warps: BEGIN failed: %s\n", errmsg);
        if (errmsg) sqlite3_free(errmsg);
        return -1;
    }

    /* Iterate through sectors from 11 up to numSectors */
    for (int s = 11; s <= numSectors; s++) {
        // Skip this sector entirely if it's a dead-end
        if ((rand() % 100) < DEFAULT_PERCENT_DEADEND) {
            continue;
        }

        // Determine the target number of warps for this sector
        int targetWarps = maxWarps;
        if ((rand() % 100) > DEFAULT_PERCENT_JUMP) {
            targetWarps = 2 + rand() % (maxWarps - 1);
        }

        int current_degree = get_out_degree(db, s);
        if (current_degree < 0) goto fail;

        int attempts = 0;
        while (current_degree < targetWarps && attempts < 100) {
            int target = 11 + (rand() % (numSectors - 10));
            if (target == s) {
                attempts++;
                continue;
            }

            int ins = insert_warp_unique(db, s, target);
            if (ins < 0) goto fail;

            if (ins == 1) {
                // Add back-link unless ONEWAY chance triggers
                if ((rand() % 100) >= DEFAULT_PERCENT_ONEWAY) {
                    insert_warp_unique(db, target, s);
                }
            }
            current_degree = get_out_degree(db, s);
            if (current_degree < 0) goto fail;
            attempts++;
        }

        if (s % 50 == 0) {
            fprintf(stderr, "BIGBANG: warps seeded up to sector %d/%d\n", s, numSectors);
        }
    }

    rc = sqlite3_exec(db, "COMMIT;", NULL, NULL, &errmsg);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "create_random_warps: COMMIT failed: %s\n", errmsg);
        if (errmsg) sqlite3_free(errmsg);
        return -1;
    }
    return 0;

fail:
    sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
    return -1;
}

/* ----------------------------------------------------
 * bigbang - entry point
 * ---------------------------------------------------- */
int bigbang(void) {
    sqlite3 *db = db_get_handle();
    if (!db) {
        fprintf(stderr, "bigbang: Failed to get DB handle\n");
        return -1;
    }
    srand((unsigned int)time(NULL));

    struct twconfig *cfg = config_load();
    if (!cfg) {
        fprintf(stderr, "bigbang: config_load failed\n");
        return -1;
    }

    int numSectors = cfg->default_nodes;
    int maxWarps = cfg->maxwarps_per_sector;    

    fprintf(stderr, "BIGBANG: Creating universe...\n");

    // Create tables and insert defaults first
    if (db_create_tables() != 0) {
        fprintf(stderr, "BIGBANG: Failed to create tables.\n");
        free(cfg);
        return -1;
    }

    // NOTE: The call to db_insert_defaults() is intentionally removed here
    // because it is already handled by db_init() in the main program flow.
    // Calling it here would cause duplicate entries in the database.

    fprintf(stderr, "BIGBANG: Creating sectors...\n");
    if (create_sectors() != 0) {
        free(cfg);
        return -1;
    }

    fprintf(stderr, "BIGBANG: Creating random warps...\n");
    if (create_random_warps(db, cfg->default_nodes, cfg->maxwarps_per_sector) != 0) {
        fprintf(stderr, "BIGBANG: random warp generation failed\n");
        free(cfg);
        return -1;
    }

    fprintf(stderr, "BIGBANG: Creating complex warps...\n");    
    // Call the warp post-processing function here
    if (create_complex_warps(db, numSectors) != 0) {
        fprintf(stderr, "Failed to create complex warps.\n");
        return -1;
    }    

    fprintf(stderr, "BIGBANG: Creating ports...\n");
    if (create_ports() != 0) {
        free(cfg);
        return -1;
    }

    fprintf(stderr, "BIGBANG: Creating Ferringhi home sector...\n");
    if (create_ferringhi() != 0) {
        free(cfg);
        return -1;
    }

    fprintf(stderr, "BIGBANG: Creating Imperial Starship...\n");
    if (create_imperial() != 0) {
        free(cfg);
        return -1;
    }


    fprintf(stderr, "BIGBANG: Creating planets...\n");
    if (create_planets() != 0) {
        free(cfg);
        return -1;
    }

    free(cfg);
    fprintf(stderr, "BIGBANG: Universe successfully created.\n");
    return 0;
}

/* ----------------------------------------------------
 * Universe population functions
 * ---------------------------------------------------- */


int create_sectors(void) {
    sqlite3 *db = db_get_handle();
    struct twconfig *cfg = config_load();
    if (!cfg) {
        fprintf(stderr, "create_sectors: could not load config\n");
        return -1;
    }

    int numSectors = cfg->default_nodes;
    free(cfg);

    fprintf(stderr, "BIGBANG: Creating %d sectors (1–10 reserved for Fedspace).\n", numSectors);

    char sql[256];
    char name_buffer[50]; // Buffer to hold the generated name
    char neb_name_buffer[50]; // Buffer to hold the generated name
    char beacon[50] = "Brackus was here!"; // Buffer to hold the generated name        

    // This is the single loop that correctly creates the desired number of sectors.
    for (int i = 11; i <= numSectors; i++)
    {
        // Generate a new name for the sector
        consellationName(name_buffer);
	consellationName(neb_name_buffer);
        if (i % 64)
	  {
        snprintf(sql, sizeof(sql),
                 "INSERT INTO sectors (name, beacon, nebulae) "
                 "VALUES ('%s', '', '%s');",
                 name_buffer,neb_name_buffer);
	  }
	else
	  {
        snprintf(sql, sizeof(sql),
                 "INSERT INTO sectors (name, beacon, nebulae) "
                 "VALUES ('%s', '%s', '%s');",
                 name_buffer,beacon,neb_name_buffer);
	  }
        if (sqlite3_exec(db, sql, NULL, NULL, NULL) != SQLITE_OK)
        {
            fprintf(stderr, "create_sectors failed at %d: %s\n", i, sqlite3_errmsg(db));
            return -1;
        }
    }
    return 0;
}


int create_ports(void) {
    sqlite3 *db = db_get_handle();
    struct twconfig *cfg = config_load();
    if (!cfg) {
        fprintf(stderr, "create_ports: could not load config\n");
        return -1;
    }

    int numSectors = cfg->default_nodes;
    int maxPorts = cfg->max_ports;
    free(cfg);

    if (maxPorts < 1) {
        fprintf(stderr, "create_ports: max_ports < 1 in config\n");
        return 0;
    }

    char name_buffer[50];

    /* Stardock (Port 1) */
    int stardock_sector = 0;
    if (numSectors > 11) {
      stardock_sector = (rand() % (numSectors - 10)) + 11;
    } else {
      stardock_sector = 11;
    }
    
    // Prepare statements for efficiency
    const char *port_sql = "INSERT INTO ports (number, name, location, size, techlevel, credits, type, invisible) VALUES (?, 'Port ' || ?, ?, ?, ?, ?, ?, 0);";
    sqlite3_stmt *port_stmt;
    if (sqlite3_prepare_v2(db, port_sql, -1, &port_stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "create_ports prepare failed: %s\n", sqlite3_errmsg(db));
        return -1;
    }

    const char *trade_sql = "INSERT INTO port_trade (port_id, commodity, mode, maxproduct) VALUES (?, ?, ?, ?);";
    sqlite3_stmt *trade_stmt;
    if (sqlite3_prepare_v2(db, trade_sql, -1, &trade_stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "create_ports prepare failed for trade data: %s\n", sqlite3_errmsg(db));
        sqlite3_finalize(port_stmt);
        return -1;
    }

    // --- Create and populate Stardock ---
    // Bind values for the Stardock port
    sqlite3_bind_int(port_stmt, 1, 1);
    sqlite3_bind_text(port_stmt, 2, "Stardock", -1, SQLITE_STATIC);
    sqlite3_bind_int(port_stmt, 3, stardock_sector);
    sqlite3_bind_int(port_stmt, 4, 10);
    sqlite3_bind_int(port_stmt, 5, 5);
    sqlite3_bind_int(port_stmt, 6, 1000000);
    sqlite3_bind_int(port_stmt, 7, 9); // Type ID for Stardock

    if (sqlite3_step(port_stmt) != SQLITE_DONE) {
        fprintf(stderr, "create_ports failed (Stardock): %s\n", sqlite3_errmsg(db));
        sqlite3_finalize(port_stmt);
        sqlite3_finalize(trade_stmt);
        return -1;
    }
    sqlite3_reset(port_stmt);
    sqlite3_int64 stardock_id = sqlite3_last_insert_rowid(db);

    // Populate trade data for Stardock (type 9: Buy all)
    int maxproduct_amount = 5000;
    sqlite3_bind_int(trade_stmt, 1, stardock_id);
    sqlite3_bind_text(trade_stmt, 2, "ore", -1, SQLITE_STATIC);
    sqlite3_bind_text(trade_stmt, 3, "buy", -1, SQLITE_STATIC);
    sqlite3_bind_int(trade_stmt, 4, maxproduct_amount);
    sqlite3_step(trade_stmt);
    sqlite3_reset(trade_stmt);
    
    sqlite3_bind_int(trade_stmt, 1, stardock_id);
    sqlite3_bind_text(trade_stmt, 2, "organics", -1, SQLITE_STATIC);
    sqlite3_bind_text(trade_stmt, 3, "buy", -1, SQLITE_STATIC);
    sqlite3_bind_int(trade_stmt, 4, maxproduct_amount);
    sqlite3_step(trade_stmt);
    sqlite3_reset(trade_stmt);

    sqlite3_bind_int(trade_stmt, 1, stardock_id);
    sqlite3_bind_text(trade_stmt, 2, "equipment", -1, SQLITE_STATIC);
    sqlite3_bind_text(trade_stmt, 3, "buy", -1, SQLITE_STATIC);
    sqlite3_bind_int(trade_stmt, 4, maxproduct_amount);
    sqlite3_step(trade_stmt);
    sqlite3_reset(trade_stmt);

    // --- Create and populate the rest of the ports ---
    for (int i = 2; i <= maxPorts; i++) {
        int sector;
        do {
            sector = 11 + (rand() % (numSectors - 10));
        } while (sector == stardock_sector);

        int type_id = (rand() % 8) + 1;
        randomname(name_buffer);

        int size = 5;
        int techlevel = 3;
        int credits = 500000;

        // Special case for Ferrengi homeworld
        if (i == 7) {
            snprintf(name_buffer, sizeof(name_buffer), "Ferrengi Home");
            size = 10;
            techlevel = 5;
            credits = 500000;
        }

        // Bind values for the port
        sqlite3_bind_int(port_stmt, 1, i);
        sqlite3_bind_text(port_stmt, 2, name_buffer, -1, SQLITE_STATIC);
        sqlite3_bind_int(port_stmt, 3, sector);
        sqlite3_bind_int(port_stmt, 4, size);
        sqlite3_bind_int(port_stmt, 5, techlevel);
        sqlite3_bind_int(port_stmt, 6, credits);
        sqlite3_bind_int(port_stmt, 7, type_id);

        if (sqlite3_step(port_stmt) != SQLITE_DONE) {
            fprintf(stderr, "create_ports failed at %d: %s\n", i, sqlite3_errmsg(db));
            sqlite3_finalize(port_stmt);
            sqlite3_finalize(trade_stmt);
            return -1;
        }
        sqlite3_reset(port_stmt);

        sqlite3_int64 port_id = sqlite3_last_insert_rowid(db);
        maxproduct_amount = (rand() % 1000) + 1000;

        // Populate trade data based on type_id
        switch (type_id) {
            case 1: // BBS: Buy Ore and Organics, Sell Equipment
                sqlite3_bind_int(trade_stmt, 1, port_id);
                sqlite3_bind_text(trade_stmt, 2, "ore", -1, SQLITE_STATIC);
                sqlite3_bind_text(trade_stmt, 3, "buy", -1, SQLITE_STATIC);
                sqlite3_bind_int(trade_stmt, 4, maxproduct_amount);
                sqlite3_step(trade_stmt);
                sqlite3_reset(trade_stmt);

                sqlite3_bind_int(trade_stmt, 1, port_id);
                sqlite3_bind_text(trade_stmt, 2, "organics", -1, SQLITE_STATIC);
                sqlite3_bind_text(trade_stmt, 3, "buy", -1, SQLITE_STATIC);
                sqlite3_bind_int(trade_stmt, 4, maxproduct_amount);
                sqlite3_step(trade_stmt);
                sqlite3_reset(trade_stmt);

                sqlite3_bind_int(trade_stmt, 1, port_id);
                sqlite3_bind_text(trade_stmt, 2, "equipment", -1, SQLITE_STATIC);
                sqlite3_bind_text(trade_stmt, 3, "sell", -1, SQLITE_STATIC);
                sqlite3_bind_int(trade_stmt, 4, maxproduct_amount);
                sqlite3_step(trade_stmt);
                sqlite3_reset(trade_stmt);
                break;
            case 2: // BSB: Buy Ore and Equipment, Sell Organics
                sqlite3_bind_int(trade_stmt, 1, port_id);
                sqlite3_bind_text(trade_stmt, 2, "ore", -1, SQLITE_STATIC);
                sqlite3_bind_text(trade_stmt, 3, "buy", -1, SQLITE_STATIC);
                sqlite3_bind_int(trade_stmt, 4, maxproduct_amount);
                sqlite3_step(trade_stmt);
                sqlite3_reset(trade_stmt);
                
                sqlite3_bind_int(trade_stmt, 1, port_id);
                sqlite3_bind_text(trade_stmt, 2, "equipment", -1, SQLITE_STATIC);
                sqlite3_bind_text(trade_stmt, 3, "buy", -1, SQLITE_STATIC);
                sqlite3_bind_int(trade_stmt, 4, maxproduct_amount);
                sqlite3_step(trade_stmt);
                sqlite3_reset(trade_stmt);

                sqlite3_bind_int(trade_stmt, 1, port_id);
                sqlite3_bind_text(trade_stmt, 2, "organics", -1, SQLITE_STATIC);
                sqlite3_bind_text(trade_stmt, 3, "sell", -1, SQLITE_STATIC);
                sqlite3_bind_int(trade_stmt, 4, maxproduct_amount);
                sqlite3_step(trade_stmt);
                sqlite3_reset(trade_stmt);
                break;
            case 3: // BSS: Buy Ore, Sell Organics and Equipment
                sqlite3_bind_int(trade_stmt, 1, port_id);
                sqlite3_bind_text(trade_stmt, 2, "ore", -1, SQLITE_STATIC);
                sqlite3_bind_text(trade_stmt, 3, "buy", -1, SQLITE_STATIC);
                sqlite3_bind_int(trade_stmt, 4, maxproduct_amount);
                sqlite3_step(trade_stmt);
                sqlite3_reset(trade_stmt);
                
                sqlite3_bind_int(trade_stmt, 1, port_id);
                sqlite3_bind_text(trade_stmt, 2, "organics", -1, SQLITE_STATIC);
                sqlite3_bind_text(trade_stmt, 3, "sell", -1, SQLITE_STATIC);
                sqlite3_bind_int(trade_stmt, 4, maxproduct_amount);
                sqlite3_step(trade_stmt);
                sqlite3_reset(trade_stmt);

                sqlite3_bind_int(trade_stmt, 1, port_id);
                sqlite3_bind_text(trade_stmt, 2, "equipment", -1, SQLITE_STATIC);
                sqlite3_bind_text(trade_stmt, 3, "sell", -1, SQLITE_STATIC);
                sqlite3_bind_int(trade_stmt, 4, maxproduct_amount);
                sqlite3_step(trade_stmt);
                sqlite3_reset(trade_stmt);
                break;
            case 4: // SBB: Sell Ore, Buy Organics and Equipment
                sqlite3_bind_int(trade_stmt, 1, port_id);
                sqlite3_bind_text(trade_stmt, 2, "ore", -1, SQLITE_STATIC);
                sqlite3_bind_text(trade_stmt, 3, "sell", -1, SQLITE_STATIC);
                sqlite3_bind_int(trade_stmt, 4, maxproduct_amount);
                sqlite3_step(trade_stmt);
                sqlite3_reset(trade_stmt);
                
                sqlite3_bind_int(trade_stmt, 1, port_id);
                sqlite3_bind_text(trade_stmt, 2, "organics", -1, SQLITE_STATIC);
                sqlite3_bind_text(trade_stmt, 3, "buy", -1, SQLITE_STATIC);
                sqlite3_bind_int(trade_stmt, 4, maxproduct_amount);
                sqlite3_step(trade_stmt);
                sqlite3_reset(trade_stmt);

                sqlite3_bind_int(trade_stmt, 1, port_id);
                sqlite3_bind_text(trade_stmt, 2, "equipment", -1, SQLITE_STATIC);
                sqlite3_bind_text(trade_stmt, 3, "buy", -1, SQLITE_STATIC);
                sqlite3_bind_int(trade_stmt, 4, maxproduct_amount);
                sqlite3_step(trade_stmt);
                sqlite3_reset(trade_stmt);
                break;
            case 5: // SBS: Sell Ore and Equipment, Buy Organics
                sqlite3_bind_int(trade_stmt, 1, port_id);
                sqlite3_bind_text(trade_stmt, 2, "ore", -1, SQLITE_STATIC);
                sqlite3_bind_text(trade_stmt, 3, "sell", -1, SQLITE_STATIC);
                sqlite3_bind_int(trade_stmt, 4, maxproduct_amount);
                sqlite3_step(trade_stmt);
                sqlite3_reset(trade_stmt);

                sqlite3_bind_int(trade_stmt, 1, port_id);
                sqlite3_bind_text(trade_stmt, 2, "equipment", -1, SQLITE_STATIC);
                sqlite3_bind_text(trade_stmt, 3, "sell", -1, SQLITE_STATIC);
                sqlite3_bind_int(trade_stmt, 4, maxproduct_amount);
                sqlite3_step(trade_stmt);
                sqlite3_reset(trade_stmt);
                
                sqlite3_bind_int(trade_stmt, 1, port_id);
                sqlite3_bind_text(trade_stmt, 2, "organics", -1, SQLITE_STATIC);
                sqlite3_bind_text(trade_stmt, 3, "buy", -1, SQLITE_STATIC);
                sqlite3_bind_int(trade_stmt, 4, maxproduct_amount);
                sqlite3_step(trade_stmt);
                sqlite3_reset(trade_stmt);
                break;
            case 6: // SSB: Sell Ore and Organics, Buy Equipment
                sqlite3_bind_int(trade_stmt, 1, port_id);
                sqlite3_bind_text(trade_stmt, 2, "ore", -1, SQLITE_STATIC);
                sqlite3_bind_text(trade_stmt, 3, "sell", -1, SQLITE_STATIC);
                sqlite3_bind_int(trade_stmt, 4, maxproduct_amount);
                sqlite3_step(trade_stmt);
                sqlite3_reset(trade_stmt);
                
                sqlite3_bind_int(trade_stmt, 1, port_id);
                sqlite3_bind_text(trade_stmt, 2, "organics", -1, SQLITE_STATIC);
                sqlite3_bind_text(trade_stmt, 3, "sell", -1, SQLITE_STATIC);
                sqlite3_bind_int(trade_stmt, 4, maxproduct_amount);
                sqlite3_step(trade_stmt);
                sqlite3_reset(trade_stmt);

                sqlite3_bind_int(trade_stmt, 1, port_id);
                sqlite3_bind_text(trade_stmt, 2, "equipment", -1, SQLITE_STATIC);
                sqlite3_bind_text(trade_stmt, 3, "buy", -1, SQLITE_STATIC);
                sqlite3_bind_int(trade_stmt, 4, maxproduct_amount);
                sqlite3_step(trade_stmt);
                sqlite3_reset(trade_stmt);
                break;
            case 7: // SSS: Sell All
                sqlite3_bind_int(trade_stmt, 1, port_id);
                sqlite3_bind_text(trade_stmt, 2, "ore", -1, SQLITE_STATIC);
                sqlite3_bind_text(trade_stmt, 3, "sell", -1, SQLITE_STATIC);
                sqlite3_bind_int(trade_stmt, 4, maxproduct_amount);
                sqlite3_step(trade_stmt);
                sqlite3_reset(trade_stmt);
                
                sqlite3_bind_int(trade_stmt, 1, port_id);
                sqlite3_bind_text(trade_stmt, 2, "organics", -1, SQLITE_STATIC);
                sqlite3_bind_text(trade_stmt, 3, "sell", -1, SQLITE_STATIC);
                sqlite3_bind_int(trade_stmt, 4, maxproduct_amount);
                sqlite3_step(trade_stmt);
                sqlite3_reset(trade_stmt);

                sqlite3_bind_int(trade_stmt, 1, port_id);
                sqlite3_bind_text(trade_stmt, 2, "equipment", -1, SQLITE_STATIC);
                sqlite3_bind_text(trade_stmt, 3, "sell", -1, SQLITE_STATIC);
                sqlite3_bind_int(trade_stmt, 4, maxproduct_amount);
                sqlite3_step(trade_stmt);
                sqlite3_reset(trade_stmt);
                break;
            case 8: // BBB: Buy All
                sqlite3_bind_int(trade_stmt, 1, port_id);
                sqlite3_bind_text(trade_stmt, 2, "ore", -1, SQLITE_STATIC);
                sqlite3_bind_text(trade_stmt, 3, "buy", -1, SQLITE_STATIC);
                sqlite3_bind_int(trade_stmt, 4, maxproduct_amount);
                sqlite3_step(trade_stmt);
                sqlite3_reset(trade_stmt);
                
                sqlite3_bind_int(trade_stmt, 1, port_id);
                sqlite3_bind_text(trade_stmt, 2, "organics", -1, SQLITE_STATIC);
                sqlite3_bind_text(trade_stmt, 3, "buy", -1, SQLITE_STATIC);
                sqlite3_bind_int(trade_stmt, 4, maxproduct_amount);
                sqlite3_step(trade_stmt);
                sqlite3_reset(trade_stmt);

                sqlite3_bind_int(trade_stmt, 1, port_id);
                sqlite3_bind_text(trade_stmt, 2, "equipment", -1, SQLITE_STATIC);
                sqlite3_bind_text(trade_stmt, 3, "buy", -1, SQLITE_STATIC);
                sqlite3_bind_int(trade_stmt, 4, maxproduct_amount);
                sqlite3_step(trade_stmt);
                sqlite3_reset(trade_stmt);
                break;
            default:
                break;
        }
    }
    sqlite3_finalize(port_stmt);
    sqlite3_finalize(trade_stmt);
    fprintf(stderr, "create_ports: Stardock at sector %d, plus %d normal ports\n", stardock_sector, maxPorts - 1);
    return 0;
}


int create_ferringhi(void) {
    sqlite3 *db = db_get_handle();
    if (!db) {
        fprintf(stderr, "create_ferringhi: Failed to get DB handle\n");
        return -1;
    }
    
    // Dynamically retrieve the shiptype IDs, filtered by can_purchase
    int merchant_freighter_id = get_purchasable_shiptype_id_by_name(db, "Merchant Freighter");
    int ferengi_warship_id = get_npc_shiptype_id_by_name(db, "Ferrengi Warship");

    if (merchant_freighter_id == -1 || ferengi_warship_id == -1) {
        fprintf(stderr, "create_ferringhi: Failed to get required shiptype IDs.\n");
        return -1;
    }
    // Create the view to find the longest tunnels
    const char *create_view_sql = "CREATE VIEW IF NOT EXISTS longest_tunnels AS WITH all_sectors AS ( SELECT from_sector AS id FROM sector_warps UNION SELECT to_sector AS id FROM to_sector_warps ), outdeg AS ( SELECT a.id, COALESCE(COUNT(w.to_sector),0) AS deg FROM all_sectors a LEFT JOIN sector_warps w ON w.from_sector = a.id GROUP BY a.id ), edges AS ( SELECT from_sector, to_sector FROM sector_warps ), entry AS ( SELECT e.from_sector AS entry, e.to_sector AS next FROM edges e JOIN outdeg df ON df.id = e.from_sector AND df.deg > 1 JOIN outdeg dn ON dn.id = e.to_sector AND dn.deg = 1 ), rec(entry, curr, path, steps) AS ( SELECT entry, next, printf('%d->%d', entry, next), 1 FROM entry UNION ALL SELECT r.entry, e.to_sector, r.path || '->' || printf('%d', e.to_sector), r.steps + 1 FROM rec r JOIN edges e ON e.from_sector = r.curr JOIN outdeg d ON d.id = r.curr AND d.deg = 1 WHERE instr(r.path, '->' || printf('%d', e.to_sector) || '->') = 0 ) SELECT r.entry AS entry_sector, r.curr AS exit_sector, r.path AS tunnel_path, r.steps AS tunnel_length_edges FROM rec r JOIN outdeg d_exit ON d_exit.id = r.curr WHERE d_exit.deg <> 1 AND r.steps >= 2 ORDER BY r.steps DESC, r.entry, r.curr;";
    if (sqlite3_exec(db, create_view_sql, NULL, NULL, NULL) != SQLITE_OK) {
        fprintf(stderr, "create_ferringhi: Failed to create longest_tunnels view: %s\n", sqlite3_errmsg(db));
        return -1;
    }

    int longest_tunnel_sector = 0;
    const char *q = "SELECT exit_sector FROM longest_tunnels LIMIT 1;";
    sqlite3_stmt *st = NULL;

    if (sqlite3_prepare_v2(db, q, -1, &st, NULL) != SQLITE_OK) {
        fprintf(stderr, "create_ferringhi prepare failed: %s\n", sqlite3_errmsg(db));
        return -1;
    }

    if (sqlite3_step(st) == SQLITE_ROW) {
        longest_tunnel_sector = sqlite3_column_int(st, 0);
    }
    sqlite3_finalize(st);

    if (longest_tunnel_sector == 0) {
        fprintf(stderr, "create_ferringhi: No tunnels of length >= 2 found. Defaulting to sector 20.\n");
        longest_tunnel_sector = 20;
    }

    char sql_sector[256];
    snprintf(sql_sector, sizeof(sql_sector),
             "UPDATE sectors SET beacon='Ferringhi SPACE, Leave Now!', nebulae='Ferringhi' WHERE id=%d;",
             longest_tunnel_sector);

    if (sqlite3_exec(db, sql_sector, NULL, NULL, NULL) != SQLITE_OK) {
        fprintf(stderr, "create_ferringhi failed: %s\n", sqlite3_errmsg(db));
        return -1;
    }

    // Insert the Ferrengi homeworld into the planets table
    char sql_planet[512];
    snprintf(sql_planet, sizeof(sql_planet),
             "INSERT INTO planets (num, name, sector, owner, population, ore, organics, energy, fighters, citadel_level, type) "
             "VALUES (2, 'Ferringhi', %d, 0, %d, %d, %d, %d, %d, %d, %d);",
             longest_tunnel_sector,
             (rand() % 3001) + 1000,   // Population (1k-3k)
             (rand() % 1000000) + 1,  // Ore (1-1M)
             (rand() % 1000000) + 1,  // Organics (1-1M)
             (rand() % 1000000) + 1,  // Energy (1-1M)
             (rand() % 2501) + 2500,  // Fighters (2.5k-5k)
             (rand() % 2) + 2,        // Citadel Level (level 2-3)
             1); // Assuming type 1 is a valid planet type

    if (sqlite3_exec(db, sql_planet, NULL, NULL, NULL) != SQLITE_OK) {
        fprintf(stderr, "create_ferringhi failed to create planet: %s\n", sqlite3_errmsg(db));
        return -1;
    }

    // Get the ID of the new planet
    sqlite3_int64 planet_id = sqlite3_last_insert_rowid(db);

    // Insert the citadel details into the citadels table
    char sql_citadel[512];
    snprintf(sql_citadel, sizeof(sql_citadel),
             "INSERT INTO citadels (planet_id, level, shields, treasury, military) "
             "VALUES (%lld, %d, %d, %d, 1);",
             planet_id,
             (rand() % 2) + 2,        // Citadel Level (2-3)
             (rand() % 1001) + 1000,  // Shields (1k-2k)
             (rand() % 5000001) + 1000000); // Credits (1M-6M)

    if (sqlite3_exec(db, sql_citadel, NULL, NULL, NULL) != SQLITE_OK) {
        fprintf(stderr, "create_ferringhi failed to create citadel: %s\n", sqlite3_errmsg(db));
        return -1;
    }

    // Seed 5 Ferrengi traders as both players and ships
    for (int i = 0; i < 5; i++) {
        // Create the player (NPC) entry
        char sql_player[512];
        snprintf(sql_player, sizeof(sql_player),
                 "INSERT INTO players (name, passwd, credits, sector) "
                 "VALUES ('Ferrengi Trader %d', 'BOT', %d, %d);",
                 i + 1, (rand() % 10000) + 1000, longest_tunnel_sector); // 1-10k credits
        if (sqlite3_exec(db, sql_player, NULL, NULL, NULL) != SQLITE_OK) {
            fprintf(stderr, "create_ferringhi failed to create player %d: %s\n", i, sqlite3_errmsg(db));
            return -1;
        }

        // Get the ID of the new player
        sqlite3_int64 player_id = sqlite3_last_insert_rowid(db);

        // Randomly choose between a trader and a warship type
        int ship_type = (rand() % 100) < 80 ? merchant_freighter_id : ferengi_warship_id; // Use the dynamic IDs here
        int ship_fighters = (rand() % 1501) + 500; // 500-2000 fighters
        int ship_shields = (rand() % 4001) + 1000; // 1000-5000 shields
        int ship_holds = (rand() % 801) + 200; // 200-1000 holds

        // --- NEW CODE FOR POPULATING HOLDS ---
        int holds_to_fill = (rand() % (ship_holds - 1)) + 1;
        int ore = 0;
        int organics = 0;
        int equipment = 0;

        for (int h = 0; h < holds_to_fill; h++) {
            switch (rand() % 3) {
                case 0:
                    ore++;
                    break;
                case 1:
                    organics++;
                    break;
                case 2:
                    equipment++;
                    break;
            }
        }
        // --- END OF NEW CODE ---

        // Create the ship entry linked to the player
        char sql_ship[512];
        snprintf(sql_ship, sizeof(sql_ship),
                 "INSERT INTO ships (name, type, location, owner, holds, fighters, shields, holds_used, ore, organics, equipment) "
                 "VALUES ('Ferrengi Trader %d', %d, %d, %lld, %d, %d, %d, %d, %d, %d, %d);",
                 i + 1, ship_type, longest_tunnel_sector, player_id, ship_holds, ship_fighters, ship_shields, holds_to_fill, ore, organics, equipment);

        if (sqlite3_exec(db, sql_ship, NULL, NULL, NULL) != SQLITE_OK) {
            fprintf(stderr, "create_ferringhi failed to create ship %d: %s\n", i, sqlite3_errmsg(db));
            return -1;
        }
    }

    fprintf(stderr, "create_ferringhi: Placed at sector %d (end of a long tunnel).\n", longest_tunnel_sector);

    return 0;
}

int create_planets(void) {
    // The provided planet creation logic is incomplete and has a simple error.
    // Earth is already created by the schema, so this function is intentionally empty.
    return 0;
}

int create_imperial(void) {
    sqlite3 *db = db_get_handle();
    if (!db) {
        fprintf(stderr, "create_imperial: Failed to get DB handle\n");
        return -1;
    }

    struct twconfig *cfg = config_load();
    if (!cfg) {
        fprintf(stderr, "create_imperial: could not load config\n");
        return -1;
    }
    
    // Dynamically retrieve the NPC shiptype ID, filtered by can_purchase = 0
    int imperial_starship_id = get_npc_shiptype_id_by_name(db, "Imperial Starship (NPC)");

    if (imperial_starship_id == -1) {
        fprintf(stderr, "create_imperial: Failed to get required NPC shiptype ID.\n");
        return -1;
    }
    // Randomly place the Imperial Starship in a sector
    int imperial_sector = 11 + (rand() % (cfg->default_nodes - 10));
    free(cfg);

    // Create the "Imperial" player (NPC) entry
    char sql_player[512];
    snprintf(sql_player, sizeof(sql_player),
             "INSERT INTO players (name, passwd, credits, alignment) VALUES ('Imperial Starship', 'BOT', 10000000, 100);"); // Large credits and high alignment
    if (sqlite3_exec(db, sql_player, NULL, NULL, NULL) != SQLITE_OK) {
        fprintf(stderr, "create_imperial failed to create player: %s\n", sqlite3_errmsg(db));
        return -1;
    }
    sqlite3_int64 imperial_player_id = sqlite3_last_insert_rowid(db);

    // Create the "Imperial Starship" ship entry linked to the player
    char sql_ship[512];
    snprintf(sql_ship, sizeof(sql_ship),
             "INSERT INTO ships (name, type, location, owner, fighters, shields, holds, photons, genesis) "
             "VALUES ('Imperial Starship', %d, %d, %lld, 32000, 65000, 100, 100, 10);",
             imperial_starship_id, imperial_sector, imperial_player_id);
    if (sqlite3_exec(db, sql_ship, NULL, NULL, NULL) != SQLITE_OK) {
        fprintf(stderr, "create_imperial failed to create ship: %s\n", sqlite3_errmsg(db));
        return -1;
    }

    fprintf(stderr, "create_imperial: Imperial Starship placed at sector %d.\n", imperial_sector);

    return 0;
}
