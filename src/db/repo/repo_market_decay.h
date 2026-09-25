#ifndef REPO_MARKET_DECAY_H
#define REPO_MARKET_DECAY_H

#include <stdint.h>
#include "db/db_api.h"

/* ===================================================================
 * Phase 10: Market Decay & Stabilisation
 *
 * Prevents permanent drift/ratcheting of market prices by applying
 * deterministic decay to port and cluster market state.
 *
 * Formulas (integer math only):
 *   port.rolling_volume    := floor(vol * decay_num / decay_den)
 *   cluster.pressure       := floor(pressure * decay_num / decay_den)
 *
 * Config keys (deterministic, tunable):
 *   market.decay.num       (default: 98)   -- numerator of decay fraction
 *   market.decay.den       (default: 100)  -- denominator of decay fraction
 *   cluster.pressure.min   (default: -50000) -- min pressure after clamp
 *   cluster.pressure.max   (default: 50000)  -- max pressure after clamp
 *
 * Cron task: market_state_decay (runs hourly, idempotent, gated by config)
 * =================================================================== */

/* Sensible defaults for market decay */
#define MARKET_DECAY_DEFAULT_NUM 98      /* 0.98x per cycle */
#define MARKET_DECAY_DEFAULT_DEN 100
#define MARKET_DECAY_MIN_NUM 1
#define MARKET_DECAY_MIN_DEN 1
#define MARKET_DECAY_MAX_NUM 100
#define MARKET_DECAY_MAX_DEN 1

/* Default pressure bounds (prevent unbounded accumulation) */
#define MARKET_PRESSURE_DEFAULT_MIN -50000
#define MARKET_PRESSURE_DEFAULT_MAX 50000

/* ===================================================================
 * Public API Functions
 * =================================================================== */

/**
 * Decay port-level market state (rolling_volume only).
 *
 * For each row in port_commodity_state:
 *   - rolling_volume := floor(rolling_volume * num / den)
 *   - stock_level stays unchanged (it is inventory)
 *   - Clamp rolling_volume to >= 0
 *
 * Deterministic, idempotent. Safe to call multiple times.
 *
 * Returns:
 *   0 on success
 *  -1 on database error
 */
int repo_market_decay_port_state(
    db_t *db,
    int decay_num,   /* numerator (e.g., 98) */
    int decay_den    /* denominator (e.g., 100) */
);

/**
 * Decay cluster-level market state (pressure only).
 *
 * For each row in cluster_commodity_pressure:
 *   - pressure := floor(pressure * num / den)
 *   - rolling_volume := floor(rolling_volume * num / den)
 *   - Clamp pressure to [min_pressure, max_pressure]
 *   - Clamp rolling_volume to >= 0
 *
 * Deterministic, idempotent. Safe to call multiple times.
 *
 * Returns:
 *   0 on success
 *  -1 on database error
 */
int repo_market_decay_cluster_state(
    db_t *db,
    int decay_num,        /* numerator (e.g., 98) */
    int decay_den,        /* denominator (e.g., 100) */
    int min_pressure,     /* clamp floor */
    int max_pressure      /* clamp ceil */
);

/**
 * Execute full market decay cycle (both port and cluster states).
 *
 * Wrapper that:
 *   1. Calls repo_market_decay_port_state()
 *   2. Calls repo_market_decay_cluster_state()
 *
 * If either step fails, returns -1 (no partial state).
 * If market.dynamic_pricing_enabled == false, returns 0 (no-op).
 *
 * Returns:
 *   0 on success (or gated by config)
 *  -1 on error
 */
int repo_market_decay_execute_cycle(
    db_t *db,
    int decay_num,
    int decay_den,
    int min_pressure,
    int max_pressure
);

#endif /* REPO_MARKET_DECAY_H */
