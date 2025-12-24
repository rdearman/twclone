#include <string.h>             // For strncpy, strcmp
#include <stdlib.h>             // For malloc, free
// local includes
#include "database_market.h"
#include "database.h"           // For db_get_handle, db_begin, db_commit, db_rollback
#include "server_log.h"         // For LOGE, LOGI
#include "db/db_api.h"
#include "common.h"

// Helper to insert a new commodity order
// Returns the new order's ID on success, or -1 on failure.
int
db_insert_commodity_order (db_t *db,
                           const char *actor_type,
                           int actor_id,
                           const char *location_type,
                           int location_id,
                           int commodity_id,
                           const char *side,
                           int quantity,
			   int price,
			   long long expires_at)
{

  int64_t now = time(NULL);
  db_error_t err;
  db_error_clear(&err);

  db_bind_t params[9];
  params[0] = db_bind_text(actor_type);
  params[1] = db_bind_i32(actor_id);
  params[2] = db_bind_text(location_type);
  params[3] = db_bind_i32(location_id);
  params[4] = db_bind_i32(commodity_id);
  params[5] = db_bind_text(side);
  params[6] = db_bind_i32(quantity);
  params[7] = db_bind_i32(price);
  params[8] = db_bind_i64(now);
  params[9] = (expires_at > 0)
    ? db_bind_i64(expires_at)
    : db_bind_null();
  
  const char *sql =
    "INSERT INTO commodity_orders ("
    "actor_type, actor_id, location_type, location_id, commodity_id, side, quantity, filled_quantity, price, ts, expires_at) "
    "VALUES ($1, $2, $3, $4, $5, $6, $7, 0, $8, $9, $10)";

    int64_t new_order_id = -1;

    if (!db_exec_insert_id(db, sql, params, 10, &new_order_id, &err)) {
        LOGE("db_insert_commodity_order: insert failed: %s (code=%d backend=%d)",
             err.message, err.code, err.backend_code);
        return -1;
    }

    return (int)new_order_id;
}


// Helper to update an existing commodity order's quantity, status, and filled_quantity.
int
db_update_commodity_order (db_t *db,
                           int order_id,
                           int new_quantity,
                           int new_filled_quantity,
			   const char *new_status)
{

  db_error_t err;
  db_error_clear(&err);
  db_bind_t params[4];
  params[0] = db_bind_i32(order_id);
  params[1] = db_bind_i32(new_quantity);
  params[2] = db_bind_i32(new_filled_quantity);
  params[3] = db_bind_text(new_status);  

  const char *sql =
    "UPDATE commodity_orders SET quantity = $2, filled_quantity = $3, status = $4 "
    "WHERE id = $1;";

    if (!db_exec(db, sql, params, 4, &err)) {
        LOGE("db_update_commodity_order: update failed: %s (code=%d backend=%d)",
             err.message, err.code, err.backend_code);
        return -1;
    }

    return 0;
}


// Helper to cancel open commodity orders for a given actor and commodity on a specific side.
int
db_cancel_commodity_orders_for_actor_and_commodity (db_t *db,
                                                    const char *actor_type,
                                                    int actor_id,
                                                    int commodity_id,
                                                    const char *side)
{

  db_error_t err;
  db_error_clear(&err);
  db_bind_t params[4];
  params[0] = db_bind_text(actor_type);
  params[1] = db_bind_i32(actor_id);
  params[2] = db_bind_i32(commodity_id);
  params[3] = db_bind_text(side);  

  const char *sql =
    "UPDATE commodity_orders SET status = 'cancelled' "
    "WHERE actor_type = $1 AND actor_id = $2 AND commodity_id = $3 AND side = $4 AND status = 'open';";

    if (!db_exec(db, sql, params, 4,  &err)) {
        LOGE("db_cancel_commodity_orders_for_actor_and_commodity: update failed: %s (code=%d backend=%d)",
             err.message, err.code, err.backend_code);
        return -1;
    }

    return 0;
  
}

//////////////////////////////////////

// Helper to cancel open commodity orders for a given port and commodity on a specific side.

int
db_cancel_commodity_orders_for_port_and_commodity (db_t *db,
                                                   int port_id,
                                                   int commodity_id,
                                                   const char *side)
{
  return db_cancel_commodity_orders_for_actor_and_commodity (db,
                                                             "port",
                                                             port_id,
                                                             commodity_id,
                                                             side);
}


// Helper to load open commodity orders for a given commodity and side.
// Returns a dynamically allocated array of commodity_order_t structs.
int
db_get_open_order (db_t *db,
                   const char *actor_type,
                   int actor_id,
                   int commodity_id,
                   const char *side,
                   commodity_order_t *out_order)
{
    if (!out_order || !actor_type) {
        return ERR_DB_NO_ROWS;
    }

    db_error_t err;
    db_error_clear(&err);

    db_bind_t params[4];
    params[0] = db_bind_text(actor_type);
    params[1] = db_bind_i32(actor_id);
    params[2] = db_bind_i32(commodity_id);
    params[3] = db_bind_text(side);

    const char *sql =
        "SELECT id, actor_type, actor_id, commodity_id, side, quantity, price, status, ts, expires_at, filled_quantity "
        "FROM commodity_orders "
        "WHERE actor_type = $1 AND actor_id = $2 AND commodity_id = $3 AND side = $4 AND status = 'open' "
        "LIMIT 1;";

    db_res_t *res = NULL;
    if (!db_query(db, sql, params, 4, &res, &err)) {
        LOGE("db_get_open_order: query failed: %s (code=%d backend=%d)",
             err.message, err.code, err.backend_code);
        return -1;
    }

    int rc = ERR_DB_NO_ROWS;   /* default if no row */
    if (db_res_step(res, &err)) {
        /* Column 0..10 match SELECT list */
        out_order->id = (int) db_res_col_i64(res, 0, &err);

        h_copy_cstr(out_order->actor_type, sizeof(out_order->actor_type),
                    db_res_col_text(res, 1, &err));

        out_order->actor_id = (int) db_res_col_i64(res, 2, &err);
        out_order->commodity_id = (int) db_res_col_i64(res, 3, &err);

        h_copy_cstr(out_order->side, sizeof(out_order->side),
                    db_res_col_text(res, 4, &err));

        out_order->quantity = (int) db_res_col_i64(res, 5, &err);
        out_order->price = (int) db_res_col_i64(res, 6, &err);

        h_copy_cstr(out_order->status, sizeof(out_order->status),
                    db_res_col_text(res, 7, &err));

        /* Handle NULL expires_at safely if your schema allows it */
        out_order->ts = db_res_col_i64(res, 8, &err);

        if (db_res_col_is_null(res, 9)) {
            out_order->expires_at = 0;
        } else {
            out_order->expires_at = db_res_col_i64(res, 9, &err);
        }

        out_order->filled_quantity = (int) db_res_col_i64(res, 10, &err);
        out_order->remaining_quantity = out_order->quantity - out_order->filled_quantity;

        rc = 0;
    } else {
        /* db_res_step() returned false: either end-of-rows (err.code==0) or error */
        if (err.code != 0) {
            LOGE("db_get_open_order: step failed: %s (code=%d backend=%d)",
                 err.message, err.code, err.backend_code);
            rc = -1;
        } else {
            rc = ERR_DB_NO_ROWS;
        }
    }

    db_res_finalize(res);
    return rc;
}


// Helper to get a specific open order for a port.

int
db_get_open_order_for_port (db_t *db,
                            int port_id,
                            int commodity_id,
                            const char *side, commodity_order_t *out_order)
{
  return db_get_open_order (db, "port", port_id, commodity_id, side,
                            out_order);
}


// Helper to load open commodity orders for a given commodity and side.
// Returns a dynamically allocated array of commodity_order_t structs.
// The caller is responsible for freeing the returned array.
// The 'count' parameter will be set to the number of orders loaded.
commodity_order_t *
db_load_open_orders_for_commodity (db_t *db,
                                   int commodity_id,
                                   const char *side,
                                   int *count)
{
    if (!count || !side) return NULL;
    *count = 0;

    db_error_t err;
    db_error_clear(&err);

    /* Only two binds: $1=commodity_id, $2=side (or swap) */
    db_bind_t params[2];
    params[0] = db_bind_i32(commodity_id);
    params[1] = db_bind_text(side);

    const char *sql = NULL;

    if (strcmp(side, "buy") == 0) {
        sql =
            "SELECT id, actor_type, actor_id, commodity_id, side, quantity, price, status, ts, expires_at, filled_quantity "
            "FROM commodity_orders "
            "WHERE commodity_id = $1 AND side = $2 AND status = 'open' "
            "ORDER BY price DESC, ts ASC;";
    } else if (strcmp(side, "sell") == 0) {
        sql =
            "SELECT id, actor_type, actor_id, commodity_id, side, quantity, price, status, ts, expires_at, filled_quantity "
            "FROM commodity_orders "
            "WHERE commodity_id = $1 AND side = $2 AND status = 'open' "
            "ORDER BY price ASC, ts ASC;";
    } else {
        LOGE("db_load_open_orders_for_commodity: Invalid side '%s'", side);
        return NULL;
    }

    db_res_t *res = NULL;
    if (!db_query(db, sql, params, 2, &res, &err)) {
        LOGE("db_load_open_orders_for_commodity: query failed: %s (code=%d backend=%d)",
             err.message, err.code, err.backend_code);
        return NULL;
    }

    /* One-pass: grow array */
    size_t cap = 16;
    commodity_order_t *orders = malloc(sizeof(*orders) * cap);
    if (!orders) {
        LOGE("db_load_open_orders_for_commodity: OOM");
        db_res_finalize(res);
        return NULL;
    }

    while (db_res_step(res, &err)) {
        if ((size_t)(*count) == cap) {
            cap *= 2;
            commodity_order_t *tmp = realloc(orders, sizeof(*orders) * cap);
            if (!tmp) {
                LOGE("db_load_open_orders_for_commodity: realloc failed");
                free(orders);
                db_res_finalize(res);
                return NULL;
            }
            orders = tmp;
        }

        commodity_order_t *o = &orders[*count];

        o->id = (int) db_res_col_i64(res, 0, &err);
        h_copy_cstr(o->actor_type, sizeof(o->actor_type), db_res_col_text(res, 1, &err));
        o->actor_id = (int) db_res_col_i64(res, 2, &err);
        o->commodity_id = (int) db_res_col_i64(res, 3, &err);
        h_copy_cstr(o->side, sizeof(o->side), db_res_col_text(res, 4, &err));
        o->quantity = (int) db_res_col_i64(res, 5, &err);
        o->price = (int) db_res_col_i64(res, 6, &err);
        h_copy_cstr(o->status, sizeof(o->status), db_res_col_text(res, 7, &err));
        o->ts = db_res_col_i64(res, 8, &err);

        if (db_res_col_is_null(res, 9)) {
            o->expires_at = 0;
        } else {
            o->expires_at = db_res_col_i64(res, 9, &err);
        }

        o->filled_quantity = (int) db_res_col_i64(res, 10, &err);
        o->remaining_quantity = o->quantity - o->filled_quantity;

        (*count)++;
    }

    /* If loop stopped due to error (not just EOF) */
    if (err.code != 0) {
        LOGE("db_load_open_orders_for_commodity: step failed: %s (code=%d backend=%d)",
             err.message, err.code, err.backend_code);
        free(orders);
        db_res_finalize(res);
        return NULL;
    }

    db_res_finalize(res);

    if (*count == 0) {
        free(orders);
        return NULL;
    }

    /* Optional: shrink to fit */
    commodity_order_t *shrink = realloc(orders, sizeof(*orders) * (size_t)(*count));
    if (shrink) orders = shrink;

    return orders;
}

////// WORKING HERE ////////
// Helper to insert a new commodity trade.
// Returns the new trade's ID on success, or -1 on failure.
int
db_insert_commodity_trade (db_t *db,
                           int buy_order_id,
                           int sell_order_id,
                           int quantity,
                           int price,
                           const char *buyer_actor_type,
                           int buyer_actor_id,
                           const char *seller_actor_type,
                           int seller_actor_id,
                           int settlement_tx_buy,
                           int settlement_tx_sell
                           )
{

  int64_t now = time(NULL);
  db_error_t err;
  db_error_clear(&err);

  db_bind_t params[10];

  params[0] = db_bind_i32(buy_order_id);
  params[1] = db_bind_i32(sell_order_id);
  params[2] = db_bind_i32(quantity);
  params[3] = db_bind_i32(price);
  params[4] = db_bind_text(buyer_actor_type);
  params[5] = db_bind_i64(now);  
  params[6] = db_bind_i32(buyer_actor_id);
  params[7] = db_bind_text(seller_actor_type);
  params[8] = db_bind_i32(seller_actor_id);
  params[9] = db_bind_i32(settlement_tx_buy);
  params[10] = db_bind_i32(settlement_tx_sell);
  
  const char *sql =
    "INSERT INTO commodity_trades ("
    "buy_order_id, sell_order_id, quantity, price, trade_at, "
    "buyer_actor_type, buyer_actor_id, seller_actor_type, seller_actor_id, "
    "settlement_tx_buy, settlement_tx_sell) "
    "VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11);";

    int64_t new_order_id = -1;

    if (!db_exec_insert_id(db, sql, params, 11, &new_order_id, &err)) {
        LOGE("db_insert_commodity_trade: insert failed: %s (code=%d backend=%d)",
             err.message, err.code, err.backend_code);
        return -1;
    }

    return (int)new_order_id;
}


// Helper to list orders for a specific actor (read-only, for diagnostics)
// Returns a JSON array of order objects (caller owns reference)
json_t *
db_list_actor_orders (db_t *db, const char *actor_type, int actor_id)
{
  
  json_t *orders = json_array ();

  int64_t now = time(NULL);
  db_error_t err;
  db_error_clear(&err);

  db_bind_t params[2];

  params[0] = db_bind_text(actor_type);
  params[1] = db_bind_i32(actor_id);
  
  const char *sql =
    "SELECT id, side, commodity_id, quantity, filled_quantity, price, status, ts, expires_at "
    "FROM commodity_orders "
    "WHERE actor_type = $1 AND actor_id = $2 " "ORDER BY status ASC, ts DESC;";

    db_res_t *res = NULL;
    if (!db_query(db, sql, params, 2, &res, &err)) {
        LOGE("db_load_open_orders_for_commodity: query failed: %s (code=%d backend=%d)",
             err.message, err.code, err.backend_code);
        return NULL;
    }

  
    while (db_res_step(res, &err)) {
        int id           = (int) db_res_col_i64(res, 0, &err);
        const char *side = db_res_col_text(res, 1, &err);
        int comm_id      = (int) db_res_col_i64(res, 2, &err);
        int qty          = (int) db_res_col_i64(res, 3, &err);
        int filled       = (int) db_res_col_i64(res, 4, &err);
        int price        = (int) db_res_col_i64(res, 5, &err);
        const char *status = db_res_col_text(res, 6, &err);
        int64_t ts_val   = db_res_col_i64(res, 7, &err);

        int64_t expires = 0;
        if (!db_res_col_is_null(res, 8)) {
            expires = db_res_col_i64(res, 8, &err);
        }

        json_t *obj = json_object();
        if (!obj) {
            /* out of memory: bail cleanly */
            db_res_finalize(res);
            json_decref(orders);
            return NULL;
        }

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

    /* If loop ended because of an error (not just “no more rows”) */
    if (err.code != 0) {
        LOGE("db_list_actor_orders: step failed: %s (code=%d backend=%d)",
             err.message, err.code, err.backend_code);
        db_res_finalize(res);
        json_decref(orders);
        return NULL;
    }

    db_res_finalize(res);
    return orders;
}



// Helper to list open orders for a specific port (read-only, for diagnostics)
// Returns a JSON array of order objects (caller owns reference)
json_t *
db_list_port_orders (db_t *db, int port_id)
{
  return db_list_actor_orders (db, "port", port_id);
}

json_t *
db_orders_summary (db_t *db, int filter_commodity_id)
{
    json_t *summary = json_object();
    if (!summary) return NULL;

    db_error_t err;
    db_error_clear(&err);

    const char *sql_all =
        "SELECT commodity_id, side, COUNT(*), SUM(quantity - filled_quantity) "
        "FROM commodity_orders "
        "WHERE status = 'open' "
        "GROUP BY commodity_id, side;";

    const char *sql_filter =
        "SELECT commodity_id, side, COUNT(*), SUM(quantity - filled_quantity) "
        "FROM commodity_orders "
        "WHERE status = 'open' AND commodity_id = $1 "
        "GROUP BY commodity_id, side;";

    const char *sql = (filter_commodity_id > 0) ? sql_filter : sql_all;

    db_bind_t params[1];
    size_t n_params = 0;

    if (filter_commodity_id > 0) {
        params[0] = db_bind_i32(filter_commodity_id);
        n_params = 1;
    }

    db_res_t *res = NULL;
    if (!db_query(db, sql, (n_params ? params : NULL), n_params, &res, &err)) {
        LOGE("db_orders_summary: query failed: %s (code=%d backend=%d)",
             err.message, err.code, err.backend_code);
        json_decref(summary);
        return NULL;
    }

    while (db_res_step(res, &err)) {
        int comm_id = (int) db_res_col_i64(res, 0, &err);
        const char *side = db_res_col_text(res, 1, &err);
        int count = (int) db_res_col_i64(res, 2, &err);
        int64_t total_qty = db_res_col_i64(res, 3, &err);

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

    if (err.code != 0) {
        LOGE("db_orders_summary: step failed: %s (code=%d backend=%d)",
             err.message, err.code, err.backend_code);
        db_res_finalize(res);
        json_decref(summary);
        return NULL;
    }

    db_res_finalize(res);
    return summary;
}
