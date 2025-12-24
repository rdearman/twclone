#include "db_pg.h"
#include "../db_api.h"
#include "../db_int.h"
#include "../../server_log.h" // Assuming server_log is still accessible

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libpq-fe.h>
#include <arpa/inet.h> // For pid-related functionality if app_name_pid is used
#include <unistd.h>    // For getpid()
#include <errno.h>     // For strtol/strtoull error checking


// Internal PostgreSQL implementation specific data
typedef struct db_pg_impl_s {
  PGconn *conn;
  bool in_tx;
  // Any other PG-specific state
} db_pg_impl_t;

// Internal PostgreSQL result set implementation specific data
typedef struct db_pg_res_impl_s {
    PGresult *pg_res;
    int current_row;
} db_pg_res_impl_t;


// --- Helper for error mapping ---
static void pg_map_error(PGresult *pg_res, PGconn *conn, db_error_t *err) {
    if (!err) return;
    db_error_clear(err);

    if (pg_res) {
        // Error from a PGresult
        err->backend_code = PQresultStatus(pg_res);
        const char *sqlstate = PQresultErrorField(pg_res, PG_DIAG_SQLSTATE);
        const char *message = PQresultErrorMessage(pg_res);
        
        // Populate error message
        size_t msg_len = 0;
        if (sqlstate) {
            msg_len = strlcpy(err->message, sqlstate, sizeof(err->message));
        }
        if (message && msg_len < sizeof(err->message) - 1) {
            if (msg_len > 0) msg_len = strlcat(err->message, " ", sizeof(err->message));
            strlcat(err->message, message, sizeof(err->message));
        }


        // Map SQLSTATE to generic error codes
        if (sqlstate) {
            if (strcmp(sqlstate, "40P01") == 0 || strcmp(sqlstate, "40001") == 0 ||
                strcmp(sqlstate, "55P03") == 0 || strcmp(sqlstate, "57014") == 0) {
                err->code = ERR_DB_BUSY; // Transient/retryable
            } else if (strcmp(sqlstate, "23505") == 0 || strcmp(sqlstate, "23503") == 0 ||
                       strcmp(sqlstate, "23514") == 0) {
                err->code = ERR_DB_CONSTRAINT;
            } else if (strncmp(sqlstate, "28", 2) == 0) {
                err->code = ERR_DB_AUTH;
            } else if (strncmp(sqlstate, "42", 2) == 0) {
                err->code = ERR_DB_SYNTAX;
            } else if (strncmp(sqlstate, "08", 2) == 0) {
                err->code = ERR_DB_CONNECT; // Connection error during command
            } else {
                err->code = ERR_DB_INTERNAL; // Default generic error
            }
        } else {
            err->code = ERR_DB_INTERNAL; // No SQLSTATE, generic failure
        }
    } else if (conn) {
        // Error from connection itself
        err->code = ERR_DB_CONNECT;
        err->backend_code = PQstatus(conn);
        strlcpy(err->message, PQerrorMessage(conn), sizeof(err->message));
    } else {
        err->code = ERR_DB_INTERNAL;
        strlcpy(err->message, "Unknown PostgreSQL error", sizeof(err->message));
    }
}


// --- Helper for placeholder translation ($N to $N for libpq) ---
// Not needed as libpq uses $N natively.

// --- Helper to convert db_bind_t to libpq's format ---
// Dynamically allocates strings for text parameters. BLOBs are handled directly.
// Returns a malloc'd string or NULL for DB_BIND_NULL.
static char* pg_bind_param_to_string_copy(const db_bind_t *param) {
    char *buf = NULL;
    int res;

    switch (param->type) {
        case DB_BIND_NULL:
            return NULL; // libpq expects NULL for value
        case DB_BIND_I64:
            res = asprintf(&buf, "%lld", (long long)param->v.i64);
            break;
        case DB_BIND_U64:
            res = asprintf(&buf, "%llu", (unsigned long long)param->v.u64);
            break;
        case DB_BIND_I32:
            res = asprintf(&buf, "%d", param->v.i32);
            break;
        case DB_BIND_U32:
            res = asprintf(&buf, "%u", param->v.u32);
            break;
        case DB_BIND_BOOL:
            res = asprintf(&buf, "%s", param->v.b ? "TRUE" : "FALSE");
            break;
        case DB_BIND_TEXT:
            if (param->v.text.ptr) {
                size_t len = (param->v.text.len > 0) ? param->v.text.len : strlen(param->v.text.ptr);
                buf = (char*)malloc(len + 1);
                if (buf) {
                    strncpy(buf, param->v.text.ptr, len);
                    buf[len] = '\0';
                    res = 0; // Success for strncpy
                } else {
                    res = -1; // Malloc failed
                }
            } else { // Empty string
                buf = (char*)calloc(1,1);
                res = 0;
            }
            break;
        case DB_BIND_BLOB:
            // BLOBs are handled directly via param_values/param_lengths/param_formats.
            // This function should not be called for BLOBs.
            LOGE("DB_PG: pg_bind_param_to_string_copy called for BLOB type.");
            return NULL;
    }
    if (res == -1) { // asprintf returns -1 on error, or number of chars on success
        LOGE("DB_PG: Failed to allocate string for parameter conversion.");
        if (buf) free(buf);
        return NULL;
    }
    return buf;
}

// -----------------------------------------------------------------------------
// VTable Implementations
// -----------------------------------------------------------------------------

static void pg_close_impl(db_t *db) {
    db_pg_impl_t *pg_impl = (db_pg_impl_t*)db->impl;
    if (pg_impl) {
        if (pg_impl->conn) {
            PQfinish(pg_impl->conn);
        }
        free(pg_impl);
    }
    // db handle itself is freed by db_api.c -> db_close()
}

static bool pg_tx_begin_impl(db_t *db, db_tx_flags_t flags, db_error_t *err) {
    db_pg_impl_t *pg_impl = (db_pg_impl_t*)db->impl;
    PGresult *res;
    
    // For now, flags are mostly ignored for simplicity in BEGIN.
    // A future enhancement could map DB_TX_IMMEDIATE to specific PG commands.
    (void)flags; 

    res = PQexec(pg_impl->conn, "BEGIN");
    if (PQresultStatus(res) != PGRES_COMMAND_OK) {
        pg_map_error(res, pg_impl->conn, err);
        PQclear(res);
        return false;
    }
    PQclear(res);
    pg_impl->in_tx = true;
    return true;
}

static bool pg_tx_commit_impl(db_t *db, db_error_t *err) {
    db_pg_impl_t *pg_impl = (db_pg_impl_t*)db->impl;
    PGresult *res;

    if (!pg_impl->in_tx) {
        err->code = ERR_DB_TX;
        strlcpy(err->message, "pg_tx_commit: Not in a transaction", sizeof(err->message));
        return false;
    }

    res = PQexec(pg_impl->conn, "COMMIT");
    if (PQresultStatus(res) != PGRES_COMMAND_OK) {
        pg_map_error(res, pg_impl->conn, err);
        PQclear(res);
        return false;
    }
    PQclear(res);
    pg_impl->in_tx = false;
    return true;
}

static bool pg_tx_rollback_impl(db_t *db, db_error_t *err) {
    db_pg_impl_t *pg_impl = (db_pg_impl_t*)db->impl;
    PGresult *res;

    if (!pg_impl->in_tx) {
        err->code = ERR_DB_TX;
        strlcpy(err->message, "pg_tx_rollback: Not in a transaction", sizeof(err->message));
        return false;
    }

    res = PQexec(pg_impl->conn, "ROLLBACK");
    if (PQresultStatus(res) != PGRES_COMMAND_OK) {
        pg_map_error(res, pg_impl->conn, err);
        PQclear(res);
        return false;
    }
    PQclear(res);
    pg_impl->in_tx = false;
    return true;
}

static bool pg_exec_internal(db_t *db, const char *sql, const db_bind_t *params, size_t n_params, int64_t *out_rows, db_error_t *err) {
    db_pg_impl_t *pg_impl = (db_pg_impl_t*)db->impl;
    PGresult *res = NULL;
    bool success = false;

    char **param_values = (char**)calloc(n_params, sizeof(char*));
    int *param_lengths = (int*)calloc(n_params, sizeof(int));
    int *param_formats = (int*)calloc(n_params, sizeof(int));

    if (!param_values || !param_lengths || !param_formats) {
        pg_map_error(NULL, pg_impl->conn, err); // OOM error
        err->code = ERR_DB_NOMEM;
        strlcpy(err->message, "pg_exec_internal: Out of memory for parameters", sizeof(err->message));
        goto cleanup;
    }

    for (size_t i = 0; i < n_params; ++i) {
        if (params[i].type == DB_BIND_BLOB) {
            // For binary, pass the pointer and length directly
            param_values[i] = (char*)params[i].v.blob.ptr;
            param_lengths[i] = params[i].v.blob.len;
            param_formats[i] = 1; // Binary format
        } else {
            // For non-binary, convert to string (will be freed in cleanup)
            param_values[i] = pg_bind_param_to_string_copy(&params[i]);
            if (param_values[i]) {
                param_lengths[i] = strlen(param_values[i]);
                param_formats[i] = 0; // Text format
            } else if (params[i].type != DB_BIND_NULL) {
                // Conversion failed for non-NULL param (e.g., asprintf failed)
                pg_map_error(NULL, pg_impl->conn, err);
                err->code = ERR_DB_INTERNAL;
                strlcpy(err->message, "pg_exec_internal: Failed to convert parameter to string", sizeof(err->message));
                goto cleanup;
            }
            // If param_values[i] is NULL for DB_BIND_NULL, libpq handles it by sending NULL
        }
    }

    res = PQexecParams(pg_impl->conn, sql, n_params, NULL, 
                       (const char* const*)param_values, param_lengths, param_formats, 0);
    
    // Check status for EXEC/EXEC_ROWS_AFFECTED which expect COMMAND_OK
    if (PQresultStatus(res) != PGRES_COMMAND_OK) {
        pg_map_error(res, pg_impl->conn, err);
        goto cleanup;
    }

    if (out_rows) {
        *out_rows = atoll(PQcmdTuples(res));
    }
    success = true;

cleanup:
    if (res) PQclear(res);
    for (size_t i = 0; i < n_params; ++i) {
        // Free only strings allocated by pg_bind_param_to_string_copy (non-BLOB)
        // BLOB ptrs are owned by caller.
        if (params[i].type != DB_BIND_BLOB && param_values[i]) {
            free(param_values[i]);
        }
    }
    free(param_values);
    free(param_lengths);
    free(param_formats);
    return success;
}


static bool pg_exec_impl(db_t *db, const char *sql, const db_bind_t *params, size_t n_params, db_error_t *err) {
    return pg_exec_internal(db, sql, params, n_params, NULL, err);
}

static bool pg_exec_rows_affected_impl(db_t *db, const char *sql, const db_bind_t *params, size_t n_params, int64_t *out_rows, db_error_t *err) {
    return pg_exec_internal(db, sql, params, n_params, out_rows, err);
}


static bool pg_query_impl(db_t *db, const char *sql, const db_bind_t *params, size_t n_params, db_res_t **out_res, db_error_t *err) {
    db_pg_impl_t *pg_impl = (db_pg_impl_t*)db->impl;
    PGresult *pg_query_res = NULL; // Renamed to avoid confusion with the 'res' param.
    bool success = false;

    char **param_values = (char**)calloc(n_params, sizeof(char*));
    int *param_lengths = (int*)calloc(n_params, sizeof(int));
    int *param_formats = (int*)calloc(n_params, sizeof(int));

    if (!param_values || !param_lengths || !param_formats) {
        pg_map_error(NULL, pg_impl->conn, err); // OOM error
        err->code = ERR_DB_NOMEM;
        strlcpy(err->message, "pg_query_impl: Out of memory for parameters", sizeof(err->message));
        goto cleanup;
    }

    for (size_t i = 0; i < n_params; ++i) {
        if (params[i].type == DB_BIND_BLOB) {
            param_values[i] = (char*)params[i].v.blob.ptr;
            param_lengths[i] = params[i].v.blob.len;
            param_formats[i] = 1; // Binary format
        } else {
            param_values[i] = pg_bind_param_to_string_copy(&params[i]);
            if (param_values[i]) {
                param_lengths[i] = strlen(param_values[i]);
                param_formats[i] = 0; // Text format
            } else if (params[i].type != DB_BIND_NULL) {
                pg_map_error(NULL, pg_impl->conn, err);
                err->code = ERR_DB_INTERNAL;
                strlcpy(err->message, "pg_query_impl: Failed to convert parameter to string", sizeof(err->message));
                goto cleanup;
            }
        }
    }

    pg_query_res = PQexecParams(pg_impl->conn, sql, n_params, NULL, 
                                (const char* const*)param_values, param_lengths, param_formats, 0);
    
    if (PQresultStatus(pg_query_res) != PGRES_TUPLES_OK) { // Expect tuples for a query
        pg_map_error(pg_query_res, pg_impl->conn, err);
        goto cleanup;
    }

    // Allocate and populate generic result handle
    db_pg_res_impl_t *pg_res_impl = (db_pg_res_impl_t*)calloc(1, sizeof(db_pg_res_impl_t));
    if (!pg_res_impl) {
        pg_map_error(NULL, pg_impl->conn, err); // OOM error
        err->code = ERR_DB_NOMEM;
        strlcpy(err->message, "pg_query_impl: Out of memory for PG result implementation", sizeof(err->message));
        goto cleanup;
    }
    pg_res_impl->pg_res = pg_query_res;
    pg_res_impl->current_row = -1; // Before first row

    db_res_t *res_handle = (db_res_t*)calloc(1, sizeof(db_res_t));
    if (!res_handle) {
        free(pg_res_impl);
        pg_map_error(NULL, pg_impl->conn, err); // OOM error
        err->code = ERR_DB_NOMEM;
        strlcpy(err->message, "pg_query_impl: Out of memory for generic db_res_t handle", sizeof(err->message));
        goto cleanup;
    }
    res_handle->db = db;
    res_handle->impl = pg_res_impl;
    res_handle->num_rows = PQntuples(pg_query_res);
    res_handle->num_cols = PQnfields(pg_query_res);
    res_handle->current_row = -1; // -1 indicates before first row

    *out_res = res_handle;
    success = true;

cleanup:
    // If not successful, and pg_query_res was allocated, it needs to be cleared here.
    // Otherwise, it's owned by db_res_t->impl.
    if (!success && pg_query_res) {
        PQclear(pg_query_res);
    }
    for (size_t i = 0; i < n_params; ++i) {
        if (params[i].type != DB_BIND_BLOB && param_values[i]) { // Free only strings allocated for non-BLOBs
            free(param_values[i]);
        }
    }
    free(param_values);
    free(param_lengths);
    free(param_formats);
    return success;
}

static bool pg_res_step_impl(db_res_t *res, db_error_t *err) {
    db_pg_res_impl_t *pg_res_impl = (db_pg_res_impl_t*)res->impl;
    db_error_clear(err); // Clear error state for this step

    res->current_row++; // Advance generic current row
    pg_res_impl->current_row++; // Advance backend current row

    if (pg_res_impl->current_row < res->num_rows) {
        return true; // Row available
    }
    
    // No more rows
    err->code = 0; 
    return false;
}

static int pg_res_col_count_impl(const db_res_t *res) {
    if (!res) return -1;
    return res->num_cols;
}

static bool pg_res_col_is_null_impl(const db_res_t *res, int col_idx) {
    db_pg_res_impl_t *pg_res_impl = (db_pg_res_impl_t*)res->impl;
    if (!res || !pg_res_impl || col_idx < 0 || col_idx >= res->num_cols || res->current_row < 0 || res->current_row >= res->num_rows) {
        return true; // Invalid state implies NULL
    }
    return PQgetisnull(pg_res_impl->pg_res, res->current_row, col_idx);
}

// Helper to safely get value as string, or NULL if invalid/null
static const char* get_pg_value_as_str(const db_res_t *res, int col_idx) {
    db_pg_res_impl_t *pg_res_impl = (db_pg_res_impl_t*)res->impl;
    if (!res || !pg_res_impl || col_idx < 0 || col_idx >= res->num_cols || res->current_row < 0 || res->current_row >= res->num_rows) {
        return NULL;
    }
    if (PQgetisnull(pg_res_impl->pg_res, res->current_row, col_idx)) {
        return NULL;
    }
    return PQgetvalue(pg_res_impl->pg_res, res->current_row, col_idx);
}

static int64_t pg_res_col_i64_impl(const db_res_t *res, int col_idx, db_error_t *err) {
    db_error_clear(err);
    const char *val = get_pg_value_as_str(res, col_idx);
    if (!val) { err->code = ERR_NOT_FOUND; return 0; }
    char *endptr;
    long long parsed_val = strtoll(val, &endptr, 10);
    if (*endptr != '\0') {
        err->code = ERR_DB_TYPE; strlcpy(err->message, "pg_res_col_i64: Type conversion failed", sizeof(err->message)); return 0;
    }
    return parsed_val;
}

static uint64_t pg_res_col_u64_impl(const db_res_t *res, int col_idx, db_error_t *err) {
    db_error_clear(err);
    const char *val = get_pg_value_as_str(res, col_idx);
    if (!val) { err->code = ERR_NOT_FOUND; return 0; }
    char *endptr;
    unsigned long long parsed_val = strtoull(val, &endptr, 10);
    if (*endptr != '\0') {
        err->code = ERR_DB_TYPE; strlcpy(err->message, "pg_res_col_u64: Type conversion failed", sizeof(err->message)); return 0;
    }
    return parsed_val;
}

static int32_t pg_res_col_i32_impl(const db_res_t *res, int col_idx, db_error_t *err) {
    db_error_clear(err);
    return (int32_t)pg_res_col_i64_impl(res, col_idx, err); // Rely on i64 conversion
}

static uint32_t pg_res_col_u32_impl(const db_res_t *res, int col_idx, db_error_t *err) {
    db_error_clear(err);
    return (uint32_t)pg_res_col_u64_impl(res, col_idx, err); // Rely on u64 conversion
}

static bool pg_res_col_bool_impl(const db_res_t *res, int col_idx, db_error_t *err) {
    db_error_clear(err);
    const char *val = get_pg_value_as_str(res, col_idx);
    if (!val) { err->code = ERR_NOT_FOUND; return false; }
    // libpq returns "t" or "f" for bools by default in text format
    if (strcmp(val, "t") == 0 || strcmp(val, "1") == 0 || strcmp(val, "true") == 0) return true;
    if (strcmp(val, "f") == 0 || strcmp(val, "0") == 0 || strcmp(val, "false") == 0) return false;
    err->code = ERR_DB_TYPE; strlcpy(err->message, "pg_res_col_bool: Type conversion failed", sizeof(err->message)); return false;
}

static double pg_res_col_double_impl(const db_res_t *res, int col_idx, db_error_t *err) {
    db_error_clear(err);
    const char *val = get_pg_value_as_str(res, col_idx);
    if (!val) { err->code = ERR_NOT_FOUND; return 0.0; }
    char *endptr;
    double parsed_val = strtod(val, &endptr);
    if (*endptr != '\0') {
        err->code = ERR_DB_TYPE; strlcpy(err->message, "pg_res_col_double: Type conversion failed", sizeof(err->message)); return 0.0;
    }
    return parsed_val;
}

static const char *pg_res_col_text_impl(const db_res_t *res, int col_idx, db_error_t *err) {
    db_error_clear(err);
    const char *val = get_pg_value_as_str(res, col_idx);
    if (!val) { err->code = ERR_NOT_FOUND; return NULL; }
    return val; // Pointer is valid until next step or finalize
}

static const void *pg_res_col_blob_impl(const db_res_t *res, int col_idx, size_t *out_len, db_error_t *err) {
    db_pg_res_impl_t *pg_res_impl = (db_pg_res_impl_t*)res->impl;
    db_error_clear(err);
    if (col_idx < 0 || col_idx >= res->num_cols || res->current_row < 0 || res->current_row >= res->num_rows) {
        err->code = ERR_NOT_FOUND; if (out_len) *out_len = 0; return NULL;
    }
    if (pg_res_col_is_null_impl(res, col_idx)) { if (out_len) *out_len = 0; return NULL; }

    // libpq returns binary data directly for binary format columns.
    // PQgetlength gives the actual data length, not including null terminator.
    if (out_len) *out_len = PQgetlength(pg_res_impl->pg_res, res->current_row, col_idx);
    return PQgetvalue(pg_res_impl->pg_res, res->current_row, col_idx);
}

static void pg_res_finalize_impl(db_res_t *res) {
    if (!res) return;
    db_pg_res_impl_t *pg_res_impl = (db_pg_res_impl_t*)res->impl;
    if (pg_res_impl) {
        if (pg_res_impl->pg_res) {
            PQclear(pg_res_impl->pg_res);
        }
        free(pg_res_impl);
    }
    free(res); // Free the opaque handle itself
}


// --- PostgreSQL Vtable ---
static const db_vt_t pg_vt = {
    .close = pg_close_impl,
    .tx_begin = pg_tx_begin_impl,
    .tx_commit = pg_tx_commit_impl,
    .tx_rollback = pg_tx_rollback_impl,
    .exec = pg_exec_impl,
    .exec_rows_affected = pg_exec_rows_affected_impl,
    .query = pg_query_impl,
    .res_step = pg_res_step_impl,
    .res_col_count = pg_res_col_count_impl,
    .res_col_is_null = pg_res_col_is_null_impl,
    .res_col_i64 = pg_res_col_i64_impl,
    .res_col_u64 = pg_res_col_u64_impl,
    .res_col_i32 = pg_res_col_i32_impl,
    .res_col_u32 = pg_res_col_u32_impl,
    .res_col_bool = pg_res_col_bool_impl,
    .res_col_double = pg_res_col_double_impl,
    .res_col_text = pg_res_col_text_impl,
    .res_col_blob = pg_res_col_blob_impl,
    .res_finalize = pg_res_finalize_impl,
};

// --- Operations VTable (example, will be expanded) ---
static const db_ops_vtable_t pg_ops_vtable = {
    // Example entries, set to NULL if not implemented
    .player_get_by_id = NULL,
    // .bank_transfer = NULL, // Removed as per db_api.h definition
};


// --- Internal Open Function (called by db_api.c) ---
void* db_pg_open_internal(db_t *parent_db, const db_config_t *cfg, db_error_t *err) {
    db_error_clear(err);

    PGconn *conn = PQconnectdb(cfg->pg_conninfo);
    
    if (PQstatus(conn) != CONNECTION_OK) {
        pg_map_error(NULL, conn, err);
        PQfinish(conn);
        return NULL;
    }

    // Set session parameters (e.g., application_name, timeouts)
    char set_cmd[256];
    // Use strlcpy and snprintf for safer string handling
    
    // Application Name
    if (cfg->app_name_pid) {
        snprintf(set_cmd, sizeof(set_cmd), "SET application_name = 'twclone-server-%d'", (int)getpid());
        PQexec(conn, set_cmd);
    } else {
        PQexec(conn, "SET application_name = 'twclone-server'");
    }

    // Statement Timeout
    if (cfg->statement_timeout_ms > 0) {
        snprintf(set_cmd, sizeof(set_cmd), "SET statement_timeout = %d", cfg->statement_timeout_ms);
        PQexec(conn, set_cmd);
    }
    // Lock Timeout
    if (cfg->lock_timeout_ms > 0) {
        snprintf(set_cmd, sizeof(set_cmd), "SET lock_timeout = %d", cfg->lock_timeout_ms);
        PQexec(conn, set_cmd);
    }

    db_pg_impl_t *pg_impl = (db_pg_impl_t*)calloc(1, sizeof(db_pg_impl_t));
    if (!pg_impl) {
        pg_map_error(NULL, conn, err);
        err->code = ERR_DB_NOMEM;
        strlcpy(err->message, "db_pg_open_internal: Out of memory for PG implementation struct", sizeof(err->message));
        PQfinish(conn);
        return NULL;
    }
    pg_impl->conn = conn;
    pg_impl->in_tx = false;

    // Populate the parent_db's vtable and ops_vt
    parent_db->vt = &pg_vt;
    parent_db->ops_vt = &pg_ops_vtable;

    return pg_impl;
}