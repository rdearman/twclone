#define TW_DB_INTERNAL 1
#include "db/db_api.h"
#include "db/db_int.h"
#include "db/repo/repo_cmd.h"
#include "db/sql_driver.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* This focused binary exercises the PostgreSQL driver only. */
void *
db_mysql_open_internal (db_t *db, const db_config_t *cfg, db_error_t *err)
{
  (void) db;
  (void) cfg;
  (void) err;
  return NULL;
}

void
server_log_printf (int priority, const char *format, ...)
{
  (void) priority;
  (void) format;
}

static int
fail (const char *stage, const db_error_t *err)
{
  fprintf (stderr, "FAIL: %s", stage);
  if (err && err->message[0])
    fprintf (stderr, " (backend code %d: %s)", err->backend_code,
             err->message);
  fputc ('\n', stderr);
  return 1;
}

static int
check_status (db_t *db, int player_id, const char *expected_status,
              int expect_sleep, int64_t expected_until,
              int64_t *last_reset_out, db_error_t *err)
{
  const char *query =
    "SELECT status,big_sleep_until,podded_last_reset "
    "FROM podded_status WHERE player_id={1};";
  char sql[256];
  db_res_t *res = NULL;
  if (sql_build (db, query, sql, sizeof sql) != 0
      || !db_query (db, sql, (db_bind_t[]){db_bind_i64 (player_id)}, 1,
                    &res, err)
      || !db_res_step (res, err))
    {
      if (res)
        db_res_finalize (res);
      return 0;
    }

  const char *status = db_res_col_text (res, 0, err);
  int has_sleep = !db_res_col_is_null (res, 1);
  int64_t until = has_sleep ? db_res_col_i64 (res, 1, err) : 0;
  int64_t last_reset = db_res_col_i64 (res, 2, err);
  int ok = status && strcmp (status, expected_status) == 0
    && has_sleep == expect_sleep
    && (!expect_sleep || until == expected_until)
    && last_reset > 1700000000 && last_reset <= (int64_t) time (NULL) + 5
    && err->code == 0;
  if (last_reset_out)
    *last_reset_out = last_reset;
  db_res_finalize (res);
  return ok;
}

static int
count_rows (db_t *db, int player_id, int64_t *count_out, db_error_t *err)
{
  char sql[256];
  db_res_t *res = NULL;
  if (sql_build (db, "SELECT COUNT(*) FROM podded_status WHERE player_id={1};",
                 sql, sizeof sql) != 0
      || !db_query (db, sql, (db_bind_t[]){db_bind_i64 (player_id)}, 1,
                    &res, err)
      || !db_res_step (res, err))
    {
      if (res)
        db_res_finalize (res);
      return 0;
    }
  *count_out = db_res_col_i64 (res, 0, err);
  db_res_finalize (res);
  return err->code == 0;
}

int
main (void)
{
  const char *database = getenv ("PGDATABASE");
  if (!database || (strncmp (database, "twclone_rc2_", 12) != 0
                    && strncmp (database, "twclone_qa_", 11) != 0))
    {
      fputs ("REFUSED: PGDATABASE must name an isolated twclone_rc2_* or "
             "twclone_qa_* database\n", stderr);
      return 77;
    }

  db_config_t config = { 0 };
  config.backend = DB_BACKEND_POSTGRES;
  config.pg_conninfo = ""; /* Use the libpq PG* environment. */
  config.connect_timeout_ms = 5000;
  db_error_t err = { 0 };
  db_t *db = db_open (&config, &err);
  if (!db)
    return fail ("connect to isolated PostgreSQL database", &err);

  const char *schema =
    "CREATE TEMP TABLE podded_status ("
    "player_id integer PRIMARY KEY,"
    "status text NOT NULL DEFAULT 'active',"
    "big_sleep_until integer,"
    "reason text,"
    "podded_count_today integer NOT NULL DEFAULT 0,"
    "podded_last_reset integer);";
  if (!db_exec (db, schema, NULL, 0, &err))
    {
      db_close (db);
      return fail ("create production-shaped temporary podded_status", &err);
    }

  if (db_create_podded_status_entry (db, 7001) != 0
      || !check_status (db, 7001, "active", 0, 0, NULL, &err))
    {
      db_close (db);
      return fail ("create row with integer epoch reset and no timestamp-only columns", &err);
    }

  int64_t until = (int64_t) time (NULL) + 3600;
  if (db_update_player_podded_status (db, 7001, "big_sleep", until) != 0
      || !check_status (db, 7001, "big_sleep", 1, until, NULL, &err))
    {
      db_close (db);
      return fail ("update status and big_sleep_until", &err);
    }

  if (db_update_player_podded_status (db, 7001, "active", 0) != 0
      || !check_status (db, 7001, "active", 0, 0, NULL, &err))
    {
      db_close (db);
      return fail ("clear big_sleep_until when status becomes active", &err);
    }

  if (!db_tx_begin (db, DB_TX_DEFAULT, &err)
      || db_create_podded_status_entry (db, 7002) != 0
      || db_update_player_podded_status (db, 7002, "big_sleep", until) != 0
      || !db_tx_rollback (db, &err))
    {
      (void) db_tx_rollback (db, &err);
      db_close (db);
      return fail ("rollback pod creation and update", &err);
    }

  int64_t remaining = -1;
  if (!count_rows (db, 7002, &remaining, &err) || remaining != 0)
    {
      db_close (db);
      return fail ("verify rollback removed the temporary pod status row", &err);
    }

  db_close (db);
  puts ("PostgreSQL pod-status repository passed: create, status/deadline update, clear deadline, rollback");
  return 0;
}
