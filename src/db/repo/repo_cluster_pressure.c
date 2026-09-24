#include "repo_cluster_pressure.h"
#include "db_api.h"
#include "repo_clusters.h"
#include <string.h>
#include <stdlib.h>
#include <time.h>

/**
 * Check if cluster is lawless (law_severity = 0)
 */
int
repo_cluster_pressure_is_lawless(db_t *db, int64_t cluster_id)
{
    if (!db || cluster_id <= 0)
        return -1;

    db_res_t *res = NULL;
    db_error_t err;
    db_error_clear(&err);

    const char *query = "SELECT law_severity FROM clusters WHERE cluster_id = {1};";

    if (db_query(db, query, (db_bind_t[]){
                    db_bind_i64(cluster_id)
                }, 1, &res, &err) != 0)
    {
        return -1;
    }

    int ret = db_res_step(res, &err);
    int is_lawless = -1;

    if (ret == 0)
    {
        int law_severity = (int)db_res_col_i64(res, 0, &err);
        is_lawless = (law_severity == 0) ? 1 : 0;
    }
    else if (ret != 1)
    {
        // Error reading cluster
        is_lawless = -1;
    }
    else
    {
        // Cluster not found; treat as lawless (return 1)
        is_lawless = 1;
    }

    db_res_finalize(res);
    return is_lawless;
}

/**
 * Ensure cluster pressure row exists (idempotent)
 */
int
repo_cluster_pressure_ensure(db_t *db, int64_t cluster_id,
                             const char *commodity_code)
{
    if (!db || cluster_id <= 0 || !commodity_code)
        return -1;

    db_error_t err;
    db_error_clear(&err);

    // Try to insert; ignore if exists
    const char *query = "INSERT INTO cluster_commodity_pressure "
                        "(cluster_id, commodity_code, pressure, rolling_volume, updated_at) "
                        "VALUES ({1}, {2}, 0, 0, CURRENT_TIMESTAMP) "
                        "ON CONFLICT (cluster_id, commodity_code) DO NOTHING;";

    if (db_exec(db, query, (db_bind_t[]){
                    db_bind_i64(cluster_id),
                    db_bind_text((char *)commodity_code)
                }, 2, &err) != 0)
    {
        return -1;
    }

    return 0;
}

/**
 * Get current cluster pressure state
 */
int
repo_cluster_pressure_get(db_t *db, int64_t cluster_id,
                          const char *commodity_code,
                          cluster_pressure_t *out_state, bool *out_found)
{
    if (!db || cluster_id <= 0 || !commodity_code || !out_state || !out_found)
        return -1;

    *out_found = false;
    memset(out_state, 0, sizeof(*out_state));

    db_res_t *res = NULL;
    db_error_t err;
    db_error_clear(&err);

    const char *query = "SELECT cluster_id, commodity_code, pressure, rolling_volume, "
                        "EXTRACT(EPOCH FROM updated_at)::bigint "
                        "FROM cluster_commodity_pressure "
                        "WHERE cluster_id = {1} AND commodity_code = {2};";

    if (db_query(db, query, (db_bind_t[]){
                    db_bind_i64(cluster_id),
                    db_bind_text((char *)commodity_code)
                }, 2, &res, &err) != 0)
    {
        return -1;
    }

    int ret = db_res_step(res, &err);
    if (ret == 0)
    {
        out_state->cluster_id = db_res_col_i64(res, 0, &err);
        
        const char *code_ptr = db_res_col_text(res, 1, &err);
        if (code_ptr)
            out_state->commodity_code = strdup(code_ptr);
        
        out_state->pressure = (int)db_res_col_i64(res, 2, &err);
        out_state->rolling_volume = (int)db_res_col_i64(res, 3, &err);
        
        int64_t updated_epoch = db_res_col_i64(res, 4, &err);
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
 * Get cluster pressure multiplier (100 = 1.0x, neutral)
 * Formula: pressure_mul = 100 + (pressure / k_pressure_div)
 * Clamped to [min_mul, max_mul]
 */
int
repo_cluster_pressure_get_multiplier(db_t *db, int64_t cluster_id,
                                     const char *commodity_code,
                                     int min_mul, int max_mul,
                                     int k_pressure_div)
{
    if (!db || cluster_id <= 0 || !commodity_code || k_pressure_div <= 0)
        return 100;  // Graceful fallback to neutral

    // Check if cluster is lawless (law_severity = 0)
    int is_lawless = repo_cluster_pressure_is_lawless(db, cluster_id);
    if (is_lawless == 1)
        return 100;  // Lawless clusters always use neutral multiplier

    cluster_pressure_t state;
    bool found = false;

    if (repo_cluster_pressure_get(db, cluster_id, commodity_code, &state, &found) != 0 || !found)
    {
        // State missing: graceful fallback, return neutral
        return 100;
    }

    // Calculate cluster pressure multiplier using integer arithmetic
    // pressure_mul = 100 + (pressure / k_pressure_div)
    int pressure_factor = state.pressure / k_pressure_div;
    int pressure_mul = 100 + pressure_factor;

    // Clamp to [min_mul, max_mul]
    if (pressure_mul < min_mul)
        pressure_mul = min_mul;
    if (pressure_mul > max_mul)
        pressure_mul = max_mul;

    if (state.commodity_code)
        free(state.commodity_code);

    return pressure_mul;
}

/**
 * Update cluster pressure state on trade
 */
int
repo_cluster_pressure_apply_trade(db_t *db, int64_t cluster_id,
                                  const char *commodity_code,
                                  int delta_pressure, int volume_increment)
{
    if (!db || cluster_id <= 0 || !commodity_code)
        return -1;

    // Check if cluster is lawless; if so, don't write state
    int is_lawless = repo_cluster_pressure_is_lawless(db, cluster_id);
    if (is_lawless == 1)
        return 0;  // Lawless clusters don't accumulate pressure

    db_error_t err;
    db_error_clear(&err);

    // Ensure row exists first
    if (repo_cluster_pressure_ensure(db, cluster_id, commodity_code) != 0)
        return -1;

    // Update: pressure changes by delta_pressure, rolling_volume increases, timestamp updates
    const char *query = "UPDATE cluster_commodity_pressure "
                        "SET pressure = pressure + {1}, "
                        "    rolling_volume = rolling_volume + {2}, "
                        "    updated_at = CURRENT_TIMESTAMP "
                        "WHERE cluster_id = {3} AND commodity_code = {4};";

    if (db_exec(db, query, (db_bind_t[]){
                    db_bind_i64(delta_pressure),
                    db_bind_i64(volume_increment),
                    db_bind_i64(cluster_id),
                    db_bind_text((char *)commodity_code)
                }, 4, &err) != 0)
    {
        return -1;
    }

    return 0;
}
