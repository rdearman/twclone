#ifndef SERVER_LOOP_H
#define SERVER_LOOP_H

/* server_loop.h
 * Main server event loop handling incoming connections and game ticks.
 */

#include "database.h"		/* for db_handle, etc. */
#include "universe.h"		/* for sector/planet structures if needed */
#include "player_interaction.h"

/* Starts the server loop.
 * Typically called from main() after world_init() and db_init().
 * Returns 0 on normal exit, -1 on error.
 */
int server_loop (void);

#endif /* SERVER_LOOP_H */
