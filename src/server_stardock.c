#include <sqlite3.h>
#include <jansson.h>
#include <string.h> // For strcasecmp
#include "server_stardock.h"
#include "common.h"
#include "database.h"
#include "server_envelope.h"
#include "errors.h"
#include "server_ports.h" // For port types, etc.
#include "server_ships.h" // For h_get_active_ship_id
#include "server_cmds.h" // For send_error_response, send_json_response and send_error_and_return
#include "server_loop.h" // For idemp_fingerprint_json

// Implementation for hardware.list RPC
int cmd_hardware_list(client_ctx_t *ctx, json_t *root) {
    sqlite3 *db = db_get_handle();
    int player_id = ctx->player_id;
    int ship_id = 0;
    int sector_id = 0;

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

    sqlite3_stmt *stmt = NULL;
    const char *sql_loc_check = "SELECT id, type FROM ports WHERE sector = ? AND (type = 9 OR type = 0);"; // type 9 for Stardock, 0 for Class-0
    int rc = sqlite3_prepare_v2(db, sql_loc_check, -1, &stmt, NULL);

    if (rc == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, sector_id);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            port_id = sqlite3_column_int(stmt, 0);
            int port_type = sqlite3_column_int(stmt, 1);
            if (port_type == 9) { // Stardock
                strncpy(location_type, "STARDOCK", sizeof(location_type) - 1);
            } else if (port_type == 0) { // Class-0
                strncpy(location_type, "CLASS0", sizeof(location_type) - 1);
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
    int can_transwarp = 0, can_planet_scan = 0, can_long_range_scan = 0, can_cloak = 0; // capabilities

    // Get ship current state
    const char *sql_ship_state = "SELECT s.holds, s.fighters, s.shields, s.genesis, s.detonators, s.probes, s.cloaking_devices, s.has_transwarp, s.has_planet_scanner, s.has_long_range_scanner, st.maxholds, st.maxfighters, st.maxshields, st.maxgenesis, st.max_detonators, st.max_probes, st.can_transwarp, st.can_planet_scan, st.can_long_range_scan, st.can_cloak FROM ships s JOIN shiptypes st ON s.type_id = st.id WHERE s.id = ?;";
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
        can_cloak = sqlite3_column_int(stmt, 19);
    }
    sqlite3_finalize(stmt);
    stmt = NULL;


    json_t *items_array = json_array();
    const char *sql_hardware = "SELECT code, name, price, max_per_ship, category FROM hardware_items WHERE enabled = 1 AND (? = 'STARDOCK' OR (? = 'CLASS0' AND sold_in_class0 = 1))";
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
        // int max_per_ship_hw = sqlite3_column_int(stmt, 3); // max_per_ship from hardware_items
        int max_per_ship_hw = sqlite3_column_type(stmt, 3) == SQLITE_NULL ? -1 : sqlite3_column_int(stmt, 3); // -1 means use shiptype max
        const char *category = (const char*)sqlite3_column_text(stmt, 4);

        int max_purchase = 0;
        bool ship_has_capacity = true;
        bool item_supported = true;

        if (strcasecmp(category, "FIGHTER") == 0) {
            max_purchase = MAX(0, max_fighters - current_fighters);
        } else if (strcasecmp(category, "SHIELD") == 0) {
            max_purchase = MAX(0, max_shields - current_shields);
        } else if (strcasecmp(category, "HOLD") == 0) {
            max_purchase = MAX(0, max_holds - current_holds);
        } else if (strcasecmp(category, "SPECIAL") == 0) {
            if (strcasecmp(code, "GENESIS") == 0) {
                int limit = (max_per_ship_hw != -1 && max_per_ship_hw < max_genesis) ? max_per_ship_hw : max_genesis;
                max_purchase = MAX(0, limit - current_genesis);
            } else if (strcasecmp(code, "DETONATOR") == 0) {
                int limit = (max_per_ship_hw != -1 && max_per_ship_hw < max_detonators_st) ? max_per_ship_hw : max_detonators_st;
                max_purchase = MAX(0, limit - current_detonators);
            } else if (strcasecmp(code, "PROBE") == 0) {
                int limit = (max_per_ship_hw != -1 && max_per_ship_hw < max_probes_st) ? max_per_ship_hw : max_probes_st;
                max_purchase = MAX(0, limit - current_probes);
            } else {
                item_supported = false; // Unknown special item
            }
        } else if (strcasecmp(category, "MODULE") == 0) {
            // Modules are generally 0 or 1. Check if ship can support and if already installed.
            if (strcasecmp(code, "CLOAK") == 0) {
                if (!can_cloak) item_supported = false;
                max_purchase = (can_cloak && current_cloaks == 0) ? 1 : 0;
            } else if (strcasecmp(code, "TWARP") == 0) { // TransWarp Drive
                if (!can_transwarp) item_supported = false;
                max_purchase = (can_transwarp && has_transwarp == 0) ? 1 : 0;
            } else if (strcasecmp(code, "PSCANNER") == 0) { // Planet Scanner
                if (!can_planet_scan) item_supported = false;
                max_purchase = (can_planet_scan && has_planet_scanner == 0) ? 1 : 0;
            } else if (strcasecmp(code, "LSCANNER") == 0) { // Long-Range Scanner
                if (!can_long_range_scan) item_supported = false;
                max_purchase = (can_long_range_scan && has_long_range_scanner == 0) ? 1 : 0;
            } else {
                item_supported = false; // Unknown module
            }
        } else {
            item_supported = false; // Unknown category
        }
        
        if (max_purchase <= 0) ship_has_capacity = false;

        if (item_supported && (max_purchase > 0 || strcmp(category, "MODULE") == 0) ) { // Always show modules if supported, even if max_purchase is 0 for "already installed" status
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
    sqlite3 *db = db_get_handle();
    int player_id = ctx->player_id;
    int ship_id = 0;
    int sector_id = 0;
    int rc = 0; // Declare rc here
    json_t *data = json_object_get(root, "data");

    const char *code = NULL;
    int quantity = 0;
    const char *idempotency_key = NULL;

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
    sector_id = ctx->sector_id;

    // 2. Parse Request
    if (!data) {
        send_enveloped_error(ctx->fd, root, ERR_BAD_REQUEST, "Missing data payload.");
        return 0;
    }
    code = json_get_string_or_null(data, "code");
    if (!json_get_int_flexible(data, "quantity", &quantity)) {
        send_enveloped_error(ctx->fd, root, ERR_MISSING_FIELD, "Missing or invalid 'quantity'.");
        return 0;
    }
    idempotency_key = json_get_string_or_null(data, "idempotency_key");

    if (!code) {
        send_enveloped_error(ctx->fd, root, ERR_MISSING_FIELD, "Missing 'code' for hardware item.");
        return 0;
    }
    if (quantity <= 0) {
        send_enveloped_error(ctx->fd, root, ERR_HARDWARE_QUANTITY_INVALID, "Quantity must be greater than zero.");
        return 0;
    }

    // 3. Idempotency Check (as early as possible after parsing key)
    if (idempotency_key) {
        // Assume db_idemp_fetch and db_idemp_try_begin exist and work correctly
        char *prev_cmd = NULL;
        char *prev_req_fp = NULL;
        char *prev_response_json = NULL;

        // Hash the current request payload (excluding idempotency_key itself)
        json_t *req_payload_for_fp = json_deep_copy(data);
        json_object_del(req_payload_for_fp, "idempotency_key");
        char req_fp[17]; // FNV-1a hash needs 16 chars + null terminator
        idemp_fingerprint_json(req_payload_for_fp, req_fp);
        json_decref(req_payload_for_fp);
        
        int rc = db_idemp_fetch(idempotency_key, &prev_cmd, &prev_req_fp, &prev_response_json);
        if (rc == SQLITE_OK) {
            // Check if it's the same request
            if (prev_req_fp && strcmp(prev_req_fp, req_fp) == 0) {
                if (prev_response_json) {
                    json_t *prev_response = json_loads(prev_response_json, 0, NULL);
                    if (prev_response) {
                        send_json_response(ctx, prev_response); // Assumes send_json_response handles decref
                        free(prev_cmd); free(prev_req_fp); free(prev_response_json);
                        return 0; // Idempotent success
                    }
                }
            } else {
                // Key collision with different request payload, or other issue, treat as error
                send_enveloped_error(ctx->fd, root, ERR_DUPLICATE_REQUEST, "Idempotency key already used for a different request.");
                free(prev_cmd); free(prev_req_fp); free(prev_response_json);
                return 0;
            }
        } else if (rc == SQLITE_NOTFOUND) {
            // New idempotency key, try to record it
            if (db_idemp_try_begin(idempotency_key, "hardware.buy", req_fp) != SQLITE_OK) {
                send_enveloped_error(ctx->fd, root, ERR_DB, "Failed to record idempotency key.");
                free(prev_cmd); free(prev_req_fp); free(prev_response_json);
                return 0;
            }
        } else {
            send_enveloped_error(ctx->fd, root, ERR_DB, "Idempotency check failed.");
            free(prev_cmd); free(prev_req_fp); free(prev_response_json);
            return 0;
        }
        free(prev_cmd); free(prev_req_fp); free(prev_response_json);
    }


    // 4. Fetch Hardware Item details
    char hw_name[64];
    int hw_price = 0;
    int hw_max_per_ship = -1; // -1 means use shiptype max
    char hw_category[32];
    int hw_requires_stardock = 0;
    int hw_sold_in_class0 = 0;

    sqlite3_stmt *stmt = NULL;
    const char *sql_get_hw_item = "SELECT name, price, max_per_ship, category, requires_stardock, sold_in_class0 FROM hardware_items WHERE code = ? AND enabled = 1;";
    rc = sqlite3_prepare_v2(db, sql_get_hw_item, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        send_enveloped_error(ctx->fd, root, ERR_DB_QUERY_FAILED, "Failed to get hardware item details.");
        return 0;
    }
    sqlite3_bind_text(stmt, 1, code, -1, SQLITE_STATIC);
    if (sqlite3_step(stmt) != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        send_enveloped_refused(ctx->fd, root, ERR_HARDWARE_INVALID_ITEM, "Invalid or unavailable hardware item.", NULL);
        return 0;
    }
    strncpy(hw_name, (const char*)sqlite3_column_text(stmt, 0), sizeof(hw_name) - 1);
    hw_price = sqlite3_column_int(stmt, 1);
    hw_max_per_ship = sqlite3_column_type(stmt, 2) == SQLITE_NULL ? -1 : sqlite3_column_int(stmt, 2);
    strncpy(hw_category, (const char*)sqlite3_column_text(stmt, 3), sizeof(hw_category) - 1);
    hw_requires_stardock = sqlite3_column_int(stmt, 4);
    hw_sold_in_class0 = sqlite3_column_int(stmt, 5);
    sqlite3_finalize(stmt);
    stmt = NULL;

    // 5. Determine Location Type and check vendor rules
    char location_type[16] = "OTHER";
    int port_found_id = 0; // Renamed to avoid conflict
    
    const char *sql_loc_check = "SELECT id, type FROM ports WHERE sector = ? AND (type = 9 OR type = 0);"; // type 9 for Stardock, 0 for Class-0
    rc = sqlite3_prepare_v2(db, sql_loc_check, -1, &stmt, NULL);
    if (rc == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, sector_id);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            port_found_id = sqlite3_column_int(stmt, 0);
            int port_type = sqlite3_column_int(stmt, 1);
            if (port_type == 9) { // Stardock
                strncpy(location_type, "STARDOCK", sizeof(location_type) - 1);
            } else if (port_type == 0) { // Class-0
                strncpy(location_type, "CLASS0", sizeof(location_type) - 1);
            }
        }
        sqlite3_finalize(stmt);
        stmt = NULL;
    } else {
        send_enveloped_error(ctx->fd, root, ERR_DB_QUERY_FAILED, "Failed to check port details.");
        return 0;
    }

    if (port_found_id == 0 || (strcmp(location_type, "STARDOCK") != 0 && strcmp(location_type, "CLASS0") != 0)) {
        send_enveloped_refused(ctx->fd, root, ERR_HARDWARE_NOT_AVAILABLE, "Hardware can only be purchased at Stardock or Class-0 ports.", NULL);
        return 0;
    }
    if (strcmp(location_type, "CLASS0") == 0 && hw_sold_in_class0 == 0) {
        send_enveloped_refused(ctx->fd, root, ERR_HARDWARE_NOT_AVAILABLE, "This hardware item is not sold at Class-0 ports.", NULL);
        return 0;
    }
    if (strcmp(location_type, "OTHER") == 0) { // Should not happen if port_found_id != 0, but defensive
        send_enveloped_refused(ctx->fd, root, ERR_HARDWARE_NOT_AVAILABLE, "Hardware can only be purchased at Stardock or Class-0 ports.", NULL);
        return 0;
    }


    // 6. Fetch Ship State & Capabilities
    int current_player_credits = 0;
    int current_holds = 0, current_fighters = 0, current_shields = 0;
    int current_genesis = 0, current_detonators = 0, current_probes = 0;
    int current_cloaks = 0; // cloaking_devices in ships table
    int has_transwarp_ship = 0, has_planet_scanner_ship = 0, has_long_range_scanner_ship = 0;
    
    int max_holds = 0, max_fighters = 0, max_shields = 0;
    int max_genesis = 0, max_detonators_st = 0, max_probes_st = 0; // _st suffix for shiptype limits
    int can_transwarp_st = 0, can_planet_scan_st = 0, can_long_range_scan_st = 0, can_cloak_st = 0; // capabilities on shiptype

    // Get player credits and ship current state and shiptype capabilities
    const char *sql_player_ship_state = "SELECT p.credits, s.holds, s.fighters, s.shields, s.genesis, s.detonators, s.probes, s.cloaking_devices, s.has_transwarp, s.has_planet_scanner, s.has_long_range_scanner, st.maxholds, st.maxfighters, st.maxshields, st.maxgenesis, st.max_detonators, st.max_probes, st.can_transwarp, st.can_planet_scan, st.can_long_range_scan, st.can_cloak FROM players p JOIN ships s ON p.ship = s.id JOIN shiptypes st ON s.type_id = st.id WHERE p.id = ? AND s.id = ?;";
    rc = sqlite3_prepare_v2(db, sql_player_ship_state, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        send_enveloped_error(ctx->fd, root, ERR_DB_QUERY_FAILED, "Failed to get player and ship state.");
        return 0;
    }
    sqlite3_bind_int(stmt, 1, player_id);
    sqlite3_bind_int(stmt, 2, ship_id);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        current_player_credits = sqlite3_column_int(stmt, 0);
        current_holds = sqlite3_column_int(stmt, 1);
        current_fighters = sqlite3_column_int(stmt, 2);
        current_shields = sqlite3_column_int(stmt, 3);
        current_genesis = sqlite3_column_int(stmt, 4);
        current_detonators = sqlite3_column_int(stmt, 5);
        current_probes = sqlite3_column_int(stmt, 6);
        current_cloaks = sqlite3_column_int(stmt, 7); // cloaking_devices
        has_transwarp_ship = sqlite3_column_int(stmt, 8);
        has_planet_scanner_ship = sqlite3_column_int(stmt, 9);
        has_long_range_scanner_ship = sqlite3_column_int(stmt, 10);

        max_holds = sqlite3_column_int(stmt, 11);
        max_fighters = sqlite3_column_int(stmt, 12);
        max_shields = sqlite3_column_int(stmt, 13);
        max_genesis = sqlite3_column_int(stmt, 14);
        max_detonators_st = sqlite3_column_int(stmt, 15);
        max_probes_st = sqlite3_column_int(stmt, 16);
        can_transwarp_st = sqlite3_column_int(stmt, 17);
        can_planet_scan_st = sqlite3_column_int(stmt, 18);
        can_long_range_scan_st = sqlite3_column_int(stmt, 19);
        can_cloak_st = sqlite3_column_int(stmt, 20);
    } else {
        sqlite3_finalize(stmt);
        send_enveloped_error(ctx->fd, root, ERR_DB_QUERY_FAILED, "Player or ship not found.");
        return 0;
    }
    sqlite3_finalize(stmt);
    stmt = NULL;

    // 7. Calculate Total Price and Check Funds
    long long total_price = (long long)hw_price * quantity;
    if (current_player_credits < total_price) {
        send_enveloped_refused(ctx->fd, root, ERR_HARDWARE_INSUFFICIENT_FUNDS, "Insufficient credits on ship for purchase.", NULL);
        return 0;
    }

    // 8. Check Ship Capability & Capacity
    int new_value = 0;
    const char *update_col = NULL;
    int *current_ship_val = NULL; // Pointer to the relevant current value on ship
    int max_shiptype_val = 0; // Max limit from shiptypes
    int *has_module_flag = NULL; // Pointer to the relevant has_module_flag on ship

    if (strcasecmp(hw_category, "FIGHTER") == 0) {
        current_ship_val = &current_fighters;
        max_shiptype_val = max_fighters;
        update_col = "fighters";
    } else if (strcasecmp(hw_category, "SHIELD") == 0) {
        current_ship_val = &current_shields;
        max_shiptype_val = max_shields;
        update_col = "shields";
    } else if (strcasecmp(hw_category, "HOLD") == 0) {
        current_ship_val = &current_holds;
        max_shiptype_val = max_holds;
        update_col = "holds";
    } else if (strcasecmp(hw_category, "SPECIAL") == 0) {
        if (strcasecmp(code, "GENESIS") == 0) {
            current_ship_val = &current_genesis;
            max_shiptype_val = max_genesis;
            update_col = "genesis";
        } else if (strcasecmp(code, "DETONATOR") == 0) {
            current_ship_val = &current_detonators;
            max_shiptype_val = max_detonators_st;
            update_col = "detonators";
        } else if (strcasecmp(code, "PROBE") == 0) {
            current_ship_val = &current_probes;
            max_shiptype_val = max_probes_st;
            update_col = "probes";
        } else {
            send_enveloped_refused(ctx->fd, root, ERR_HARDWARE_INVALID_ITEM, "Unsupported special item.", NULL);
            return 0;
        }
    } else if (strcasecmp(hw_category, "MODULE") == 0) {
        if (strcasecmp(code, "CLOAK") == 0) {
            if (!can_cloak_st) {
                send_enveloped_refused(ctx->fd, root, ERR_HARDWARE_NOT_SUPPORTED_BY_SHIP, "Ship type cannot equip Cloaking Device.", NULL);
                return 0;
            }
            if (current_cloaks > 0) { // Already has one
                send_enveloped_refused(ctx->fd, root, ERR_HARDWARE_CAPACITY_EXCEEDED, "Cloaking Device already installed.", NULL);
                return 0;
            }
            current_ship_val = &current_cloaks; // Target cloaking_devices column
            max_shiptype_val = 1; // Modules are typically 1
            update_col = "cloaking_devices";
            has_module_flag = &can_cloak_st;
        } else if (strcasecmp(code, "TWARP") == 0) { // TransWarp Drive
            if (!can_transwarp_st) {
                send_enveloped_refused(ctx->fd, root, ERR_HARDWARE_NOT_SUPPORTED_BY_SHIP, "Ship type cannot equip TransWarp Drive.", NULL);
                return 0;
            }
            if (has_transwarp_ship > 0) {
                 send_enveloped_refused(ctx->fd, root, ERR_HARDWARE_CAPACITY_EXCEEDED, "TransWarp Drive already installed.", NULL);
                 return 0;
            }
            current_ship_val = &has_transwarp_ship;
            max_shiptype_val = 1;
            update_col = "has_transwarp";
            has_module_flag = &can_transwarp_st;
        } else if (strcasecmp(code, "PSCANNER") == 0) { // Planet Scanner
            if (!can_planet_scan_st) {
                send_enveloped_refused(ctx->fd, root, ERR_HARDWARE_NOT_SUPPORTED_BY_SHIP, "Ship type cannot equip Planet Scanner.", NULL);
                return 0;
            }
            if (has_planet_scanner_ship > 0) {
                 send_enveloped_refused(ctx->fd, root, ERR_HARDWARE_CAPACITY_EXCEEDED, "Planet Scanner already installed.", NULL);
                 return 0;
            }
            current_ship_val = &has_planet_scanner_ship;
            max_shiptype_val = 1;
            update_col = "has_planet_scanner";
            has_module_flag = &can_planet_scan_st;
        } else if (strcasecmp(code, "LSCANNER") == 0) { // Long-Range Scanner
            if (!can_long_range_scan_st) {
                send_enveloped_refused(ctx->fd, root, ERR_HARDWARE_NOT_SUPPORTED_BY_SHIP, "Ship type cannot equip Long-Range Scanner.", NULL);
                return 0;
            }
            if (has_long_range_scanner_ship > 0) {
                 send_enveloped_refused(ctx->fd, root, ERR_HARDWARE_CAPACITY_EXCEEDED, "Long-Range Scanner already installed.", NULL);
                 return 0;
            }
            current_ship_val = &has_long_range_scanner_ship;
            max_shiptype_val = 1;
            update_col = "has_long_range_scanner";
            has_module_flag = &can_long_range_scan_st;
        } else {
            send_enveloped_refused(ctx->fd, root, ERR_HARDWARE_INVALID_ITEM, "Unsupported module item.", NULL);
            return 0;
        }
        
        if (!has_module_flag || *has_module_flag == 0) { // Check shiptype capability
            send_enveloped_refused(ctx->fd, root, ERR_HARDWARE_NOT_SUPPORTED_BY_SHIP, "Ship type does not support this module.", NULL);
            return 0;
        }
        
        // For modules, if max_per_ship is -1 (meaning use shiptype default of 1) and already installed, capacity is exceeded
        if (hw_max_per_ship == 1 && *current_ship_val >= 1) { // If max_per_ship is 1, and we already have it
             send_enveloped_refused(ctx->fd, root, ERR_HARDWARE_CAPACITY_EXCEEDED, "Module already installed and cannot be stacked.", NULL);
             return 0;
        }
        // If max_per_ship is NULL (-1) and shiptype max is 1, assume 1 is max, so if current is 1, capacity exceeded
        if (hw_max_per_ship == -1 && max_shiptype_val == 1 && *current_ship_val >= 1) {
            send_enveloped_refused(ctx->fd, root, ERR_HARDWARE_CAPACITY_EXCEEDED, "Module already installed and cannot be stacked.", NULL);
            return 0;
        }
        
        new_value = *current_ship_val + quantity;
        if (hw_max_per_ship != -1) { // If hardware_items specifies a max
            if (new_value > hw_max_per_ship) {
                send_enveloped_refused(ctx->fd, root, ERR_HARDWARE_CAPACITY_EXCEEDED, "Purchase would exceed item-specific limit.", NULL);
                return 0;
            }
        } else { // Use shiptype max
            if (new_value > max_shiptype_val) {
                send_enveloped_refused(ctx->fd, root, ERR_HARDWARE_CAPACITY_EXCEEDED, "Purchase would exceed ship type's maximum capacity.", NULL);
                return 0;
            }
        }

    } else {
        send_enveloped_refused(ctx->fd, root, ERR_HARDWARE_INVALID_ITEM, "Unsupported hardware category.", NULL);
        return 0;
    }

    // Capacity check for non-modules
    if (strcmp(hw_category, "MODULE") != 0 && current_ship_val && max_shiptype_val > 0) {
        new_value = *current_ship_val + quantity;
        if (new_value > max_shiptype_val) {
            send_enveloped_refused(ctx->fd, root, ERR_HARDWARE_CAPACITY_EXCEEDED, "Purchase would exceed ship type's maximum capacity.", NULL);
            return 0;
        }
        // If hw_max_per_ship is set, it might override shiptype_val
        if (hw_max_per_ship != -1 && new_value > hw_max_per_ship) {
            send_enveloped_refused(ctx->fd, root, ERR_HARDWARE_CAPACITY_EXCEEDED, "Purchase would exceed item-specific limit.", NULL);
            return 0;
        }
    }


    // 9. Apply Changes
    long long new_player_credits = 0;
    int old_value = 0; // The value before the change
    
    // Deduct credits
    const char *sql_deduct_credits = "UPDATE players SET credits = credits - ? WHERE id = ?;";
    rc = sqlite3_prepare_v2(db, sql_deduct_credits, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        send_enveloped_error(ctx->fd, root, ERR_DB_QUERY_FAILED, "Failed to prepare credit deduction.");
        return 0;
    }
    sqlite3_bind_int64(stmt, 1, total_price);
    sqlite3_bind_int(stmt, 2, player_id);
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        send_enveloped_error(ctx->fd, root, ERR_DB, "Failed to deduct credits.");
        return 0;
    }
    sqlite3_finalize(stmt);
    stmt = NULL;

    // Get new player credits for response
    rc = sqlite3_prepare_v2(db, "SELECT credits FROM players WHERE id = ?;", -1, &stmt, NULL);
    if (rc == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, player_id);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            new_player_credits = sqlite3_column_int(stmt, 0);
        }
    }
    sqlite3_finalize(stmt);
    stmt = NULL;

    // Update ship hardware
    char *sql_update_ship_hw = NULL;
    if (strcmp(hw_category, "MODULE") == 0) {
        sql_update_ship_hw = sqlite3_mprintf("UPDATE ships SET %q = 1 WHERE id = ?;", update_col);
    } else {
        sql_update_ship_hw = sqlite3_mprintf("UPDATE ships SET %q = %q + ? WHERE id = ?;", update_col, update_col);
    }

    if (!sql_update_ship_hw) {
        send_enveloped_error(ctx->fd, root, ERR_OOM, "Memory allocation failed for ship update.");
        return 0;
    }

    rc = sqlite3_prepare_v2(db, sql_update_ship_hw, -1, &stmt, NULL);
    sqlite3_free(sql_update_ship_hw);
    if (rc != SQLITE_OK) {
        send_enveloped_error(ctx->fd, root, ERR_DB_QUERY_FAILED, "Failed to prepare ship hardware update.");
        return 0;
    }

    if (strcmp(hw_category, "MODULE") != 0) { // For non-modules, bind quantity
        sqlite3_bind_int(stmt, 1, quantity);
        sqlite3_bind_int(stmt, 2, ship_id);
    } else { // For modules, only ship_id
        sqlite3_bind_int(stmt, 1, ship_id);
    }
    
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        send_enveloped_error(ctx->fd, root, ERR_DB, "Failed to update ship hardware.");
        return 0;
    }
    sqlite3_finalize(stmt);
    stmt = NULL;
    

    // Log hardware.purchase event
    json_t *event_payload = json_pack(
        "{s:i, s:i, s:s, s:i, s:i, s:i}",
        "player_id", player_id,
        "ship_id", ship_id,
        "hardware_code", code,
        "quantity", quantity,
        "price_per_unit", hw_price,
        "total_price", total_price
    );
    db_log_engine_event(time(NULL), "hardware.purchase", "player", player_id, sector_id, event_payload, idempotency_key);


    // 10. Construct Response - Fetch updated ship state for convenience
    // Re-fetch ship state after update
    int final_holds = current_holds, final_fighters = current_fighters, final_shields = current_shields;
    int final_genesis = current_genesis, final_detonators = current_detonators, final_probes = current_probes;
    int final_cloaks = current_cloaks; // cloaking_devices in ships table
    int final_has_transwarp = has_transwarp_ship, final_has_planet_scanner = has_planet_scanner_ship, final_has_long_range_scanner = has_long_range_scanner_ship;

    const char *sql_final_ship_state = "SELECT holds, fighters, shields, genesis, detonators, probes, cloaking_devices, has_transwarp, has_planet_scanner, has_long_range_scanner FROM ships WHERE id = ?;";
    rc = sqlite3_prepare_v2(db, sql_final_ship_state, -1, &stmt, NULL);
    if (rc == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, ship_id);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            final_holds = sqlite3_column_int(stmt, 0);
            final_fighters = sqlite3_column_int(stmt, 1);
            final_shields = sqlite3_column_int(stmt, 2);
            final_genesis = sqlite3_column_int(stmt, 3);
            final_detonators = sqlite3_column_int(stmt, 4);
            final_probes = sqlite3_column_int(stmt, 5);
            final_cloaks = sqlite3_column_int(stmt, 6);
            final_has_transwarp = sqlite3_column_int(stmt, 7);
            final_has_planet_scanner = sqlite3_column_int(stmt, 8);
            final_has_long_range_scanner = sqlite3_column_int(stmt, 9);
        }
    }
    sqlite3_finalize(stmt);
    
    json_t *ship_snapshot = json_pack(
        "{s:i, s:i, s:i, s:i, s:i, s:i, s:i, s:i, s:i}",
        "holds", final_holds,
        "fighters", final_fighters,
        "shields", final_shields,
        "genesis_torps", final_genesis,
        "detonators", final_detonators,
        "probes", final_probes,
        "cloaks_installed", final_cloaks,
        "has_transwarp", final_has_transwarp,
        "has_planet_scanner", final_has_planet_scanner,
        "has_long_range_scanner", final_has_long_range_scanner
    );

    json_t *response_data = json_pack(
        "{s:s, s:i, s:i, s:o}",
        "code", code,
        "quantity", quantity,
        "credits_spent", total_price,
        "ship", ship_snapshot
    );
    send_enveloped_ok(ctx->fd, root, "hardware.purchase_v1", response_data);

    // Record idempotency response
    if (idempotency_key) {
        char *response_str = json_dumps(response_data, 0);
        db_idemp_store_response(idempotency_key, response_str);
        free(response_str);
    }
    
    return 0;
}