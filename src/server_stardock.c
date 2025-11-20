#include <sqlite3.h>
#include <jansson.h>
#include <string.h> // For strcasecmp
#include <math.h> // For floor() function
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
    send_enveloped_ok(ctx->fd, root, "shipyard.upgrade_v1", response_data);

    free(cfg);
    return 0;
}