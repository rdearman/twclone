#include "db_sqlite.h"
#include "../db_api.h"
#include "../db_int.h"
#include "../../server_log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sqlite3.h>

typedef struct db_sqlite_impl_s {
  sqlite3 *db_conn;
  bool in_tx;
} db_sqlite_impl_t;

typedef struct db_sqlite_res_impl_s {
    sqlite3_stmt *stmt;
} db_sqlite_res_impl_t;

static void sqlite_map_error(sqlite3 *db_conn, int sqlite_rc, db_error_t *err) {
    if (!err) return;
    db_error_clear(err);
    err->backend_code = sqlite_rc;
    const char *sqlite_msg = sqlite3_errmsg(db_conn);
    if (sqlite_msg) strlcpy(err->message, sqlite_msg, sizeof(err->message));
    else strlcpy(err->message, "Unknown SQLite error", sizeof(err->message));

    switch (sqlite_rc) {
        case SQLITE_OK: err->code = 0; break;
        case SQLITE_BUSY:
        case SQLITE_LOCKED: err->code = ERR_DB_BUSY; break;
        case SQLITE_CONSTRAINT: err->code = ERR_DB_CONSTRAINT; break;
        case SQLITE_NOMEM: err->code = ERR_DB_NOMEM; break;
        case SQLITE_MISUSE: err->code = ERR_DB_PROTOCOL; break;
        default: err->code = ERR_DB_INTERNAL; break;
    }
}

static char* sqlite_translate_placeholders(const char* sql) {
    size_t sql_len = strlen(sql);
    char *new_sql = (char*)malloc(sql_len * 2 + 1);
    if (!new_sql) return NULL;
    const char *p_src = sql;
    char *p_dest = new_sql;
    while (*p_src != '\0') {
        if (*p_src == '$' && isdigit((unsigned char)p_src[1])) {
            *p_dest++ = '?';
            p_src++;
            while (isdigit((unsigned char)*p_src)) *p_dest++ = *p_src++;
        } else {
            *p_dest++ = *p_src++;
        }
    }
    *p_dest = '\0';
    return new_sql;
}

static int sqlite_bind_all(sqlite3_stmt *stmt, const db_bind_t *params, size_t n_params, db_error_t *err) {
    for (size_t i = 0; i < n_params; ++i) {
        int rc;
        int bind_idx = i + 1;
        switch (params[i].type) {
            case DB_BIND_NULL: rc = sqlite3_bind_null(stmt, bind_idx); break;
            case DB_BIND_I64: rc = sqlite3_bind_int64(stmt, bind_idx, params[i].v.i64); break;
            case DB_BIND_I32: rc = sqlite3_bind_int(stmt, bind_idx, params[i].v.i32); break;
            case DB_BIND_BOOL: rc = sqlite3_bind_int(stmt, bind_idx, params[i].v.b ? 1 : 0); break;
            case DB_BIND_TEXT: rc = sqlite3_bind_text(stmt, bind_idx, params[i].v.text.ptr, -1, SQLITE_TRANSIENT); break;
            case DB_BIND_BLOB: rc = sqlite3_bind_blob(stmt, bind_idx, params[i].v.blob.ptr, params[i].v.blob.len, SQLITE_TRANSIENT); break;
            default: return SQLITE_ERROR;
        }
        if (rc != SQLITE_OK) {
            if (err) sqlite_map_error(sqlite3_db_handle(stmt), rc, err);
            return rc;
        }
    }
    return SQLITE_OK;
}

static void sqlite_close_impl(db_t *db) {
    db_sqlite_impl_t *impl = (db_sqlite_impl_t*)db->impl;
    if (impl) {
        if (impl->db_conn) sqlite3_close(impl->db_conn);
        free(impl);
    }
}

static bool sqlite_tx_begin_impl(db_t *db, db_tx_flags_t flags, db_error_t *err) {
    db_sqlite_impl_t *impl = (db_sqlite_impl_t*)db->impl;
    const char *sql = (flags & DB_TX_IMMEDIATE) ? "BEGIN IMMEDIATE;" : "BEGIN;";
    int rc = sqlite3_exec(impl->db_conn, sql, NULL, NULL, NULL);
    if (rc != SQLITE_OK) { sqlite_map_error(impl->db_conn, rc, err); return false; }
    impl->in_tx = true; return true;
}

static bool sqlite_tx_commit_impl(db_t *db, db_error_t *err) {
    db_sqlite_impl_t *impl = (db_sqlite_impl_t*)db->impl;
    int rc = sqlite3_exec(impl->db_conn, "COMMIT;", NULL, NULL, NULL);
    if (rc != SQLITE_OK) { sqlite_map_error(impl->db_conn, rc, err); return false; }
    impl->in_tx = false; return true;
}

static bool sqlite_tx_rollback_impl(db_t *db, db_error_t *err) {
    db_sqlite_impl_t *impl = (db_sqlite_impl_t*)db->impl;
    int rc = sqlite3_exec(impl->db_conn, "ROLLBACK;", NULL, NULL, NULL);
    if (rc != SQLITE_OK) { sqlite_map_error(impl->db_conn, rc, err); return false; }
    impl->in_tx = false; return true;
}

static bool sqlite_exec_internal(db_t *db, const char *sql, const db_bind_t *params, size_t n_params, int64_t *out_rows, db_error_t *err) {
    db_sqlite_impl_t *impl = (db_sqlite_impl_t*)db->impl;
    sqlite3_stmt *stmt = NULL;
    char *tsql = sqlite_translate_placeholders(sql);
    if (!tsql) { err->code = ERR_DB_NOMEM; return false; }
    int rc = sqlite3_prepare_v2(impl->db_conn, tsql, -1, &stmt, NULL);
    free(tsql);
    if (rc != SQLITE_OK) { sqlite_map_error(impl->db_conn, rc, err); return false; }
    if (sqlite_bind_all(stmt, params, n_params, err) != SQLITE_OK) { sqlite3_finalize(stmt); return false; }
    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) { sqlite_map_error(impl->db_conn, rc, err); sqlite3_finalize(stmt); return false; }
    if (out_rows) *out_rows = sqlite3_changes(impl->db_conn);
    sqlite3_finalize(stmt); return true;
}

static bool sqlite_exec_impl(db_t *db, const char *sql, const db_bind_t *params, size_t n_params, db_error_t *err) {
    return sqlite_exec_internal(db, sql, params, n_params, NULL, err);
}

static bool sqlite_exec_rows_affected_impl(db_t *db, const char *sql, const db_bind_t *params, size_t n_params, int64_t *out_rows, db_error_t *err) {
    return sqlite_exec_internal(db, sql, params, n_params, out_rows, err);
}

static bool sqlite_exec_insert_id_impl(db_t *db, const char *sql, const db_bind_t *params, size_t n_params, int64_t *out_id, db_error_t *err) {
    db_sqlite_impl_t *impl = (db_sqlite_impl_t*)db->impl;
    if (!sqlite_exec_internal(db, sql, params, n_params, NULL, err)) return false;
    if (out_id) *out_id = sqlite3_last_insert_rowid(impl->db_conn);
    return true;
}

static bool sqlite_query_impl(db_t *db, const char *sql, const db_bind_t *params, size_t n_params, db_res_t **out_res, db_error_t *err) {
    db_sqlite_impl_t *impl = (db_sqlite_impl_t*)db->impl;
    sqlite3_stmt *stmt = NULL;
    char *tsql = sqlite_translate_placeholders(sql);
    if (!tsql) { err->code = ERR_DB_NOMEM; return false; }
    int rc = sqlite3_prepare_v2(impl->db_conn, tsql, -1, &stmt, NULL);
    free(tsql);
    if (rc != SQLITE_OK) { sqlite_map_error(impl->db_conn, rc, err); return false; }
    if (sqlite_bind_all(stmt, params, n_params, err) != SQLITE_OK) { sqlite3_finalize(stmt); return false; }
    db_sqlite_res_impl_t *res_impl = calloc(1, sizeof(db_sqlite_res_impl_t));
    res_impl->stmt = stmt;
    db_res_t *res = calloc(1, sizeof(db_res_t));
    res->db = db; res->impl = res_impl; res->num_cols = sqlite3_column_count(stmt); res->current_row = -1;
    *out_res = res; return true;
}

static bool sqlite_res_step_impl(db_res_t *res, db_error_t *err) {
    db_sqlite_res_impl_t *impl = (db_sqlite_res_impl_t*)res->impl;
    int rc = sqlite3_step(impl->stmt);
    if (rc == SQLITE_ROW) { res->current_row++; return true; }
    if (rc == SQLITE_DONE) { db_error_clear(err); return false; }
    sqlite_map_error(sqlite3_db_handle(impl->stmt), rc, err); return false;
}

static void sqlite_res_finalize_impl(db_res_t *res) {
    if (!res) return;
    db_sqlite_res_impl_t *impl = (db_sqlite_res_impl_t*)res->impl;
    if (impl) { sqlite3_finalize(impl->stmt); free(impl); }
    free(res);
}

static void sqlite_res_cancel_impl(db_res_t *res) {
    db_sqlite_res_impl_t *impl = (db_sqlite_res_impl_t*)res->impl;
    if (impl) sqlite3_reset(impl->stmt);
}

static int sqlite_res_col_count_impl(const db_res_t *res) { return res->num_cols; }

static const char* sqlite_res_col_name_impl(const db_res_t *res, int col_idx) {
    return sqlite3_column_name(((db_sqlite_res_impl_t*)res->impl)->stmt, col_idx);
}

static db_col_type_t sqlite_res_col_type_impl(const db_res_t *res, int col_idx) {
    int st = sqlite3_column_type(((db_sqlite_res_impl_t*)res->impl)->stmt, col_idx);
    switch (st) {
        case SQLITE_INTEGER: return DB_TYPE_INTEGER;
        case SQLITE_FLOAT:   return DB_TYPE_FLOAT;
        case SQLITE_TEXT:    return DB_TYPE_TEXT;
        case SQLITE_BLOB:    return DB_TYPE_BLOB;
        case SQLITE_NULL:    return DB_TYPE_NULL;
        default:             return DB_TYPE_UNKNOWN;
    }
}

static bool sqlite_res_col_is_null_impl(const db_res_t *res, int col_idx) {
    return sqlite3_column_type(((db_sqlite_res_impl_t*)res->impl)->stmt, col_idx) == SQLITE_NULL;
}

static int64_t sqlite_res_col_i64_impl(const db_res_t *res, int col_idx, db_error_t *err) {
    (void)err; return sqlite3_column_int64(((db_sqlite_res_impl_t*)res->impl)->stmt, col_idx);
}
static int32_t sqlite_res_col_i32_impl(const db_res_t *res, int col_idx, db_error_t *err) {
    (void)err; return sqlite3_column_int(((db_sqlite_res_impl_t*)res->impl)->stmt, col_idx);
}
static double sqlite_res_col_double_impl(const db_res_t *res, int col_idx, db_error_t *err) {
    (void)err; return sqlite3_column_double(((db_sqlite_res_impl_t*)res->impl)->stmt, col_idx);
}
static const char* sqlite_res_col_text_impl(const db_res_t *res, int col_idx, db_error_t *err) {
    (void)err; return (const char*)sqlite3_column_text(((db_sqlite_res_impl_t*)res->impl)->stmt, col_idx);
}

static const db_vt_t sqlite_vt = {
    .close = sqlite_close_impl,
    .tx_begin = sqlite_tx_begin_impl, .tx_commit = sqlite_tx_commit_impl, .tx_rollback = sqlite_tx_rollback_impl,
    .exec = sqlite_exec_impl, .exec_rows_affected = sqlite_exec_rows_affected_impl, .exec_insert_id = sqlite_exec_insert_id_impl,
    .query = sqlite_query_impl,
    .res_step = sqlite_res_step_impl, .res_finalize = sqlite_res_finalize_impl, .res_cancel = sqlite_res_cancel_impl,
    .res_col_count = sqlite_res_col_count_impl, .res_col_name = sqlite_res_col_name_impl, .res_col_type = sqlite_res_col_type_impl,
    .res_col_is_null = sqlite_res_col_is_null_impl,
    .res_col_i64 = sqlite_res_col_i64_impl, .res_col_i32 = sqlite_res_col_i32_impl,
    .res_col_double = sqlite_res_col_double_impl, .res_col_text = sqlite_res_col_text_impl,
};

void* db_sqlite_open_internal(db_t *parent_db, const db_config_t *cfg, db_error_t *err) {
    sqlite3 *db_conn;
    int rc = sqlite3_open_v2(cfg->sqlite_path, &db_conn, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, NULL);
    if (rc != SQLITE_OK) { sqlite_map_error(NULL, rc, err); return NULL; }
    db_sqlite_impl_t *impl = calloc(1, sizeof(db_sqlite_impl_t));
    impl->db_conn = db_conn;
    parent_db->vt = &sqlite_vt;
    return impl;
}