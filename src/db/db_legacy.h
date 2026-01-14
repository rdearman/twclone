#ifndef DB_LEGACY_H
#define DB_LEGACY_H

/*
 * *****************************************************************************
 * *****************************************************************************
 *
 *   TEMPORARY COMPATIBILITY HEADER
 *
 *   DO NOT USE IN NEW CODE
 *
 *   TO BE REMOVED AFTER PHASE 3
 *
 * *****************************************************************************
 * *****************************************************************************
 */

/**
 * @file db_legacy.h
 * @brief TEMPORARY COMPATIBILITY HEADER — REMOVE AFTER PHASE 3.
 * 
 * Provides low-level SQL execution primitives to non-DB code during the 
 * transition to a DB-agnostic architecture.
 * 
 * DO NOT ADD NEW CODE TO THIS HEADER.
 * DO NOT INCLUDE THIS HEADER IN NEW MODULES.
 */

#include "db_api.h"

#ifdef __cplusplus
extern "C" {
#endif

// -----------------------------------------------------------------------------
// Low-level SQL execution primitives (Temporarily public)
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
 * @brief Executes an SQL statement that does not return rows.
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
 * @brief Executes an SQL query that returns rows.
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

#endif // DB_LEGACY_H
