/**
 * @file game_db.h
 * @brief Manages the application's database connections.
 *
 * This module transitions away from a single global DB handle to a per-thread
 * connection model to ensure thread safety and proper behavior after a fork.
 *
 * - A thread-local storage (TLS) key is used to store a db_t* for each thread.
 * - game_db_get_handle() is the sole access point. It returns the handle for
 *   the calling thread, creating and connecting it on first use.
 * - game_db_init() caches the connection configuration but does not create a
 *   global connection.
 * - Connections are automatically closed on thread exit via a TLS destructor.
 */
#ifndef GAME_DB_H
#define GAME_DB_H

#include "db/db_api.h"

/**
 * @brief Initializes the database layer.
 * Caches the connection configuration needed for subsequent per-thread connections.
 * Must be called once at server startup.
 * @return 0 on success, -1 on failure.
 */
int game_db_init (void);

/**
 * @brief Closes the database layer and cleans up resources.
 * Note: This is for overall shutdown. Per-thread handles are managed automatically.
 */
void game_db_close (void);

/**
 * @brief Gets the database handle for the current thread.
 * If a handle does not exist for the thread, a new connection is established.
 * @return A pointer to the db_t handle, or NULL on connection failure.
 */
db_t *game_db_get_handle (void);

/**
 * @brief Cleans up database state in a child process after a fork.
 * This should be called immediately in the child process to prevent using
 * inherited file descriptors.
 */
void game_db_after_fork_child (void);

#endif /* GAME_DB_H */
