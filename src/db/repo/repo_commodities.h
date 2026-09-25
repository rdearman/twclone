/**
 * repo_commodities.h - Data-driven commodity queries (Phase 2)
 *
 * Provides DB-driven access to commodity metadata and eligibility rules.
 * Replaces hardcoded commodity lists in gameplay code.
 *
 * All commodity codes are 3-character strings (e.g., ORE, SLV, DRG, ALT).
 */

#ifndef REPO_COMMODITIES_H
#define REPO_COMMODITIES_H

#include "db/db_api.h"
#include <stdbool.h>

/**
 * Get list of tradeable commodities at a port, filtered by player alignment.
 *
 * Parameters:
 *   db: Database handle
 *   port_id: Port ID
 *   alignment: Player alignment (-1000 to +1000 typical)
 *   out_codes: Output array of commodity codes (caller must free)
 *   out_count: Output count of commodities returned
 *   out_buy_flags: Output array of bools (true = port buys this commodity)
 *   out_sell_flags: Output array of bools (true = port sells this commodity)
 *
 * Returns:
 *   0 on success
 *   ERR_* on error
 *
 * Note:
 *   - Alignment-based filtering: illegal commodities blocked for good alignment
 *   - Result includes only commodities configured in port_trade
 *   - Caller must free out_codes, out_buy_flags, out_sell_flags
 */
int repo_commodities_list_tradeable_at_port(
    db_t *db,
    int port_id,
    int alignment,
    char ***out_codes,
    int *out_count,
    bool **out_buy_flags,
    bool **out_sell_flags
);

/**
 * Get list of commodities transferable to/from a planet, filtered by alignment.
 *
 * Parameters:
 *   db: Database handle
 *   planet_id: Planet ID
 *   alignment: Player alignment
 *   out_codes: Output array of commodity codes (caller must free)
 *   out_count: Output count of commodities returned
 *
 * Returns:
 *   0 on success
 *   ERR_* on error
 *
 * Note:
 *   - Alignment-based filtering applied
 *   - Result includes only commodities configured in planet_goods
 *   - Caller must free out_codes
 */
int repo_commodities_list_planet_transferable(
    db_t *db,
    int planet_id,
    int alignment,
    char ***out_codes,
    int *out_count
);

/**
 * Check if a commodity is legal.
 *
 * Parameters:
 *   db: Database handle
 *   commodity_code: 3-char commodity code
 *   out_is_legal: Output boolean (true = legal, false = illegal)
 *
 * Returns:
 *   0 on success
 *   ERR_DB_NOT_FOUND if commodity does not exist
 *   ERR_* on error
 */
int repo_commodities_is_legal(
    db_t *db,
    const char *commodity_code,
    bool *out_is_legal
);

/**
 * Get the per-ship maximum hold limit for a commodity.
 *
 * Parameters:
 *   db: Database handle
 *   commodity_code: 3-char commodity code
 *   out_max: Output maximum holds (-1 if unlimited/NULL in DB)
 *
 * Returns:
 *   0 on success
 *   ERR_DB_NOT_FOUND if commodity does not exist
 *   ERR_* on error
 *
 * Note:
 *   - If max_holds_per_ship is NULL in DB, returns -1 (unlimited)
 *   - Caller can check: if (*out_max == -1) treat as unlimited
 */
int repo_commodities_get_max_holds_per_ship(
    db_t *db,
    const char *commodity_code,
    int *out_max
);

#endif /* REPO_COMMODITIES_H */
