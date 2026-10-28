#define TW_DB_INTERNAL 1
#include "common.h"
#include "db/db_api.h"
#include "db/db_int.h"
#include "db/repo/repo_cmd.h"
#include "db/sql_driver.h"
#include "errors.h"
#include "server_combat.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static db_t *g_db;
static int g_notice_count;

void *
db_mysql_open_internal (db_t *db, const db_config_t *cfg, db_error_t *err)
{
  (void) db;
  (void) cfg;
  (void) err;
  return NULL;
}

db_t *
game_db_get_handle (void)
{
  return g_db;
}

void
server_log_printf (int priority, const char *format, ...)
{
  (void) priority;
  (void) format;
}

int
db_destroy_ship (db_t *db, int player_id, int ship_id)
{
  db_error_t err = { 0 };
  (void) player_id;
  return db_exec (db,
                  "UPDATE combat_test_ships SET destroyed=TRUE WHERE ship_id={1};",
                  (db_bind_t[]){db_bind_i64 (ship_id)}, 1, &err) ? 0 : err.code;
}

int
h_send_message_to_player (db_t *db, int player_id, int channel,
                          const char *subject, const char *message)
{
  (void) db;
  (void) player_id;
  (void) channel;
  (void) subject;
  (void) message;
  g_notice_count++;
  return 0;
}

static int
exec_sql (db_t *db, const char *sql)
{
  db_error_t err = { 0 };
  if (!db_exec (db, sql, NULL, 0, &err))
    {
      fprintf (stderr, "SQL failed (%d): %s\n", err.code, err.message);
      return 0;
    }
  return 1;
}

static int64_t
scalar_i64 (db_t *db, const char *sql)
{
  db_res_t *res = NULL;
  db_error_t err = { 0 };
  int64_t value = -1;
  if (db_query (db, sql, NULL, 0, &res, &err) && db_res_step (res, &err))
    value = db_res_col_i64 (res, 0, &err);
  if (res)
    db_res_finalize (res);
  if (err.code != 0)
    fprintf (stderr, "query failed (%d): %s\n", err.code, err.message);
  return value;
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
  g_db = db_open (&config, &err);
  if (!g_db)
    {
      fprintf (stderr, "database open failed (%d): %s\n", err.code,
               err.message);
      return 1;
    }

  if (!exec_sql (g_db,
                 "CREATE TEMP TABLE combat_test_ships (ship_id integer "
                 "PRIMARY KEY, destroyed boolean NOT NULL DEFAULT FALSE);")
      || !exec_sql (g_db,
                    "INSERT INTO combat_test_ships(ship_id) VALUES (9001);")
      || !exec_sql (g_db,
                    "CREATE TEMP TABLE podded_status (player_id integer "
                    "PRIMARY KEY, status text NOT NULL DEFAULT 'active', "
                    "podded_count_today integer NOT NULL DEFAULT 0, "
                    "podded_last_reset integer);"))
    {
      db_close (g_db);
      return 1;
    }

  client_ctx_t ctx = { 0 };
  ctx.player_id = 7001;
  ctx.sector_id = 3;
  int rc = fedspace_enforce_no_aggression_hard (&ctx, 9001, 7001,
                                                 "test.attack");
  if (rc != -ERR_DB_EXEC_FAILED
      || scalar_i64 (g_db,
          "SELECT CASE WHEN destroyed THEN 1 ELSE 0 END FROM combat_test_ships "
          "WHERE ship_id=9001;") != 0
      || scalar_i64 (g_db,
          "SELECT COUNT(*) FROM podded_status WHERE player_id=7001;") != 0
      || g_notice_count != 0)
    {
      fputs ("FAIL: pod update error did not roll back ship destruction, "
             "podded row, and notice\n", stderr);
      db_close (g_db);
      return 1;
    }

  if (!exec_sql (g_db,
                 "ALTER TABLE podded_status ADD COLUMN big_sleep_until integer;"))
    {
      db_close (g_db);
      return 1;
    }
  rc = fedspace_enforce_no_aggression_hard (&ctx, 9001, 7001,
                                             "test.attack");
  if (rc != 1
      || scalar_i64 (g_db,
          "SELECT CASE WHEN destroyed THEN 1 ELSE 0 END FROM combat_test_ships "
          "WHERE ship_id=9001;") != 1
      || scalar_i64 (g_db,
          "SELECT COUNT(*) FROM podded_status WHERE player_id=7001 "
          "AND status='active' AND big_sleep_until IS NULL;") != 1
      || g_notice_count != 1)
    {
      fputs ("FAIL: successful enforcement did not commit destruction and "
             "podded status together\n", stderr);
      db_close (g_db);
      return 1;
    }

  db_close (g_db);
  puts ("PASS: PostgreSQL Captain Z failure rolls back ship and pod state; "
        "successful enforcement commits both before notification");
  return 0;
}
