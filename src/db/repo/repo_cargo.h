#ifndef REPO_CARGO_H
#define REPO_CARGO_H

#include "db/db_api.h"
#include <stdint.h>

/**
 * Phase 1: Dynamic cargo operations
 * 
 * All cargo is now stored in ship_cargo table (commodity_code, quantity).
 * These functions provide atomic, capacity-aware cargo management.
 * Legacy ships.* columns are kept for Phase 1 compatibility (dual-write).
 */

/**
 * repo_cargo_get_total - Get total quantity of cargo across all commodities for a ship
 * @db: database handle
 * @ship_id: ship identifier
 * @total_out: pointer to int64_t to receive total
 * 
 * Returns: 0 on success, error code on failure
 *          ERR_SHIP_NOT_FOUND if ship doesn't exist
 */
int repo_cargo_get_total(db_t *db, int32_t ship_id, int64_t *total_out);

/**
 * repo_cargo_get - Get quantity of a specific commodity in a ship
 * @db: database handle
 * @ship_id: ship identifier
 * @commodity_code: 3-char commodity code (e.g., "ORE", "DRG")
 * @quantity_out: pointer to int64_t to receive quantity (0 if not present)
 * 
 * Returns: 0 on success, error code on failure
 *          ERR_SHIP_NOT_FOUND if ship doesn't exist
 *          Returns 0 in quantity_out if commodity not in cargo
 */
int repo_cargo_get(db_t *db, int32_t ship_id, const char *commodity_code, int64_t *quantity_out);

/**
 * repo_cargo_add - Add or remove cargo, enforcing holds capacity
 * @db: database handle
 * @ship_id: ship identifier
 * @commodity_code: 3-char commodity code (e.g., "ORE", "DRG")
 * @delta: quantity delta (positive to add, negative to remove)
 * @new_quantity_out: optional pointer to receive resulting quantity
 * 
 * Atomically updates ship_cargo:
 * - Validates commodity code exists
 * - Prevents negative quantities
 * - Enforces: sum(all commodities) + delta <= ships.holds
 * - Dual-writes legacy ships.* columns for Phase 1 compatibility
 * 
 * Returns: 0 on success, error code on failure
 *          ERR_SHIP_NOT_FOUND if ship doesn't exist
 *          ERR_HOLD_FULL if operation would exceed holds
 *          ERR_DB_MISUSE if delta would result in negative quantity
 *          ERR_INVALID_ARG if commodity_code is invalid
 */
int repo_cargo_add(db_t *db, int32_t ship_id, const char *commodity_code, int64_t delta, int64_t *new_quantity_out);

/**
 * repo_cargo_sync_legacy_columns - Sync legacy ships.* columns from ship_cargo
 * @db: database handle
 * @ship_id: ship identifier
 * 
 * One-directional: reads ship_cargo, updates ships.(ore, organics, equipment, colonists, slaves, weapons, drugs)
 * Used during Phase 1 for backward compatibility.
 * In Phase 2, legacy columns may be removed.
 * 
 * Returns: 0 on success, error code on failure
 *          ERR_SHIP_NOT_FOUND if ship doesn't exist
 */
int repo_cargo_sync_legacy_columns(db_t *db, int32_t ship_id);

#endif
