#ifndef REPO_MARKET_DYNAMIC_H
#define REPO_MARKET_DYNAMIC_H

#include "db_api.h"
#include <time.h>
#include <stdint.h>

/**
 * Struct for port commodity market state
 * Tracks supply/demand signals for dynamic pricing
 */
typedef struct {
    int port_commodity_state_id;
    int port_id;
    char *commodity_code;
    int stock_level;              /* Current inventory at port */
    int rolling_volume;           /* Trading volume (demand signal) */
    time_t last_trade_at;         /* Timestamp of last trade (0 if never) */
    time_t updated_at;            /* Last state update time */
} market_state_t;

/**
 * Get current market state for (port_id, commodity_code)
 * Returns 0 on success, -1 if not found, other on error
 * Caller must free out_state->commodity_code if found
 */
int repo_market_state_get(db_t *db, int port_id, const char *commodity_code,
                          market_state_t *out_state, bool *out_found);

/**
 * Ensure market state row exists for (port_id, commodity_code)
 * Idempotent: creates row if missing, returns success if row exists
 * Returns 0 on success, error code otherwise
 */
int repo_market_state_ensure(db_t *db, int port_id, const char *commodity_code);

/**
 * Apply trade atomically: update stock_level and rolling_volume
 * This should be called within trading transaction to keep state consistent
 * 
 * delta_qty: quantity change (positive for sell to port, negative for buy from port)
 * volume_increment: quantity added to rolling_volume (always positive, counts activity)
 * 
 * Returns 0 on success, error code otherwise
 */
int repo_market_apply_trade(db_t *db, int port_id, const char *commodity_code,
                            int delta_qty, int volume_increment);

/**
 * Calculate dynamic price multiplier based on current market state
 * Model: dynamic_mul = 100 + ((rolling_volume / K_VOL_DIV) - (stock_level / K_STOCK_DIV))
 * Clamped between min_mul and max_mul
 * 
 * Returns multiplier (100 = 1.0x)
 * Returns 100 if state not found (graceful fallback)
 */
int repo_market_dynamic_mul(db_t *db, int port_id, const char *commodity_code,
                            int min_mul, int max_mul,
                            int k_vol_div, int k_stock_div);

/**
 * Constants for multiplier calculation
 * Defaults can be overridden via config keys
 */
#define MARKET_DEFAULT_MIN_MUL 50       /* 0.5x minimum */
#define MARKET_DEFAULT_MAX_MUL 200      /* 2.0x maximum */
#define MARKET_DEFAULT_K_VOL_DIV 1000   /* Volume sensitivity */
#define MARKET_DEFAULT_K_STOCK_DIV 100  /* Stock sensitivity */

#endif /* REPO_MARKET_DYNAMIC_H */
