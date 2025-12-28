#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "../errors.h" // Force include at the top
#include "db_api.h"
#include "db_int.h" // Internal header for shared struct definitions

// Include specific backend open functions
#include "pg/db_pg.h"
#ifdef DB_BACKEND_SQLITE
#include "sqlite/db_sqlite.h" // Include for SQLite backend
#endif

// The actual definitions for struct db_s and struct db_res_s are in db_int.h
// These are not needed here again.



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

    return db->vt->exec_insert_id(db, sql, params, n_params, out_id, err);
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
#ifdef DB_BACKEND_SQLITE
        case DB_BACKEND_SQLITE:
            if (!cfg->sqlite_path) {
                err->code = ERR_DB_CONFIG;
                strncpy(err->message, "db_open: SQLite path is NULL", sizeof(err->message));
                free(db);
                return NULL;
            }
            db->impl = db_sqlite_open_internal(db, cfg, err);
            break;
#endif
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
        err->code = ERR_DB_CLOSED;
        strncpy(err->message, "db_exec: Invalid DB handle or vtable", sizeof(err->message));
        return false;
    }
    return db->vt->exec(db, sql, params, n_params, err);
}

bool db_exec_rows_affected(db_t *db, const char *sql, const db_bind_t *params, size_t n_params, int64_t *out_rows, db_error_t *err) {
    if (!db || !db->vt || !db->vt->exec_rows_affected) {
        err->code = ERR_DB_CLOSED;
        strncpy(err->message, "db_exec_rows_affected: Invalid DB handle or vtable", sizeof(err->message));
        return false;
    }
    return db->vt->exec_rows_affected(db, sql, params, n_params, out_rows, err);
}

bool db_query(db_t *db, const char *sql, const db_bind_t *params, size_t n_params, db_res_t **out_res, db_error_t *err) {
    if (!db || !db->vt || !db->vt->query) {
        err->code = ERR_DB_CLOSED;
        strncpy(err->message, "db_query: Invalid DB handle or vtable", sizeof(err->message));
        return false;
    }
    return db->vt->query(db, sql, params, n_params, out_res, err);
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


