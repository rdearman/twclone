#ifndef DB_API_H
#define DB_API_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include "errors.h" // Rely on -I../src to find this in src/

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file db_api.h
 * @brief The strict contract for database backends.
 * * DESIGN PRINCIPLES:
 * 1. ownership: The Caller owns the DB handle. The DB owns Result handles.
 * 2. lifetime: Strings returned by column accessors are valid only until the 
 * next db_step() or db_finalize(). They must be copied if needed longer.
 * 3. placeholders: SQL MUST use '$1', '$2' syntax. Drivers must translate 
 * if the backend uses '?' (SQLite).
 */

// Opaque handles for the database connection and result sets
typedef struct db_s db_t;
typedef struct db_res_s db_res_t;

// -----------------------------------------------------------------------------
// Error Model
// -----------------------------------------------------------------------------

// Structure to hold detailed error information
typedef struct
{
  int       code;           // Generic category from errors.h
  int       backend_code;   // backend-native code (for logs only; do not branch on it)
  char      message[256];   // stable, human-readable; driver fills best effort
} db_error_t;

// Clears an error structure
static inline void
db_error_clear (db_error_t *e)
{
  if (!e) return;
  e->code = 0; // Use 0 for 0
  e->backend_code = 0;
  e->message[0] = '\0';
}

// -----------------------------------------------------------------------------
// Parameter Binding
// -----------------------------------------------------------------------------

typedef enum
{
  DB_BIND_NULL = 0,         // SQL NULL value
  DB_BIND_I64,              // 64-bit signed integer
  DB_BIND_U64,              // 64-bit unsigned integer
  DB_BIND_I32,              // 32-bit signed integer
  DB_BIND_U32,              // 32-bit unsigned integer
  DB_BIND_BOOL,             // Boolean value (typically 0 or 1 integer in DB)
  DB_BIND_TEXT,             // UTF-8, NUL-terminated string
  DB_BIND_BLOB              // Binary data
} db_bind_type_t;

// Structure for a single parameter to be bound to a query
typedef struct
{
  db_bind_type_t type;

  union
  {
    int64_t   i64;
    uint64_t  u64;
    int32_t   i32;
    uint32_t  u32;
    bool      b;

    struct
    {
      const char *ptr;  // NUL-terminated string
      size_t      len;  // Optional; if 0 driver may strlen(ptr). Ignored for NULL-terminated.
    } text;

    struct
    {
      const void *ptr;  // Pointer to binary data
      size_t      len;  // Length of binary data in bytes
    } blob;
  } v;
} db_bind_t;

// Convenience constructors for db_bind_t
static inline db_bind_t db_bind_null (void) { db_bind_t b = { .type = DB_BIND_NULL }; return b; }
static inline db_bind_t db_bind_i64  (int64_t x) { db_bind_t b = { .type = DB_BIND_I64, .v.i64 = x }; return b; }
static inline db_bind_t db_bind_u64  (uint64_t x) { db_bind_t b = { .type = DB_BIND_U64, .v.u64 = x }; return b; }
static inline db_bind_t db_bind_i32  (int32_t x) { db_bind_t b = { .type = DB_BIND_I32, .v.i32 = x }; return b; }
static inline db_bind_t db_bind_u32  (uint32_t x) { db_bind_t b = { .type = DB_BIND_U32, .v.u32 = x }; return b; }
static inline db_bind_t db_bind_bool (bool x) { db_bind_t b = { .type = DB_BIND_BOOL, .v.b = x }; return b; }

static inline db_bind_t
db_bind_text (const char *s)
{
  db_bind_t b = { .type = DB_BIND_TEXT, .v.text = { .ptr = s, .len = 0 } };
  return b;
}

static inline db_bind_t
db_bind_text_n (const char *s, size_t n)
{
  db_bind_t b = { .type = DB_BIND_TEXT, .v.text = { .ptr = s, .len = n } };
  return b;
}

static inline db_bind_t
db_bind_blob (const void *p, size_t n)
{
  db_bind_t b = { .type = DB_BIND_BLOB, .v.blob = { .ptr = p, .len = n } };
  return b;
}

// -----------------------------------------------------------------------------
// Driver & Result Opaque Types
// -----------------------------------------------------------------------------

// Opaque handle for a database connection
typedef struct db_s         db_t;
// Opaque handle for a query result set
typedef struct db_res_s     db_res_t;

// -----------------------------------------------------------------------------
// Driver Identification
// -----------------------------------------------------------------------------

typedef enum
{
  DB_BACKEND_UNKNOWN = 0,
  DB_BACKEND_SQLITE,
  DB_BACKEND_POSTGRES
  // Future: DB_BACKEND_MYSQL, etc.
} db_backend_t;

// -----------------------------------------------------------------------------
// Configuration
// -----------------------------------------------------------------------------

// Minimal configuration structure for opening a database connection
typedef struct
{
  db_backend_t backend;         // Which backend to use

  // SQLite specific configuration
  const char  *sqlite_path;     // Path to the SQLite database file (e.g., "./twclone.db")

  // Postgres specific configuration
  const char  *pg_conninfo;     // libpq connection string (e.g., "host=localhost dbname=twclone")

  // Generic connection parameters (copied from db_cfg in old db_api.h)
  int connect_timeout_ms;      // Optional, 0 means default
  int statement_timeout_ms;    // Optional, 0 means default
  int lock_timeout_ms;         // Optional, 0 means default
  bool app_name_pid;           // If true, append pid to application_name
  int log_slow_ms;             // Log queries slower than this (0 disables)
} db_config_t;

// -----------------------------------------------------------------------------
// Core Connection Lifecycle
// -----------------------------------------------------------------------------

/**
 * @brief Opens a database connection based on the provided configuration.
 * @param cfg Pointer to the configuration structure.
 * @param err Pointer to an error structure to fill on failure.
 * @return An opaque db_t handle on success, or NULL on failure.
 *         Caller is responsible for calling db_close() on the returned handle.
 */
db_t * db_open (const db_config_t *cfg, db_error_t *err);

/**
 * @brief Closes a database connection and frees associated resources.
 * @param db The database handle to close. Safe to call with NULL.
 */
void   db_close (db_t *db);

/**
 * @brief Returns the type of the backend for the given database handle.
 * @param db The database handle.
 * @return The backend type (e.g., DB_BACKEND_POSTGRES).
 */
db_backend_t db_backend (const db_t *db);

// -----------------------------------------------------------------------------
// Transactions
// -----------------------------------------------------------------------------

typedef enum
{
  DB_TX_DEFAULT = 0,            // Standard transaction behavior
  DB_TX_IMMEDIATE               // SQLite: BEGIN IMMEDIATE; Postgres: ignored or mapped
} db_tx_flags_t;

/**
 * @brief Begins a new database transaction.
 * @param db The database handle.
 * @param flags Flags to modify transaction behavior (e.g., DB_TX_IMMEDIATE).
 * @param err Pointer to an error structure to fill on failure.
 * @return true on success, false on failure.
 */
bool db_tx_begin    (db_t *db, db_tx_flags_t flags, db_error_t *err);

/**
 * @brief Commits the current database transaction.
 * @param db The database handle.
 * @param err Pointer to an error structure to fill on failure.
 * @return true on success, false on failure.
 */
bool db_tx_commit   (db_t *db, db_error_t *err);

/**
 * @brief Rolls back the current database transaction.
 * @param db The database handle.
 * @param err Pointer to an error structure to fill on failure.
 * @return true on success, false on failure.
 */
bool db_tx_rollback (db_t *db, db_error_t *err);

// -----------------------------------------------------------------------------
// Execution (No Rows Returned)
// -----------------------------------------------------------------------------

/**
 * @brief Executes an SQL statement that does not return rows (e.g., INSERT, UPDATE, DELETE, DDL).
 *        SQL uses '$N' for placeholders.
 * @param db The database handle.
 * @param sql The SQL string.
 * @param params Array of db_bind_t structures for parameter binding.
 * @param n_params Number of parameters in the array.
 * @param err Pointer to an error structure to fill on failure.
 * @return true on success, false on failure.
 */
bool db_exec (db_t *db,
              const char *sql,
              const db_bind_t *params,
              size_t n_params,
              db_error_t *err);

/**
 * @brief Executes an SQL statement and retrieves the number of rows affected.
 *        SQL uses '$N' for placeholders.
 * @param db The database handle.
 * @param sql The SQL string.
 * @param params Array of db_bind_t structures.
 * @param n_params Number of parameters.
 * @param out_rows Pointer to a int64_t to store the number of affected rows.
 * @param err Pointer to an error structure.
 * @return true on success, false on failure.
 */
bool db_exec_rows_affected (db_t *db,
                            const char *sql,
                            const db_bind_t *params,
                            size_t n_params,
                            int64_t *out_rows,
                            db_error_t *err);

// -----------------------------------------------------------------------------
// Query (Rows Returned)
// -----------------------------------------------------------------------------

/**
 * @brief Executes an SQL query that returns rows. SQL uses '$N' for placeholders.
 * @param db The database handle.
 * @param sql The SQL string.
 * @param params Array of db_bind_t structures.
 * @param n_params Number of parameters.
 * @param out_res Pointer to a db_res_t* to store the result set handle.
 *                Caller MUST call db_res_finalize() on the returned handle.
 * @param err Pointer to an error structure.
 * @return true on success (*out_res will be non-NULL), false on failure.
 */
bool db_query (db_t *db,
               const char *sql,
               const db_bind_t *params,
               size_t n_params,
               db_res_t **out_res,
               db_error_t *err);

// -----------------------------------------------------------------------------
// Result Set Inspection
// -----------------------------------------------------------------------------

typedef enum
{
  DB_TYPE_UNKNOWN = 0,
  DB_TYPE_INTEGER,
  DB_TYPE_FLOAT,
  DB_TYPE_TEXT,
  DB_TYPE_BLOB,
  DB_TYPE_NULL
} db_col_type_t;

/**
 * @brief Advances the result set cursor to the next row.
...
 */
bool db_res_step (db_res_t *res, db_error_t *err);

/**
 * @brief Returns the name of the specified column.
 */
const char * db_res_col_name (const db_res_t *res, int col_idx);

/**
 * @brief Returns the generic type of the specified column.
 */
db_col_type_t db_res_col_type (const db_res_t *res, int col_idx);

/**
 * @brief Cancels further processing of the result set.
 */
void db_res_cancel (db_res_t *res);

/**
 * @brief Returns the number of columns in the current result set.
 * @param res The result set handle.
 * @return Number of columns, or -1 on error/invalid handle.
 */
int  db_res_col_count (const db_res_t *res);

/**
 * @brief Checks if the value in the specified column of the current row is SQL NULL.
 * @param res The result set handle.
 * @param col_idx The zero-based column index.
 * @return true if the column is NULL, false otherwise.
 */
bool db_res_col_is_null (const db_res_t *res, int col_idx);

/**
 * @brief Retrieves the value of a column as a 64-bit signed integer.
 * @param res The result set handle.
 * @param col_idx The zero-based column index.
 * @param err Pointer to an error structure. If conversion fails, err->code is DB_ERR_TYPE.
 * @return The integer value, or 0 on error/NULL.
 */
int64_t     db_res_col_i64  (const db_res_t *res, int col_idx, db_error_t *err);

/**
 * @brief Retrieves the value of a column as a 64-bit unsigned integer.
 * @param res The result set handle.
 * @param col_idx The zero-based column index.
 * @param err Pointer to an error structure. If conversion fails, err->code is DB_ERR_TYPE.
 * @return The unsigned integer value, or 0 on error/NULL.
 */
uint64_t    db_res_col_u64  (const db_res_t *res, int col_idx, db_error_t *err);

/**
 * @brief Retrieves the value of a column as a 32-bit signed integer.
 * @param res The result set handle.
 * @param col_idx The zero-based column index.
 * @param err Pointer to an error structure. If conversion fails, err->code is DB_ERR_TYPE.
 * @return The integer value, or 0 on error/NULL.
 */
int32_t     db_res_col_i32  (const db_res_t *res, int col_idx, db_error_t *err);

/**
 * @brief Retrieves the value of a column as a 32-bit unsigned integer.
 * @param res The result set handle.
 * @param col_idx The zero-based column index.
 * @param err Pointer to an error structure. If conversion fails, err->code is DB_ERR_TYPE.
 * @return The unsigned integer value, or 0 on error/NULL.
 */
uint32_t    db_res_col_u32  (const db_res_t *res, int col_idx, db_error_t *err);

/**
 * @brief Retrieves the value of a column as a boolean.
 * @param res The result set handle.
 * @param col_idx The zero-based column index.
 * @param err Pointer to an error structure. If conversion fails, err->code is DB_ERR_TYPE.
 * @return The boolean value, or false on error/NULL.
 */
bool        db_res_col_bool (const db_res_t *res, int col_idx, db_error_t *err);

/**
 * @brief Retrieves the value of a column as a double-precision float.
 * @param res The result set handle.
 * @param col_idx The zero-based column index.
 * @param err Pointer to an error structure. If conversion fails, err->code is DB_ERR_TYPE.
 * @return The double value, or 0.0 on error/NULL.
 */
double      db_res_col_double (const db_res_t *res, int col_idx, db_error_t *err);

/**
 * @brief Retrieves the value of a column as a NUL-terminated text string.
 * @param res The result set handle.
 * @param col_idx The zero-based column index.
 * @param err Pointer to an error structure. If conversion fails, err->code is DB_ERR_TYPE.
 * @return A pointer to the string, or NULL if the column is SQL NULL or on error.
 *         The pointer is valid until the next db_res_step() or db_res_finalize().
 *         Caller MUST NOT free this pointer.
 */
const char *db_res_col_text (const db_res_t *res, int col_idx, db_error_t *err);

/**
 * @brief Retrieves the value of a column as binary data (BLOB).
 * @param res The result set handle.
 * @param col_idx The zero-based column index.
 * @param out_len Pointer to store the length of the BLOB in bytes.
 * @param err Pointer to an error structure. If conversion fails, err->code is DB_ERR_TYPE.
 * @return A pointer to the binary data, or NULL if the column is SQL NULL or on error.
 *         The pointer is valid until the next db_res_step() or db_res_finalize().
 *         Caller MUST NOT free this pointer.
 */
const void *db_res_col_blob (const db_res_t *res, int col_idx, size_t *out_len, db_error_t *err);

/**
 * @brief Frees the result set and associated resources. Invalidates any pointers
 *        previously returned by column accessors for this result set.
 * @param res The result set handle. Safe to call with NULL.
 */
void db_res_finalize (db_res_t *res);

// -----------------------------------------------------------------------------
// Optional: Operation-Level API (V-Table for Optimized Backend Operations)
// -----------------------------------------------------------------------------

// Forward declarations for game structures (will be defined in game_db.h)
struct player_s;
struct ship_s;
struct bank_tx_info_s; // Example

/**
 * @brief Structure containing function pointers for high-level, optimized operations.
 *        A driver may implement these functions to leverage backend-specific features
 *        (e.g., stored procedures) for atomic or complex business logic.
 *        If a pointer is NULL, the Logic Manager (game_db.c) must fall back to
 *        generic SQL.
 */
typedef struct
{
    // Example: Retrieve player data (optimised for backend)
    // Returns 0 on success (player_s filled), DB_ERR_NOT_FOUND, or other error.
    int (*player_get_by_id)(db_t *db, int64_t player_id, struct player_s *out_player, db_error_t *err);
    
    // Example: Atomically post a group of bank transactions
    // int (*bank_post_transaction_group)(db_t *db, const struct bank_tx_info_s *tx_group, db_error_t *err);

    // Add more operation-level functions here as needed
} db_ops_vtable_t;

/**
 * @brief Retrieves the operation-level v-table for the given database handle.
 * @param db The database handle.
 * @return A pointer to the db_ops_vtable_t for this driver, or NULL if none.
 */
const db_ops_vtable_t* db_get_ops(db_t *db);


/* Execute an INSERT and return the generated primary key.
 * Contract: sql must be the INSERT statement for the target table.
 * Drivers implement the backend-specific way of retrieving the key.
 */
bool db_exec_insert_id(db_t *db,
                       const char *sql,
                       const db_bind_t *params,
                       size_t n_params,
                       int64_t *out_id,
                       db_error_t *err);

  
#ifdef __cplusplus
}
#endif

#endif // DB_API_H
