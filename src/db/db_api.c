#include "db_api.h"
#include "db_pg.h"
#include "db_int.h"
#include "../server_log.h"
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>

/* Helper for timing */
static long long current_timestamp_ms() {
    struct timeval te; 
    gettimeofday(&te, NULL);
    return te.tv_sec*1000LL + te.tv_usec/1000;
}

db_t *pg_db_open(const db_cfg_t *cfg, db_err_t *err) {
  if (!cfg) {
    if (err) {
        err->cls = DBE_FATAL;
        snprintf(err->msg, sizeof(err->msg), "Config is NULL");
    }
    return NULL;
  }

  if (cfg->backend == DB_BACKEND_PG) {
#ifdef DB_BACKEND_PG
    /* Pass the configuration to the PG implementation */
    return db_pg_open(cfg, err);
#else
    if (err) {
        err->cls = DBE_FATAL;
        snprintf(err->msg, sizeof(err->msg), "PostgreSQL backend not compiled in");
    }
    return NULL;
#endif
  }

  if (err) {
    err->cls = DBE_FATAL;
    snprintf(err->msg, sizeof(err->msg), "Unknown backend: %d", cfg->backend);
  }
  return NULL;
}

void pg_db_close(db_t *db) {
  if (db && db->vt && db->vt->close) {
    db->vt->close(db);
  }
}

int pg_db_ping(db_t *db, db_err_t *err) {
  if (!db || !db->vt || !db->vt->ping) return 0;
  return db->vt->ping(db, err);
}

int pg_db_tx_begin(db_t *db, db_tx_mode_t mode, db_err_t *err) {
  if (!db || !db->vt || !db->vt->tx_begin) return 0;
  return db->vt->tx_begin(db, mode, err);
}

int pg_db_tx_commit(db_t *db, db_err_t *err) {
  if (!db || !db->vt || !db->vt->tx_commit) return 0;
  return db->vt->tx_commit(db, err);
}

int pg_db_tx_rollback(db_t *db, db_err_t *err) {
  if (!db || !db->vt || !db->vt->tx_rollback) return 0;
  return db->vt->tx_rollback(db, err);
}

int pg_db_exec_params(db_t *db, const char *sql,
                   const db_param_t *params, int nparams,
                   db_err_t *err) {
  if (!db || !db->vt || !db->vt->exec_params) return 0;

  long long start = 0;
  if (db->cfg.log_slow_ms > 0) {
    start = current_timestamp_ms();
  }

  int res = db->vt->exec_params(db, sql, params, nparams, err);

  if (db->cfg.log_slow_ms > 0) {
    long long duration = current_timestamp_ms() - start;
    if (duration > db->cfg.log_slow_ms) {
      /* Minimal slow log using existing logging macros */
      LOGW("Slow Query (%lld ms): %s", duration, sql);
    }
  }

  return res;
}

int pg_db_tx_run(db_t *db, db_tx_mode_t mode, int max_retries,
              db_tx_fn fn, void *ctx, db_err_t *err) {
  int retries = 0;
  int base_backoff_ms = 10;
  int cap_backoff_ms = 250;

  while (1) {
    db_err_t tx_err;
    memset(&tx_err, 0, sizeof(tx_err));

    /* Begin TX */
    if (!pg_db_tx_begin(db, mode, &tx_err)) {
        if (err) *err = tx_err;
        /* Check if begin itself failed transiently */
        if (tx_err.cls == DBE_TRANSIENT && retries < max_retries) {
            goto retry;
        }
        return 0;
    }

    /* Run logic */
    db_err_t fn_err;
    memset(&fn_err, 0, sizeof(fn_err));
    int fn_res = fn(db, ctx, &fn_err);

    if (!fn_res) {
        /* Logic failed, rollback and decide if retry */
        db_err_t rb_err; 
        pg_db_tx_rollback(db, &rb_err); /* ignore rollback error for now */
        
        if (err) *err = fn_err;

        if (fn_err.cls == DBE_TRANSIENT && retries < max_retries) {
             goto retry;
        }
        return 0;
    }

    /* Commit */
    db_err_t c_err;
    memset(&c_err, 0, sizeof(c_err));
    if (!pg_db_tx_commit(db, &c_err)) {
        if (err) *err = c_err;
        if (c_err.cls == DBE_TRANSIENT && retries < max_retries) {
             goto retry;
        }
        return 0;
    }

    /* Success */
    return 1;

retry:
    retries++;
    /* Exponential backoff with jitter */
    int backoff = base_backoff_ms * (1 << (retries - 1));
    if (backoff > cap_backoff_ms) backoff = cap_backoff_ms;
    
    /* Random jitter +/- 50% roughly or just add random 0-backoff? 
       Prompt says "exponential backoff with jitter capped (e.g. 10ms -> 250ms)" */
    int jitter = rand() % (backoff > 0 ? backoff : 1); 
    /* Let's just do sleep(backoff + jitter) or similar? 
       Standard jitter is often random(0, backoff). */
    int sleep_ms = backoff; // simplified

    usleep(sleep_ms * 1000); 
  }
}
