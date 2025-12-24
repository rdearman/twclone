#ifndef GAME_DB_H
#define GAME_DB_H

#include <stdbool.h>
#include "db/db_api.h" // Include for db_t

/**
 * @file game_db.h
 * @brief Public game database API. This is the single header that server logic
 *        should include for all database operations.
 */

#ifdef __cplusplus
extern "C" {
#endif

// -----------------------------------------------------------------------------
// Lifecycle
// -----------------------------------------------------------------------------

/**
 * @brief Initializes the global database connection based on server config.
 * @return 0 on success, -1 on failure.
 */
int game_db_init (void);

/**
 * @brief Closes the global database connection.
 */
void game_db_close (void);

/**
 * @brief Gets the global, shared database handle.
 * @return A pointer to the db_t handle, or NULL if not initialized.
 */
db_t * game_db_get_handle (void);


// -----------------------------------------------------------------------------
// High-Level Game Operations (Examples to be migrated)
// -----------------------------------------------------------------------------

// Example player function
// int game_db_get_player_xp(int player_id, int *out_xp);

// Example ship function
// int game_db_mark_ship_destroyed(int ship_id);


#ifdef __cplusplus
}
#endif

#endif // GAME_DB_H
