#include "game_db.h"
#include "server_config.h" // For g_cfg
#include "server_log.h"
#include <string.h>

// Global handle for the database connection, managed by this module.
static db_t *g_game_db = NULL;


/**
 * @brief Initializes the global database connection.
 * This function reads the server configuration and opens a connection to the
 * configured database backend (SQLite or PostgreSQL).
 *
 * @return 0 on success, -1 on failure.
 */
int
game_db_init (void)
{
  if (g_game_db)
    {
      LOGI ("Game DB already initialized.");
      return 0;
    }

  // This relies on the server config being loaded first.
  // In the future, this could take a config object directly.
  db_config_t db_cfg = {0};
  db_error_t err = {0};

#ifdef DB_BACKEND_PG
  db_cfg.backend = DB_BACKEND_POSTGRES;
  db_cfg.pg_conninfo = g_cfg.pg_conn_str;   // Assumes g_cfg is populated
  LOGI("game_db_init: Attempting to connect with backend=%d, conninfo='%s'", db_cfg.backend, db_cfg.pg_conninfo ? db_cfg.pg_conninfo : "(null)");
  if (!db_cfg.pg_conninfo)
    {
      LOGE (
        "game_db_init: PostgreSQL backend is enabled, but no connection string is configured.");
      return -1;
    }
#else
  db_cfg.backend = DB_BACKEND_SQLITE;
  db_cfg.sqlite_path = DEFAULT_DB_NAME;   // From database.h, should be moved to config
#endif

  g_game_db = db_open (&db_cfg, &err);

  if (!g_game_db)
    {
      LOGE ("game_db_init: Failed to open database (code: %d): %s",
            err.code,
            err.message);
      return -1;
    }

  LOGI ("Game DB initialized successfully (backend: %d).", db_cfg.backend);

  // Here we would run any necessary schema migrations or checks that are
  // independent of the Big Bang process. For now, we assume the DB is ready.

  return 0;
}


/**
 * @brief Closes the global database connection.
 */
void
game_db_close (void)
{
  if (g_game_db)
    {
      db_close (g_game_db);
      g_game_db = NULL;
      LOGI ("Game DB connection closed.");
    }
}


/**
 * @brief Gets the global, shared database handle.
 *
 * @return A pointer to the db_t handle, or NULL if not initialized.
 */
db_t *
game_db_get_handle (void)
{
  if (!g_game_db)
    {
      LOGW ("game_db_get_handle: DB handle requested before initialization.");
    }
  return g_game_db;
}


// -----------------------------------------------------------------------------
// Migrated Game Logic Functions Will Go Here
// -----------------------------------------------------------------------------

/*
 * Example of a future migrated function:
 *
 * int game_db_get_player_xp(int player_id, int *out_xp) {
 *     db_t *db = game_db_get_handle();
 *     if (!db || !out_xp) return -1;
 *
 *     db_error_t err = {0};
 *     db_res_t *res = NULL;
 *
 *     const char *sql = "SELECT experience FROM players WHERE id = $1;";
 *     db_bind_t params[] = { DB_P_INT(player_id) };
 *
 *     if (!db_query(db, sql, params, 1, &res, &err)) {
 *         LOGE("game_db_get_player_xp: Query failed (code %d): %s", err.code, err.message);
 *         return -1;
 *     }
 *
 *     int rc = -1; // Default to not found
 *     if (db_res_step(res, &err)) {
 *         *out_xp = db_res_col_i32(res, 0, &err);
 *         if (err.code == 0) {
 *             rc = 0; // Success
 *         }
 *     }
 *
 *     db_res_finalize(res);
 *     return rc;
 * }
 */

void db_handle_close_and_reset(void) {}
