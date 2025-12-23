#ifndef DB_API_H
#define DB_API_H

#ifdef __cplusplus
extern "C" {
#endif

/* A) Backend selection */
enum db_backend {
  DB_BACKEND_PG = 1 /* future: MYSQL, MSSQL */
};

/* B) Config object */
typedef struct db_cfg {
  int backend;                 /* enum db_backend */
  const char *conninfo;        /* libpq conninfo string for PG */
  int connect_timeout_ms;      /* optional */
  int statement_timeout_ms;    /* optional default per session */
  int lock_timeout_ms;         /* optional */
  int app_name_pid;            /* if true, append pid to application_name */
  int log_slow_ms;             /* log queries slower than this */
} db_cfg_t;

/* C) Opaque handles */
typedef struct db db_t;
typedef struct db_stmt db_stmt_t;

/* D) Error model */
typedef enum {
  DBE_OK = 0,
  DBE_TRANSIENT,      /* retryable: deadlock/serialization/lock timeout */
  DBE_CONSTRAINT,     /* unique/fk/check */
  DBE_NOTFOUND,
  DBE_PROTOCOL,       /* SQL syntax, wrong params, unexpected results */
  DBE_AUTH,
  DBE_FATAL           /* connection lost, corruption, etc */
} db_err_class_t;

typedef struct db_err {
  db_err_class_t cls;
  int native_code;            /* PG: SQLSTATE packed or 0; or store separate */
  char sqlstate[6];           /* PG: "40P01", "23505", ... */
  char msg[256];              /* short copy for logs */
} db_err_t;

/* E) Core connection lifecycle */
db_t *pg_db_open(const db_cfg_t *cfg, db_err_t *err);
void  pg_db_close(db_t *db);

int   pg_db_ping(db_t *db, db_err_t *err); /* SELECT 1 */

/* F) Transaction API */
typedef enum { DB_TX_READONLY=1, DB_TX_READWRITE=2 } db_tx_mode_t;

int pg_db_tx_begin(db_t *db, db_tx_mode_t mode, db_err_t *err);
int pg_db_tx_commit(db_t *db, db_err_t *err);
int pg_db_tx_rollback(db_t *db, db_err_t *err);

/* Run fn inside tx with retries on transient errors. */
typedef int (*db_tx_fn)(db_t *db, void *ctx, db_err_t *err);

int pg_db_tx_run(db_t *db, db_tx_mode_t mode, int max_retries,
              db_tx_fn fn, void *ctx, db_err_t *err);

/* G) Parameterised exec helper (no full ORM) */
typedef enum { DBP_NULL, DBP_I64, DBP_F64, DBP_TEXT, DBP_BYTES } db_param_type_t;

typedef struct db_param {
  db_param_type_t t;
  const char *name;      /* optional, can be NULL */
  union {
    long long i64;
    double f64;
    struct { const char *s; int n; } text;
    struct { const void *p; int n; } bytes;
  } v;
} db_param_t;

int pg_db_exec_params(db_t *db, const char *sql,
                   const db_param_t *params, int nparams,
                   db_err_t *err);

#ifdef __cplusplus
}
#endif

#endif /* DB_API_H */
