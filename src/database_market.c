#include "database_market.h"
#include "database.h" // For db_get_handle, db_begin, db_commit, db_rollback
#include "server_log.h" // For LOGE, LOGI
#include <string.h> // For strncpy, strcmp
#include <stdlib.h> // For malloc, free

// Helper to insert a new commodity order
// Returns the new order's ID on success, or -1 on failure.
int db_insert_commodity_order(
    sqlite3 *db,
    const char *actor_type,
    int actor_id,
    int commodity_id,
    const char *side,
    int quantity,
    int price,
    long long expires_at
) {
    sqlite3_stmt *stmt;
    int rc; // Declare rc here
    const char *sql =
        "INSERT INTO commodity_orders ("
        "actor_type, actor_id, commodity_id, side, quantity, price, ts, expires_at) "
        "VALUES (?, ?, ?, ?, ?, ?, strftime('%s', 'now'), ?);";

    rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        LOGE("db_insert_commodity_order: Failed to prepare statement: %s", sqlite3_errmsg(db));
        return -1;
    }

    sqlite3_bind_text(stmt, 1, actor_type, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, actor_id);
    sqlite3_bind_int(stmt, 3, commodity_id);
    sqlite3_bind_text(stmt, 4, side, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 5, quantity);
    sqlite3_bind_int(stmt, 6, price);
    if (expires_at > 0) {
        sqlite3_bind_int64(stmt, 7, expires_at);
    } else {
        sqlite3_bind_null(stmt, 7);
    }

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        LOGE("db_insert_commodity_order: Failed to execute statement: %s", sqlite3_errmsg(db));
        sqlite3_finalize(stmt);
        return -1;
    }

    int new_order_id = sqlite3_last_insert_rowid(db);
    sqlite3_finalize(stmt);
    return new_order_id;
}

// Helper to update an existing commodity order's quantity, status, and filled_quantity.
// Returns SQLITE_OK on success.
int db_update_commodity_order(
    sqlite3 *db,
    int order_id,
    int new_quantity,
    int new_filled_quantity,
    const char *new_status
) {
    sqlite3_stmt *stmt;
    const char *sql =
        "UPDATE commodity_orders SET quantity = ?, filled_quantity = ?, status = ? "
        "WHERE id = ?;";

    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        LOGE("db_update_commodity_order: Failed to prepare statement: %s", sqlite3_errmsg(db));
        return rc;
    }

    sqlite3_bind_int(stmt, 1, new_quantity);
    sqlite3_bind_int(stmt, 2, new_filled_quantity);
    sqlite3_bind_text(stmt, 3, new_status, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 4, order_id);

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        LOGE("db_update_commodity_order: Failed to execute statement: %s", sqlite3_errmsg(db));
        sqlite3_finalize(stmt);
        return rc;
    }

    sqlite3_finalize(stmt);
    return SQLITE_OK;
}

// Helper to cancel open commodity orders for a given actor and commodity on a specific side.
// Returns SQLITE_OK on success.
int db_cancel_commodity_orders_for_actor_and_commodity(
    sqlite3 *db,
    const char *actor_type,
    int actor_id,
    int commodity_id,
    const char *side
) {
    sqlite3_stmt *stmt;
    const char *sql =
        "UPDATE commodity_orders SET status = 'cancelled' "
        "WHERE actor_type = ? AND actor_id = ? AND commodity_id = ? AND side = ? AND status = 'open';";

    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        LOGE("db_cancel_commodity_orders_for_actor_and_commodity: Failed to prepare statement: %s", sqlite3_errmsg(db));
        return rc;
    }

    sqlite3_bind_text(stmt, 1, actor_type, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, actor_id);
    sqlite3_bind_int(stmt, 3, commodity_id);
    sqlite3_bind_text(stmt, 4, side, -1, SQLITE_STATIC);

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        LOGE("db_cancel_commodity_orders_for_actor_and_commodity: Failed to execute statement: %s", sqlite3_errmsg(db));
        sqlite3_finalize(stmt);
        return rc;
    }

    sqlite3_finalize(stmt);
    return SQLITE_OK;
}

// Helper to cancel open commodity orders for a given port and commodity on a specific side.
// Returns SQLITE_OK on success.
int db_cancel_commodity_orders_for_port_and_commodity(
    sqlite3 *db,
    int port_id,
    int commodity_id,
    const char *side
) {
    return db_cancel_commodity_orders_for_actor_and_commodity(db, "port", port_id, commodity_id, side);
}

// Helper to load open commodity orders for a given commodity and side.
// Returns a dynamically allocated array of commodity_order_t structs.
int db_get_open_order(
    sqlite3 *db,
    const char *actor_type,
    int actor_id,
    int commodity_id,
    const char *side,
    commodity_order_t *out_order
) {
    if (!out_order || !actor_type) return SQLITE_MISUSE;

    sqlite3_stmt *stmt;
    const char *sql =
        "SELECT id, actor_type, actor_id, commodity_id, side, quantity, price, status, ts, expires_at, filled_quantity "
        "FROM commodity_orders "
        "WHERE actor_type = ? AND actor_id = ? AND commodity_id = ? AND side = ? AND status = 'open' "
        "LIMIT 1;";

    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        LOGE("db_get_open_order: Failed to prepare statement: %s", sqlite3_errmsg(db));
        return rc;
    }

    sqlite3_bind_text(stmt, 1, actor_type, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, actor_id);
    sqlite3_bind_int(stmt, 3, commodity_id);
    sqlite3_bind_text(stmt, 4, side, -1, SQLITE_STATIC);

    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        out_order->id = sqlite3_column_int(stmt, 0);
        strncpy(out_order->actor_type, (const char*)sqlite3_column_text(stmt, 1), sizeof(out_order->actor_type) - 1);
        out_order->actor_type[sizeof(out_order->actor_type) - 1] = '\0';
        out_order->actor_id = sqlite3_column_int(stmt, 2);
        out_order->commodity_id = sqlite3_column_int(stmt, 3);
        strncpy(out_order->side, (const char*)sqlite3_column_text(stmt, 4), sizeof(out_order->side) - 1);
        out_order->side[sizeof(out_order->side) - 1] = '\0';
        out_order->quantity = sqlite3_column_int(stmt, 5);
        out_order->price = sqlite3_column_int(stmt, 6);
        strncpy(out_order->status, (const char*)sqlite3_column_text(stmt, 7), sizeof(out_order->status) - 1);
        out_order->status[sizeof(out_order->status) - 1] = '\0';
        out_order->ts = sqlite3_column_int64(stmt, 8);
        out_order->expires_at = sqlite3_column_int64(stmt, 9);
        out_order->filled_quantity = sqlite3_column_int(stmt, 10);
        out_order->remaining_quantity = out_order->quantity - out_order->filled_quantity;
        rc = SQLITE_OK;
    } else if (rc == SQLITE_DONE) {
        rc = SQLITE_NOTFOUND;
    } else {
        LOGE("db_get_open_order: Failed to step statement: %s", sqlite3_errmsg(db));
    }

    sqlite3_finalize(stmt);
    return rc;
}

// Helper to get a specific open order for a port.
// Returns SQLITE_OK if found (populating out_order), SQLITE_NOTFOUND if not found, or error code.
int db_get_open_order_for_port(
    sqlite3 *db,
    int port_id,
    int commodity_id,
    const char *side,
    commodity_order_t *out_order
) {
    return db_get_open_order(db, "port", port_id, commodity_id, side, out_order);
}

// Helper to load open commodity orders for a given commodity and side.
// Returns a dynamically allocated array of commodity_order_t structs.
// The caller is responsible for freeing the returned array.
// The 'count' parameter will be set to the number of orders loaded.
commodity_order_t* db_load_open_orders_for_commodity(
    sqlite3 *db,
    int commodity_id,
    const char *side,
    int *count
) {
    *count = 0;
    sqlite3_stmt *stmt;
    char *sql = NULL;

    // Adjust sort order based on side
    if (strcmp(side, "buy") == 0) {
        sql = sqlite3_mprintf(
            "SELECT id, actor_type, actor_id, commodity_id, side, quantity, price, status, ts, expires_at, filled_quantity "
            "FROM commodity_orders "
            "WHERE commodity_id = ? AND side = ? AND status = 'open' "
            "ORDER BY price DESC, ts ASC;");
    } else if (strcmp(side, "sell") == 0) {
        sql = sqlite3_mprintf(
            "SELECT id, actor_type, actor_id, commodity_id, side, quantity, price, status, ts, expires_at, filled_quantity "
            "FROM commodity_orders "
            "WHERE commodity_id = ? AND side = ? AND status = 'open' "
            "ORDER BY price ASC, ts ASC;");
    } else {
        LOGE("db_load_open_orders_for_commodity: Invalid side '%s'", side);
        return NULL;
    }

    if (!sql) {
        LOGE("db_load_open_orders_for_commodity: sqlite3_mprintf failed");
        return NULL;
    }

    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    sqlite3_free(sql); // Free the SQL string after preparation
    if (rc != SQLITE_OK) {
        LOGE("db_load_open_orders_for_commodity: Failed to prepare statement: %s", sqlite3_errmsg(db));
        return NULL;
    }

    sqlite3_bind_int(stmt, 1, commodity_id);
    sqlite3_bind_text(stmt, 2, side, -1, SQLITE_STATIC);

    // Initial pass to count rows
    int num_rows = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        num_rows++;
    }
    sqlite3_reset(stmt); // Reset to re-read from the beginning

    if (num_rows == 0) {
        sqlite3_finalize(stmt);
        return NULL;
    }

    commodity_order_t *orders = (commodity_order_t *)malloc(sizeof(commodity_order_t) * num_rows);
    if (!orders) {
        LOGE("db_load_open_orders_for_commodity: Failed to allocate memory for orders.");
        sqlite3_finalize(stmt);
        return NULL;
    }

    int i = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        orders[i].id = sqlite3_column_int(stmt, 0);
        strncpy(orders[i].actor_type, (const char*)sqlite3_column_text(stmt, 1), sizeof(orders[i].actor_type) - 1);
        orders[i].actor_type[sizeof(orders[i].actor_type) - 1] = '\0';
        orders[i].actor_id = sqlite3_column_int(stmt, 2);
        orders[i].commodity_id = sqlite3_column_int(stmt, 3);
        strncpy(orders[i].side, (const char*)sqlite3_column_text(stmt, 4), sizeof(orders[i].side) - 1);
        orders[i].side[sizeof(orders[i].side) - 1] = '\0';
        orders[i].quantity = sqlite3_column_int(stmt, 5);
        orders[i].price = sqlite3_column_int(stmt, 6);
        strncpy(orders[i].status, (const char*)sqlite3_column_text(stmt, 7), sizeof(orders[i].status) - 1);
        orders[i].status[sizeof(orders[i].status) - 1] = '\0';
        orders[i].ts = sqlite3_column_int64(stmt, 8);
        orders[i].expires_at = sqlite3_column_int64(stmt, 9);
        orders[i].filled_quantity = sqlite3_column_int(stmt, 10);
        orders[i].remaining_quantity = orders[i].quantity - orders[i].filled_quantity;
        i++;
    }
    sqlite3_finalize(stmt);
    *count = num_rows;
    return orders;
}


// Helper to insert a new commodity trade.
// Returns the new trade's ID on success, or -1 on failure.
int db_insert_commodity_trade(
    sqlite3 *db,
    int buy_order_id,
    int sell_order_id,
    int quantity,
    int price,
    const char *buyer_actor_type,
    int buyer_actor_id,
    const char *seller_actor_type,
    int seller_actor_id,
    int settlement_tx_buy, // bank_tx.id
    int settlement_tx_sell // bank_tx.id
) {
    sqlite3_stmt *stmt;
    const char *sql =
        "INSERT INTO commodity_trades ("
        "buy_order_id, sell_order_id, quantity, price, trade_at, "
        "buyer_actor_type, buyer_actor_id, seller_actor_type, seller_actor_id, "
        "settlement_tx_buy, settlement_tx_sell) "
        "VALUES (?, ?, ?, ?, strftime('%s', 'now'), ?, ?, ?, ?, ?, ?);";

    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        LOGE("db_insert_commodity_trade: Failed to prepare statement: %s", sqlite3_errmsg(db));
        return -1;
    }

    sqlite3_bind_int(stmt, 1, buy_order_id);
    sqlite3_bind_int(stmt, 2, sell_order_id);
    sqlite3_bind_int(stmt, 3, quantity);
    sqlite3_bind_int(stmt, 4, price);
    sqlite3_bind_text(stmt, 5, buyer_actor_type, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 6, buyer_actor_id);
    sqlite3_bind_text(stmt, 7, seller_actor_type, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 8, seller_actor_id);
    sqlite3_bind_int(stmt, 9, settlement_tx_buy);
    sqlite3_bind_int(stmt, 10, settlement_tx_sell);

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        LOGE("db_insert_commodity_trade: Failed to execute statement: %s", sqlite3_errmsg(db));
        sqlite3_finalize(stmt);
        return -1;
    }

    int new_trade_id = sqlite3_last_insert_rowid(db);
    sqlite3_finalize(stmt);
    return new_trade_id;
}

// Helper to list orders for a specific actor (read-only, for diagnostics)
// Returns a JSON array of order objects (caller owns reference)
json_t *db_list_actor_orders(sqlite3 *db, const char *actor_type, int actor_id) {
    json_t *orders = json_array();
    sqlite3_stmt *stmt;
    const char *sql = 
        "SELECT id, side, commodity_id, quantity, filled_quantity, price, status, ts, expires_at "
        "FROM commodity_orders "
        "WHERE actor_type = ? AND actor_id = ? "
        "ORDER BY status ASC, ts DESC;";

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        LOGE("db_list_actor_orders: Prepare failed: %s", sqlite3_errmsg(db));
        return orders; // Return empty array on error
    }

    sqlite3_bind_text(stmt, 1, actor_type, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, actor_id);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        int id = sqlite3_column_int(stmt, 0);
        const char *side = (const char *)sqlite3_column_text(stmt, 1);
        int comm_id = sqlite3_column_int(stmt, 2);
        int qty = sqlite3_column_int(stmt, 3);
        int filled = sqlite3_column_int(stmt, 4);
        int price = sqlite3_column_int(stmt, 5);
        const char *status = (const char *)sqlite3_column_text(stmt, 6);
        long long ts_val = sqlite3_column_int64(stmt, 7);
        long long expires = sqlite3_column_int64(stmt, 8); // Can be NULL, but will be 0

        json_t *obj = json_object();
        json_object_set_new(obj, "order_id", json_integer(id));
        json_object_set_new(obj, "side", json_string(side ? side : ""));
        json_object_set_new(obj, "commodity_id", json_integer(comm_id));
        json_object_set_new(obj, "quantity", json_integer(qty));
        json_object_set_new(obj, "remaining_quantity", json_integer(qty - filled));
        json_object_set_new(obj, "price", json_integer(price));
        json_object_set_new(obj, "status", json_string(status ? status : ""));
        json_object_set_new(obj, "ts", json_integer(ts_val));
        json_object_set_new(obj, "expires_at", json_integer(expires));
        
        json_array_append_new(orders, obj);
    }
    
    sqlite3_finalize(stmt);
    return orders;
}

// Helper to list open orders for a specific port (read-only, for diagnostics)
// Returns a JSON array of order objects (caller owns reference)
json_t *db_list_port_orders(sqlite3 *db, int port_id) {
    return db_list_actor_orders(db, "port", port_id);
}


json_t *db_orders_summary(sqlite3 *db, int filter_commodity_id) {
    json_t *summary = json_object();
    sqlite3_stmt *stmt;
    const char *sql_all = 
        "SELECT commodity_id, side, COUNT(*), SUM(quantity - filled_quantity) "
        "FROM commodity_orders "
        "WHERE status = 'open' "
        "GROUP BY commodity_id, side;";
        
    const char *sql_filter = 
        "SELECT commodity_id, side, COUNT(*), SUM(quantity - filled_quantity) "
        "FROM commodity_orders "
        "WHERE status = 'open' AND commodity_id = ? "
        "GROUP BY commodity_id, side;";

    const char *sql = (filter_commodity_id > 0) ? sql_filter : sql_all;

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        LOGE("db_orders_summary: Prepare failed: %s", sqlite3_errmsg(db));
        return summary;
    }

    if (filter_commodity_id > 0) {
        sqlite3_bind_int(stmt, 1, filter_commodity_id);
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        int comm_id = sqlite3_column_int(stmt, 0);
        const char *side = (const char *)sqlite3_column_text(stmt, 1);
        int count = sqlite3_column_int(stmt, 2);
        long long total_qty = sqlite3_column_int64(stmt, 3);

        char key[32];
        snprintf(key, sizeof(key), "%d", comm_id);
        
        json_t *comm_obj = json_object_get(summary, key);
        if (!comm_obj) {
            comm_obj = json_object();
            json_object_set_new(summary, key, comm_obj);
        }
        
        if (side && strcmp(side, "buy") == 0) {
            json_object_set_new(comm_obj, "count_buy", json_integer(count));
            json_object_set_new(comm_obj, "total_qty_buy", json_integer(total_qty));
        } else if (side && strcmp(side, "sell") == 0) {
            json_object_set_new(comm_obj, "count_sell", json_integer(count));
            json_object_set_new(comm_obj, "total_qty_sell", json_integer(total_qty));
        }
    }

    sqlite3_finalize(stmt);
    return summary;
}
