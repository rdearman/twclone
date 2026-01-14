#define TW_DB_INTERNAL 1
#include "db_int.h"
#include "db/sql_driver.h"
#include "server_log.h"
#include "repo_database.h"
#include <time.h>
#include <stdio.h>

/* This file is deprecated. Functionality moved to game_db.c and database_cmd.c */
/* ...but reused for S2S key repo pilot... */

int
repo_s2s_create_key (db_t *db, const char *key_id, const char *key_b64)
{
  const char *now_expr = sql_now_expr(db);
  if (!now_expr)
    {
      LOGE ("REPO_S2S: Unsupported database backend\n");
      return -1;
    }

  char sql_tmpl[512];
  snprintf(sql_tmpl, sizeof(sql_tmpl),
    "INSERT INTO s2s_keys (key_id, key_b64, active, created_ts, is_default_tx) "
    "VALUES ({1}, {2}, true, %s, 0);",
    now_expr);

  char SQL_INSERT[512];
  if (sql_build(db, sql_tmpl, SQL_INSERT, sizeof(SQL_INSERT)) != 0)
    {
      LOGE ("REPO_S2S: Failed to build SQL\n");
      return -1;
    }

  time_t now = time (NULL);
  db_bind_t params[] = {
    db_bind_text (key_id),
    db_bind_text (key_b64),
    db_bind_i64 ((long long)now)
  };

  db_error_t err;
  if (!db_exec (db, SQL_INSERT, params, 3, &err))
    {
      LOGE ("REPO_S2S: Failed to execute insert: %s\n", err.message);
      return -1;
    }

  return 0;
}

int
repo_s2s_get_default_key (db_t *db, char *out_key_id, size_t key_id_size, char *out_key_b64, size_t key_b64_size)
{
  const char *sql =
    "SELECT key_id, key_b64 FROM s2s_keys WHERE active = TRUE ORDER BY created_ts DESC LIMIT 1";

  db_error_t err;
  db_error_clear (&err);

  db_res_t *res = NULL;

  if (!db_query (db, sql, NULL, 0, &res, &err))
    {
      LOGE ("repo_s2s_get_default_key: query failed: %s (code=%d backend=%d)",
            err.message, err.code, err.backend_code);
      return -1;
    }

  if (db_res_step (res, &err))
    {
      /* Found a row */
      const char *kid = db_res_col_text (res, 0, &err);
      const char *kb64 = db_res_col_text (res, 1, &err);

      if (kid && kb64)
        {
          snprintf(out_key_id, key_id_size, "%s", kid);
          snprintf(out_key_b64, key_b64_size, "%s", kb64);
          db_res_finalize (res);
          return 0;
        }
    }

  /* No row OR step error */
  if (err.code != 0)
    {
      LOGE ("repo_s2s_get_default_key: step failed: %s (code=%d backend=%d)",
            err.message, err.code, err.backend_code);
    }

  db_res_finalize (res);
  return -1;
}

int repo_database_raw_query(db_t *db, const char *sql, db_res_t **out_res)
{
    /* SQL_VERBATIM: USER_DYNAMIC */
    db_error_t err;
    if (db_query(db, sql, NULL, 0, out_res, &err)) {
        return 0;
    }
    return err.code;
}
