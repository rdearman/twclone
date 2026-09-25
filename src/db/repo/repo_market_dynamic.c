#include "repo_market_dynamic.h"
#include "db_api.h"
#include <string.h>
#include <stdlib.h>
#include <time.h>

/**
 * Get current market state for (port_id, commodity_code)
 */
int
repo_market_state_get(db_t *db, int port_id, const char *commodity_code,
                      market_state_t *out_state, bool *out_found)
{
    if (!db || port_id <= 0 || !commodity_code || !out_state || !out_found)
        return -1;

    *out_found = false;
    memset(out_state, 0, sizeof(*out_state));

    db_res_t *res = NULL;
    db_error_t err;
    db_error_clear(&err);

    const char *query = "SELECT port_commodity_state_id, port_id, commodity_code, "
                        "stock_level, rolling_volume, "
                        "EXTRACT(EPOCH FROM last_trade_at)::bigint, "
                        "EXTRACT(EPOCH FROM updated_at)::bigint "
                        "FROM port_commodity_state "
                        "WHERE port_id = {1} AND commodity_code = {2};";

    if (db_query(db, query, (db_bind_t[]){
                    db_bind_i64(port_id),
                    db_bind_text((char *)commodity_code)
                }, 2, &res, &err) != 0)
    {
        return -1;
    }

    int ret = db_res_step(res, &err);
    if (ret == 0)
    {
        out_state->port_commodity_state_id = (int)db_res_col_i64(res, 0, &err);
        out_state->port_id = (int)db_res_col_i64(res, 1, &err);
        
        /* For commodity_code TEXT column, use db_res_col_text */
        const char *code_ptr = db_res_col_text(res, 2, &err);
        if (code_ptr)
            out_state->commodity_code = strdup(code_ptr);
        
        out_state->stock_level = (int)db_res_col_i64(res, 3, &err);
        out_state->rolling_volume = (int)db_res_col_i64(res, 4, &err);
        
        int64_t last_trade_epoch = db_res_col_i64(res, 5, &err);
        out_state->last_trade_at = (time_t)last_trade_epoch;
        
        int64_t updated_epoch = db_res_col_i64(res, 6, &err);
        out_state->updated_at = (time_t)updated_epoch;
        
        *out_found = true;
    }
    else if (ret != 1)
    {
        // ret == 1 means no row (not an error)
        db_res_finalize(res);
        return -1;
    }

    db_res_finalize(res);
    return 0;
}

/**
 * Ensure market state row exists (idempotent)
 */
int
repo_market_state_ensure(db_t *db, int port_id, const char *commodity_code)
{
    if (!db || port_id <= 0 || !commodity_code)
        return -1;

    db_error_t err;
    db_error_clear(&err);

    // Try to insert; ignore if exists
    const char *query = "INSERT INTO port_commodity_state "
                        "(port_id, commodity_code, stock_level, rolling_volume, updated_at) "
                        "VALUES ({1}, {2}, 0, 0, CURRENT_TIMESTAMP) "
                        "ON CONFLICT (port_id, commodity_code) DO NOTHING;";

    if (db_exec(db, query, (db_bind_t[]){
                    db_bind_i64(port_id),
                    db_bind_text((char *)commodity_code)
                }, 2, &err) != 0)
    {
        return -1;
    }

    return 0;
}

/**
 * Apply trade: atomically update stock_level and rolling_volume
 * This should be called within the same transaction as the trade
 */
int
repo_market_apply_trade(db_t *db, int port_id, const char *commodity_code,
                        int delta_qty, int volume_increment)
{
    if (!db || port_id <= 0 || !commodity_code)
        return -1;

    db_error_t err;
    db_error_clear(&err);

    // Ensure row exists first
    if (repo_market_state_ensure(db, port_id, commodity_code) != 0)
        return -1;

    // Update: stock_level changes by delta_qty, rolling_volume increases, timestamp updates
    const char *query = "UPDATE port_commodity_state "
                        "SET stock_level = stock_level + {1}, "
                        "    rolling_volume = rolling_volume + {2}, "
                        "    last_trade_at = CURRENT_TIMESTAMP, "
                        "    updated_at = CURRENT_TIMESTAMP "
                        "WHERE port_id = {3} AND commodity_code = {4};";

    if (db_exec(db, query, (db_bind_t[]){
                    db_bind_i64(delta_qty),
                    db_bind_i64(volume_increment),
                    db_bind_i64(port_id),
                    db_bind_text((char *)commodity_code)
                }, 4, &err) != 0)
    {
        return -1;
    }

    return 0;
}

/**
 * Calculate dynamic multiplier based on market state
 * Model: dynamic_mul = 100 + ((rolling_volume / k_vol) - (stock_level / k_stock))
 * Clamped to [min_mul, max_mul]
 */
int
repo_market_dynamic_mul(db_t *db, int port_id, const char *commodity_code,
                        int min_mul, int max_mul,
                        int k_vol_div, int k_stock_div)
{
    if (!db || port_id <= 0 || !commodity_code || k_vol_div <= 0 || k_stock_div <= 0)
        return 100;  // Graceful fallback to neutral

    market_state_t state;
    bool found = false;

    if (repo_market_state_get(db, port_id, commodity_code, &state, &found) != 0 || !found)
    {
        // State missing: graceful fallback, ensure row for next time
        repo_market_state_ensure(db, port_id, commodity_code);
        return 100;
    }

    // Calculate dynamic multiplier using integer arithmetic
    // dynamic_mul = 100 + ((rolling_volume / k_vol) - (stock_level / k_stock))
    int vol_factor = state.rolling_volume / k_vol_div;
    int stock_factor = state.stock_level / k_stock_div;
    int dynamic_mul = 100 + vol_factor - stock_factor;

    // Clamp to [min_mul, max_mul]
    if (dynamic_mul < min_mul)
        dynamic_mul = min_mul;
    if (dynamic_mul > max_mul)
        dynamic_mul = max_mul;

    if (state.commodity_code)
        free(state.commodity_code);

    return dynamic_mul;
}
