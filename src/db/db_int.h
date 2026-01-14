#ifndef DB_INT_H
#define DB_INT_H

#ifndef TW_DB_INTERNAL
#error "db_int.h is internal. Only src/db/** may include it."
#endif

#include "db_api.h" // For db_t, db_res_t, db_error_t, etc.

// -----------------------------------------------------------------------------
// Low-level SQL execution primitives (Moved from db_api.h)
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

/**
 * @brief Executes an SQL statement that does not return rows (e.g., INSERT, UPDATE, DELETE, DDL).
 *        SQL uses '$N' for placeholders.
 */
bool db_exec (db_t *db,
              const char *sql,
              const db_bind_t *params,
              size_t n_params,
              db_error_t *err);

/**
 * @brief Executes an SQL statement and retrieves the number of rows affected.
 */
bool db_exec_rows_affected (db_t *db,
                            const char *sql,
                            const db_bind_t *params,
                            size_t n_params,
                            int64_t *out_rows,
                            db_error_t *err);

/**
 * @brief Executes an SQL query that returns rows. SQL uses '$N' for placeholders.
 */
bool db_query (db_t *db,
               const char *sql,
               const db_bind_t *params,
               size_t n_params,
               db_res_t **out_res,
               db_error_t *err);

/**
 * @brief Executes an INSERT/UPDATE with RETURNING clause and returns result set.
 */
bool db_exec_returning (db_t *db,
                        const char *sql,
                        const db_bind_t *params,
                        size_t n_params,
                        db_res_t **out_res,
                        db_error_t *err);

/**
 * @brief Execute an INSERT and return the generated primary key.
 * Contract: sql must be the INSERT statement for the target table.
 * Drivers implement the backend-specific way of retrieving the key.
 */
bool db_exec_insert_id(db_t *db,
                       const char *sql,
                       const db_bind_t *params,
                       size_t n_params,
                       int64_t *out_id,
                       db_error_t *err);

/**
 * @file db_int.h
 * @brief Internal header for generic DB API. Defines structures used by db_api.c
 *        and backend drivers but opaque to external consumers.
 */

// Forward declarations for internal backend-specific data structures
struct db_pg_impl_s; // For PostgreSQL driver
struct db_sqlite_impl_s; // For SQLite driver (future)


// V-table for generic DB operations
typedef struct db_vt_s {
    // Core connection lifecycle
    void (*close)(db_t *db);

    // Transactions
    bool (*tx_begin)(db_t *db, db_tx_flags_t flags, db_error_t *err);
    bool (*tx_commit)(db_t *db, db_error_t *err);
    bool (*tx_rollback)(db_t *db, db_error_t *err);

    // Execution (no rows)
    bool (*exec)(db_t *db, const char *sql, const db_bind_t *params, size_t n_params, db_error_t *err);
    bool (*exec_rows_affected)(db_t *db, const char *sql, const db_bind_t *params, size_t n_params, int64_t *out_rows, db_error_t *err);
  bool (*exec_insert_id)(db_t *db, const char *sql, const db_bind_t *params, size_t n_params,  int64_t *out_id,	 db_error_t *err);

    // Query (rows)
    bool (*query)(db_t *db, const char *sql, const db_bind_t *params, size_t n_params, db_res_t **out_res, db_error_t *err);
    bool (*exec_returning)(db_t *db, const char *sql, const db_bind_t *params, size_t n_params, db_res_t **out_res, db_error_t *err);

    // Result Set Navigation
    bool (*res_step)(db_res_t *res, db_error_t *err);
    void (*res_cancel)(db_res_t *res);
    int  (*res_col_count)(const db_res_t *res);
    const char* (*res_col_name)(const db_res_t *res, int col_idx);
    db_col_type_t (*res_col_type)(const db_res_t *res, int col_idx);
    bool (*res_col_is_null)(const db_res_t *res, int col_idx);

    // Column Accessors
    int64_t     (*res_col_i64)(const db_res_t *res, int col_idx, db_error_t *err);
    uint64_t    (*res_col_u64)(const db_res_t *res, int col_idx, db_error_t *err);
    int32_t     (*res_col_i32)(const db_res_t *res, int col_idx, db_error_t *err);
    uint32_t    (*res_col_u32)(const db_res_t *res, int col_idx, db_error_t *err);
    bool        (*res_col_bool)(const db_res_t *res, int col_idx, db_error_t *err);
    double      (*res_col_double)(const db_res_t *res, int col_idx, db_error_t *err);
    const char *(*res_col_text)(const db_res_t *res, int col_idx, db_error_t *err);
    const void *(*res_col_blob)(const db_res_t *res, int col_idx, size_t *out_len, db_error_t *err);

    // Finalize result set
    void (*res_finalize)(db_res_t *res);
    /* ---- Domain helpers (temporary until full SQL->SP migration) ---- */
  bool (*ship_repair_atomic)(db_t *db,
                           int player_id,
                           int ship_id,
                           int cost,
                           int64_t *out_new_credits,
                           db_error_t *err);

  
} db_vt_t;


// db_t handle definition (opaque to external users)
struct db_s {
    db_backend_t backend;           // Which backend this is
    const db_vt_t *vt;              // Vtable for backend-specific operations
    void *impl;                     // Pointer to the backend's internal data (e.g., db_pg_impl_s)
    db_config_t config;             // Copy of the configuration used to open this DB
    const db_ops_vtable_t *ops_vt;  // Optional vtable for high-level optimized operations
    int tx_nest_level;              // To track nested transaction calls
};

// db_res_t handle definition (opaque to external users)
struct db_res_s {
    const db_t *db;                 // Pointer to the parent db_t handle
    void *impl;                     // Pointer to the backend's internal result data (e.g., PGresult*)
    int current_row;                // Current row index for multi-row result sets
    int num_rows;                   // Total number of rows in the result set
    int num_cols;                   // Total number of columns in the result set
};



#endif // DB_INT_H
