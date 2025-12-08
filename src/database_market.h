#ifndef DATABASE_MARKET_H
#define DATABASE_MARKET_H

#include <sqlite3.h>
#include <jansson.h>

// Struct to represent a commodity order
typedef struct {
    int id;
    char actor_type[16]; // e.g., "port"
    int actor_id;
    int commodity_id;
    char side[8];        // "buy" or "sell"
    int quantity;
    int price;
    char status[16];     // "open", "filled", "cancelled", "expired"
    long long ts;
    long long expires_at;
    int filled_quantity;
    int remaining_quantity; // Derived: quantity - filled_quantity
} commodity_order_t;

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
);

// Helper to update an existing commodity order's quantity, status, and filled_quantity.
// Returns SQLITE_OK on success.
int db_update_commodity_order(
    sqlite3 *db,
    int order_id,
    int new_quantity, // Total quantity of the order (can be less than original if partially filled)
    int new_filled_quantity,
    const char *new_status
);

// Helper to cancel open commodity orders for a given port and commodity on a specific side.
// Returns SQLITE_OK on success.
int db_cancel_commodity_orders_for_port_and_commodity(
    sqlite3 *db,
    int port_id,
    int commodity_id,
    const char *side
);

// Helper to cancel open commodity orders for a given actor and commodity on a specific side.
// Returns SQLITE_OK on success.
int db_cancel_commodity_orders_for_actor_and_commodity(
    sqlite3 *db,
    const char *actor_type,
    int actor_id,
    int commodity_id,
    const char *side
);

// Helper to load open commodity orders for a given commodity and side.
// Returns a dynamically allocated array of commodity_order_t structs.
// The caller is responsible for freeing the returned array.
// The 'count' parameter will be set to the number of orders loaded.
commodity_order_t* db_load_open_orders_for_commodity(
    sqlite3 *db,
    int commodity_id,
    const char *side,
    int *count
);

// Helper to get a specific open order for a port.
// Returns SQLITE_OK if found (populating out_order), SQLITE_NOTFOUND if not found, or error code.
int db_get_open_order_for_port(
    sqlite3 *db,
    int port_id,
    int commodity_id,
    const char *side,
    commodity_order_t *out_order
);

// Helper to get a specific open order for any actor type.
// Returns SQLITE_OK if found (populating out_order), SQLITE_NOTFOUND if not found, or error code.
int db_get_open_order(
    sqlite3 *db,
    const char *actor_type,
    int actor_id,
    int commodity_id,
    const char *side,
    commodity_order_t *out_order
);

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
);

// Helper to list open orders for a specific port (read-only, for diagnostics)
// Returns a JSON array of order objects (caller owns reference)
json_t *db_list_port_orders(sqlite3 *db, int port_id);

// Helper to list orders for a specific actor (read-only, for diagnostics)
// Returns a JSON array of order objects (caller owns reference)
json_t *db_list_actor_orders(sqlite3 *db, const char *actor_type, int actor_id);

// Helper to summarize orders by commodity (read-only, for diagnostics)
// Returns a JSON object (caller owns reference)
json_t *db_orders_summary(sqlite3 *db, int filter_commodity_id);

#endif // DATABASE_MARKET_H
