#define TW_DB_INTERNAL 1
#include "db/db_api.h"
#include "db/db_int.h"
#include "db/mysql/db_mysql.h"
#include "db/sql_driver.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* db_api.c contains both backend dispatch branches. Keep this focused test
 * linked only to the MySQL driver and satisfy the unused PostgreSQL hook. */
void *db_pg_open_internal (db_t *db, const db_config_t *cfg, db_error_t *err)
{ (void) db; (void) cfg; (void) err; return NULL; }

static int fail (const char *message, const db_error_t *err)
{
  fprintf (stderr, "FAIL: %s", message);
  if (err && err->message[0]) fprintf (stderr, " (%s)", err->message);
  fputc ('\n', stderr);
  return 1;
}

static db_res_t *query (db_t *db, const char *sql, const db_bind_t *params,
                       size_t n, db_error_t *err)
{
  db_res_t *res = NULL;
  if (!db_query (db, sql, params, n, &res, err)) return NULL;
  return res;
}

static int count_rows (db_t *db, db_error_t *err)
{
  db_res_t *res = query (db, "SELECT COUNT(*) AS n FROM tw_mysql_driver_test", NULL, 0, err);
  int count = -1;
  if (!res) return -1;
  if (db_res_step (res, err)) count = db_res_col_int (res, 0, err);
  db_res_finalize (res);
  return count;
}

int main (void)
{
  const char *host = getenv ("MYSQL_TEST_HOST");
  const char *user = getenv ("MYSQL_TEST_USER");
  const char *database = getenv ("MYSQL_TEST_DATABASE");
  const char *password = getenv ("MYSQL_TEST_PASSWORD");
  const char *socket_path = getenv ("MYSQL_TEST_SOCKET");
  const char *port_text = getenv ("MYSQL_TEST_PORT");
  db_config_t cfg = { 0 };
  db_error_t err;
  db_t *db;
  db_res_t *res;
  char sparse_sql[128];
  char epoch_sql[128];
  char epoch_expr[64];
  int64_t initial_id = 0, rows = 0;
  long port = port_text ? strtol (port_text, NULL, 10) : 0;

  if (!host || !user || !database)
    {
      fprintf (stderr, "SKIP: set MYSQL_TEST_HOST, MYSQL_TEST_USER, and MYSQL_TEST_DATABASE\n");
      return 77;
    }
  cfg.backend = DB_BACKEND_MYSQL;
  cfg.mysql_host = host;
  cfg.mysql_user = user;
  cfg.mysql_password = password;
  cfg.mysql_database = database;
  cfg.mysql_unix_socket = socket_path;
  cfg.mysql_port = port > 0 && port <= UINT16_MAX ? (uint16_t) port : 0;
  cfg.connect_timeout_ms = 5000;

  db = db_open (&cfg, &err);
  if (!db) return fail ("connect", &err);
  if (db_backend (db) != DB_BACKEND_MYSQL) return fail ("backend identity", NULL);

  db_bind_t scalar_params[] = { db_bind_i64 (9223372036854770000LL), db_bind_text ("prepared value") };
  res = query (db, "SELECT {1} AS number_value, {2} AS text_value, NULL AS missing_value",
               scalar_params, 2, &err);
  if (!res) { db_close (db); return fail ("parameterized scalar query", &err); }
  if (db_res_col_count (res) != 3 || strcmp (db_res_col_name (res, 0), "number_value") != 0 ||
      !db_res_step (res, &err) || db_res_col_i64 (res, 0, &err) != 9223372036854770000LL ||
      strcmp (db_res_col_text (res, 1, &err), "prepared value") != 0 ||
      !db_res_col_is_null (res, 2))
    { db_res_finalize (res); db_close (db); return fail ("typed result reading", &err); }
  if (db_res_step (res, &err))
    { db_res_finalize (res); db_close (db); return fail ("single-row result termination", &err); }
  db_res_finalize (res);

  const char *epoch_fmt = sql_epoch_param_to_timestamptz (db);
  db_bind_t epoch_param = db_bind_i64 (0);
  if (!epoch_fmt || snprintf (epoch_expr, sizeof (epoch_expr), epoch_fmt, "{1}") >= (int) sizeof (epoch_expr) ||
      snprintf (epoch_sql, sizeof (epoch_sql), "SELECT %s AS ts", epoch_expr) >= (int) sizeof (epoch_sql))
    { db_close (db); return fail ("MySQL epoch-to-timestamp SQL", NULL); }
  char epoch_template[128];
  strcpy (epoch_template, epoch_sql);
  if (sql_build (db, epoch_template, epoch_sql, sizeof (epoch_sql)) != 0)
    { db_close (db); return fail ("build MySQL epoch-to-timestamp SQL", NULL); }
  res = query (db, epoch_sql, &epoch_param, 1, &err);
  if (!res || !db_res_step (res, &err) ||
      strncmp (db_res_col_text (res, 0, &err), "1970-01-01 00:00:00", 19) != 0)
    { if (res) db_res_finalize (res); db_close (db); return fail ("UTC epoch-to-timestamp conversion", &err); }
  db_res_finalize (res);

  db_bind_t sparse_params[] = { db_bind_text ("first"), db_bind_i64 (22),
                                db_bind_i64 (33), db_bind_text ("fourth") };
  if (sql_build (db, "SELECT {1} AS val_a, {4} AS val_d",
                 sparse_sql, sizeof (sparse_sql)) != 0)
    { db_close (db); return fail ("build sparse numbered SQL", NULL); }
  res = query (db, sparse_sql, sparse_params, 4, &err);
  if (!res || !db_res_step (res, &err) ||
      strcmp (db_res_col_text (res, 0, &err), "first") != 0 ||
      strcmp (db_res_col_text (res, 1, &err), "fourth") != 0)
    { if (res) db_res_finalize (res); db_close (db); return fail ("sparse numbered parameter binding", &err); }
  db_res_finalize (res);

  db_bind_t repeated_param = db_bind_text ("numbered marker");
  res = query (db, "SELECT $1 AS repeated_one, $1 AS repeated_two", &repeated_param, 1, &err);
  if (!res || !db_res_step (res, &err))
    { if (res) db_res_finalize (res); db_close (db); return fail ("numbered/repeated parameter binding", &err); }
  const char *first_value = db_res_col_text (res, 0, &err);
  const char *second_value = db_res_col_text (res, 1, &err);
  if (!first_value || !second_value || strcmp (first_value, "numbered marker") != 0 ||
      strcmp (second_value, "numbered marker") != 0)
    { db_res_finalize (res); db_close (db); return fail ("numbered/repeated parameter binding", &err); }
  db_res_finalize (res);

  if (!db_exec (db, "CREATE TEMPORARY TABLE tw_mysql_driver_test (id BIGINT NOT NULL AUTO_INCREMENT PRIMARY KEY, label VARCHAR(100) NOT NULL)", NULL, 0, &err))
    { db_close (db); return fail ("temporary test table creation", &err); }
  if (!db_tx_begin (db, DB_TX_DEFAULT, &err))
    { db_close (db); return fail ("transaction begin", &err); }
  db_bind_t label = db_bind_text ("committed");
  if (!db_exec_insert_id (db, "INSERT INTO tw_mysql_driver_test (label) VALUES ($1)", &label, 1, "id", &initial_id, &err) || initial_id <= 0)
    { (void) db_tx_rollback (db, NULL); db_close (db); return fail ("prepared insert and generated ID", &err); }
  if (!db_tx_commit (db, &err) || count_rows (db, &err) != 1)
    { db_close (db); return fail ("transaction commit", &err); }

  if (!db_tx_begin (db, DB_TX_DEFAULT, &err))
    { db_close (db); return fail ("rollback transaction begin", &err); }
  label = db_bind_text ("rolled back");
  if (!db_exec_rows_affected (db, "INSERT INTO tw_mysql_driver_test (label) VALUES ($1)", &label, 1, &rows, &err) || rows != 1)
    { (void) db_tx_rollback (db, NULL); db_close (db); return fail ("transactional insert", &err); }
  if (!db_tx_rollback (db, &err) || count_rows (db, &err) != 1)
    { db_close (db); return fail ("transaction rollback", &err); }

  db_close (db);
  puts ("MySQL backend integration tests passed: connect, binds, result reads, insert ID, commit, rollback");
  return 0;
}
