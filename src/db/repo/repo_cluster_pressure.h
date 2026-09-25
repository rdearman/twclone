#ifndef REPO_CLUSTER_PRESSURE_H
#define REPO_CLUSTER_PRESSURE_H

#include "db_api.h"
#include <time.h>
#include <stdint.h>

/**
 * Struct for cluster commodity pressure state
 * Tracks supply/demand signals at cluster level for dynamic pricing
 */
typedef struct {
    int64_t cluster_id;
    char *commodity_code;
    int pressure;              /* Signed pressure signal (-ve = oversupply, +ve = demand) */
    int rolling_volume;        /* Cumulative trading volume */
    time_t updated_at;         /* Last update timestamp */
} cluster_pressure_t;

/**
 * Get cluster pressure multiplier for (cluster_id, commodity_code)
 * Returns multiplier as integer (100 = 1.0x, neutral)
 * 
 * If cluster is lawless (law_severity=0), returns 100 (neutral)
 * If row not found, gracefully returns 100 (neutral)
 * 
 * Formula: pressure_mul = clamp(100 + (pressure / K_PRESSURE_DIV), MIN_MUL, MAX_MUL)
 */
int repo_cluster_pressure_get_multiplier(db_t *db, int64_t cluster_id, 
                                         const char *commodity_code,
                                         int min_mul, int max_mul,
                                         int k_pressure_div);

/**
 * Ensure cluster pressure row exists (idempotent)
 * Creates row with pressure=0, rolling_volume=0 if missing
 * Returns 0 on success, error code otherwise
 */
int repo_cluster_pressure_ensure(db_t *db, int64_t cluster_id, 
                                 const char *commodity_code);

/**
 * Update cluster pressure state on trade
 * delta_pressure: change in pressure (+qty for sell, -qty for buy)
 * volume_increment: quantity added to rolling_volume (always +qty)
 * 
 * Returns 0 on success, error code otherwise
 */
int repo_cluster_pressure_apply_trade(db_t *db, int64_t cluster_id,
                                      const char *commodity_code,
                                      int delta_pressure, int volume_increment);

/**
 * Get current cluster pressure state (optional utility)
 * Returns 0 on success, -1 if not found, error code on failure
 * Caller must free out_state->commodity_code if found
 */
int repo_cluster_pressure_get(db_t *db, int64_t cluster_id, 
                              const char *commodity_code,
                              cluster_pressure_t *out_state, bool *out_found);

/**
 * Check if cluster is lawless (law_severity = 0)
 * Lawless clusters should NOT accumulate pressure or write state rows
 * Returns 1 if lawless, 0 if not, negative on error
 */
int repo_cluster_pressure_is_lawless(db_t *db, int64_t cluster_id);

/**
 * Constants for cluster pressure calculation
 * Defaults can be overridden via config keys
 */
#define CLUSTER_PRESSURE_DEFAULT_MIN_MUL 80        /* 0.8x minimum */
#define CLUSTER_PRESSURE_DEFAULT_MAX_MUL 150       /* 1.5x maximum */
#define CLUSTER_PRESSURE_DEFAULT_K_DIV 500         /* Pressure sensitivity */

#endif /* REPO_CLUSTER_PRESSURE_H */
