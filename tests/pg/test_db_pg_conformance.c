#define TW_DB_INTERNAL 1
#include "db/db_api.h"
#include "db/db_int.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdarg.h>

/* This conformance binary exercises the PostgreSQL backend only. */
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
read_count (db_t *db, const char *table, int64_t *count, db_error_t *err)
{
  char sql[256];
  db_res_t *res = NULL;
  if (snprintf (sql, sizeof sql, "SELECT COUNT(*) FROM %s", table)
        >= (int) sizeof sql
      || !db_query (db, sql, NULL, 0, &res, err)
      || !db_res_step (res, err))
    {
      if (res)
        db_res_finalize (res);
      return 0;
    }
  *count = db_res_col_i64 (res, 0, err);
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
  config.pg_conninfo = "";
  config.connect_timeout_ms = 5000;
  db_error_t err = { 0 };
  db_t *db = db_open (&config, &err);
  if (!db)
    return fail ("connect", &err);

  char table[80], ddl[256], dml[256];
  snprintf (table, sizeof table, "tw_db_conformance_%ld", (long) getpid ());
  snprintf (ddl, sizeof ddl,
            "CREATE TABLE %s (id BIGSERIAL PRIMARY KEY, label TEXT NOT NULL, "
            "amount BIGINT NOT NULL, optional_value TEXT)", table);
  if (!db_exec (db, ddl, NULL, 0, &err))
    { db_close (db); return fail ("create isolated conformance table", &err); }

  db_res_t *res = NULL;
  db_bind_t typed[] = { db_bind_i64 (INT64_C (9223372036854770000)),
                        db_bind_text ("typed value") };
  if (!db_query (db, "SELECT $1::bigint AS large_integer, $2::text AS text_value, NULL::text AS absent",
                 typed, 2, &res, &err)
      || !db_res_step (res, &err)
      || db_res_col_i64 (res, 0, &err) != INT64_C (9223372036854770000)
      || strcmp (db_res_col_text (res, 1, &err), "typed value") != 0
      || !db_res_col_is_null (res, 2))
    {
      if (res) db_res_finalize (res);
      snprintf (dml, sizeof dml, "DROP TABLE IF EXISTS %s", table);
      (void) db_exec (db, dml, NULL, 0, NULL);
      db_close (db);
      return fail ("typed, large integer and NULL result conversion", &err);
    }
  db_res_finalize (res);

  db_bind_t json_param = db_bind_i32 (42);
  if (!db_query (db,
        "SELECT CURRENT_TIMESTAMP AS native_timestamp, "
        "jsonb_build_object('value', $1::integer) AS native_json",
        &json_param, 1, &res, &err)
      || !db_res_step (res, &err))
    {
      if (res) db_res_finalize (res);
      snprintf (dml, sizeof dml, "DROP TABLE IF EXISTS %s", table);
      (void) db_exec (db, dml, NULL, 0, NULL);
      db_close (db);
      return fail ("read native timestamp and JSON values", &err);
    }
  const char *native_timestamp = db_res_col_text (res, 0, &err);
  const char *native_json = db_res_col_text (res, 1, &err);
  if (!native_timestamp || strlen (native_timestamp) < 19
      || !native_json || !strstr (native_json, "value")
      || !strstr (native_json, "42"))
    {
      db_res_finalize (res);
      snprintf (dml, sizeof dml, "DROP TABLE IF EXISTS %s", table);
      (void) db_exec (db, dml, NULL, 0, NULL);
      db_close (db);
      return fail ("native timestamp/JSON text accessors", &err);
    }
  db_res_finalize (res);

  db_bind_t repeated = db_bind_text ("repeated binding");
  if (!db_query (db, "SELECT $1::text, $1::text", &repeated, 1, &res, &err)
      || !db_res_step (res, &err)
      || strcmp (db_res_col_text (res, 0, &err), "repeated binding") != 0
      || strcmp (db_res_col_text (res, 1, &err), "repeated binding") != 0)
    {
      if (res) db_res_finalize (res);
      snprintf (dml, sizeof dml, "DROP TABLE IF EXISTS %s", table);
      (void) db_exec (db, dml, NULL, 0, NULL);
      db_close (db);
      return fail ("reused numbered parameter", &err);
    }
  db_res_finalize (res);

  db_bind_t sparse[] = { db_bind_i64 (11), db_bind_i64 (22),
                         db_bind_i64 (33) };
  if (!db_query (db, "SELECT $1::int, $3::int", sparse, 3, &res, &err)
      || !db_res_step (res, &err)
      || db_res_col_i32 (res, 0, &err) != 11
      || db_res_col_i32 (res, 1, &err) != 33)
    {
      if (res) db_res_finalize (res);
      snprintf (dml, sizeof dml, "DROP TABLE IF EXISTS %s", table);
      (void) db_exec (db, dml, NULL, 0, NULL);
      db_close (db);
      return fail ("sparse numbered parameters", &err);
    }
  db_res_finalize (res);

  if (!db_tx_begin (db, DB_TX_DEFAULT, &err))
    { db_close (db); return fail ("begin committed insert", &err); }
  snprintf (dml, sizeof dml, "INSERT INTO %s (label, amount) VALUES ($1, $2)", table);
  db_bind_t insert[] = { db_bind_text ("committed"), db_bind_i64 (7) };
  int64_t id = 0;
  if (!db_exec_insert_id (db, dml, insert, 2, "id", &id, &err) || id <= 0
      || !db_tx_commit (db, &err))
    {
      (void) db_tx_rollback (db, NULL);
      snprintf (dml, sizeof dml, "DROP TABLE IF EXISTS %s", table);
      (void) db_exec (db, dml, NULL, 0, NULL);
      db_close (db);
      return fail ("generated ID and commit", &err);
    }

  if (!db_tx_begin (db, DB_TX_DEFAULT, &err))
    { db_close (db); return fail ("begin rollback insert", &err); }
  db_bind_t rolled_back = db_bind_text ("rolled back");
  int64_t inserted_rows = 0;
  snprintf (dml, sizeof dml, "INSERT INTO %s (label, amount) VALUES ($1, 9)", table);
  if (!db_exec_rows_affected (db, dml, &rolled_back, 1, &inserted_rows, &err)
      || inserted_rows != 1)
    {
      (void) db_tx_rollback (db, NULL);
      snprintf (dml, sizeof dml, "DROP TABLE IF EXISTS %s", table);
      (void) db_exec (db, dml, NULL, 0, NULL);
      db_close (db);
      return fail ("rollback test insert", &err);
    }
  if (!db_tx_rollback (db, &err))
    {
      snprintf (dml, sizeof dml, "DROP TABLE IF EXISTS %s", table);
      (void) db_exec (db, dml, NULL, 0, NULL);
      db_close (db);
      return fail ("rollback transaction", &err);
    }
  int64_t count = -1;
  if (!read_count (db, table, &count, &err) || count != 1)
    {
      snprintf (dml, sizeof dml, "DROP TABLE IF EXISTS %s", table);
      (void) db_exec (db, dml, NULL, 0, NULL);
      db_close (db);
      return fail ("rollback removed inserted row", &err);
    }

  db_t *locker = db_open (&config, &err);
  if (!locker || !db_tx_begin (db, DB_TX_DEFAULT, &err)
      || !db_tx_begin (locker, DB_TX_DEFAULT, &err)
      || !db_exec (locker, "SET LOCAL lock_timeout = '100ms'", NULL, 0, &err))
    {
      if (locker) db_close (locker);
      snprintf (dml, sizeof dml, "DROP TABLE IF EXISTS %s", table);
      (void) db_exec (db, dml, NULL, 0, NULL);
      db_close (db);
      return fail ("start two-connection lock test", &err);
    }
  snprintf (dml, sizeof dml, "SELECT id FROM %s WHERE id=$1 FOR UPDATE", table);
  db_bind_t id_param = db_bind_i64 (id);
  if (!db_query (db, dml, &id_param, 1, &res, &err) || !db_res_step (res, &err))
    {
      if (res) db_res_finalize (res);
      (void) db_tx_rollback (locker, NULL);
      (void) db_tx_rollback (db, NULL);
      db_close (locker);
      snprintf (dml, sizeof dml, "DROP TABLE IF EXISTS %s", table);
      (void) db_exec (db, dml, NULL, 0, NULL);
      db_close (db);
      return fail ("acquire row lock", &err);
    }
  db_res_finalize (res);
  snprintf (dml, sizeof dml, "UPDATE %s SET amount=amount+1 WHERE id=$1", table);
  int64_t affected = 0;
  bool conflicting_update = db_exec_rows_affected (locker, dml, &id_param, 1,
                                                    &affected, &err);
  if (conflicting_update || err.category != DB_ERR_CAT_LOCK_TIMEOUT
      || !db_tx_rollback (locker, NULL)
      || !db_tx_commit (db, &err))
    {
      (void) db_tx_rollback (locker, NULL);
      (void) db_tx_rollback (db, NULL);
      db_close (locker);
      snprintf (dml, sizeof dml, "DROP TABLE IF EXISTS %s", table);
      (void) db_exec (db, dml, NULL, 0, NULL);
      db_close (db);
      return fail ("bounded wait reports the conflicting row lock", &err);
    }
  db_close (locker);

  db_close (db);
  db = db_open (&config, &err);
  if (!db || !read_count (db, table, &count, &err) || count != 1)
    {
      if (db) db_close (db);
      return fail ("reconnect persistence", &err);
    }
  snprintf (dml, sizeof dml, "DROP TABLE %s", table);
  if (!db_exec (db, dml, NULL, 0, &err))
    { db_close (db); return fail ("drop QA conformance table", &err); }
  db_close (db);
  puts ("PostgreSQL conformance passed: typed/sparse/reused binds, NULL, native "
       "timestamp/JSON reads, generated ID, affected rows, commit/rollback, "
       "lock conflict and reconnect persistence");
  return 0;
}
