#include <sqlite3.h>
#include <jansson.h>
#include <string.h> // For strcasecmp
#include <math.h> // For floor() function
#include <ctype.h> // For isalnum, isspace
#include "server_stardock.h"
#include "common.h"
#include "database.h"
#include "server_envelope.h"
#include "errors.h"
#include "server_ports.h" // For port types, etc.
#include "server_ships.h" // For h_get_active_ship_id
#include "server_cmds.h" // For send_error_response, send_json_response and send_error_and_return
#include "server_loop.h" // For idemp_fingerprint_json
#include "server_config.h"
#include "server_log.h" // For LOGE
#include "server_communication.h" // For server_broadcast_to_sector

struct tavern_settings g_tavern_cfg;

// Static Forward Declarations for helper functions
static bool has_sufficient_funds(sqlite3 *db, int player_id, long long required_amount);
static int update_player_credits_gambling(sqlite3 *db, int player_id, long long amount, bool is_win);
static int validate_bet_limits(sqlite3 *db, int player_id, long long bet_amount);

static bool is_player_in_tavern_sector(sqlite3 *db, int sector_id);
bool get_player_loan(sqlite3 *db, int player_id, long long *principal, int *interest_rate, int *due_date, int *is_defaulted);
static void sanitize_text(char *text, size_t max_len);


// Implementation for hardware.list RPC
int cmd_hardware_list(client_ctx_t *ctx, json_t *root) {
    sqlite3 *db = db_get_handle();
    int player_id = ctx->player_id;
    int ship_id = 0;
    int sector_id = 0;
    sqlite3_stmt *stmt = NULL; // Declare stmt here
    int port_type = 0; // Declare port_type here

    // 1. Authenticate player and get ship/sector context
    if (player_id <= 0) {
        send_enveloped_refused(ctx->fd, root, ERR_NOT_AUTHENTICATED, "Authentication required.", NULL);
        return 0;
    }
    ship_id = h_get_active_ship_id(db, player_id);
    if (ship_id <= 0) {
        send_enveloped_refused(ctx->fd, root, ERR_SHIP_NOT_FOUND, "No active ship found.", NULL);
        return 0;
    }
    sector_id = ctx->sector_id; // Get current sector from context

    // 2. Determine location type (Stardock, Class-0, or Other)
    char location_type[16] = "OTHER";
    int port_id = 0;

    const char *sql_loc_check = "SELECT id, type FROM ports WHERE sector = ? AND (type = " QUOTE(PORT_TYPE_STARDOCK) " OR type = " QUOTE(PORT_TYPE_CLASS0) ");"; // type 9 for Stardock, 0 for Class-0
    int rc = sqlite3_prepare_v2(db, sql_loc_check, -1, &stmt, NULL);

    if (rc == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, sector_id);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            port_type = sqlite3_column_int(stmt, 1);
            if (port_type == PORT_TYPE_STARDOCK) { // Stardock
                strncpy(location_type, LOCATION_STARDOCK, sizeof(location_type) - 1);
            } else if (port_type == PORT_TYPE_CLASS0) { // Class-0
                strncpy(location_type, LOCATION_CLASS0, sizeof(location_type) - 1);
            }
        }
        sqlite3_finalize(stmt);
        stmt = NULL;
    }

    if (port_id == 0) { // No Stardock or Class-0 port in this sector
        send_enveloped_ok(ctx->fd, root, "hardware.list_v1", json_pack("{s:i, s:s, s:o}", "sector_id", sector_id, "location_type", location_type, "items", json_array()));
        return 0;
    }

    // 3. Fetch current ship state and shiptype capabilities
    int current_holds = 0, current_fighters = 0, current_shields = 0;
    int current_genesis = 0, current_detonators = 0, current_probes = 0;
    int current_cloaks = 0; // cloaking_devices in ships table
    int has_transwarp = 0, has_planet_scanner = 0, has_long_range_scanner = 0;
    
    int max_holds = 0, max_fighters = 0, max_shields = 0;
    int max_genesis = 0, max_detonators_st = 0, max_probes_st = 0; // _st suffix for shiptype limits
    int can_transwarp = 0, can_planet_scan = 0, can_long_range_scan = 0;

    // Get ship current state
    const char *sql_ship_state = "SELECT s.holds, s.fighters, s.shields, s.genesis, s.detonators, s.probes, s.cloaking_devices, s.has_transwarp, s.has_planet_scanner, s.has_long_range_scanner, st.maxholds, st.maxfighters, st.maxshields, st.maxgenesis, st.max_detonators, st.max_probes, st.can_transwarp, st.can_planet_scan, st.can_long_range_scan, st.max_cloaks FROM ships s JOIN shiptypes st ON s.type_id = st.id WHERE s.id = ?;";
    rc = sqlite3_prepare_v2(db, sql_ship_state, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        send_enveloped_error(ctx->fd, root, ERR_DB_QUERY_FAILED, "Failed to get ship state.");
        return 0;
    }
    sqlite3_bind_int(stmt, 1, ship_id);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        current_holds = sqlite3_column_int(stmt, 0);
        current_fighters = sqlite3_column_int(stmt, 1);
        current_shields = sqlite3_column_int(stmt, 2);
        current_genesis = sqlite3_column_int(stmt, 3);
        current_detonators = sqlite3_column_int(stmt, 4);
        current_probes = sqlite3_column_int(stmt, 5);
        current_cloaks = sqlite3_column_int(stmt, 6); // cloaking_devices
        has_transwarp = sqlite3_column_int(stmt, 7);
        has_planet_scanner = sqlite3_column_int(stmt, 8);
        has_long_range_scanner = sqlite3_column_int(stmt, 9);

        max_holds = sqlite3_column_int(stmt, 10);
        max_fighters = sqlite3_column_int(stmt, 11);
        max_shields = sqlite3_column_int(stmt, 12);
        max_genesis = sqlite3_column_int(stmt, 13);
        max_detonators_st = sqlite3_column_int(stmt, 14);
        max_probes_st = sqlite3_column_int(stmt, 15);
        can_transwarp = sqlite3_column_int(stmt, 16);
        can_planet_scan = sqlite3_column_int(stmt, 17);
        can_long_range_scan = sqlite3_column_int(stmt, 18);
    }
    sqlite3_finalize(stmt);
    stmt = NULL;


    json_t *items_array = json_array();
    const char *sql_hardware = "SELECT code, name, price, max_per_ship, category FROM hardware_items WHERE enabled = 1 AND (? = '" LOCATION_STARDOCK "' OR (? = '" LOCATION_CLASS0 "' AND sold_in_class0 = 1))";
    rc = sqlite3_prepare_v2(db, sql_hardware, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        send_enveloped_error(ctx->fd, root, ERR_DB_QUERY_FAILED, "Failed to get hardware items.");
        return 0;
    }
    sqlite3_bind_text(stmt, 1, location_type, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, location_type, -1, SQLITE_STATIC);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *code = (const char*)sqlite3_column_text(stmt, 0);
        const char *name = (const char*)sqlite3_column_text(stmt, 1);
        int price = sqlite3_column_int(stmt, 2);
        int max_per_ship_hw = sqlite3_column_type(stmt, 3) == SQLITE_NULL ? -1 : sqlite3_column_int(stmt, 3); // -1 means use shiptype max
        const char *category = (const char*)sqlite3_column_text(stmt, 4);

        int max_purchase = 0;
        bool ship_has_capacity = true;
        bool item_supported = true;

        if (strcasecmp(category, HW_CATEGORY_FIGHTER) == 0) {
            max_purchase = MAX(0, max_fighters - current_fighters);
        } else if (strcasecmp(category, HW_CATEGORY_SHIELD) == 0) {
            max_purchase = MAX(0, max_shields - current_shields);
        } else if (strcasecmp(category, HW_CATEGORY_HOLD) == 0) {
            max_purchase = MAX(0, max_holds - current_holds);
        } else if (strcasecmp(category, HW_CATEGORY_SPECIAL) == 0) {
            if (strcasecmp(code, HW_ITEM_GENESIS) == 0) {
                int limit = (max_per_ship_hw != HW_MAX_PER_SHIP_DEFAULT && max_per_ship_hw < max_genesis) ? max_per_ship_hw : max_genesis;
                max_purchase = MAX(0, limit - current_genesis);
            } else if (strcasecmp(code, HW_ITEM_DETONATOR) == 0) {
                int limit = (max_per_ship_hw != HW_MAX_PER_SHIP_DEFAULT && max_per_ship_hw < max_detonators_st) ? max_per_ship_hw : max_detonators_st;
                max_purchase = MAX(0, limit - current_detonators);
            } else if (strcasecmp(code, HW_ITEM_PROBE) == 0) {
                int limit = (max_per_ship_hw != HW_MAX_PER_SHIP_DEFAULT && max_per_ship_hw < max_probes_st) ? max_per_ship_hw : max_probes_st;
                max_purchase = MAX(0, limit - current_probes);
            } else {
                item_supported = false; // Unknown special item
            }
        } else if (strcasecmp(category, HW_CATEGORY_MODULE) == 0) {
            if (strcasecmp(code, HW_ITEM_CLOAK) == 0) {
                max_purchase = (current_cloaks == 0) ? 1 : 0;
            } else if (strcasecmp(code, HW_ITEM_TWARP) == 0) {
                if (!can_transwarp) item_supported = false;
                max_purchase = (can_transwarp && has_transwarp == 0) ? 1 : 0;
            } else if (strcasecmp(code, HW_ITEM_PSCANNER) == 0) {
                if (!can_planet_scan) item_supported = false;
                max_purchase = (can_planet_scan && has_planet_scanner == 0) ? 1 : 0;
            } else if (strcasecmp(code, HW_ITEM_LSCANNER) == 0) {
                if (!can_long_range_scan) item_supported = false;
                max_purchase = (can_long_range_scan && has_long_range_scanner == 0) ? 1 : 0;
            } else {
                item_supported = false;
            }
        } else {
            item_supported = false;
        }

        
        if (max_purchase <= 0) ship_has_capacity = false;

        if (item_supported && (max_purchase > HW_MIN_QUANTITY || strcmp(category, HW_CATEGORY_MODULE) == 0) ) {
            json_t *item_obj = json_object();
            json_object_set_new(item_obj, "code", json_string(code));
            json_object_set_new(item_obj, "name", json_string(name));
            json_object_set_new(item_obj, "price", json_integer(price));
            json_object_set_new(item_obj, "max_purchase", json_integer(max_purchase));
            json_object_set_new(item_obj, "ship_has_capacity", json_boolean(ship_has_capacity));
            json_array_append_new(items_array, item_obj);
        }
    }
    sqlite3_finalize(stmt);

    send_enveloped_ok(ctx->fd, root, "hardware.list_v1", json_pack("{s:i, s:s, s:o}", "sector_id", sector_id, "location_type", location_type, "items", items_array));
    return 0;
}

// Implementation for hardware.buy RPC
int cmd_hardware_buy(client_ctx_t *ctx, json_t *root) {
    // This function was lost in a git restore, recovering from history.
    // The user has fixed most errors, so this is a placeholder for now.
    // I will need to get the correct implementation from the user if this is not right.
    send_enveloped_error(ctx->fd, root, ERR_NOT_IMPLEMENTED, "hardware.buy is not fully restored yet.");
    return 0;
}

// Implementation for shipyard.list RPC
int cmd_shipyard_list(client_ctx_t *ctx, json_t *root) {
    sqlite3 *db = db_get_handle();
    if (!ctx || ctx->player_id <= 0) {
        return send_error_response(ctx, root, ERR_NOT_AUTHENTICATED, "Authentication required.");
    }

    int player_id = ctx->player_id;
    int sector_id = ctx->sector_id;
    sqlite3_stmt *stmt = NULL;

    // Check if player is docked at a port of type 9 or 10 in the current sector
    const char *sql_loc = "SELECT p.id FROM ports p "
                          "JOIN ships s ON s.ported = p.id "
                          "JOIN players pl ON pl.ship = s.id "
                          "WHERE p.sector = ? AND pl.id = ? AND (p.type = 9 OR p.type = 10);";

    int rc = sqlite3_prepare_v2(db, sql_loc, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        return send_error_response(ctx, root, ERR_DB_QUERY_FAILED, "Failed to check shipyard location.");
    }
    sqlite3_bind_int(stmt, 1, sector_id);
    sqlite3_bind_int(stmt, 2, player_id);

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        return send_error_response(ctx, root, ERR_NOT_AT_SHIPYARD, "You are not docked at a shipyard.");
    }
    int port_id = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    stmt = NULL;

    // Load configuration
    struct twconfig *cfg = config_load();
    if (!cfg) {
        return send_error_response(ctx, root, ERR_SERVER_ERROR, "Could not load server configuration.");
    }

    // Get current player and ship info
    const char *sql_info = "SELECT "
                           "p.alignment, p.commission, p.experience, "
                           "s.id, s.type_id, st.name, st.basecost, "
                           "s.fighters, s.shields, s.mines, s.limpets, s.genesis, s.detonators, s.probes, s.cloaking_devices, "
                           "s.colonists, s.ore, s.organics, s.equipment, "
                           "s.has_transwarp, s.has_planet_scanner, s.has_long_range_scanner "
                           "FROM players p JOIN ships s ON p.ship = s.id JOIN shiptypes st ON s.type_id = st.id "
                           "WHERE p.id = ?;";
    
    rc = sqlite3_prepare_v2(db, sql_info, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        free(cfg);
        return send_error_response(ctx, root, ERR_DB_QUERY_FAILED, "Failed to fetch player/ship info.");
    }
    sqlite3_bind_int(stmt, 1, player_id);

    if (sqlite3_step(stmt) != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        free(cfg);
        return send_error_response(ctx, root, ERR_SHIP_NOT_FOUND, "Could not find player's active ship.");
    }

    // Current player/ship state
    int player_alignment = sqlite3_column_int(stmt, 0);
    int player_commission = sqlite3_column_int(stmt, 1);
    int player_experience = sqlite3_column_int(stmt, 2);
    long long current_ship_basecost = sqlite3_column_int64(stmt, 6);
    int current_fighters = sqlite3_column_int(stmt, 7);
    int current_shields = sqlite3_column_int(stmt, 8);
    int current_mines = sqlite3_column_int(stmt, 9);
    int current_limpets = sqlite3_column_int(stmt, 10);
    int current_genesis = sqlite3_column_int(stmt, 11);
    int current_detonators = sqlite3_column_int(stmt, 12);
    int current_probes = sqlite3_column_int(stmt, 13);
    int current_cloaks = sqlite3_column_int(stmt, 14);
    long long current_cargo = sqlite3_column_int64(stmt, 15) + sqlite3_column_int64(stmt, 16) + sqlite3_column_int64(stmt, 17) + sqlite3_column_int64(stmt, 18);
    int has_transwarp = sqlite3_column_int(stmt, 19);
    int has_planet_scanner = sqlite3_column_int(stmt, 20);
    int has_long_range_scanner = sqlite3_column_int(stmt, 21);
    
    long trade_in_value = floor(current_ship_basecost * (cfg->shipyard_trade_in_factor_bp / 10000.0));

    json_t *response_data = json_object();
    json_object_set_new(response_data, "sector_id", json_integer(sector_id));
    json_object_set_new(response_data, "is_shipyard", json_true());
    
    json_t *current_ship_obj = json_object();
    json_object_set_new(current_ship_obj, "type", json_string((const char*)sqlite3_column_text(stmt, 5)));
    json_object_set_new(current_ship_obj, "base_price", json_integer(current_ship_basecost));
    json_object_set_new(current_ship_obj, "trade_in_value", json_integer(trade_in_value));
    json_object_set_new(response_data, "current_ship", current_ship_obj);
    
    sqlite3_finalize(stmt);
    stmt = NULL;

    // Fetch shipyard inventory and build "available" array
    json_t *available_array = json_array();
    const char *sql_inventory = "SELECT si.ship_type_id, st.name, st.basecost, st.required_alignment, st.required_commission, st.required_experience, st.maxholds, st.maxfighters, st.maxshields, st.maxgenesis, st.max_detonators, st.max_probes, st.max_cloaks, st.can_transwarp, st.can_planet_scan, st.can_long_range_scan, st.maxmines, st.maxlimpets "
                                "FROM shipyard_inventory si JOIN shiptypes st ON si.ship_type_id = st.id "
                                "WHERE si.port_id = ? AND si.enabled = 1 AND st.enabled = 1;";
    
    rc = sqlite3_prepare_v2(db, sql_inventory, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        json_decref(response_data);
        free(cfg);
        return send_error_response(ctx, root, ERR_DB_QUERY_FAILED, "Failed to fetch shipyard inventory.");
    }
    sqlite3_bind_int(stmt, 1, port_id);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        json_t *ship_obj = json_object();
        json_t *reasons_array = json_array();
        bool eligible = true;

        const char *type_name = (const char*)sqlite3_column_text(stmt, 1);
        long long new_ship_basecost = sqlite3_column_int64(stmt, 2);
        long long net_cost = new_ship_basecost - trade_in_value;
        
        json_object_set_new(ship_obj, "type", json_string(type_name));
        json_object_set_new(ship_obj, "name", json_string(type_name)); // Using type_name as name for now
        json_object_set_new(ship_obj, "base_price", json_integer(new_ship_basecost));
        json_object_set_new(ship_obj, "shipyard_price", json_integer(new_ship_basecost)); // No markup for now
        json_object_set_new(ship_obj, "trade_in_value", json_integer(trade_in_value));
        json_object_set_new(ship_obj, "net_cost", json_integer(net_cost));

        // Eligibility Checks
        if (sqlite3_column_type(stmt, 3) != SQLITE_NULL && player_alignment < sqlite3_column_int(stmt, 3)) {
            eligible = false;
            json_array_append_new(reasons_array, json_string("alignment_too_low"));
        }
        if (sqlite3_column_type(stmt, 4) != SQLITE_NULL && player_commission < sqlite3_column_int(stmt, 4)) {
            eligible = false;
            json_array_append_new(reasons_array, json_string("commission_too_low"));
        }
        if (sqlite3_column_type(stmt, 5) != SQLITE_NULL && player_experience < sqlite3_column_int(stmt, 5)) {
            eligible = false;
            json_array_append_new(reasons_array, json_string("experience_too_low"));
        }
        if (cfg->shipyard_require_cargo_fit && current_cargo > sqlite3_column_int64(stmt, 6)) {
            eligible = false;
            json_array_append_new(reasons_array, json_string("cargo_would_not_fit"));
        }
        if (cfg->shipyard_require_fighters_fit && current_fighters > sqlite3_column_int(stmt, 7)) {
            eligible = false;
            json_array_append_new(reasons_array, json_string("fighters_exceed_capacity"));
        }
        if (cfg->shipyard_require_shields_fit && current_shields > sqlite3_column_int(stmt, 8)) {
            eligible = false;
            json_array_append_new(reasons_array, json_string("shields_exceed_capacity"));
        }
        if (cfg->shipyard_require_hardware_compat) {
            if (current_genesis > sqlite3_column_int(stmt, 9)) { eligible = false; json_array_append_new(reasons_array, json_string("genesis_exceed_capacity")); }
            if (current_detonators > sqlite3_column_int(stmt, 10)) { eligible = false; json_array_append_new(reasons_array, json_string("detonators_exceed_capacity")); }
            if (current_probes > sqlite3_column_int(stmt, 11)) { eligible = false; json_array_append_new(reasons_array, json_string("probes_exceed_capacity")); }
            if (current_cloaks > sqlite3_column_int(stmt, 12)) { eligible = false; json_array_append_new(reasons_array, json_string("cloak_not_supported")); }
            if (has_transwarp && !sqlite3_column_int(stmt, 13)) { eligible = false; json_array_append_new(reasons_array, json_string("transwarp_not_supported")); }
            if (has_planet_scanner && !sqlite3_column_int(stmt, 14)) { eligible = false; json_array_append_new(reasons_array, json_string("planet_scan_not_supported")); }
            if (has_long_range_scanner && !sqlite3_column_int(stmt, 15)) { eligible = false; json_array_append_new(reasons_array, json_string("long_range_not_supported")); }
            if (current_mines > sqlite3_column_int(stmt, 16)) { eligible = false; json_array_append_new(reasons_array, json_string("mines_exceed_capacity")); }
            if (current_limpets > sqlite3_column_int(stmt, 17)) { eligible = false; json_array_append_new(reasons_array, json_string("limpets_exceed_capacity")); }
        }

        json_object_set_new(ship_obj, "eligible", json_boolean(eligible));
        json_object_set_new(ship_obj, "reasons", reasons_array);
        json_array_append_new(available_array, ship_obj);
    }
    sqlite3_finalize(stmt);

    json_object_set_new(response_data, "available", available_array);
    send_enveloped_ok(ctx->fd, root, "shipyard.list_v1", response_data);

    free(cfg);
    return 0;
}

// Implementation for shipyard.upgrade RPC
int cmd_shipyard_upgrade(client_ctx_t *ctx, json_t *root) {
    sqlite3 *db = db_get_handle();
    if (!ctx || ctx->player_id <= 0) {
        return send_error_response(ctx, root, ERR_NOT_AUTHENTICATED, "Authentication required.");
    }

    json_t *data = json_object_get(root, "data");
    if (!data) {
        return send_error_response(ctx, root, ERR_BAD_REQUEST, "Missing data payload.");
    }

    int new_type_id = 0;
    const char *new_ship_name = NULL;

    if (!json_get_int_flexible(data, "new_type_id", &new_type_id) || new_type_id <= 0) {
        return send_error_response(ctx, root, ERR_MISSING_FIELD, "Missing or invalid 'new_type_id'.");
    }
    new_ship_name = json_get_string_or_null(data, "new_ship_name");
    if (!new_ship_name || strlen(new_ship_name) == 0) {
        return send_error_response(ctx, root, ERR_MISSING_FIELD, "Missing or invalid 'new_ship_name'.");
    }

    // Begin transaction
    if (sqlite3_exec(db, "BEGIN IMMEDIATE TRANSACTION;", NULL, NULL, NULL) != SQLITE_OK) {
        return send_error_response(ctx, root, ERR_DB, "Failed to start transaction.");
    }

    // Re-validate location
    int port_id = 0;
    sqlite3_stmt *stmt = NULL;
    const char *sql_loc = "SELECT p.id FROM ports p "
                          "JOIN ships s ON s.ported = p.id "
                          "JOIN players pl ON pl.ship = s.id "
                          "WHERE p.sector = ? AND pl.id = ? AND (p.type = 9 OR p.type = 10);";

    int rc = sqlite3_prepare_v2(db, sql_loc, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
        return send_error_response(ctx, root, ERR_DB_QUERY_FAILED, "Failed to check shipyard location.");
    }
    sqlite3_bind_int(stmt, 1, ctx->sector_id);
    sqlite3_bind_int(stmt, 2, ctx->player_id);
    rc = sqlite3_step(stmt);
    if (rc != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
        return send_error_response(ctx, root, ERR_NOT_AT_SHIPYARD, "You are not docked at a shipyard.");
    }
    port_id = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    stmt = NULL;

    struct twconfig *cfg = config_load();
    if (!cfg) {
        sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
        return send_error_response(ctx, root, ERR_SERVER_ERROR, "Could not load server configuration.");
    }
    
    if (strlen(new_ship_name) > cfg->max_ship_name_length) {
        free(cfg);
        sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
        return send_error_response(ctx, root, ERR_INVALID_ARG, "Ship name is too long.");
    }

    const char *sql_info = "SELECT "
        "p.alignment, p.commission, p.experience, s.credits, "
        "s.fighters, s.shields, s.mines, s.limpets, s.genesis, s.detonators, s.probes, s.cloaking_devices, "
        "s.colonists, s.ore, s.organics, s.equipment, "
        "s.has_transwarp, s.has_planet_scanner, s.has_long_range_scanner, s.id, s.type_id, st.basecost "
        "FROM players p JOIN ships s ON p.ship = s.id JOIN shiptypes st ON s.type_id = st.id "
        "WHERE p.id = ?;";
    rc = sqlite3_prepare_v2(db, sql_info, -1, &stmt, NULL);
    if (rc != SQLITE_OK || sqlite3_bind_int(stmt, 1, ctx->player_id) != SQLITE_OK || sqlite3_step(stmt) != SQLITE_ROW) {
        if(stmt) sqlite3_finalize(stmt);
        free(cfg);
        sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
        return send_error_response(ctx, root, ERR_DB_QUERY_FAILED, "Failed to fetch current player/ship state.");
    }
    int player_alignment = sqlite3_column_int(stmt, 0);
    int player_commission = sqlite3_column_int(stmt, 1);
    int player_experience = sqlite3_column_int(stmt, 2);
    long long current_credits = sqlite3_column_int64(stmt, 3);
    int current_fighters = sqlite3_column_int(stmt, 4);
    int current_shields = sqlite3_column_int(stmt, 5);
    int current_mines = sqlite3_column_int(stmt, 6);
    int current_limpets = sqlite3_column_int(stmt, 7);
    int current_genesis = sqlite3_column_int(stmt, 8);
    int current_detonators = sqlite3_column_int(stmt, 9);
    int current_probes = sqlite3_column_int(stmt, 10);
    int current_cloaks = sqlite3_column_int(stmt, 11);
    long long current_cargo = sqlite3_column_int64(stmt, 12) + sqlite3_column_int64(stmt, 13) + sqlite3_column_int64(stmt, 14) + sqlite3_column_int64(stmt, 15);
    int has_transwarp = sqlite3_column_int(stmt, 16);
    int has_planet_scanner = sqlite3_column_int(stmt, 17);
    int has_long_range_scanner = sqlite3_column_int(stmt, 18);
    int current_ship_id = sqlite3_column_int(stmt, 19);
    long long old_ship_basecost = sqlite3_column_int64(stmt, 21);
    sqlite3_finalize(stmt);
    stmt = NULL;

    const char *sql_target_type = "SELECT basecost, required_alignment, required_commission, required_experience, maxholds, maxfighters, maxshields, maxgenesis, max_detonators, max_probes, max_cloaks, can_transwarp, can_planet_scan, can_long_range_scan, maxmines, maxlimpets, name FROM shiptypes WHERE id = ? AND enabled = 1;";
    rc = sqlite3_prepare_v2(db, sql_target_type, -1, &stmt, NULL);
    if (rc != SQLITE_OK || sqlite3_bind_int(stmt, 1, new_type_id) != SQLITE_OK || sqlite3_step(stmt) != SQLITE_ROW) {
        if(stmt) sqlite3_finalize(stmt);
        free(cfg);
        sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
        return send_error_response(ctx, root, ERR_SHIPYARD_INVALID_SHIP_TYPE, "Target ship type not found or is not available.");
    }
    
    bool eligible_for_upgrade = true;
    if (sqlite3_column_type(stmt, 1) != SQLITE_NULL && player_alignment < sqlite3_column_int(stmt, 1)) { eligible_for_upgrade = false; }
    if (sqlite3_column_type(stmt, 2) != SQLITE_NULL && player_commission < sqlite3_column_int(stmt, 2)) { eligible_for_upgrade = false; }
    if (sqlite3_column_type(stmt, 3) != SQLITE_NULL && player_experience < sqlite3_column_int(stmt, 3)) { eligible_for_upgrade = false; }
    
    int new_max_holds = sqlite3_column_int(stmt, 4);
    if (cfg->shipyard_require_cargo_fit && current_cargo > new_max_holds) { eligible_for_upgrade = false; }

    int new_max_fighters = sqlite3_column_int(stmt, 5);
    if (cfg->shipyard_require_fighters_fit && current_fighters > new_max_fighters) { eligible_for_upgrade = false; }
    
    int new_max_shields = sqlite3_column_int(stmt, 6);
    if (cfg->shipyard_require_shields_fit && current_shields > new_max_shields) { eligible_for_upgrade = false; }

    if (cfg->shipyard_require_hardware_compat) {
        if (current_genesis > sqlite3_column_int(stmt, 7)) { eligible_for_upgrade = false; }
        if (current_detonators > sqlite3_column_int(stmt, 8)) { eligible_for_upgrade = false; }
        if (current_probes > sqlite3_column_int(stmt, 9)) { eligible_for_upgrade = false; }
        if (current_cloaks > sqlite3_column_int(stmt, 10)) { eligible_for_upgrade = false; }
        if (has_transwarp && !sqlite3_column_int(stmt, 11)) { eligible_for_upgrade = false; }
        if (has_planet_scanner && !sqlite3_column_int(stmt, 12)) { eligible_for_upgrade = false; }
        if (has_long_range_scanner && !sqlite3_column_int(stmt, 13)) { eligible_for_upgrade = false; }
        if (current_mines > sqlite3_column_int(stmt, 14)) { eligible_for_upgrade = false; }
        if (current_limpets > sqlite3_column_int(stmt, 15)) { eligible_for_upgrade = false; }
    }
    
    if (!eligible_for_upgrade) {
        sqlite3_finalize(stmt);
        free(cfg);
        sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
        return send_error_response(ctx, root, ERR_SHIPYARD_REQUIREMENTS_NOT_MET, "Ship upgrade requirements not met (capacity or capabilities).");
    }
    
    long long new_shiptype_basecost = sqlite3_column_int64(stmt, 0);
    sqlite3_finalize(stmt);
    stmt = NULL;

    long trade_in_value = floor(old_ship_basecost * (cfg->shipyard_trade_in_factor_bp / 10000.0));
    long tax = floor(new_shiptype_basecost * (cfg->shipyard_tax_bp / 10000.0));
    long long final_cost = new_shiptype_basecost - trade_in_value + tax;

    if (current_credits < final_cost) {
        free(cfg);
        sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
        return send_error_response(ctx, root, ERR_SHIPYARD_INSUFFICIENT_FUNDS, "Insufficient credits for ship upgrade.");
    }

    const char *sql_update = "UPDATE ships SET type_id = ?, name = ?, credits = credits - ? WHERE id = ?;";
    rc = sqlite3_prepare_v2(db, sql_update, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        free(cfg);
        sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
        return send_error_response(ctx, root, ERR_DB_QUERY_FAILED, "Failed to prepare ship update.");
    }
    sqlite3_bind_int(stmt, 1, new_type_id);
    sqlite3_bind_text(stmt, 2, new_ship_name, -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 3, final_cost);
    sqlite3_bind_int(stmt, 4, current_ship_id);
    
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        free(cfg);
        sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
        return send_error_response(ctx, root, ERR_DB, "Failed to execute ship upgrade.");
    }
    sqlite3_finalize(stmt);

    if (sqlite3_exec(db, "COMMIT;", NULL, NULL, NULL) != SQLITE_OK) {
        free(cfg);
        return send_error_response(ctx, root, ERR_DB, "Failed to commit transaction.");
    }

    json_t *event_payload = json_pack("{s:i, s:i}", "player_id", ctx->player_id, "new_type_id", new_type_id);
    db_log_engine_event(time(NULL), "shipyard.upgrade", "player", ctx->player_id, ctx->sector_id, event_payload, NULL);
    
    json_t *response_data = json_pack("{s:s}", "status", "success");
    free(cfg);
    return 0;
}

// Function to load tavern settings from the database
int tavern_settings_load(void) {
    sqlite3 *db = db_get_handle();
    sqlite3_stmt *stmt = NULL;

    const char *sql = "SELECT max_bet_per_transaction, daily_max_wager, enable_dynamic_wager_limit, graffiti_max_posts, notice_expires_days, buy_round_cost, buy_round_alignment_gain, loan_shark_enabled FROM tavern_settings WHERE id = 1;";

    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        LOGE("Tavern settings prepare error: %s", sqlite3_errmsg(db));
        return -1;
    }

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        g_tavern_cfg.max_bet_per_transaction = sqlite3_column_int(stmt, 0);
        g_tavern_cfg.daily_max_wager = sqlite3_column_int(stmt, 1);
        g_tavern_cfg.enable_dynamic_wager_limit = sqlite3_column_int(stmt, 2);
        g_tavern_cfg.graffiti_max_posts = sqlite3_column_int(stmt, 3);
        g_tavern_cfg.notice_expires_days = sqlite3_column_int(stmt, 4);
        g_tavern_cfg.buy_round_cost = sqlite3_column_int(stmt, 5);
        g_tavern_cfg.buy_round_alignment_gain = sqlite3_column_int(stmt, 6);
        g_tavern_cfg.loan_shark_enabled = sqlite3_column_int(stmt, 7);
    } else {
        LOGE("Tavern settings not found in database. Using defaults.");
        // Set default values if not found in DB
        g_tavern_cfg.max_bet_per_transaction = 5000;
        g_tavern_cfg.daily_max_wager = 50000;
        g_tavern_cfg.enable_dynamic_wager_limit = 0;
        g_tavern_cfg.graffiti_max_posts = 100;
        g_tavern_cfg.notice_expires_days = 7;
        g_tavern_cfg.buy_round_cost = 1000;
        g_tavern_cfg.buy_round_alignment_gain = 5;
        g_tavern_cfg.loan_shark_enabled = 1;
    }

    sqlite3_finalize(stmt);
    return 0;
}

// Helper function to check if a player is in a tavern sector
static bool is_player_in_tavern_sector(sqlite3 *db, int sector_id) {
    sqlite3_stmt *stmt = NULL;
    const char *sql = "SELECT 1 FROM taverns WHERE sector_id = ? AND enabled = 1;";
    bool in_tavern = false;

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        LOGE("is_player_in_tavern_sector: Failed to prepare statement: %s", sqlite3_errmsg(db));
        return false;
    }
    sqlite3_bind_int(stmt, 1, sector_id);

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        in_tavern = true;
    }

    sqlite3_finalize(stmt);
    return in_tavern;
}

// Helper to retrieve player loan details
bool get_player_loan(sqlite3 *db, int player_id, long long *principal, int *interest_rate, int *due_date, int *is_defaulted) {
    sqlite3_stmt *stmt = NULL;
    const char *sql = "SELECT principal, interest_rate, due_date, is_defaulted FROM tavern_loans WHERE player_id = ?;";
    bool found = false;

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        LOGE("get_player_loan: Failed to prepare statement: %s", sqlite3_errmsg(db));
        return false;
    }
    sqlite3_bind_int(stmt, 1, player_id);

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        if (principal) *principal = sqlite3_column_int64(stmt, 0);
        if (interest_rate) *interest_rate = sqlite3_column_int(stmt, 1);
        if (due_date) *due_date = sqlite3_column_int(stmt, 2);
        if (is_defaulted) *is_defaulted = sqlite3_column_int(stmt, 3);
        found = true;
    }

    sqlite3_finalize(stmt);
    return found;
}

// Helper function to sanitize text input
static void sanitize_text(char *text, size_t max_len) {
    if (!text) return;
    size_t len = strnlen(text, max_len);
    for (size_t i = 0; i < len; i++) {
        // Allow basic alphanumeric, spaces, and common punctuation
        if (!isalnum((unsigned char)text[i]) && !isspace((unsigned char)text[i]) &&
            strchr(".,!?-:;'\"()[]{}", text[i]) == NULL) {
            text[i] = '_'; // Replace disallowed characters
        }
    }
    // Ensure null-termination
    text[len > (max_len - 1) ? (max_len - 1) : len] = '\0';
}



// Helper function to validate and apply bet limits
// Returns 0 on success, -1 if bet exceeds transaction limit, -2 if bet exceeds daily limit, -3 if exceeds dynamic limit
static int validate_bet_limits(sqlite3 *db, int player_id, long long bet_amount) {
    // Check max bet per transaction
    if (bet_amount > g_tavern_cfg.max_bet_per_transaction) {
        return -1;
    }

    // Check daily maximum wager - Placeholder for future implementation
    // This will require tracking daily wagers in the database.
    // For now, this check is a no-op, always returning success for this specific limit.
    // A robust solution would involve a `player_daily_wager` table and a cron job to reset it.
    
    // Check dynamic wager limit (if enabled)
    if (g_tavern_cfg.enable_dynamic_wager_limit) {
        long long player_credits = 0;
        sqlite3_stmt *stmt = NULL;
        const char *sql_credits = "SELECT credits FROM players WHERE id = ?;";
        if (sqlite3_prepare_v2(db, sql_credits, -1, &stmt, NULL) == SQLITE_OK) {
            sqlite3_bind_int(stmt, 1, player_id);
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                player_credits = sqlite3_column_int64(stmt, 0);
            }
            sqlite3_finalize(stmt);
        } else {
             LOGE("validate_bet_limits: Failed to prepare credits statement: %s", sqlite3_errmsg(db));
             // If we can't get credits, we can't apply dynamic limit, so fail safe
             return -3; 
        }
        
        // Example dynamic limit: bet cannot exceed 10% of liquid credits
        if (bet_amount > (player_credits / 10)) {
            return -3; // Exceeds dynamic wager limit
        }
    }

    return 0; // Success
}

// Function to handle player credit changes for gambling (deducts bet, adds winnings)
static int update_player_credits_gambling(sqlite3 *db, int player_id, long long amount, bool is_win) {
    const char *sql_update = is_win ?
        "UPDATE players SET credits = credits + ? WHERE id = ?;" :
        "UPDATE players SET credits = credits - ? WHERE id = ?;";
    sqlite3_stmt *stmt = NULL;
    int rc = -1;

    if (sqlite3_prepare_v2(db, sql_update, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, amount);
        sqlite3_bind_int(stmt, 2, player_id);
        if (sqlite3_step(stmt) == SQLITE_DONE) {
            rc = 0; // Success
        } else {
            LOGE("update_player_credits_gambling: Failed to update player credits: %s", sqlite3_errmsg(db));
        }
        sqlite3_finalize(stmt);
    } else {
        LOGE("update_player_credits_gambling: Failed to prepare statement: %s", sqlite3_errmsg(db));
    }
    return rc;
}

// Helper to check for sufficient funds
static bool has_sufficient_funds(sqlite3 *db, int player_id, long long required_amount) {
    long long player_credits = 0;
    sqlite3_stmt *stmt = NULL;
    const char *sql = "SELECT credits FROM players WHERE id = ?;";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, player_id);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            player_credits = sqlite3_column_int64(stmt, 0);
        }
        sqlite3_finalize(stmt);
    } else {
        LOGE("has_sufficient_funds: Failed to prepare credits statement: %s", sqlite3_errmsg(db));
        return false;
    }
    return player_credits >= required_amount;
}

// Helper to check for loan default
bool check_loan_default(sqlite3 *db, int player_id, int current_time) {
    long long principal = 0;
    int due_date = 0;
    int is_defaulted = 0;

    if (get_player_loan(db, player_id, &principal, NULL, &due_date, &is_defaulted) == SQLITE_OK) {
        if (is_defaulted == 0 && current_time > due_date && principal > 0) {
            // Mark as defaulted
            const char *sql_default = "UPDATE tavern_loans SET is_defaulted = 1 WHERE player_id = ?;";
            sqlite3_stmt *stmt = NULL;
            if (sqlite3_prepare_v2(db, sql_default, -1, &stmt, NULL) == SQLITE_OK) {
                sqlite3_bind_int(stmt, 1, player_id);
                if (sqlite3_step(stmt) != SQLITE_DONE) {
                    LOGE("check_loan_default: Failed to mark loan as defaulted: %s", sqlite3_errmsg(db));
                }
                sqlite3_finalize(stmt);
            } else {
                LOGE("check_loan_default: Failed to prepare default statement: %s", sqlite3_errmsg(db));
            }
            return true; // Just defaulted
        } else if (is_defaulted == 1) {
            return true; // Already defaulted
        }
    }
    return false; // Not defaulted or no loan
}

// Helper to apply interest to a loan
int apply_loan_interest(sqlite3 *db, int player_id, long long current_principal, int interest_rate_bp) {
    long long interest_amount = (current_principal * interest_rate_bp) / 10000; // interest_rate_bp is basis points
    long long new_principal = current_principal + interest_amount;

    sqlite3_stmt *stmt = NULL;
    const char *sql_update = "UPDATE tavern_loans SET principal = ? WHERE player_id = ?;";
    int rc = sqlite3_prepare_v2(db, sql_update, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        LOGE("apply_loan_interest: Failed to prepare statement: %s", sqlite3_errmsg(db));
        return rc;
    }
    sqlite3_bind_int64(stmt, 1, new_principal);
    sqlite3_bind_int(stmt, 2, player_id);
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        LOGE("apply_loan_interest: Failed to update loan principal for player %d: %s", player_id, sqlite3_errmsg(db));
        rc = sqlite3_errcode(db);
    } else {
        rc = SQLITE_OK;
    }
    sqlite3_finalize(stmt);
    return rc;
}


// Implementation for tavern.lottery.buy_ticket RPC
int cmd_tavern_lottery_buy_ticket(client_ctx_t *ctx, json_t *root) {
    sqlite3 *db = db_get_handle();
    if (!ctx || ctx->player_id <= 0) {
        return send_error_response(ctx, root, ERR_NOT_AUTHENTICATED, "Authentication required.");
    }
    if (!is_player_in_tavern_sector(db, ctx->sector_id)) {
        return send_error_response(ctx, root, ERR_NOT_AT_TAVERN, "You are not in a tavern sector.");
    }

    json_t *data = json_object_get(root, "data");
    if (!data) {
        return send_error_response(ctx, root, ERR_BAD_REQUEST, "Missing data payload.");
    }

    int ticket_number = 0;
    if (!json_get_int_flexible(data, "number", &ticket_number) || ticket_number <= 0 || ticket_number > 999) {
        return send_error_response(ctx, root, ERR_INVALID_ARG, "Lottery ticket number must be between 1 and 999.");
    }

    // Determine ticket price (example: 100 credits)
    long long ticket_price = 100; // This could be configurable in tavern_settings

    // Validate bet limits
    int limit_check = validate_bet_limits(db, ctx->player_id, ticket_price);
    if (limit_check == -1) {
        return send_error_response(ctx, root, ERR_TAVERN_BET_TOO_HIGH, "Bet exceeds maximum per transaction.");
    } else if (limit_check == -2) {
        return send_error_response(ctx, root, ERR_TAVERN_DAILY_WAGER_EXCEEDED, "Daily wager limit exceeded.");
    } else if (limit_check == -3) {
        return send_error_response(ctx, root, ERR_TAVERN_DYNAMIC_WAGER_EXCEEDED, "Dynamic wager limit exceeded based on liquid assets.");
    }

    // Check player funds
    if (!has_sufficient_funds(db, ctx->player_id, ticket_price)) {
        return send_error_response(ctx, root, ERR_INSUFFICIENT_FUNDS, "Insufficient credits to buy lottery ticket.");
    }

    // Get current draw date
    char draw_date_str[32];
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    strftime(draw_date_str, sizeof(draw_date_str), "%Y-%m-%d", tm_info);

    // Deduct ticket price and insert ticket
    // This should ideally be wrapped in a transaction, but per user instruction, we avoid explicit BEGIN/COMMIT.
    // The calling context should manage the transaction.
    int rc = update_player_credits_gambling(db, ctx->player_id, ticket_price, false); // Deduct
    if (rc != 0) {
        return send_error_response(ctx, root, ERR_DB, "Failed to deduct credits for lottery ticket.");
    }

    const char *sql_insert_ticket = "INSERT INTO tavern_lottery_tickets (draw_date, player_id, number, cost, purchased_at) VALUES (?, ?, ?, ?, ?);";
    sqlite3_stmt *stmt = NULL;
    rc = sqlite3_prepare_v2(db, sql_insert_ticket, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        // Rollback credits if this fails and no explicit transaction is used
        update_player_credits_gambling(db, ctx->player_id, ticket_price, true); // Refund credits
        return send_error_response(ctx, root, ERR_DB_QUERY_FAILED, "Failed to prepare lottery ticket insert.");
    }
    sqlite3_bind_text(stmt, 1, draw_date_str, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, ctx->player_id);
    sqlite3_bind_int(stmt, 3, ticket_number);
    sqlite3_bind_int64(stmt, 4, ticket_price);
    sqlite3_bind_int(stmt, 5, (int)now);

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        update_player_credits_gambling(db, ctx->player_id, ticket_price, true); // Refund credits
        return send_error_response(ctx, root, ERR_DB, "Failed to insert lottery ticket.");
    }
    sqlite3_finalize(stmt);

    json_t *response_data = json_pack("{s:s, s:i, s:s}", "status", "Ticket purchased", "ticket_number", ticket_number, "draw_date", draw_date_str);
    send_enveloped_ok(ctx->fd, root, "tavern.lottery.buy_ticket_v1", response_data);
    return 0;
}

// Implementation for tavern.lottery.status RPC
int cmd_tavern_lottery_status(client_ctx_t *ctx, json_t *root) {
    sqlite3 *db = db_get_handle();
    if (!ctx || ctx->player_id <= 0) {
        return send_error_response(ctx, root, ERR_NOT_AUTHENTICATED, "Authentication required.");
    }
    if (!is_player_in_tavern_sector(db, ctx->sector_id)) {
        return send_error_response(ctx, root, ERR_NOT_AT_TAVERN, "You are not in a tavern sector.");
    }

    json_t *response_data = json_object();
    json_object_set_new(response_data, "draw_date", json_null());
    json_object_set_new(response_data, "winning_number", json_null());
    json_object_set_new(response_data, "jackpot", json_integer(0));
    json_object_set_new(response_data, "player_tickets", json_array());

    // Get current draw date
    char draw_date_str[32];
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    strftime(draw_date_str, sizeof(draw_date_str), "%Y-%m-%d", tm_info);
    json_object_set_new(response_data, "current_draw_date", json_string(draw_date_str));


    // Query current lottery state
    sqlite3_stmt *stmt = NULL;
    const char *sql_state = "SELECT draw_date, winning_number, jackpot FROM tavern_lottery_state WHERE draw_date = ?;";
    int rc = sqlite3_prepare_v2(db, sql_state, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        json_decref(response_data);
        return send_error_response(ctx, root, ERR_DB_QUERY_FAILED, "Failed to prepare lottery state query.");
    }
    sqlite3_bind_text(stmt, 1, draw_date_str, -1, SQLITE_STATIC);

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        json_object_set_new(response_data, "draw_date", json_string((const char*)sqlite3_column_text(stmt, 0)));
        if (sqlite3_column_type(stmt, 1) != SQLITE_NULL) {
            json_object_set_new(response_data, "winning_number", json_integer(sqlite3_column_int(stmt, 1)));
        }
        json_object_set_new(response_data, "jackpot", json_integer(sqlite3_column_int64(stmt, 2)));
    }
    sqlite3_finalize(stmt);
    stmt = NULL;

    // Query player's tickets for the current draw
    json_t *player_tickets_array = json_array();
    const char *sql_player_tickets = "SELECT number, cost, purchased_at FROM tavern_lottery_tickets WHERE player_id = ? AND draw_date = ?;";
    rc = sqlite3_prepare_v2(db, sql_player_tickets, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        json_decref(response_data);
        return send_error_response(ctx, root, ERR_DB_QUERY_FAILED, "Failed to prepare player tickets query.");
    }
    sqlite3_bind_int(stmt, 1, ctx->player_id);
    sqlite3_bind_text(stmt, 2, draw_date_str, -1, SQLITE_STATIC);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        json_t *ticket_obj = json_object();
        json_object_set_new(ticket_obj, "number", json_integer(sqlite3_column_int(stmt, 0)));
        json_object_set_new(ticket_obj, "cost", json_integer(sqlite3_column_int64(stmt, 1)));
        json_object_set_new(ticket_obj, "purchased_at", json_integer(sqlite3_column_int(stmt, 2)));
        json_array_append_new(player_tickets_array, ticket_obj);
    }
    sqlite3_finalize(stmt);
    json_object_set_new(response_data, "player_tickets", player_tickets_array);

    send_enveloped_ok(ctx->fd, root, "tavern.lottery.status_v1", response_data);
    return 0;
}


// Implementation for tavern.deadpool.place_bet RPC
int cmd_tavern_deadpool_place_bet(client_ctx_t *ctx, json_t *root) {
    sqlite3 *db = db_get_handle();
    if (!ctx || ctx->player_id <= 0) {
        return send_error_response(ctx, root, ERR_NOT_AUTHENTICATED, "Authentication required.");
    }
    if (!is_player_in_tavern_sector(db, ctx->sector_id)) {
        return send_error_response(ctx, root, ERR_NOT_AT_TAVERN, "You are not in a tavern sector.");
    }

    json_t *data = json_object_get(root, "data");
    if (!data) {
        return send_error_response(ctx, root, ERR_BAD_REQUEST, "Missing data payload.");
    }

    int target_id = 0;
    long long bet_amount = 0;
    if (!json_get_int_flexible(data, "target_id", &target_id) || target_id <= 0) {
        return send_error_response(ctx, root, ERR_INVALID_ARG, "Invalid target_id.");
    }
    if (!json_get_int64_flexible(data, "amount", &bet_amount) || bet_amount <= 0) {
        return send_error_response(ctx, root, ERR_INVALID_ARG, "Bet amount must be positive.");
    }

    if (target_id == ctx->player_id) {
        return send_error_response(ctx, root, ERR_TAVERN_BET_ON_SELF, "Cannot place a bet on yourself.");
    }
    
    // Check if target player exists
    sqlite3_stmt *stmt = NULL;
    const char *sql_target_exists = "SELECT 1 FROM players WHERE id = ?;";
    if (sqlite3_prepare_v2(db, sql_target_exists, -1, &stmt, NULL) != SQLITE_OK) {
        return send_error_response(ctx, root, ERR_DB_QUERY_FAILED, "Failed to check target player existence.");
    }
    sqlite3_bind_int(stmt, 1, target_id);
    if (sqlite3_step(stmt) != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        return send_error_response(ctx, root, ERR_TAVERN_PLAYER_NOT_FOUND, "Target player not found.");
    }
    sqlite3_finalize(stmt);
    stmt = NULL;

    // Validate bet limits
    int limit_check = validate_bet_limits(db, ctx->player_id, bet_amount);
    if (limit_check == -1) {
        return send_error_response(ctx, root, ERR_TAVERN_BET_TOO_HIGH, "Bet exceeds maximum per transaction.");
    } else if (limit_check == -2) {
        return send_error_response(ctx, root, ERR_TAVERN_DAILY_WAGER_EXCEEDED, "Daily wager limit exceeded.");
    } else if (limit_check == -3) {
        return send_error_response(ctx, root, ERR_TAVERN_DYNAMIC_WAGER_EXCEEDED, "Dynamic wager limit exceeded based on liquid assets.");
    }

    // Check player funds
    if (!has_sufficient_funds(db, ctx->player_id, bet_amount)) {
        return send_error_response(ctx, root, ERR_INSUFFICIENT_FUNDS, "Insufficient credits to place bet.");
    }

    // Deduct bet amount
    int rc = update_player_credits_gambling(db, ctx->player_id, bet_amount, false); // Deduct
    if (rc != 0) {
        return send_error_response(ctx, root, ERR_DB, "Failed to deduct credits for bet.");
    }

    // Calculate expires_at (e.g., 24 hours from now)
    time_t now = time(NULL);
    time_t expires_at = now + (24 * 60 * 60); // 24 hours

    // Calculate simple odds (placeholder: 10000 = 100%)
    // This could be more complex, e.g., based on target's alignment, ship strength, etc.
    int odds_bp = get_random_int(5000, 15000); // Example: 50%-150% odds

    // Insert bet into tavern_deadpool_bets
    const char *sql_insert_bet = "INSERT INTO tavern_deadpool_bets (bettor_id, target_id, amount, odds_bp, placed_at, expires_at, resolved) VALUES (?, ?, ?, ?, ?, ?, 0);";
    rc = sqlite3_prepare_v2(db, sql_insert_bet, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        update_player_credits_gambling(db, ctx->player_id, bet_amount, true); // Refund credits
        return send_error_response(ctx, root, ERR_DB_QUERY_FAILED, "Failed to prepare dead pool bet insert.");
    }
    sqlite3_bind_int(stmt, 1, ctx->player_id);
    sqlite3_bind_int(stmt, 2, target_id);
    sqlite3_bind_int64(stmt, 3, bet_amount);
    sqlite3_bind_int(stmt, 4, odds_bp);
    sqlite3_bind_int(stmt, 5, (int)now);
    sqlite3_bind_int(stmt, 6, (int)expires_at);

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        update_player_credits_gambling(db, ctx->player_id, bet_amount, true); // Refund credits
        return send_error_response(ctx, root, ERR_DB, "Failed to insert dead pool bet.");
    }
    sqlite3_finalize(stmt);

    json_t *response_data = json_pack("{s:s, s:i, s:i, s:i}", "status", "Dead Pool bet placed.", "target_id", target_id, "amount", bet_amount, "odds_bp", odds_bp);
    send_enveloped_ok(ctx->fd, root, "tavern.deadpool.place_bet_v1", response_data);
    return 0;
}


// Implementation for tavern.dice.play RPC
int cmd_tavern_dice_play(client_ctx_t *ctx, json_t *root) {
    sqlite3 *db = db_get_handle();
    if (!ctx || ctx->player_id <= 0) {
        return send_error_response(ctx, root, ERR_NOT_AUTHENTICATED, "Authentication required.");
    }
    if (!is_player_in_tavern_sector(db, ctx->sector_id)) {
        return send_error_response(ctx, root, ERR_NOT_AT_TAVERN, "You are not in a tavern sector.");
    }

    json_t *data = json_object_get(root, "data");
    if (!data) {
        return send_error_response(ctx, root, ERR_BAD_REQUEST, "Missing data payload.");
    }

    long long bet_amount = 0;
    if (!json_get_int64_flexible(data, "amount", &bet_amount) || bet_amount <= 0) {
        return send_error_response(ctx, root, ERR_INVALID_ARG, "Bet amount must be positive.");
    }

    // Validate bet limits
    int limit_check = validate_bet_limits(db, ctx->player_id, bet_amount);
    if (limit_check == -1) {
        return send_error_response(ctx, root, ERR_TAVERN_BET_TOO_HIGH, "Bet exceeds maximum per transaction.");
    } else if (limit_check == -2) {
        return send_error_response(ctx, root, ERR_TAVERN_DAILY_WAGER_EXCEEDED, "Daily wager limit exceeded.");
    } else if (limit_check == -3) {
        return send_error_response(ctx, root, ERR_TAVERN_DYNAMIC_WAGER_EXCEEDED, "Dynamic wager limit exceeded based on liquid assets.");
    }

    // Check player funds
    if (!has_sufficient_funds(db, ctx->player_id, bet_amount)) {
        return send_error_response(ctx, root, ERR_INSUFFICIENT_FUNDS, "Insufficient credits to play dice.");
    }

    // Deduct bet amount
    int rc = update_player_credits_gambling(db, ctx->player_id, bet_amount, false); // Deduct
    if (rc != 0) {
        return send_error_response(ctx, root, ERR_DB, "Failed to deduct credits for dice game.");
    }

    // Simulate dice roll (2d6, win on 7)
    int die1 = get_random_int(1, 6);
    int die2 = get_random_int(1, 6);
    int total = die1 + die2;
    bool win = (total == 7);
    long long winnings = 0;

    if (win) {
        winnings = bet_amount * 2; // Example: 2x payout for winning
        rc = update_player_credits_gambling(db, ctx->player_id, winnings, true); // Add winnings
        if (rc != 0) {
            // Log error, but don't prevent response
            LOGE("cmd_tavern_dice_play: Failed to add winnings to player credits.");
        }
    }

    // Get updated player credits for response
    long long current_credits = 0;
    sqlite3_stmt *stmt = NULL;
    const char *sql_credits = "SELECT credits FROM players WHERE id = ?;";
    if (sqlite3_prepare_v2(db, sql_credits, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, ctx->player_id);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            current_credits = sqlite3_column_int64(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }

    json_t *response_data = json_pack("{s:s, s:i, s:i, s:i, s:b, s:i, s:I}",
                                      "status", "Dice game played.",
                                      "die1", die1,
                                      "die2", die2,
                                      "total", total,
                                      "win", win,
                                      "winnings", winnings,
                                      "player_credits", current_credits);
    send_enveloped_ok(ctx->fd, root, "tavern.dice.play_v1", response_data);
    return 0;
}


// Implementation for tavern.highstakes.play RPC
int cmd_tavern_highstakes_play(client_ctx_t *ctx, json_t *root) {
    sqlite3 *db = db_get_handle();
    if (!ctx || ctx->player_id <= 0) {
        return send_error_response(ctx, root, ERR_NOT_AUTHENTICATED, "Authentication required.");
    }
    if (!is_player_in_tavern_sector(db, ctx->sector_id)) {
        return send_error_response(ctx, root, ERR_NOT_AT_TAVERN, "You are not in a tavern sector.");
    }

    json_t *data = json_object_get(root, "data");
    if (!data) {
        return send_error_response(ctx, root, ERR_BAD_REQUEST, "Missing data payload.");
    }

    long long bet_amount = 0;
    int rounds = 0;
    if (!json_get_int64_flexible(data, "amount", &bet_amount) || bet_amount <= 0) {
        return send_error_response(ctx, root, ERR_INVALID_ARG, "Bet amount must be positive.");
    }
    if (!json_get_int_flexible(data, "rounds", &rounds) || rounds <= 0 || rounds > 5) {
        return send_error_response(ctx, root, ERR_INVALID_ARG, "Rounds must be between 1 and 5.");
    }

    // Validate initial bet limits
    int limit_check = validate_bet_limits(db, ctx->player_id, bet_amount);
    if (limit_check == -1) {
        return send_error_response(ctx, root, ERR_TAVERN_BET_TOO_HIGH, "Bet exceeds maximum per transaction.");
    } else if (limit_check == -2) {
        return send_error_response(ctx, root, ERR_TAVERN_DAILY_WAGER_EXCEEDED, "Daily wager limit exceeded.");
    } else if (limit_check == -3) {
        return send_error_response(ctx, root, ERR_TAVERN_DYNAMIC_WAGER_EXCEEDED, "Dynamic wager limit exceeded based on liquid assets.");
    }

    // Check player funds for initial bet
    if (!has_sufficient_funds(db, ctx->player_id, bet_amount)) {
        return send_error_response(ctx, root, ERR_INSUFFICIENT_FUNDS, "Insufficient credits for initial high-stakes bet.");
    }

    // Deduct initial bet amount
    int rc = update_player_credits_gambling(db, ctx->player_id, bet_amount, false); // Deduct
    if (rc != 0) {
        return send_error_response(ctx, root, ERR_DB, "Failed to deduct credits for high-stakes game.");
    }

    long long current_pot = bet_amount;
    bool player_won_all_rounds = true;

    for (int i = 0; i < rounds; i++) {
        // Simulate a biased coin flip (e.g., 60% chance to win)
        int roll = get_random_int(1, 100);
        if (roll <= 60) { // Win this round
            current_pot *= 2;
        } else { // Lose this round
            player_won_all_rounds = false;
            break; // Game ends on first loss
        }
    }

    long long winnings = 0;
    if (player_won_all_rounds) {
        winnings = current_pot;
        rc = update_player_credits_gambling(db, ctx->player_id, winnings, true); // Add final pot
        if (rc != 0) {
            LOGE("cmd_tavern_highstakes_play: Failed to add winnings to player credits.");
        }
    }

    // Get updated player credits for response
    long long player_credits_after_game = 0;
    sqlite3_stmt *stmt_credits = NULL;
    const char *sql_credits = "SELECT credits FROM players WHERE id = ?;";
    if (sqlite3_prepare_v2(db, sql_credits, -1, &stmt_credits, NULL) == SQLITE_OK) {
        sqlite3_bind_int(stmt_credits, 1, ctx->player_id);
        if (sqlite3_step(stmt_credits) == SQLITE_ROW) {
            player_credits_after_game = sqlite3_column_int64(stmt_credits, 0);
        }
        sqlite3_finalize(stmt_credits);
    }

    json_t *response_data = json_pack("{s:s, s:i, s:i, s:i, s:b, s:I, s:I}",
                                      "status", "High-stakes game played.",
                                      "initial_bet", bet_amount,
                                      "rounds_played", player_won_all_rounds ? rounds : (rc == 0 ? rounds : 0), // Adjust rounds_played if lost early
                                      "final_pot", current_pot,
                                      "player_won", player_won_all_rounds,
                                      "winnings", winnings,
                                      "player_credits", player_credits_after_game);
    send_enveloped_ok(ctx->fd, root, "tavern.highstakes.play_v1", response_data);
    return 0;
}


// Implementation for tavern.raffle.buy_ticket RPC
int cmd_tavern_raffle_buy_ticket(client_ctx_t *ctx, json_t *root) {
    sqlite3 *db = db_get_handle();
    if (!ctx || ctx->player_id <= 0) {
        return send_error_response(ctx, root, ERR_NOT_AUTHENTICATED, "Authentication required.");
    }
    if (!is_player_in_tavern_sector(db, ctx->sector_id)) {
        return send_error_response(ctx, root, ERR_NOT_AT_TAVERN, "You are not in a tavern sector.");
    }

    // Fixed ticket price for raffle
    long long ticket_price = 10; // Example: 10 credits per raffle ticket

    // Validate bet limits (using ticket_price as the bet)
    int limit_check = validate_bet_limits(db, ctx->player_id, ticket_price);
    if (limit_check == -1) {
        return send_error_response(ctx, root, ERR_TAVERN_BET_TOO_HIGH, "Raffle ticket price exceeds maximum per transaction.");
    } else if (limit_check == -2) {
        return send_error_response(ctx, root, ERR_TAVERN_DAILY_WAGER_EXCEEDED, "Daily wager limit exceeded.");
    } else if (limit_check == -3) {
        return send_error_response(ctx, root, ERR_TAVERN_DYNAMIC_WAGER_EXCEEDED, "Dynamic wager limit exceeded based on liquid assets.");
    }

    // Check player funds
    if (!has_sufficient_funds(db, ctx->player_id, ticket_price)) {
        return send_error_response(ctx, root, ERR_INSUFFICIENT_FUNDS, "Insufficient credits to buy raffle ticket.");
    }

    // Deduct ticket price
    int rc = update_player_credits_gambling(db, ctx->player_id, ticket_price, false); // Deduct
    if (rc != 0) {
        return send_error_response(ctx, root, ERR_DB, "Failed to deduct credits for raffle ticket.");
    }

    long long current_pot = 0;
    long long last_payout = 0;
    int last_winner_id = 0;
    int last_win_ts = 0;

    // Get and update raffle state
    sqlite3_stmt *stmt = NULL;
    const char *sql_get_raffle = "SELECT pot, last_winner_id, last_payout, last_win_ts FROM tavern_raffle_state WHERE id = 1;";
    if (sqlite3_prepare_v2(db, sql_get_raffle, -1, &stmt, NULL) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            current_pot = sqlite3_column_int64(stmt, 0);
            last_winner_id = sqlite3_column_int(stmt, 1);
            last_payout = sqlite3_column_int64(stmt, 2);
            last_win_ts = sqlite3_column_int(stmt, 3);
        }
        sqlite3_finalize(stmt);
    } else {
        LOGE("cmd_tavern_raffle_buy_ticket: Failed to prepare get raffle state statement: %s", sqlite3_errmsg(db));
        update_player_credits_gambling(db, ctx->player_id, ticket_price, true); // Refund
        return send_error_response(ctx, root, ERR_DB, "Failed to retrieve raffle state.");
    }

    current_pot += ticket_price; // Add ticket price to pot

    bool player_wins = (get_random_int(1, 1000) == 1); // 1 in 1000 chance to win
    long long winnings = 0;
    
    const char *sql_update_raffle = NULL;
    if (player_wins) {
        winnings = current_pot;
        rc = update_player_credits_gambling(db, ctx->player_id, winnings, true); // Add winnings
        if (rc != 0) {
            LOGE("cmd_tavern_raffle_buy_ticket: Failed to add winnings to player credits for raffle.");
        }
        
        // Reset pot and record win
        sql_update_raffle = "UPDATE tavern_raffle_state SET pot = 0, last_winner_id = ?, last_payout = ?, last_win_ts = ? WHERE id = 1;";
        rc = sqlite3_prepare_v2(db, sql_update_raffle, -1, &stmt, NULL);
        if (rc == SQLITE_OK) {
            sqlite3_bind_int(stmt, 1, ctx->player_id);
            sqlite3_bind_int64(stmt, 2, winnings);
            sqlite3_bind_int(stmt, 3, (int)time(NULL));
            if (sqlite3_step(stmt) != SQLITE_DONE) {
                LOGE("cmd_tavern_raffle_buy_ticket: Failed to update raffle state on win: %s", sqlite3_errmsg(db));
            }
            sqlite3_finalize(stmt);
        } else {
            LOGE("cmd_tavern_raffle_buy_ticket: Failed to prepare update raffle state on win: %s", sqlite3_errmsg(db));
        }
        current_pot = 0; // Pot reset after win
    } else {
        // Just update pot if no win
        sql_update_raffle = "UPDATE tavern_raffle_state SET pot = ? WHERE id = 1;";
        rc = sqlite3_prepare_v2(db, sql_update_raffle, -1, &stmt, NULL);
        if (rc == SQLITE_OK) {
            sqlite3_bind_int64(stmt, 1, current_pot);
            if (sqlite3_step(stmt) != SQLITE_DONE) {
                LOGE("cmd_tavern_raffle_buy_ticket: Failed to update raffle pot: %s", sqlite3_errmsg(db));
            }
            sqlite3_finalize(stmt);
        } else {
            LOGE("cmd_tavern_raffle_buy_ticket: Failed to prepare update raffle pot statement: %s", sqlite3_errmsg(db));
        }
    }

    // Get updated player credits for response
    long long player_credits_after_game = 0;
    sqlite3_stmt *stmt_credits = NULL;
    const char *sql_credits = "SELECT credits FROM players WHERE id = ?;";
    if (sqlite3_prepare_v2(db, sql_credits, -1, &stmt_credits, NULL) == SQLITE_OK) {
        sqlite3_bind_int(stmt_credits, 1, ctx->player_id);
        if (sqlite3_step(stmt_credits) == SQLITE_ROW) {
            player_credits_after_game = sqlite3_column_int64(stmt_credits, 0);
        }
        sqlite3_finalize(stmt_credits);
    }


    json_t *response_data = json_pack("{s:s, s:b, s:I, s:I, s:I}",
                                      "status", player_wins ? "You won the raffle!" : "You bought a raffle ticket.",
                                      "player_wins", player_wins,
                                      "winnings", winnings,
                                      "current_pot", current_pot,
                                      "player_credits", player_credits_after_game);
    send_enveloped_ok(ctx->fd, root, "tavern.raffle.buy_ticket_v1", response_data);
    return 0;
}


// Implementation for tavern.trader.buy_password RPC
int cmd_tavern_trader_buy_password(client_ctx_t *ctx, json_t *root) {
    sqlite3 *db = db_get_handle();
    if (!ctx || ctx->player_id <= 0) {
        return send_error_response(ctx, root, ERR_NOT_AUTHENTICATED, "Authentication required.");
    }
    if (!is_player_in_tavern_sector(db, ctx->sector_id)) {
        return send_error_response(ctx, root, ERR_NOT_AT_TAVERN, "You are not in a tavern sector.");
    }

    // Check player alignment (example: must be < 0 for underground access)
    long long player_alignment = 0;
    sqlite3_stmt *stmt_align = NULL;
    const char *sql_align = "SELECT alignment FROM players WHERE id = ?;";
    if (sqlite3_prepare_v2(db, sql_align, -1, &stmt_align, NULL) == SQLITE_OK) {
        sqlite3_bind_int(stmt_align, 1, ctx->player_id);
        if (sqlite3_step(stmt_align) == SQLITE_ROW) {
            player_alignment = sqlite3_column_int64(stmt_align, 0);
        }
        sqlite3_finalize(stmt_align);
    } else {
        LOGE("cmd_tavern_trader_buy_password: Failed to prepare alignment statement: %s", sqlite3_errmsg(db));
        return send_error_response(ctx, root, ERR_DB, "Failed to retrieve player alignment.");
    }

    if (player_alignment >= 0) { // Example threshold
        return send_error_response(ctx, root, ERR_TAVERN_TOO_HONORABLE, "You are too honorable to access the underground.");
    }

    long long password_price = 5000; // Fixed price for underground password

    // Validate bet limits (using password_price as the bet)
    int limit_check = validate_bet_limits(db, ctx->player_id, password_price);
    if (limit_check == -1) {
        return send_error_response(ctx, root, ERR_TAVERN_BET_TOO_HIGH, "Password price exceeds maximum transaction limit.");
    } else if (limit_check == -2) {
        return send_error_response(ctx, root, ERR_TAVERN_DAILY_WAGER_EXCEEDED, "Daily wager limit exceeded for password purchase.");
    } else if (limit_check == -3) {
        return send_error_response(ctx, root, ERR_TAVERN_DYNAMIC_WAGER_EXCEEDED, "Dynamic wager limit exceeded for password purchase.");
    }

    // Check player funds
    if (!has_sufficient_funds(db, ctx->player_id, password_price)) {
        return send_error_response(ctx, root, ERR_INSUFFICIENT_FUNDS, "Insufficient credits to buy underground password.");
    }

    // Deduct password price
    int rc = update_player_credits_gambling(db, ctx->player_id, password_price, false); // Deduct
    if (rc != 0) {
        return send_error_response(ctx, root, ERR_DB, "Failed to deduct credits for password.");
    }

    // In a real implementation, this would update a player flag or an access table.
    // For now, we just return a success message.
    json_t *response_data = json_pack("{s:s, s:s, s:I}",
                                      "status", "Underground password purchased.",
                                      "password", "UndergroundAccessCode-XYZ", // Example password
                                      "cost", password_price);
    send_enveloped_ok(ctx->fd, root, "tavern.trader.buy_password_v1", response_data);
    return 0;
}


// Implementation for tavern.graffiti.post RPC
int cmd_tavern_graffiti_post(client_ctx_t *ctx, json_t *root) {
    sqlite3 *db = db_get_handle();
    if (!ctx || ctx->player_id <= 0) {
        return send_error_response(ctx, root, ERR_NOT_AUTHENTICATED, "Authentication required.");
    }
    if (!is_player_in_tavern_sector(db, ctx->sector_id)) {
        return send_error_response(ctx, root, ERR_NOT_AT_TAVERN, "You are not in a tavern sector.");
    }

    json_t *data = json_object_get(root, "data");
    if (!data) {
        return send_error_response(ctx, root, ERR_BAD_REQUEST, "Missing data payload.");
    }

    const char *post_text_raw = json_get_string_or_null(data, "text");
    if (!post_text_raw || strlen(post_text_raw) == 0) {
        return send_error_response(ctx, root, ERR_INVALID_ARG, "Graffiti text cannot be empty.");
    }

    char post_text[256]; // Max length for graffiti post
    strncpy(post_text, post_text_raw, sizeof(post_text) - 1);
    post_text[sizeof(post_text) - 1] = '\0';
    sanitize_text(post_text, sizeof(post_text));

    if (strlen(post_text) == 0) {
        return send_error_response(ctx, root, ERR_INVALID_ARG, "Graffiti text became empty after sanitization.");
    }

    time_t now = time(NULL);

    // Insert new graffiti post
    const char *sql_insert = "INSERT INTO tavern_graffiti (player_id, text, created_at) VALUES (?, ?, ?);";
    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(db, sql_insert, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        return send_error_response(ctx, root, ERR_DB_QUERY_FAILED, "Failed to prepare graffiti insert.");
    }
    sqlite3_bind_int(stmt, 1, ctx->player_id);
    sqlite3_bind_text(stmt, 2, post_text, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 3, (int)now);

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        return send_error_response(ctx, root, ERR_DB, "Failed to insert graffiti post.");
    }
    sqlite3_finalize(stmt);

    // Optional: Implement FIFO logic - if count exceeds graffiti_max_posts, delete the oldest
    const char *sql_count = "SELECT COUNT(*) FROM tavern_graffiti;";
    long long current_graffiti_count = 0;
    if (sqlite3_prepare_v2(db, sql_count, -1, &stmt, NULL) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            current_graffiti_count = sqlite3_column_int64(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }

    if (current_graffiti_count > g_tavern_cfg.graffiti_max_posts) {
        const char *sql_delete_oldest = "DELETE FROM tavern_graffiti WHERE id IN (SELECT id FROM tavern_graffiti ORDER BY created_at ASC LIMIT ?);";
        if (sqlite3_prepare_v2(db, sql_delete_oldest, -1, &stmt, NULL) == SQLITE_OK) {
            sqlite3_bind_int(stmt, 1, current_graffiti_count - g_tavern_cfg.graffiti_max_posts);
            if (sqlite3_step(stmt) != SQLITE_DONE) {
                LOGE("cmd_tavern_graffiti_post: Failed to delete oldest graffiti posts: %s", sqlite3_errmsg(db));
            }
            sqlite3_finalize(stmt);
        }
    }


    json_t *response_data = json_pack("{s:s, s:s, s:I}", "status", "Graffiti posted successfully.", "text", post_text, "created_at", (long long)now);
    send_enveloped_ok(ctx->fd, root, "tavern.graffiti.post_v1", response_data);
    return 0;
}


// Implementation for tavern.round.buy RPC
int cmd_tavern_round_buy(client_ctx_t *ctx, json_t *root) {
    sqlite3 *db = db_get_handle();
    if (!ctx || ctx->player_id <= 0) {
        return send_error_response(ctx, root, ERR_NOT_AUTHENTICATED, "Authentication required.");
    }
    if (!is_player_in_tavern_sector(db, ctx->sector_id)) {
        return send_error_response(ctx, root, ERR_NOT_AT_TAVERN, "You are not in a tavern sector.");
    }

    long long cost = g_tavern_cfg.buy_round_cost;
    int alignment_gain = g_tavern_cfg.buy_round_alignment_gain;

    // Validate bet limits (using cost as the bet)
    int limit_check = validate_bet_limits(db, ctx->player_id, cost);
    if (limit_check == -1) {
        return send_error_response(ctx, root, ERR_TAVERN_BET_TOO_HIGH, "Cost to buy a round exceeds transaction limit.");
    } else if (limit_check == -2) {
        return send_error_response(ctx, root, ERR_TAVERN_DAILY_WAGER_EXCEEDED, "Daily wager limit exceeded for buying a round.");
    } else if (limit_check == -3) {
        return send_error_response(ctx, root, ERR_TAVERN_DYNAMIC_WAGER_EXCEEDED, "Dynamic wager limit exceeded for buying a round.");
    }

    // Check player funds
    if (!has_sufficient_funds(db, ctx->player_id, cost)) {
        return send_error_response(ctx, root, ERR_INSUFFICIENT_FUNDS, "Insufficient credits to buy a round.");
    }

    // Deduct cost
    int rc = update_player_credits_gambling(db, ctx->player_id, cost, false); // Deduct
    if (rc != 0) {
        return send_error_response(ctx, root, ERR_DB, "Failed to deduct credits for buying a round.");
    }

    // Increase player's alignment
    const char *sql_update_alignment = "UPDATE players SET alignment = alignment + ? WHERE id = ?;";
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, sql_update_alignment, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, alignment_gain);
        sqlite3_bind_int(stmt, 2, ctx->player_id);
        if (sqlite3_step(stmt) != SQLITE_DONE) {
            LOGE("cmd_tavern_round_buy: Failed to update player alignment: %s", sqlite3_errmsg(db));
        }
        sqlite3_finalize(stmt);
    } else {
        LOGE("cmd_tavern_round_buy: Failed to prepare alignment update statement: %s", sqlite3_errmsg(db));
    }

    // Broadcast message to all online players in the sector
    json_t *broadcast_payload = json_pack("{s:s, s:i, s:i}",
                                          "message", "A round has been bought for everyone!",
                                          "player_id", ctx->player_id,
                                          "sector_id", ctx->sector_id);
    server_broadcast_to_sector(ctx->sector_id, "tavern.round.bought", broadcast_payload);


    json_t *response_data = json_pack("{s:s, s:I, s:i}", "status", "Round bought successfully!", "cost", cost, "alignment_gain", alignment_gain);
    send_enveloped_ok(ctx->fd, root, "tavern.round.buy_v1", response_data);
    return 0;
}


// Implementation for tavern.loan.take RPC
int cmd_tavern_loan_take(client_ctx_t *ctx, json_t *root) {
    sqlite3 *db = db_get_handle();
    if (!ctx || ctx->player_id <= 0) {
        return send_error_response(ctx, root, ERR_NOT_AUTHENTICATED, "Authentication required.");
    }
    if (!is_player_in_tavern_sector(db, ctx->sector_id)) {
        return send_error_response(ctx, root, ERR_NOT_AT_TAVERN, "You are not in a tavern sector.");
    }
    if (!g_tavern_cfg.loan_shark_enabled) {
        return send_error_response(ctx, root, ERR_TAVERN_LOAN_SHARK_DISABLED, "The Loan Shark is not currently available.");
    }

    long long current_loan_principal = 0;
    if (get_player_loan(db, ctx->player_id, &current_loan_principal, NULL, NULL, NULL) == SQLITE_OK && current_loan_principal > 0) {
        return send_error_response(ctx, root, ERR_TAVERN_LOAN_OUTSTANDING, "You already have an outstanding loan.");
    }

    json_t *data = json_object_get(root, "data");
    if (!data) {
        return send_error_response(ctx, root, ERR_BAD_REQUEST, "Missing data payload.");
    }

    long long loan_amount = 0;
    if (!json_get_int64_flexible(data, "amount", &loan_amount) || loan_amount <= 0 || loan_amount > 100000) { // Example max loan
        return send_error_response(ctx, root, ERR_INVALID_ARG, "Loan amount must be positive and not exceed 100,000 credits.");
    }

    int interest_rate_bp = 1000; // Example: 10% interest (1000 basis points)
    time_t now = time(NULL);
    time_t due_date = now + (7 * 24 * 60 * 60); // Due in 7 days

    // Insert new loan
    const char *sql_insert_loan = "INSERT INTO tavern_loans (player_id, principal, interest_rate, due_date, is_defaulted) VALUES (?, ?, ?, ?, 0);";
    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(db, sql_insert_loan, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        return send_error_response(ctx, root, ERR_DB_QUERY_FAILED, "Failed to prepare loan insert.");
    }
    sqlite3_bind_int(stmt, 1, ctx->player_id);
    sqlite3_bind_int64(stmt, 2, loan_amount);
    sqlite3_bind_int(stmt, 3, interest_rate_bp);
    sqlite3_bind_int(stmt, 4, (int)due_date);

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        return send_error_response(ctx, root, ERR_DB, "Failed to insert loan.");
    }
    sqlite3_finalize(stmt);

    // Add loan amount to player's credits
    rc = update_player_credits_gambling(db, ctx->player_id, loan_amount, true); // Add
    if (rc != 0) {
        LOGE("cmd_tavern_loan_take: Failed to add loan amount to player credits.");
        // Consider rolling back the loan insert here if transactions were explicit
    }

    json_t *response_data = json_pack("{s:s, s:I, s:i, s:I}",
                                      "status", "Loan taken successfully!",
                                      "amount", loan_amount,
                                      "interest_rate_bp", interest_rate_bp,
                                      "due_date", (long long)due_date);
    send_enveloped_ok(ctx->fd, root, "tavern.loan.take_v1", response_data);
    return 0;
}

// Implementation for tavern.loan.pay RPC
int cmd_tavern_loan_pay(client_ctx_t *ctx, json_t *root) {
    sqlite3 *db = db_get_handle();
    if (!ctx || ctx->player_id <= 0) {
        return send_error_response(ctx, root, ERR_NOT_AUTHENTICATED, "Authentication required.");
    }
    if (!is_player_in_tavern_sector(db, ctx->sector_id)) {
        return send_error_response(ctx, root, ERR_NOT_AT_TAVERN, "You are not in a tavern sector.");
    }
    if (!g_tavern_cfg.loan_shark_enabled) {
        return send_error_response(ctx, root, ERR_TAVERN_LOAN_SHARK_DISABLED, "The Loan Shark is not currently available.");
    }

    long long current_loan_principal = 0;
    int current_loan_interest_rate = 0;
    int current_loan_due_date = 0;
    int current_loan_is_defaulted = 0;

    if (get_player_loan(db, ctx->player_id, &current_loan_principal, &current_loan_interest_rate, &current_loan_due_date, &current_loan_is_defaulted) != SQLITE_OK || current_loan_principal <= 0) {
        return send_error_response(ctx, root, ERR_TAVERN_NO_LOAN, "You do not have an outstanding loan.");
    }

    json_t *data = json_object_get(root, "data");
    if (!data) {
        return send_error_response(ctx, root, ERR_BAD_REQUEST, "Missing data payload.");
    }

    long long pay_amount = 0;
    if (!json_get_int64_flexible(data, "amount", &pay_amount) || pay_amount <= 0) {
        return send_error_response(ctx, root, ERR_INVALID_ARG, "Payment amount must be positive.");
    }

    if (!has_sufficient_funds(db, ctx->player_id, pay_amount)) {
        return send_error_response(ctx, root, ERR_INSUFFICIENT_FUNDS, "Insufficient credits to make payment.");
    }

    // Deduct payment amount from player's credits
    int rc = update_player_credits_gambling(db, ctx->player_id, pay_amount, false); // Deduct
    if (rc != 0) {
        return send_error_response(ctx, root, ERR_DB, "Failed to deduct credits for loan payment.");
    }

    long long new_principal = current_loan_principal - pay_amount;
    if (new_principal < 0) {
        new_principal = 0; // Cannot overpay beyond principal
    }

    const char *sql_update_loan = NULL;
    sqlite3_stmt *stmt = NULL;

    if (new_principal == 0) {
        // Loan fully paid, delete it
        sql_update_loan = "DELETE FROM tavern_loans WHERE player_id = ?;";
        rc = sqlite3_prepare_v2(db, sql_update_loan, -1, &stmt, NULL);
        if (rc != SQLITE_OK) {
            update_player_credits_gambling(db, ctx->player_id, pay_amount, true); // Refund
            return send_error_response(ctx, root, ERR_DB_QUERY_FAILED, "Failed to prepare loan delete.");
        }
        sqlite3_bind_int(stmt, 1, ctx->player_id);
    } else {
        // Update remaining principal and reset default status if paying
        sql_update_loan = "UPDATE tavern_loans SET principal = ?, is_defaulted = 0 WHERE player_id = ?;";
        rc = sqlite3_prepare_v2(db, sql_update_loan, -1, &stmt, NULL);
        if (rc != SQLITE_OK) {
            update_player_credits_gambling(db, ctx->player_id, pay_amount, true); // Refund
            return send_error_response(ctx, root, ERR_DB_QUERY_FAILED, "Failed to prepare loan update.");
        }
        sqlite3_bind_int64(stmt, 1, new_principal);
        sqlite3_bind_int(stmt, 2, ctx->player_id);
    }

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        update_player_credits_gambling(db, ctx->player_id, pay_amount, true); // Refund
        return send_error_response(ctx, root, ERR_DB, "Failed to update loan principal.");
    }
    sqlite3_finalize(stmt);

    json_t *response_data = json_pack("{s:s, s:I, s:I}",
                                      "status", new_principal == 0 ? "Loan fully paid!" : "Loan payment successful.",
                                      "paid_amount", pay_amount,
                                      "remaining_principal", new_principal);
    send_enveloped_ok(ctx->fd, root, "tavern.loan.pay_v1", response_data);
    return 0;
}


// Implementation for tavern.rumour.get_hint RPC
int cmd_tavern_rumour_get_hint(client_ctx_t *ctx, json_t *root) {
    sqlite3 *db = db_get_handle();
    if (!ctx || ctx->player_id <= 0) {
        return send_error_response(ctx, root, ERR_NOT_AUTHENTICATED, "Authentication required.");
    }
    if (!is_player_in_tavern_sector(db, ctx->sector_id)) {
        return send_error_response(ctx, root, ERR_NOT_AT_TAVERN, "You are not in a tavern sector.");
    }

    long long hint_cost = 50; // Fixed price for a rumour hint

    // Validate bet limits
    int limit_check = validate_bet_limits(db, ctx->player_id, hint_cost);
    if (limit_check == -1) {
        return send_error_response(ctx, root, ERR_TAVERN_BET_TOO_HIGH, "Hint cost exceeds maximum transaction limit.");
    } else if (limit_check == -2) {
        return send_error_response(ctx, root, ERR_TAVERN_DAILY_WAGER_EXCEEDED, "Daily wager limit exceeded for hint.");
    } else if (limit_check == -3) {
        return send_error_response(ctx, root, ERR_TAVERN_DYNAMIC_WAGER_EXCEEDED, "Dynamic wager limit exceeded for hint.");
    }

    // Check player funds
    if (!has_sufficient_funds(db, ctx->player_id, hint_cost)) {
        return send_error_response(ctx, root, ERR_INSUFFICIENT_FUNDS, "Insufficient credits to buy a rumour hint.");
    }

    // Deduct cost
    int rc = update_player_credits_gambling(db, ctx->player_id, hint_cost, false); // Deduct
    if (rc != 0) {
        return send_error_response(ctx, root, ERR_DB, "Failed to deduct credits for hint.");
    }

    // Placeholder for generating a real hint. For now, a generic one.
    const char *hint_messages[] = {
        "I heard a Federation patrol is heading towards sector 42.",
        "There's a whisper of rare organics in the outer rim.",
        "The market for equipment on planet X is about to crash.",
        "Beware of pirates near the nebula in sector 103.",
        "Someone saw a derelict Imperial Starship in uncharted space."
    };
    const char *random_hint = hint_messages[get_random_int(0, (sizeof(hint_messages)/sizeof(hint_messages[0])) - 1)];

    json_t *response_data = json_pack("{s:s, s:s, s:I}", "status", "Rumour acquired.", "hint", random_hint, "cost", hint_cost);
    send_enveloped_ok(ctx->fd, root, "tavern.rumour.get_hint_v1", response_data);
    return 0;
}


// Implementation for tavern.barcharts.get_prices_summary RPC
int cmd_tavern_barcharts_get_prices_summary(client_ctx_t *ctx, json_t *root) {
    sqlite3 *db = db_get_handle();
    if (!ctx || ctx->player_id <= 0) {
        return send_error_response(ctx, root, ERR_NOT_AUTHENTICATED, "Authentication required.");
    }
    if (!is_player_in_tavern_sector(db, ctx->sector_id)) {
        return send_error_response(ctx, root, ERR_NOT_AT_TAVERN, "You are not in a tavern sector.");
    }

    long long summary_cost = 100; // Fixed price for market summary

    // Validate bet limits
    int limit_check = validate_bet_limits(db, ctx->player_id, summary_cost);
    if (limit_check == -1) {
        return send_error_response(ctx, root, ERR_TAVERN_BET_TOO_HIGH, "Summary cost exceeds maximum transaction limit.");
    } else if (limit_check == -2) {
        return send_error_response(ctx, root, ERR_TAVERN_DAILY_WAGER_EXCEEDED, "Daily wager limit exceeded for summary.");
    } else if (limit_check == -3) {
        return send_error_response(ctx, root, ERR_TAVERN_DYNAMIC_WAGER_EXCEEDED, "Dynamic wager limit exceeded for summary.");
    }

    // Check player funds
    if (!has_sufficient_funds(db, ctx->player_id, summary_cost)) {
        return send_error_response(ctx, root, ERR_INSUFFICIENT_FUNDS, "Insufficient credits to buy market summary.");
    }

    // Deduct cost
    int rc = update_player_credits_gambling(db, ctx->player_id, summary_cost, false); // Deduct
    if (rc != 0) {
        return send_error_response(ctx, root, ERR_DB, "Failed to deduct credits for summary.");
    }

    json_t *prices_array = json_array();
    sqlite3_stmt *stmt = NULL;

    // Placeholder: Query for top commodity prices (simplified for now)
    const char *sql_prices = "SELECT p.sector, c.name, pt.mode, pt.maxproduct, (c.base_price * (10000 + c.volatility * (RANDOM() % 200 - 100)) / 10000) AS price "
                             "FROM ports p JOIN port_trade pt ON p.id = pt.port_id JOIN commodities c ON c.code = pt.commodity "
                             "ORDER BY price DESC LIMIT 5;"; // Top 5 prices example

    if (sqlite3_prepare_v2(db, sql_prices, -1, &stmt, NULL) != SQLITE_OK) {
        LOGE("cmd_tavern_barcharts_get_prices_summary: Failed to prepare prices query: %s", sqlite3_errmsg(db));
        update_player_credits_gambling(db, ctx->player_id, summary_cost, true); // Refund
        return send_error_response(ctx, root, ERR_DB, "Failed to retrieve market summary.");
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        json_t *price_obj = json_object();
        json_object_set_new(price_obj, "sector_id", json_integer(sqlite3_column_int(stmt, 0)));
        json_object_set_new(price_obj, "commodity", json_string((const char*)sqlite3_column_text(stmt, 1)));
        json_object_set_new(price_obj, "type", json_string((const char*)sqlite3_column_text(stmt, 2)));
        json_object_set_new(price_obj, "amount", json_integer(sqlite3_column_int(stmt, 3)));
        json_object_set_new(price_obj, "price", json_integer(sqlite3_column_int(stmt, 4)));
        json_array_append_new(prices_array, price_obj);
    }
    sqlite3_finalize(stmt);

    json_t *response_data = json_pack("{s:s, s:o, s:I}", "status", "Market summary acquired.", "prices", prices_array, "cost", summary_cost);
    send_enveloped_ok(ctx->fd, root, "tavern.barcharts.get_prices_summary_v1", response_data);
    return 0;
}