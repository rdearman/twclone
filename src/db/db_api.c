#define TW_DB_INTERNAL 1
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

#include "../errors.h" // Force include at the top
#include "db_api.h"
#include "db_int.h" // Internal header for shared struct definitions
#include "sql_driver.h" // For sql_build

// Include specific backend open functions
#include "pg/db_pg.h"

/**
 * @brief Helper to set error with category
 */
static inline void
db_error_set(db_error_t *err, int code, db_error_category_t category, const char *msg)
{
  if (!err) return;
  err->code = code;
  err->category = category;
  if (msg)
    snprintf(err->message, sizeof(err->message), "%s", msg);
  else
    err->message[0] = '\0';
}

/**
 * @brief Detect if SQL contains {N} placeholders that need rendering.
 * 
 * Returns 1 if SQL contains {digits}, 0 otherwise.
 * This check is necessary to avoid redundant sql_build() calls for
 * plain SQL strings that use $N placeholders (already PostgreSQL-ready).
 */
static int
sql_needs_build(const char *sql)
{
  if (!sql) return 0;
  
  for (const char *p = sql; *p; ++p) {
    if (*p == '{') {
      /* Check if next char is a digit */
      if (p[1] && isdigit((unsigned char)p[1])) {
        /* Simple check: {digit exists, assume {N} pattern */
        return 1;
      }
    }
  }
  return 0;
}

/**
 * @brief Render SQL placeholders from {N} to backend-specific format.
 * 
 * Returns allocated SQL string if rendering was needed and successful,
 * NULL otherwise. Caller must free the returned string when done.
 * 
 * On error, sets err and returns NULL.
 */
static char *
db_render_sql(db_t *db, const char *sql, db_error_t *err)
{
  if (!sql || !sql_needs_build(sql)) {
    return NULL;  /* No rendering needed */
  }
  
  if (!db) {
    if (err) {
      err->code = ERR_DB_INTERNAL;
      snprintf(err->message, sizeof(err->message), "db_render_sql: NULL db handle");
    }
    return NULL;
  }
  
  /* Allocate buffer for rendered SQL */
  char *rendered = malloc(4096);
  if (!rendered) {
    if (err) {
      err->code = ERR_DB_NOMEM;
      snprintf(err->message, sizeof(err->message), "db_render_sql: malloc failed");
    }
    return NULL;
  }
  
  /* Render {N} to backend-specific placeholders */
  if (sql_build(db, sql, rendered, 4096) != 0) {
    free(rendered);
    if (err) {
      err->code = ERR_DB_INTERNAL;
      snprintf(err->message, sizeof(err->message), "db_render_sql: sql_build failed");
    }
    return NULL;
  }
  
  return rendered;
}

bool
db_exec_insert_id(db_t *db,
                  const char *sql,
                  const db_bind_t *params,
                  size_t n_params,
                  int64_t *out_id,
                  db_error_t *err)
{
    if (!db || !db->vt || !db->vt->exec_insert_id) {
        if (err) {
            err->code = ERR_DB_INTERNAL;
            snprintf(err->message, sizeof(err->message),
                     "exec_insert_id not implemented by backend");
        }
        return false;
    }

    char *rendered = db_render_sql(db, sql, err);
    bool result = db->vt->exec_insert_id(db, rendered ? rendered : sql, params, n_params, out_id, err);
    free(rendered);
    return result;
}


db_t *db_open(const db_config_t *cfg, db_error_t *err) {
    db_error_clear(err);

    if (!cfg) {
        err->code = ERR_DB_CONFIG;
        strncpy(err->message, "db_open: Configuration is NULL", sizeof(err->message));
        return NULL;
    }

    db_t *db = (db_t *)calloc(1, sizeof(db_t));
    if (!db) {
        err->code = ERR_DB_NOMEM;
        strncpy(err->message, "db_open: Out of memory for db_t handle", sizeof(err->message));
        return NULL;
    }

    db->backend = cfg->backend;
    db->config = *cfg; // Store a copy of the config

    switch (cfg->backend) {
        case DB_BACKEND_POSTGRES:
            if (!cfg->pg_conninfo) {
                err->code = ERR_DB_CONFIG;
                strncpy(err->message, "db_open: PostgreSQL conninfo is NULL", sizeof(err->message));
                free(db);
                return NULL;
            }
            db->impl = db_pg_open_internal(db, cfg, err); // Pass db_t for internal setup
            break;
        case DB_BACKEND_UNKNOWN:
        default:
            err->code = ERR_DB_CONFIG;
            strncpy(err->message, "db_open: Unknown or unspecified backend", sizeof(err->message));
            free(db);
            return NULL;
    }

    if (!db->impl) {
        // Error already set by backend-specific open function
        free(db);
        return NULL;
    }

    return db;
}

void db_close(db_t *db) {
    if (!db) return;
    if (db->vt && db->vt->close) {
        db->vt->close(db); // Call backend-specific close
    }
    free(db);
}

db_backend_t db_backend(const db_t *db) {
    if (!db) return DB_BACKEND_UNKNOWN;
    return db->backend;
}

bool db_tx_begin(db_t *db, db_tx_flags_t flags, db_error_t *err) {
    if (!db || !db->vt || !db->vt->tx_begin) {
        err->code = ERR_DB_CLOSED;
        strncpy(err->message, "db_tx_begin: Invalid DB handle or vtable", sizeof(err->message));
        return false;
    }

    if (db->tx_nest_level == 0) {
        if (!db->vt->tx_begin(db, flags, err)) {
            return false;
        }
    }
    db->tx_nest_level++;
    return true;
}

bool db_tx_commit(db_t *db, db_error_t *err) {
    if (!db || !db->vt || !db->vt->tx_commit) {
        err->code = ERR_DB_CLOSED;
        strncpy(err->message, "db_tx_commit: Invalid DB handle or vtable", sizeof(err->message));
        return false;
    }

    if (db->tx_nest_level <= 0) {
        err->code = ERR_DB_TX;
        strncpy(err->message, "db_tx_commit: Not in a transaction", sizeof(err->message));
        return false;
    }

    db->tx_nest_level--;
    if (db->tx_nest_level == 0) {
        return db->vt->tx_commit(db, err);
    }
    return true;
}

bool db_tx_rollback(db_t *db, db_error_t *err) {
    if (!db || !db->vt || !db->vt->tx_rollback) {
        err->code = ERR_DB_CLOSED;
        strncpy(err->message, "db_tx_rollback: Invalid DB handle or vtable", sizeof(err->message));
        return false;
    }

    // Rollback is always sent to the backend if we think we are in a transaction
    // and it resets the nest level completely.
    bool success = true;
    if (db->tx_nest_level > 0) {
        success = db->vt->tx_rollback(db, err);
    }
    db->tx_nest_level = 0;
    return success;
}

bool db_exec(db_t *db, const char *sql, const db_bind_t *params, size_t n_params, db_error_t *err) {
    if (!db || !db->vt || !db->vt->exec) {
        if (err) {
            err->code = ERR_DB_CLOSED;
            strncpy(err->message, "db_exec: Invalid DB handle or vtable", sizeof(err->message));
        }
        return false;
    }
    
    char *rendered = db_render_sql(db, sql, err);
    bool result = db->vt->exec(db, rendered ? rendered : sql, params, n_params, err);
    free(rendered);
    return result;
}

bool db_exec_rows_affected(db_t *db, const char *sql, const db_bind_t *params, size_t n_params, int64_t *out_rows, db_error_t *err) {
    if (!db || !db->vt || !db->vt->exec_rows_affected) {
        if (err) {
            err->code = ERR_DB_CLOSED;
            strncpy(err->message, "db_exec_rows_affected: Invalid DB handle or vtable", sizeof(err->message));
        }
        return false;
    }
    
    char *rendered = db_render_sql(db, sql, err);
    bool result = db->vt->exec_rows_affected(db, rendered ? rendered : sql, params, n_params, out_rows, err);
    free(rendered);
    return result;
}

bool db_query(db_t *db, const char *sql, const db_bind_t *params, size_t n_params, db_res_t **out_res, db_error_t *err) {
    if (!db || !db->vt || !db->vt->query) {
        if (err) {
            err->code = ERR_DB_CLOSED;
            strncpy(err->message, "db_query: Invalid DB handle or vtable", sizeof(err->message));
        }
        return false;
    }
    
    char *rendered = db_render_sql(db, sql, err);
    bool result = db->vt->query(db, rendered ? rendered : sql, params, n_params, out_res, err);
    free(rendered);
    return result;
}

bool db_exec_returning(db_t *db, const char *sql, const db_bind_t *params, size_t n_params, db_res_t **out_res, db_error_t *err) {
    if (!db || !db->vt) {
        if (err) {
            err->code = ERR_DB_CLOSED;
            strncpy(err->message, "db_exec_returning: Invalid DB handle or vtable", sizeof(err->message));
        }
        return false;
    }
    
    if (!db->vt->exec_returning) {
        if (err) {
            err->code = ERR_DB_INTERNAL;
            strncpy(err->message, "db_exec_returning: Not implemented for this backend", sizeof(err->message));
        }
        return false;
    }
    
    char *rendered = db_render_sql(db, sql, err);
    bool result = db->vt->exec_returning(db, rendered ? rendered : sql, params, n_params, out_res, err);
    free(rendered);
    return result;
}

bool db_res_step(db_res_t *res, db_error_t *err) {
    if (!res || !res->db || !res->db->vt || !res->db->vt->res_step) {
        err->code = ERR_DB_CLOSED;
        strncpy(err->message, "db_res_step: Invalid Result handle or vtable", sizeof(err->message));
        return false;
    }
    return res->db->vt->res_step(res, err);
}

const char * db_res_col_name(const db_res_t *res, int col_idx) {
    if (!res || !res->db || !res->db->vt || !res->db->vt->res_col_name) {
        return NULL;
    }
    return res->db->vt->res_col_name(res, col_idx);
}

db_col_type_t db_res_col_type(const db_res_t *res, int col_idx) {
    if (!res || !res->db || !res->db->vt || !res->db->vt->res_col_type) {
        return DB_TYPE_UNKNOWN;
    }
    return res->db->vt->res_col_type(res, col_idx);
}

void db_res_cancel(db_res_t *res) {
    if (!res || !res->db || !res->db->vt || !res->db->vt->res_cancel) {
        return;
    }
    res->db->vt->res_cancel(res);
}

int db_res_col_count(const db_res_t *res) {
    if (!res || !res->db || !res->db->vt || !res->db->vt->res_col_count) {
        return -1; // Indicate error
    }
    return res->db->vt->res_col_count(res);
}

bool db_res_col_is_null(const db_res_t *res, int col_idx) {
    if (!res || !res->db || !res->db->vt || !res->db->vt->res_col_is_null) {
        return true; // Safest assumption on error
    }
    return res->db->vt->res_col_is_null(res, col_idx);
}

int64_t db_res_col_i64(const db_res_t *res, int col_idx, db_error_t *err) {
    if (!res || !res->db || !res->db->vt || !res->db->vt->res_col_i64) {
        err->code = ERR_DB_CLOSED; strncpy(err->message, "db_res_col_i64: Invalid Result handle or vtable", sizeof(err->message));
        return 0;
    }
    return res->db->vt->res_col_i64(res, col_idx, err);
}

uint64_t db_res_col_u64(const db_res_t *res, int col_idx, db_error_t *err) {
    if (!res || !res->db || !res->db->vt || !res->db->vt->res_col_u64) {
        err->code = ERR_DB_CLOSED; strncpy(err->message, "db_res_col_u64: Invalid Result handle or vtable", sizeof(err->message));
        return 0;
    }
    return res->db->vt->res_col_u64(res, col_idx, err);
}

int32_t db_res_col_i32(const db_res_t *res, int col_idx, db_error_t *err) {
    if (!res || !res->db || !res->db->vt || !res->db->vt->res_col_i32) {
        err->code = ERR_DB_CLOSED; strncpy(err->message, "db_res_col_i32: Invalid Result handle or vtable", sizeof(err->message));
        return 0;
    }
    return res->db->vt->res_col_i32(res, col_idx, err);
}

uint32_t db_res_col_u32(const db_res_t *res, int col_idx, db_error_t *err) {
    if (!res || !res->db || !res->db->vt || !res->db->vt->res_col_u32) {
        err->code = ERR_DB_CLOSED; strncpy(err->message, "db_res_col_u32: Invalid Result handle or vtable", sizeof(err->message));
        return 0;
    }
    return res->db->vt->res_col_u32(res, col_idx, err);
}

bool db_res_col_bool(const db_res_t *res, int col_idx, db_error_t *err) {
    if (!res || !res->db || !res->db->vt || !res->db->vt->res_col_bool) {
        err->code = ERR_DB_CLOSED; strncpy(err->message, "db_res_col_bool: Invalid Result handle or vtable", sizeof(err->message));
        return false;
    }
    return res->db->vt->res_col_bool(res, col_idx, err);
}

double db_res_col_double(const db_res_t *res, int col_idx, db_error_t *err) {
    if (!res || !res->db || !res->db->vt || !res->db->vt->res_col_double) {
        err->code = ERR_DB_CLOSED; strncpy(err->message, "db_res_col_double: Invalid Result handle or vtable", sizeof(err->message));
        return 0.0;
    }
    return res->db->vt->res_col_double(res, col_idx, err);
}

const char *db_res_col_text(const db_res_t *res, int col_idx, db_error_t *err) {
    if (!res || !res->db || !res->db->vt || !res->db->vt->res_col_text) {
        err->code = ERR_DB_CLOSED; strncpy(err->message, "db_res_col_text: Invalid Result handle or vtable", sizeof(err->message));
        return NULL;
    }
    return res->db->vt->res_col_text(res, col_idx, err);
}

const void *db_res_col_blob(const db_res_t *res, int col_idx, size_t *out_len, db_error_t *err) {
    if (!res || !res->db || !res->db->vt || !res->db->vt->res_col_blob) {
        err->code = ERR_DB_CLOSED; strncpy(err->message, "db_res_col_blob: Invalid Result handle or vtable", sizeof(err->message));
        if (out_len) *out_len = 0;
        return NULL;
    }
    return res->db->vt->res_col_blob(res, col_idx, out_len, err);
}

void db_res_finalize(db_res_t *res) {
    if (!res) return;
    if (res->db && res->db->vt && res->db->vt->res_finalize) {
        res->db->vt->res_finalize(res);
    } else {
        // Fallback if vtable or finalize is missing (shouldn't happen with valid res)
        free(res);
    }
}

const db_ops_vtable_t* db_get_ops(db_t *db) {
    if (!db) return NULL;
    return db->ops_vt;
}


bool
db_ship_repair_atomic(db_t *db,
                      int player_id,
                      int ship_id,
                      int cost,
                      int64_t *out_new_credits,
                      db_error_t *err)
{
  if (err) db_error_clear(err);

  if (!db || !db->vt || !db->vt->ship_repair_atomic)
    {
      if (err)
        {
          err->code = ERR_DB;
          err->backend_code = 0;
          err->message[0] = '\0';
        }
      return false;
    }

  return db->vt->ship_repair_atomic(db,
                                    player_id,
                                    ship_id,
                                    cost,
                                    out_new_credits,
                                    err);
}


