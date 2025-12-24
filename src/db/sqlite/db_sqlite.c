#include "db_sqlite.h"
#include "../db_api.h"
#include "../db_int.h"
#include "../../server_log.h" // Assuming server_log is still accessible

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sqlite3.h> // SQLite C API

// Internal SQLite implementation specific data
typedef struct db_sqlite_impl_s {
  sqlite3 *db_conn;
  bool in_tx;
  // Any other SQLite-specific state
} db_sqlite_impl_t;

// Internal SQLite result set implementation specific data
typedef struct db_sqlite_res_impl_s {
    sqlite3_stmt *stmt;
    int current_row; // Keeps track of steps for iteration logic
} db_sqlite_res_impl_t;


// --- Helper for error mapping ---
static void sqlite_map_error(sqlite3 *db_conn, int sqlite_rc, db_error_t *err) {
    if (!err) return;
    db_error_clear(err);

    err->backend_code = sqlite_rc;
    const char *sqlite_msg = sqlite3_errmsg(db_conn);
    if (sqlite_msg) {
        strlcpy(err->message, sqlite_msg, sizeof(err->message));
    } else {
        strlcpy(err->message, "Unknown SQLite error", sizeof(err->message));
    }

    switch (sqlite_rc) {
        case SQLITE_OK:
            err->code = 0;
            break;
        case SQLITE_BUSY:
        case SQLITE_LOCKED:
            err->code = ERR_DB_BUSY;
            break;
        case SQLITE_CONSTRAINT:
            err->code = ERR_DB_CONSTRAINT;
            break;
        case SQLITE_NOMEM:
            err->code = ERR_DB_NOMEM;
            break;
        case SQLITE_ERROR: // Generic SQL error
            err->code = ERR_DB_GENERIC;
            // Try to deduce more specific error from message or context if possible
            break;
        case SQLITE_AUTH:
            err->code = ERR_DB_AUTH;
            break;
        case SQLITE_MISUSE:
            err->code = ERR_DB_PROTOCOL;
            break;
        default:
            err->code = ERR_DB_GENERIC;
            break;
    }
}

// --- Helper for placeholder translation ($N to ?N for SQLite) ---
// This function assumes a maximum of 99 parameters for simplicity.
// For more robust handling, realloc or a more sophisticated parser would be needed.
static char* sqlite_translate_placeholders(const char* sql) {
    size_t sql_len = strlen(sql);
    char *new_sql = (char*)malloc(sql_len + 1);
    if (!new_sql) return NULL;
    new_sql[0] = '\0';

    const char *p_src = sql;
    char *p_dest = new_sql;
    size_t current_capacity = sql_len + 1;

    while (*p_src != '\0') {
        // Strip " FOR UPDATE" (case-insensitive check)
        if (strncasecmp(p_src, " FOR UPDATE", 11) == 0) {
            p_src += 11;
            continue;
        }

        if (*p_src == '$' && isdigit((unsigned char)p_src[1])) {
            // Found a placeholder like $1, $10, etc.
            int param_num = atoi(p_src + 1);
            if (param_num > 0) {
                const char *num_start = p_src + 1;
                while (isdigit((unsigned char)*num_start)) {
                    num_start++;
                }
                size_t placeholder_len = num_start - p_src;

                if (p_dest - new_sql + 1 >= current_capacity) {
                    current_capacity += 32;
                    new_sql = (char*)realloc(new_sql, current_capacity);
                    if (!new_sql) return NULL;
                    p_dest = new_sql + strlen(new_sql);
                }
                *p_dest++ = '?';

                char num_str[16];
                snprintf(num_str, sizeof(num_str), "%d", param_num);
                size_t num_len = strlen(num_str);

                if (p_dest - new_sql + num_len >= current_capacity) {
                    current_capacity += num_len + 32;
                    new_sql = (char*)realloc(new_sql, current_capacity);
                    if (!new_sql) return NULL;
                    p_dest = new_sql + strlen(new_sql);
                }
                strlcpy(p_dest, num_str, current_capacity - (p_dest - new_sql));
                p_dest += num_len;

                p_src += placeholder_len;
                continue;
            }
        }
        if (p_dest - new_sql + 1 >= current_capacity) {
            current_capacity += 32;
            new_sql = (char*)realloc(new_sql, current_capacity);
            if (!new_sql) return NULL;
            p_dest = new_sql + strlen(new_sql);
        }
        *p_dest++ = *p_src++;
        *p_dest = '\0';
    }

    return new_sql;
}


// --- Helper to bind parameters to an sqlite3_stmt ---
static int sqlite_bind_params(sqlite3_stmt *stmt, const db_bind_t *params, size_t n_params) {
    for (size_t i = 0; i < n_params; ++i) {
        int rc;
        int bind_idx = i + 1; // SQLite bind parameters are 1-indexed

        switch (params[i].type) {
            case DB_BIND_NULL:
                rc = sqlite3_bind_null(stmt, bind_idx);
                break;
            case DB_BIND_I64:
                rc = sqlite3_bind_int64(stmt, bind_idx, params[i].v.i64);
                break;
            case DB_BIND_U64: // SQLite doesn't have unsigned int bind, use signed int64
                rc = sqlite3_bind_int64(stmt, bind_idx, (long long)params[i].v.u64);
                break;
            case DB_BIND_I32:
                rc = sqlite3_bind_int(stmt, bind_idx, params[i].v.i32);
                break;
            case DB_BIND_U32: // SQLite doesn't have unsigned int bind, use signed int
                rc = sqlite3_bind_int(stmt, bind_idx, (int)params[i].v.u32);
                break;
            case DB_BIND_BOOL:
                rc = sqlite3_bind_int(stmt, bind_idx, params[i].v.b ? 1 : 0);
                break;
            case DB_BIND_TEXT:
                // Use SQLITE_TRANSIENT to make a copy of the string
                rc = sqlite3_bind_text(stmt, bind_idx, params[i].v.text.ptr, params[i].v.text.len == 0 ? -1 : params[i].v.text.len, SQLITE_TRANSIENT);
                break;
            case DB_BIND_BLOB:
                rc = sqlite3_bind_blob(stmt, bind_idx, params[i].v.blob.ptr, params[i].v.blob.len, SQLITE_TRANSIENT);
                break;
            default:
                LOGE("DB_SQLITE: Unsupported bind type for parameter %zu", i);
                return SQLITE_ERROR;
        }
        if (rc != SQLITE_OK) {
            LOGE("DB_SQLITE: Failed to bind parameter %zu: %s", i, sqlite3_errmsg(sqlite3_db_handle(stmt)));
            return rc;
        }
    }
    return SQLITE_OK;
}

// -----------------------------------------------------------------------------
// VTable Implementations
// -----------------------------------------------------------------------------

static void sqlite_close_impl(db_t *db) {
    db_sqlite_impl_t *sqlite_impl = (db_sqlite_impl_t*)db->impl;
    if (sqlite_impl) {
        if (sqlite_impl->db_conn) {
            sqlite3_close(sqlite_impl->db_conn);
        }
        free(sqlite_impl);
    }
}

static bool sqlite_tx_begin_impl(db_t *db, db_tx_flags_t flags, db_error_t *err) {
    db_sqlite_impl_t *sqlite_impl = (db_sqlite_impl_t*)db->impl;
    int rc;
    const char *sql_cmd = "BEGIN;"; // Default

    if (flags & DB_TX_IMMEDIATE) {
        sql_cmd = "BEGIN IMMEDIATE;";
    }

    rc = sqlite3_exec(sqlite_impl->db_conn, sql_cmd, NULL, NULL, NULL);
    if (rc != SQLITE_OK) {
        sqlite_map_error(sqlite_impl->db_conn, rc, err);
        return false;
    }
    sqlite_impl->in_tx = true;
    return true;
}

static bool sqlite_tx_commit_impl(db_t *db, db_error_t *err) {
    db_sqlite_impl_t *sqlite_impl = (db_sqlite_impl_t*)db->impl;
    int rc;

    if (!sqlite_impl->in_tx) {
        err->code = ERR_DB_TX;
        strlcpy(err->message, "sqlite_tx_commit: Not in a transaction", sizeof(err->message));
        return false;
    }

    rc = sqlite3_exec(sqlite_impl->db_conn, "COMMIT;", NULL, NULL, NULL);
    if (rc != SQLITE_OK) {
        sqlite_map_error(sqlite_impl->db_conn, rc, err);
        return false;
    }
    sqlite_impl->in_tx = false;
    return true;
}

static bool sqlite_tx_rollback_impl(db_t *db, db_error_t *err) {
    db_sqlite_impl_t *sqlite_impl = (db_sqlite_impl_t*)db->impl;
    int rc;

    if (!sqlite_impl->in_tx) {
        err->code = ERR_DB_TX;
        strlcpy(err->message, "sqlite_tx_rollback: Not in a transaction", sizeof(err->message));
        return false;
    }

    rc = sqlite3_exec(sqlite_impl->db_conn, "ROLLBACK;", NULL, NULL, NULL);
    if (rc != SQLITE_OK) {
        sqlite_map_error(sqlite_impl->db_conn, rc, err);
        return false;
    }
    sqlite_impl->in_tx = false;
    return true;
}

static bool sqlite_exec_internal(db_t *db, const char *sql, const db_bind_t *params, size_t n_params, int64_t *out_rows_affected, db_error_t *err) {
    db_sqlite_impl_t *sqlite_impl = (db_sqlite_impl_t*)db->impl;
    sqlite3_stmt *stmt = NULL;
    char *translated_sql = NULL;
    bool success = false;
    int rc;

    translated_sql = sqlite_translate_placeholders(sql);
    if (!translated_sql) {
        err->code = ERR_DB_NOMEM;
        strlcpy(err->message, "sqlite_exec_internal: Out of memory for SQL translation", sizeof(err->message));
        goto cleanup;
    }

    rc = sqlite3_prepare_v2(sqlite_impl->db_conn, translated_sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        sqlite_map_error(sqlite_impl->db_conn, rc, err);
        goto cleanup;
    }

    rc = sqlite_bind_params(stmt, params, n_params);
    if (rc != SQLITE_OK) {
        sqlite_map_error(sqlite_impl->db_conn, rc, err);
        goto cleanup;
    }

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) { // For exec, we expect SQLITE_DONE
        sqlite_map_error(sqlite_impl->db_conn, rc, err);
        goto cleanup;
    }

    if (out_rows_affected) {
        *out_rows_affected = sqlite3_changes(sqlite_impl->db_conn);
    }
    success = true;

cleanup:
    if (stmt) sqlite3_finalize(stmt);
    if (translated_sql) free(translated_sql);
    return success;
}

static bool sqlite_exec_impl(db_t *db, const char *sql, const db_bind_t *params, size_t n_params, db_error_t *err) {
    return sqlite_exec_internal(db, sql, params, n_params, NULL, err);
}

static bool sqlite_exec_rows_affected_impl(db_t *db, const char *sql, const db_bind_t *params, size_t n_params, int64_t *out_rows_affected, db_error_t *err) {
    return sqlite_exec_internal(db, sql, params, n_params, out_rows_affected, err);
}


static bool sqlite_query_impl(db_t *db, const char *sql, const db_bind_t *params, size_t n_params, db_res_t **out_res, db_error_t *err) {
    db_sqlite_impl_t *sqlite_impl = (db_sqlite_impl_t*)db->impl;
    sqlite3_stmt *stmt = NULL;
    char *translated_sql = NULL;
    bool success = false;
    int rc;

    translated_sql = sqlite_translate_placeholders(sql);
    if (!translated_sql) {
        err->code = ERR_DB_NOMEM;
        strlcpy(err->message, "sqlite_query_impl: Out of memory for SQL translation", sizeof(err->message));
        goto cleanup;
    }

    rc = sqlite3_prepare_v2(sqlite_impl->db_conn, translated_sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        sqlite_map_error(sqlite_impl->db_conn, rc, err);
        goto cleanup;
    }

    rc = sqlite_bind_params(stmt, params, n_params);
    if (rc != SQLITE_OK) {
        sqlite_map_error(sqlite_impl->db_conn, rc, err);
        goto cleanup;
    }

    // Allocate and populate generic result handle
    db_sqlite_res_impl_t *sqlite_res_impl = (db_sqlite_res_impl_t*)calloc(1, sizeof(db_sqlite_res_impl_t));
    if (!sqlite_res_impl) {
        err->code = ERR_DB_NOMEM;
        strlcpy(err->message, "sqlite_query_impl: Out of memory for SQLite result implementation", sizeof(err->message));
        goto cleanup;
    }
    sqlite_res_impl->stmt = stmt;
    sqlite_res_impl->current_row = -1; // Before first row

    db_res_t *res_handle = (db_res_t*)calloc(1, sizeof(db_res_t));
    if (!res_handle) {
        free(sqlite_res_impl);
        err->code = ERR_DB_NOMEM;
        strlcpy(err->message, "sqlite_query_impl: Out of memory for generic db_res_t handle", sizeof(err->message));
        goto cleanup;
    }
    res_handle->db = db;
    res_handle->impl = sqlite_res_impl;
    res_handle->num_cols = sqlite3_column_count(stmt);
    res_handle->current_row = -1; // Before first row

    *out_res = res_handle;
    success = true;

cleanup:
    if (translated_sql) free(translated_sql);
    // If not successful, and stmt was prepared, it needs to be finalized here.
    // Otherwise, it's owned by db_res_t->impl.
    if (!success && stmt) {
        sqlite3_finalize(stmt);
    }
    return success;
}

static bool sqlite_res_step_impl(db_res_t *res, db_error_t *err) {
    db_sqlite_impl_t *sqlite_impl = (db_sqlite_impl_t*)res->db->impl;
    db_sqlite_res_impl_t *sqlite_res_impl = (db_sqlite_res_impl_t*)res->impl;
    db_error_clear(err);

    int rc = sqlite3_step(sqlite_res_impl->stmt);
    if (rc == SQLITE_ROW) {
        res->current_row++;
        return true;
    } else if (rc == SQLITE_DONE) {
        err->code = 0; // End of rows, not an error
        return false;
    } else {
        sqlite_map_error(sqlite_impl->db_conn, rc, err);
        return false;
    }
}

static int sqlite_res_col_count_impl(const db_res_t *res) {
    if (!res) return -1;
    return res->num_cols;
}

static bool sqlite_res_col_is_null_impl(const db_res_t *res, int col_idx) {
    db_sqlite_res_impl_t *sqlite_res_impl = (db_sqlite_res_impl_t*)res->impl;
    if (!res || !sqlite_res_impl || col_idx < 0 || col_idx >= res->num_cols) return true;
    return sqlite3_column_type(sqlite_res_impl->stmt, col_idx) == SQLITE_NULL;
}

// Helper to safely get column value or map error
static const void* sqlite_get_column_value(const db_res_t *res, int col_idx, db_error_t *err, int expected_type) {
    db_sqlite_res_impl_t *sqlite_res_impl = (db_sqlite_res_impl_t*)res->impl;
    if (!res || !sqlite_res_impl || col_idx < 0 || col_idx >= res->num_cols) {
        err->code = ERR_DB_NOT_FOUND; return NULL;
    }
    if (sqlite_res_col_is_null_impl(res, col_idx)) return NULL;
    
    // Check type if it matters
    if (expected_type != SQLITE_ANY && sqlite3_column_type(sqlite_res_impl->stmt, col_idx) != expected_type) {
        err->code = ERR_DB_TYPE;
        strlcpy(err->message, "SQLite column type mismatch", sizeof(err->message));
        return NULL;
    }
    return sqlite3_column_value(sqlite_res_impl->stmt, col_idx);
}

static int64_t sqlite_res_col_i64_impl(const db_res_t *res, int col_idx, db_error_t *err) {
    db_error_clear(err);
    if (sqlite_res_col_is_null_impl(res, col_idx)) { err->code = ERR_DB_NOT_FOUND; return 0; }
    return sqlite3_column_int64(((db_sqlite_res_impl_t*)res->impl)->stmt, col_idx);
}

static uint64_t sqlite_res_col_u64_impl(const db_res_t *res, int col_idx, db_error_t *err) {
    db_error_clear(err);
    if (sqlite_res_col_is_null_impl(res, col_idx)) { err->code = ERR_DB_NOT_FOUND; return 0; }
    // SQLite stores integers as signed 64-bit. Interpret as unsigned.
    return (uint64_t)sqlite3_column_int64(((db_sqlite_res_impl_t*)res->impl)->stmt, col_idx);
}

static int32_t sqlite_res_col_i32_impl(const db_res_t *res, int col_idx, db_error_t *err) {
    db_error_clear(err);
    if (sqlite_res_col_is_null_impl(res, col_idx)) { err->code = ERR_DB_NOT_FOUND; return 0; }
    return sqlite3_column_int(((db_sqlite_res_impl_t*)res->impl)->stmt, col_idx);
}

static uint32_t sqlite_res_col_u32_impl(const db_res_t *res, int col_idx, db_error_t *err) {
    db_error_clear(err);
    if (sqlite_res_col_is_null_impl(res, col_idx)) { err->code = ERR_DB_NOT_FOUND; return 0; }
    return (uint32_t)sqlite3_column_int(((db_sqlite_res_impl_t*)res->impl)->stmt, col_idx);
}

static bool sqlite_res_col_bool_impl(const db_res_t *res, int col_idx, db_error_t *err) {
    db_error_clear(err);
    if (sqlite_res_col_is_null_impl(res, col_idx)) { err->code = ERR_DB_NOT_FOUND; return false; }
    return sqlite3_column_int(((db_sqlite_res_impl_t*)res->impl)->stmt, col_idx) != 0;
}

static double sqlite_res_col_double_impl(const db_res_t *res, int col_idx, db_error_t *err) {
    db_error_clear(err);
    if (sqlite_res_col_is_null_impl(res, col_idx)) { err->code = ERR_DB_NOT_FOUND; return 0.0; }
    return sqlite3_column_double(((db_sqlite_res_impl_t*)res->impl)->stmt, col_idx);
}

static const char *sqlite_res_col_text_impl(const db_res_t *res, int col_idx, db_error_t *err) {
    db_error_clear(err);
    if (sqlite_res_col_is_null_impl(res, col_idx)) { err->code = ERR_DB_NOT_FOUND; return NULL; }
    // SQLite's text column returns UTF-8 which is compatible with C strings.
    // Pointer is valid until next sqlite3_step or sqlite3_finalize.
    return (const char*)sqlite3_column_text(((db_sqlite_res_impl_t*)res->impl)->stmt, col_idx);
}

static const void *sqlite_res_col_blob_impl(const db_res_t *res, int col_idx, size_t *out_len, db_error_t *err) {
    db_error_clear(err);
    if (sqlite_res_col_is_null_impl(res, col_idx)) { if (out_len) *out_len = 0; return NULL; }
    if (out_len) *out_len = sqlite3_column_bytes(((db_sqlite_res_impl_t*)res->impl)->stmt, col_idx);
    return sqlite3_column_blob(((db_sqlite_res_impl_t*)res->impl)->stmt, col_idx);
}

static void sqlite_res_finalize_impl(db_res_t *res) {
    if (!res) return;
    db_sqlite_res_impl_t *sqlite_res_impl = (db_sqlite_res_impl_t*)res->impl;
    if (sqlite_res_impl) {
        if (sqlite_res_impl->stmt) {
            sqlite3_finalize(sqlite_res_impl->stmt);
        }
        free(sqlite_res_impl);
    }
    free(res); // Free the opaque handle itself
}

// --- SQLite Vtable ---
static const db_vt_t sqlite_vt = {
    .close = sqlite_close_impl,
    .tx_begin = sqlite_tx_begin_impl,
    .tx_commit = sqlite_tx_commit_impl,
    .tx_rollback = sqlite_tx_rollback_impl,
    .exec = sqlite_exec_impl,
    .exec_rows_affected = sqlite_exec_rows_affected_impl,
    .query = sqlite_query_impl,
    .res_step = sqlite_res_step_impl,
    .res_col_count = sqlite_res_col_count_impl,
    .res_col_is_null = sqlite_res_col_is_null_impl,
    .res_col_i64 = sqlite_res_col_i64_impl,
    .res_col_u64 = sqlite_res_col_u64_impl,
    .res_col_i32 = sqlite_res_col_i32_impl,
    .res_col_u32 = sqlite_res_col_u32_impl,
    .res_col_bool = sqlite_res_col_bool_impl,
    .res_col_double = sqlite_res_col_double_impl,
    .res_col_text = sqlite_res_col_text_impl,
    .res_col_blob = sqlite_res_col_blob_impl,
    .res_finalize = sqlite_res_finalize_impl,
};

// --- Operations VTable (defaults to NULL for most) ---
static const db_ops_vtable_t sqlite_ops_vtable = {
    // SQLite doesn't have stored procedures, so most ops will be implemented
    // in the Logic Manager (game_db.c) using generic SQL.
    .player_get_by_id = NULL,
    .bank_transfer = NULL,
};


// --- Internal Open Function (called by db_api.c) ---
void* db_sqlite_open_internal(db_t *parent_db, const db_config_t *cfg, db_error_t *err) {
    db_error_clear(err);

    sqlite3 *db_conn;
    int rc = sqlite3_open_v2(cfg->sqlite_path, &db_conn,
                             SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
                             NULL);
    if (rc != SQLITE_OK) {
        sqlite_map_error(NULL, rc, err); // No db_conn yet for errmsg
        err->code = ERR_DB_CONNECT;
        strlcpy(err->message, sqlite3_errstr(rc), sizeof(err->message));
        // If db_conn is not NULL, a partial connection might have been made,
        // and we can get a more specific message.
        if (db_conn) {
             strlcat(err->message, ": ", sizeof(err->message));
             strlcat(err->message, sqlite3_errmsg(db_conn), sizeof(err->message));
             sqlite3_close(db_conn);
        }
        return NULL;
    }

    // Configure SQLite connection (e.g., WAL mode, foreign keys)
    sqlite3_exec(db_conn, "PRAGMA journal_mode=WAL;", NULL, NULL, NULL);
    sqlite3_exec(db_conn, "PRAGMA foreign_keys=ON;", NULL, NULL, NULL); // Enforce FKs
    sqlite3_busy_timeout(db_conn, 5000); // Set busy timeout to 5 seconds

    db_sqlite_impl_t *sqlite_impl = (db_sqlite_impl_t*)calloc(1, sizeof(db_sqlite_impl_t));
    if (!sqlite_impl) {
        err->code = ERR_DB_NOMEM;
        strlcpy(err->message, "db_sqlite_open_internal: Out of memory for SQLite implementation struct", sizeof(err->message));
        sqlite3_close(db_conn);
        return NULL;
    }
    sqlite_impl->db_conn = db_conn;
    sqlite_impl->in_tx = false;

    // Populate the parent_db's vtable and ops_vt
    parent_db->vt = &sqlite_vt;
    parent_db->ops_vt = &sqlite_ops_vtable; // Mostly NULL for SQLite operations

    return sqlite_impl;
}