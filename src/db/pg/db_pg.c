#include "db_pg.h"
#include "../db_api.h"
#include "../db_int.h"
#include "../../server_log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libpq-fe.h>

typedef struct db_pg_impl_s {
  PGconn *conn;
  bool in_tx;
} db_pg_impl_t;

typedef struct db_pg_res_impl_s {
    PGresult *pg_res;
} db_pg_res_impl_t;

static void pg_map_error(PGresult *pg_res, PGconn *conn, db_error_t *err) {
    if (!err) return;
    db_error_clear(err);
    if (pg_res) {
        err->backend_code = PQresultStatus(pg_res);
        const char *msg = PQresultErrorMessage(pg_res);
        if (msg) strlcpy(err->message, msg, sizeof(err->message));
        err->code = ERR_DB_INTERNAL; 
    } else if (conn) {
        err->code = ERR_DB_CONNECT;
        strlcpy(err->message, PQerrorMessage(conn), sizeof(err->message));
    }
}

static char* pg_bind_param_to_string(const db_bind_t *param) {
    char *buf = NULL;
    switch (param->type) {
        case DB_BIND_NULL: return NULL;
        case DB_BIND_I64: asprintf(&buf, "%lld", (long long)param->v.i64); break;
        case DB_BIND_I32: asprintf(&buf, "%d", param->v.i32); break;
        case DB_BIND_BOOL: asprintf(&buf, "%s", param->v.b ? "t" : "f"); break;
        case DB_BIND_TEXT: return strdup(param->v.text.ptr);
        default: return NULL;
    }
    return buf;
}

static void pg_close_impl(db_t *db) {
    db_pg_impl_t *impl = (db_pg_impl_t*)db->impl;
    if (impl) { if (impl->conn) PQfinish(impl->conn); free(impl); }
}

static bool pg_tx_begin_impl(db_t *db, db_tx_flags_t flags, db_error_t *err) {
    db_pg_impl_t *impl = (db_pg_impl_t*)db->impl;
    (void)flags;
    PGresult *res = PQexec(impl->conn, "BEGIN");
    if (PQresultStatus(res) != PGRES_COMMAND_OK) { pg_map_error(res, impl->conn, err); PQclear(res); return false; }
    PQclear(res); impl->in_tx = true; return true;
}

static bool pg_tx_commit_impl(db_t *db, db_error_t *err) {
    db_pg_impl_t *impl = (db_pg_impl_t*)db->impl;
    PGresult *res = PQexec(impl->conn, "COMMIT");
    if (PQresultStatus(res) != PGRES_COMMAND_OK) { pg_map_error(res, impl->conn, err); PQclear(res); return false; }
    PQclear(res); impl->in_tx = false; return true;
}

static bool pg_tx_rollback_impl(db_t *db, db_error_t *err) {
    db_pg_impl_t *impl = (db_pg_impl_t*)db->impl;
    PGresult *res = PQexec(impl->conn, "ROLLBACK");
    if (PQresultStatus(res) != PGRES_COMMAND_OK) { pg_map_error(res, impl->conn, err); PQclear(res); return false; }
    PQclear(res); impl->in_tx = false; return true;
}

static bool pg_exec_internal(db_t *db, const char *sql, const db_bind_t *params, size_t n_params, int64_t *out_rows, db_error_t *err) {
    db_pg_impl_t *impl = (db_pg_impl_t*)db->impl;
    char **values = calloc(n_params, sizeof(char*));
    for (size_t i = 0; i < n_params; i++) values[i] = pg_bind_param_to_string(&params[i]);
    PGresult *res = PQexecParams(impl->conn, sql, n_params, NULL, (const char* const*)values, NULL, NULL, 0);
    for (size_t i = 0; i < n_params; i++) free(values[i]); free(values);
    if (PQresultStatus(res) != PGRES_COMMAND_OK && PQresultStatus(res) != PGRES_TUPLES_OK) { pg_map_error(res, impl->conn, err); PQclear(res); return false; }
    if (out_rows) *out_rows = atoll(PQcmdTuples(res));
    PQclear(res); return true;
}

static bool pg_exec_impl(db_t *db, const char *sql, const db_bind_t *params, size_t n_params, db_error_t *err) {
    return pg_exec_internal(db, sql, params, n_params, NULL, err);
}

static bool pg_exec_rows_affected_impl(db_t *db, const char *sql, const db_bind_t *params, size_t n_params, int64_t *out_rows, db_error_t *err) {
    return pg_exec_internal(db, sql, params, n_params, out_rows, err);
}

static bool pg_exec_insert_id_impl(db_t *db, const char *sql, const db_bind_t *params, size_t n_params, int64_t *out_id, db_error_t *err) {
    db_pg_impl_t *impl = (db_pg_impl_t*)db->impl;
    char sql2[2048]; snprintf(sql2, sizeof(sql2), "%s RETURNING id", sql);
    char **values = calloc(n_params, sizeof(char*));
    for (size_t i = 0; i < n_params; i++) values[i] = pg_bind_param_to_string(&params[i]);
    PGresult *res = PQexecParams(impl->conn, sql2, n_params, NULL, (const char* const*)values, NULL, NULL, 0);
    for (size_t i = 0; i < n_params; i++) free(values[i]); free(values);
    if (PQresultStatus(res) != PGRES_TUPLES_OK) { pg_map_error(res, impl->conn, err); PQclear(res); return false; }
    if (out_id) *out_id = atoll(PQgetvalue(res, 0, 0));
    PQclear(res); return true;
}

static bool pg_query_impl(db_t *db, const char *sql, const db_bind_t *params, size_t n_params, db_res_t **out_res, db_error_t *err) {
    db_pg_impl_t *impl = (db_pg_impl_t*)db->impl;
    char **values = calloc(n_params, sizeof(char*));
    for (size_t i = 0; i < n_params; i++) values[i] = pg_bind_param_to_string(&params[i]);
    PGresult *pg_res = PQexecParams(impl->conn, sql, n_params, NULL, (const char* const*)values, NULL, NULL, 0);
    for (size_t i = 0; i < n_params; i++) free(values[i]); free(values);
    if (PQresultStatus(pg_res) != PGRES_TUPLES_OK) { pg_map_error(pg_res, impl->conn, err); PQclear(pg_res); return false; }
    db_pg_res_impl_t *res_impl = calloc(1, sizeof(db_pg_res_impl_t));
    res_impl->pg_res = pg_res;
    db_res_t *res = calloc(1, sizeof(db_res_t));
    res->db = db; res->impl = res_impl; res->num_rows = PQntuples(pg_res); res->num_cols = PQnfields(pg_res); res->current_row = -1;
    *out_res = res; return true;
}

static bool pg_res_step_impl(db_res_t *res, db_error_t *err) {
    (void)err; res->current_row++; return (res->current_row < res->num_rows);
}

static void pg_res_finalize_impl(db_res_t *res) {
    if (!res) return;
    db_pg_res_impl_t *impl = (db_pg_res_impl_t*)res->impl;
    if (impl) { PQclear(impl->pg_res); free(impl); }
    free(res);
}

static void pg_res_cancel_impl(db_res_t *res) { (void)res; }

static int pg_res_col_count_impl(const db_res_t *res) { return res->num_cols; }

static const char* pg_res_col_name_impl(const db_res_t *res, int col_idx) {
    return PQfname(((db_pg_res_impl_t*)res->impl)->pg_res, col_idx);
}

static db_col_type_t pg_res_col_type_impl(const db_res_t *res, int col_idx) {
    Oid type = PQftype(((db_pg_res_impl_t*)res->impl)->pg_res, col_idx);
    switch (type) {
        case 20: case 23: return DB_TYPE_INTEGER;
        case 700: case 701: return DB_TYPE_FLOAT;
        case 25: case 1043: return DB_TYPE_TEXT;
        default: return DB_TYPE_UNKNOWN;
    }
}

static bool pg_res_col_is_null_impl(const db_res_t *res, int col_idx) {
    return PQgetisnull(((db_pg_res_impl_t*)res->impl)->pg_res, res->current_row, col_idx);
}

static int64_t pg_res_col_i64_impl(const db_res_t *res, int col_idx, db_error_t *err) {
    (void)err; return atoll(PQgetvalue(((db_pg_res_impl_t*)res->impl)->pg_res, res->current_row, col_idx));
}
static int32_t pg_res_col_i32_impl(const db_res_t *res, int col_idx, db_error_t *err) {
    (void)err; return atoi(PQgetvalue(((db_pg_res_impl_t*)res->impl)->pg_res, res->current_row, col_idx));
}
static double pg_res_col_double_impl(const db_res_t *res, int col_idx, db_error_t *err) {
    (void)err; return atof(PQgetvalue(((db_pg_res_impl_t*)res->impl)->pg_res, res->current_row, col_idx));
}
static const char* pg_res_col_text_impl(const db_res_t *res, int col_idx, db_error_t *err) {
    (void)err; return PQgetvalue(((db_pg_res_impl_t*)res->impl)->pg_res, res->current_row, col_idx);
}

static const db_vt_t pg_vt = {
    .close = pg_close_impl,
    .tx_begin = pg_tx_begin_impl, .tx_commit = pg_tx_commit_impl, .tx_rollback = pg_tx_rollback_impl,
    .exec = pg_exec_impl, .exec_rows_affected = pg_exec_rows_affected_impl, .exec_insert_id = pg_exec_insert_id_impl,
    .query = pg_query_impl,
    .res_step = pg_res_step_impl, .res_finalize = pg_res_finalize_impl, .res_cancel = pg_res_cancel_impl,
    .res_col_count = pg_res_col_count_impl, .res_col_name = pg_res_col_name_impl, .res_col_type = pg_res_col_type_impl,
    .res_col_is_null = pg_res_col_is_null_impl,
    .res_col_i64 = pg_res_col_i64_impl, .res_col_i32 = pg_res_col_i32_impl,
    .res_col_double = pg_res_col_double_impl, .res_col_text = pg_res_col_text_impl,
};

void* db_pg_open_internal(db_t *parent_db, const db_config_t *cfg, db_error_t *err) {
    PGconn *conn = PQconnectdb(cfg->pg_conninfo);
    if (PQstatus(conn) != CONNECTION_OK) { pg_map_error(NULL, conn, err); return NULL; }
    db_pg_impl_t *impl = calloc(1, sizeof(db_pg_impl_t));
    impl->conn = conn;
    parent_db->vt = &pg_vt;
    return impl;
}