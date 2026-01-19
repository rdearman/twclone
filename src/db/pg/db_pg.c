#define TW_DB_INTERNAL 1
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libpq-fe.h>
#include <pthread.h>
#include <time.h>

// local includes
#include "db_pg.h"
#include "../db_api.h"
#include "../db_int.h"
#include "../../server_log.h"
#include "../../errors.h"

// GOAL C: Temporary serialization of DB calls with a mutex
static pthread_mutex_t g_pg_mutex = PTHREAD_MUTEX_INITIALIZER;

typedef struct db_pg_impl_s {
  PGconn *conn;
  bool in_tx;
} db_pg_impl_t;

typedef struct db_pg_res_impl_s {
    PGresult *pg_res;
} db_pg_res_impl_t;


// GOAL D: Improve error mapping
static void pg_map_error(PGconn *conn, PGresult *pg_res, db_error_t *err) {
    if (!err) return;
    db_error_clear(err);

    if (pg_res) {
        err->backend_code = PQresultStatus(pg_res);
        const char* sqlstate = PQresultErrorField(pg_res, PG_DIAG_SQLSTATE);
        if (sqlstate) {
            // Map PostgreSQL SQLSTATE to application error codes and categories
            if (strcmp(sqlstate, "23505") == 0 || strcmp(sqlstate, "23514") == 0) {
                // 23505 = unique_violation, 23514 = check_violation
                err->code = ERR_DB_CONSTRAINT;
                err->category = DB_ERR_CAT_CONSTRAINT;
            } else if (strcmp(sqlstate, "23503") == 0) {
                // 23503 = foreign_key_violation
                err->code = ERR_DB_CONSTRAINT;
                err->category = DB_ERR_CAT_FK;
            } else if (strcmp(sqlstate, "23502") == 0) {
                // 23502 = not_null_violation
                err->code = ERR_DB_CONSTRAINT;
                err->category = DB_ERR_CAT_CONSTRAINT;
            } else if (strncmp(sqlstate, "23", 2) == 0) {
                // All class 23 = integrity constraint violation
                err->code = ERR_DB_CONSTRAINT;
                err->category = DB_ERR_CAT_CONSTRAINT;
            } else if (strcmp(sqlstate, "40P01") == 0) {
                // 40P01 = serialization_failure (deadlock)
                err->code = ERR_DB_QUERY_FAILED;
                err->category = DB_ERR_CAT_DEADLOCK;
            } else if (strcmp(sqlstate, "55P03") == 0) {
                // 55P03 = lock_not_available
                err->code = ERR_DB_QUERY_FAILED;
                err->category = DB_ERR_CAT_LOCK_TIMEOUT;
            } else if (strcmp(sqlstate, "42P01") == 0 || strcmp(sqlstate, "42703") == 0) {
                // 42P01 = undefined_table, 42703 = undefined_column
                err->code = ERR_DB_QUERY_FAILED;
                err->category = DB_ERR_CAT_UNKNOWN;
            } else {
                // Default to generic DB error
                err->code = ERR_DB_QUERY_FAILED;
                err->category = DB_ERR_CAT_UNKNOWN;
            }
        } else {
            err->code = ERR_DB_INTERNAL;
            err->category = DB_ERR_CAT_UNKNOWN;
        }
        const char *msg = PQresultErrorMessage(pg_res);
        if (msg && *msg) {
            strlcpy(err->message, msg, sizeof(err->message));
        } else {
            strlcpy(err->message, "PostgreSQL query failed without a message.", sizeof(err->message));
        }
        LOGE("PostgreSQL Query Error: SQLSTATE=%s, message=%s", sqlstate ? sqlstate : "unknown", err->message);
    } else if (conn) {
        err->backend_code = PQstatus(conn);
        err->code = ERR_DB_CONNECT;
        err->category = DB_ERR_CAT_CONNECTION;
        const char* msg = PQerrorMessage(conn);
        if (msg && *msg) {
            strlcpy(err->message, msg, sizeof(err->message));
        } else {
            strlcpy(err->message, "PostgreSQL connection failed without a message.", sizeof(err->message));
        }
        LOGE("PostgreSQL Connection Error: %s", err->message);
    } else {
        err->code = ERR_UNKNOWN;
        err->category = DB_ERR_CAT_UNKNOWN;
        strlcpy(err->message, "An unknown database error occurred.", sizeof(err->message));
    }
}


static char* pg_bind_param_to_string(const db_bind_t *param) {
    char *buf = NULL;
    switch (param->type) {
        case DB_BIND_NULL: return NULL;
        case DB_BIND_I64: asprintf(&buf, "%lld", (long long)param->v.i64); break;
        case DB_BIND_I32: asprintf(&buf, "%d", param->v.i32); break;
        case DB_BIND_BOOL: asprintf(&buf, "%s", param->v.b ? "t" : "f"); break;
        case DB_BIND_TIMESTAMP: {
            struct tm tm;
            time_t t = (time_t)param->v.timestamp;
            gmtime_r(&t, &tm);
            char tmp[32];
            strftime(tmp, sizeof(tmp), "%Y-%m-%dT%H:%M:%SZ", &tm);
            buf = strdup(tmp);
            break;
        }
        case DB_BIND_TEXT: 
            if (param->v.text.ptr == NULL)
                return NULL;
            return strdup(param->v.text.ptr);
        default: return NULL;
    }
    return buf;
}

static void pg_close_impl(db_t *db) {
    db_pg_impl_t *impl = (db_pg_impl_t*)db->impl;
    if (impl) {
        if (impl->conn) {
            pthread_mutex_lock(&g_pg_mutex);
            PQfinish(impl->conn);
            pthread_mutex_unlock(&g_pg_mutex);
        }
        free(impl);
    }
}

static bool pg_tx_begin_impl(db_t *db, db_tx_flags_t flags, db_error_t *err) {
    db_pg_impl_t *impl = (db_pg_impl_t*)db->impl;
    (void)flags;
    pthread_mutex_lock(&g_pg_mutex);
    PGresult *res = PQexec(impl->conn, "BEGIN");
    pthread_mutex_unlock(&g_pg_mutex);
    if (PQresultStatus(res) != PGRES_COMMAND_OK) { pg_map_error(impl->conn, res, err); PQclear(res); return false; }
    PQclear(res); impl->in_tx = true; return true;
}

static bool pg_tx_commit_impl(db_t *db, db_error_t *err) {
    db_pg_impl_t *impl = (db_pg_impl_t*)db->impl;
    pthread_mutex_lock(&g_pg_mutex);
    PGresult *res = PQexec(impl->conn, "COMMIT");
    pthread_mutex_unlock(&g_pg_mutex);
    if (PQresultStatus(res) != PGRES_COMMAND_OK) { pg_map_error(impl->conn, res, err); PQclear(res); return false; }
    PQclear(res); impl->in_tx = false; return true;
}

static bool pg_tx_rollback_impl(db_t *db, db_error_t *err) {
    db_pg_impl_t *impl = (db_pg_impl_t*)db->impl;
    pthread_mutex_lock(&g_pg_mutex);
    PGresult *res = PQexec(impl->conn, "ROLLBACK");
    pthread_mutex_unlock(&g_pg_mutex);
    if (PQresultStatus(res) != PGRES_COMMAND_OK) { pg_map_error(impl->conn, res, err); PQclear(res); return false; }
    PQclear(res); impl->in_tx = false; return true;
}

static bool pg_exec_internal(db_t *db, const char *sql, const db_bind_t *params, size_t n_params, int64_t *out_rows, db_error_t *err) {
    db_pg_impl_t *impl = (db_pg_impl_t*)db->impl;
    char **values = calloc(n_params, sizeof(char*));
    if (!values && n_params > 0) {
        err->code = ERR_DB_QUERY_FAILED;
        snprintf(err->message, sizeof(err->message), "Memory allocation failed");
        return false;
    }
    for (size_t i = 0; i < n_params; i++) values[i] = pg_bind_param_to_string(&params[i]);
    pthread_mutex_lock(&g_pg_mutex);
    PGresult *res = PQexecParams(impl->conn, sql, n_params, NULL, (const char* const*)values, NULL, NULL, 0);
    pthread_mutex_unlock(&g_pg_mutex);
    for (size_t i = 0; i < n_params; i++)
      free(values[i]);
    free(values);
    if (PQresultStatus(res) != PGRES_COMMAND_OK && PQresultStatus(res) != PGRES_TUPLES_OK) { pg_map_error(impl->conn, res, err); PQclear(res); return false; }
    if (out_rows) *out_rows = atoll(PQcmdTuples(res));
    PQclear(res); return true;
}

static bool pg_exec_impl(db_t *db, const char *sql, const db_bind_t *params, size_t n_params, db_error_t *err) {
    return pg_exec_internal(db, sql, params, n_params, NULL, err);
}

static bool pg_exec_rows_affected_impl(db_t *db, const char *sql, const db_bind_t *params, size_t n_params, int64_t *out_rows, db_error_t *err) {
    return pg_exec_internal(db, sql, params, n_params, out_rows, err);
}

static bool pg_exec_insert_id_impl(db_t *db, const char *sql, const db_bind_t *params, size_t n_params, const char *id_col, int64_t *out_id, db_error_t *err) {
    db_pg_impl_t *impl = (db_pg_impl_t*)db->impl;
    
    char *sql_with_returning = (char*)sql;
    bool free_sql = false;

    if (id_col && !strcasestr(sql, "RETURNING")) {
        asprintf(&sql_with_returning, "%s RETURNING %s", sql, id_col);
        free_sql = true;
    }

    char **values = calloc(n_params, sizeof(char*));
    if (!values && n_params > 0) {
        err->code = ERR_DB_QUERY_FAILED;
        snprintf(err->message, sizeof(err->message), "Memory allocation failed");
        if (free_sql) free(sql_with_returning);
        return false;
    }
    for (size_t i = 0; i < n_params; i++) values[i] = pg_bind_param_to_string(&params[i]);
    pthread_mutex_lock(&g_pg_mutex);
    PGresult *res = PQexecParams(impl->conn, sql_with_returning, n_params, NULL, (const char* const*)values, NULL, NULL, 0);
    pthread_mutex_unlock(&g_pg_mutex);
    
    if (free_sql) free(sql_with_returning);

    for (size_t i = 0; i < n_params; i++)
      free(values[i]);
    free(values);
    if (PQresultStatus(res) != PGRES_TUPLES_OK) { pg_map_error(impl->conn, res, err); PQclear(res); return false; }
    if (out_id) *out_id = atoll(PQgetvalue(res, 0, 0));
    PQclear(res); return true;
}

static bool pg_query_impl(db_t *db, const char *sql, const db_bind_t *params, size_t n_params, db_res_t **out_res, db_error_t *err) {
    db_pg_impl_t *impl = (db_pg_impl_t*)db->impl;
    char **values = calloc(n_params, sizeof(char*));
    if (!values && n_params > 0) {
        err->code = ERR_DB_QUERY_FAILED;
        snprintf(err->message, sizeof(err->message), "Memory allocation failed");
        return false;
    }
    for (size_t i = 0; i < n_params; i++) values[i] = pg_bind_param_to_string(&params[i]);
    pthread_mutex_lock(&g_pg_mutex);
    PGresult *pg_res = PQexecParams(impl->conn, sql, n_params, NULL, (const char* const*)values, NULL, NULL, 0);
    pthread_mutex_unlock(&g_pg_mutex);
    for (size_t i = 0; i < n_params; i++)
      free(values[i]);
    free(values);
    ExecStatusType status = PQresultStatus(pg_res);
    if (status != PGRES_TUPLES_OK && status != PGRES_COMMAND_OK) { pg_map_error(impl->conn, pg_res, err); PQclear(pg_res); return false; }
    db_pg_res_impl_t *res_impl = calloc(1, sizeof(db_pg_res_impl_t));
    if (!res_impl) {
        err->code = ERR_DB_QUERY_FAILED;
        snprintf(err->message, sizeof(err->message), "Memory allocation failed");
        PQclear(pg_res);
        return false;
    }
    res_impl->pg_res = pg_res;
    db_res_t *res = calloc(1, sizeof(db_res_t));
    if (!res) {
        err->code = ERR_DB_QUERY_FAILED;
        snprintf(err->message, sizeof(err->message), "Memory allocation failed");
        free(res_impl);
        PQclear(pg_res);
        return false;
    }
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
        case 16: return DB_TYPE_INTEGER; // BOOL in PG
        case 20: case 23: return DB_TYPE_INTEGER;
        case 700: case 701: return DB_TYPE_FLOAT;
        case 25: case 1043: return DB_TYPE_TEXT;
        default: return DB_TYPE_UNKNOWN;
    }
}

static bool pg_res_col_is_null_impl(const db_res_t *res, int col_idx) {
    return PQgetisnull(((db_pg_res_impl_t*)res->impl)->pg_res, res->current_row, col_idx);
}

static bool pg_res_col_bool_impl(const db_res_t *res, int col_idx, db_error_t *err) {
    (void)err;
    const char *val = PQgetvalue(((db_pg_res_impl_t*)res->impl)->pg_res, res->current_row, col_idx);
    if (!val) return false;
    return (val[0] == 't' || val[0] == '1' || val[0] == 'T' || strcasecmp(val, "true") == 0);
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

static bool
pg_ship_repair_atomic(db_t *db,
                      int player_id,
                      int ship_id,
                      int cost,
                      int64_t *out_new_credits,
                      db_error_t *err)
{
  if (err) db_error_clear(err);

  db_pg_impl_t *impl = (db_pg_impl_t*)db->impl;
  PGconn *conn = impl ? impl->conn : NULL;

  if (!conn) {
      if (err) {
          err->code = ERR_DB_CONNECT;

          strlcpy(err->message, "No database connection.", sizeof(err->message));
      }
      return false;
  }
  pthread_mutex_lock(&g_pg_mutex);
  if (PQstatus(conn) != CONNECTION_OK)
    {
      pthread_mutex_unlock(&g_pg_mutex);
      if (err) {
        pg_map_error(conn, NULL, err);
      }
      return false;
    }

  const char *sql =
    "WITH player AS ( "
    "  UPDATE players "
    "     SET credits = credits - $1 "
    "   WHERE player_id = $2 "
    "     AND credits >= $1 "
    "   RETURNING credits "
    "), ship AS ( "
    "  UPDATE ships "
    "     SET hull = 100 "
    "   WHERE ship_id = $3 "
    "     AND EXISTS (SELECT 1 FROM player) "
    "   RETURNING ship_id "
    ") "
    "SELECT (SELECT credits FROM player) AS new_credits, "
    "       (SELECT ship_id FROM ship)        AS ship_id;";

  char p1[32], p2[32], p3[32];
  snprintf(p1, sizeof p1, "%d", cost);
  snprintf(p2, sizeof p2, "%d", player_id);
  snprintf(p3, sizeof p3, "%d", ship_id);
  const char *params[3] = { p1, p2, p3 };

  PGresult *res = PQexecParams(conn, sql, 3, NULL, params, NULL, NULL, 0);
  pthread_mutex_unlock(&g_pg_mutex);
  if (!res || (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) != 1))
    {
      pg_map_error(conn, res, err);
      if (res) PQclear(res);
      return false;
    }

  const bool credits_null = PQgetisnull(res, 0, 0);
  const bool ship_null    = PQgetisnull(res, 0, 1);

  if (credits_null)
    {
      if (err) {
          err->code = ERR_INSUFFICIENT_FUNDS;

          strlcpy(err->message, "Insufficient credits for repair.", sizeof(err->message));
      }
      PQclear(res);
      return false;
    }
  if (ship_null)
    {
      if (err) {
          err->code = ERR_SHIP_NOT_FOUND;

          strlcpy(err->message, "Ship not found or could not be repaired.", sizeof(err->message));
      }
      PQclear(res);
      return false;
    }

  if (out_new_credits)
    *out_new_credits = strtoll(PQgetvalue(res, 0, 0), NULL, 10);

  PQclear(res);
  return true;
}

/**
 * @brief PostgreSQL implementation of exec_returning.
 * 
 * PostgreSQL supports RETURNING natively, so this is a thin wrapper over pg_query_impl.
 */
static bool pg_exec_returning_impl(db_t *db, const char *sql, const db_bind_t *params, size_t n_params, db_res_t **out_res, db_error_t *err) {
  /* For PostgreSQL, RETURNING is native; delegate to query */
  return pg_query_impl(db, sql, params, n_params, out_res, err);
}


static const db_vt_t pg_vt = {
    .close = pg_close_impl,
    .tx_begin = pg_tx_begin_impl, .tx_commit = pg_tx_commit_impl, .tx_rollback = pg_tx_rollback_impl,
    .exec = pg_exec_impl, .exec_rows_affected = pg_exec_rows_affected_impl, .exec_insert_id = pg_exec_insert_id_impl,
    .query = pg_query_impl,
    .exec_returning = pg_exec_returning_impl,
    .res_step = pg_res_step_impl, .res_finalize = pg_res_finalize_impl, .res_cancel = pg_res_cancel_impl,
    .res_col_count = pg_res_col_count_impl, .res_col_name = pg_res_col_name_impl, .res_col_type = pg_res_col_type_impl,
    .res_col_is_null = pg_res_col_is_null_impl,
    .res_col_bool = pg_res_col_bool_impl,
    .res_col_i64 = pg_res_col_i64_impl, .res_col_i32 = pg_res_col_i32_impl,
    .res_col_double = pg_res_col_double_impl, .res_col_text = pg_res_col_text_impl,
    .ship_repair_atomic = pg_ship_repair_atomic,	
};

void* db_pg_open_internal(db_t *parent_db, const db_config_t *cfg, db_error_t *err) {
    pthread_mutex_lock(&g_pg_mutex);
    PGconn *conn = PQconnectdb(cfg->pg_conninfo);
    pthread_mutex_unlock(&g_pg_mutex);
    if (PQstatus(conn) != CONNECTION_OK) { pg_map_error(conn, NULL, err); if (conn) PQfinish(conn); return NULL; }

    /* Suppress NOTICE messages (e.g. "relation already exists, skipping") */
    pthread_mutex_lock(&g_pg_mutex);
    PGresult *res_quiet = PQexec(conn, "SET client_min_messages TO WARNING");
    if (res_quiet) PQclear(res_quiet);
    pthread_mutex_unlock(&g_pg_mutex);

    db_pg_impl_t *impl = calloc(1, sizeof(db_pg_impl_t));
    if (!impl) {
        err->code = ERR_DB_QUERY_FAILED;
        snprintf(err->message, sizeof(err->message), "Memory allocation failed");
        PQfinish(conn);
        return NULL;
    }
    impl->conn = conn;
    parent_db->vt = &pg_vt;
    return impl;
}


