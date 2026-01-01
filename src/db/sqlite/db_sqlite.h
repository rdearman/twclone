#ifndef DB_SQLITE_H
#define DB_SQLITE_H

#include "../db_api.h" // For db_t, db_config_t, db_error_t, etc.
#include "../db_int.h" // For db_s, db_vt_t

/**
 * @file db_sqlite.h
 * @brief Public interface for the SQLite backend driver.
 *        Declares the internal open function called by db_api.c.
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Internal function to open a SQLite database connection.
 *        This function is called by db_api.c's db_open factory.
 * @param parent_db The generic db_t handle that this backend will populate.
 * @param cfg The database configuration.
 * @param err Pointer to an error structure to fill on failure.
 * @return A pointer to the backend-specific implementation data (db_sqlite_impl_s*) on success, or NULL on failure.
 */
void* db_sqlite_open_internal(db_t *parent_db, const db_config_t *cfg, db_error_t *err);


#ifdef __cplusplus
}
#endif

#endif // DB_SQLITE_H
