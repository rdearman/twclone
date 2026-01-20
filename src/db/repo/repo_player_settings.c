#define TW_DB_INTERNAL 1
#include "db_int.h"
/* src/db_player_settings.c */
#include "repo_player_settings.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <limits.h>
#include "db/repo/repo_database.h"
#include "game_db.h"
#include "db/db_api.h"
#include "db/sql_driver.h"
#include "common.h"


int
db_player_settings_init (db_t *db)
{
  (void)db; return 0;
}

static int
h_upsert(db_t *db, const char *sql_upd, const char *sql_ins, const db_bind_t *params, int n_params) {
    db_error_t err;
    int64_t rows = 0;
    if (!db_tx_begin(db, DB_TX_IMMEDIATE, &err)) return -1;

    if (!db_exec_rows_affected(db, sql_upd, params, n_params, &rows, &err)) {
        db_tx_rollback(db, NULL);
        return -1;
    }

    if (rows == 0) {
        if (!db_exec(db, sql_ins, params, n_params, &err)) {
            db_tx_rollback(db, NULL);
            return -1;
        }
    }

    if (!db_tx_commit(db, &err)) return -1;
    return 0;
}


int
db_subscribe_upsert (db_t *db,
                     int64_t pid,
                     const char *topic,
                     const char *filter,
                     int locked)
{
  if (!db) return -1;
  const char *q_upd = "UPDATE subscriptions SET filter_json = {3}, locked = {4}, enabled = {5} "
                      "WHERE player_id = {1} AND event_type = {2};";
  const char *q_ins = "INSERT INTO subscriptions (player_id, event_type, filter_json, locked, enabled) "
                      "VALUES ({1}, {2}, {3}, {4}, {5});";
  
  char sql_upd[512]; sql_build(db, q_upd, sql_upd, sizeof(sql_upd));
  char sql_ins[512]; sql_build(db, q_ins, sql_ins, sizeof(sql_ins));

  db_bind_t params[] = {
      db_bind_i64(pid),
      db_bind_text(topic),
      db_bind_text(filter),
      db_bind_i32(locked),
      db_bind_bool(true)
  };

  return h_upsert(db, sql_upd, sql_ins, params, 5);
}


int
db_subscribe_disable (db_t *db, int64_t pid, const char *topic, int *locked_out)
{
  db_error_t err; if (!db)
    {
      return -1;
    }
  char sql[512];
  sql_build (db,
             "UPDATE subscriptions SET enabled = {3} WHERE player_id = {1} AND event_type = {2} AND locked = FALSE;",
             sql, sizeof (sql));
  if (!db_exec (db,
                sql,
                (db_bind_t[]){db_bind_i64 (pid), db_bind_text (topic), db_bind_bool(false)},
                3,
                &err))
    {
      return -1;
    }
  if (locked_out)
    {
      *locked_out = 0;
    }
  return 0;
}


int
db_bookmark_upsert (db_t *db, int64_t pid, const char *name, int64_t sid)
{
  if (!db) return -1;
  const char *q_upd = "UPDATE player_bookmarks SET sector_id = {3} WHERE player_id = {1} AND name = {2};";
  const char *q_ins = "INSERT INTO player_bookmarks (player_id, name, sector_id) VALUES ({1}, {2}, {3});";

  char sql_upd[512]; sql_build(db, q_upd, sql_upd, sizeof(sql_upd));
  char sql_ins[512]; sql_build(db, q_ins, sql_ins, sizeof(sql_ins));

  db_bind_t params[] = {
      db_bind_i64(pid),
      db_bind_text(name),
      db_bind_i64(sid)
  };

  return h_upsert(db, sql_upd, sql_ins, params, 3);
}


int
db_bookmark_list (db_t *db, int64_t pid, db_res_t **it)
{
  db_error_t err; if (!db || !it)
    {
      return -1;
    }
  char sql[512];
  sql_build (db,
             "SELECT name, sector_id FROM player_bookmarks WHERE player_id = {1} ORDER BY name;",
             sql, sizeof (sql));
  if (!db_query (db,
                 sql,
                 (db_bind_t[]){db_bind_i64 (pid)},
                 1,
                 it,
                 &err))
    {
      return -1;
    }
  return 0;
}


int
db_bookmark_remove (db_t *db, int64_t pid, const char *name)
{
  db_error_t err; if (!db)
    {
      return -1;
    }
  char sql[512];
  sql_build (db,
             "DELETE FROM player_bookmarks WHERE player_id = {1} AND name = {2};",
             sql, sizeof (sql));
  if (!db_exec (db,
                sql,
                (db_bind_t[]){db_bind_i64 (pid), db_bind_text (name)},
                2,
                &err))
    {
      return -1;
    }
  return 0;
}


int
db_avoid_add (db_t *db, int64_t pid, int64_t sid)
{
  db_error_t err;
  if (!db) return -1;

  if (!db_tx_begin(db, DB_TX_IMMEDIATE, &err)) return -1;

  /* Check if already exists */
  char sql_chk[512];
  sql_build(db, "SELECT 1 FROM player_avoid WHERE player_id = {1} AND sector_id = {2} LIMIT 1;", sql_chk, sizeof(sql_chk));
  db_res_t *res = NULL;
  bool exists = false;
  if (db_query(db, sql_chk, (db_bind_t[]){db_bind_i64(pid), db_bind_i64(sid)}, 2, &res, &err)) {
      exists = db_res_step(res, &err);
      db_res_finalize(res);
  } else {
      db_tx_rollback(db, NULL);
      return -1;
  }

  if (!exists) {
      char sql_ins[512];
      sql_build(db, "INSERT INTO player_avoid (player_id, sector_id) VALUES ({1}, {2});", sql_ins, sizeof(sql_ins));
      if (!db_exec(db, sql_ins, (db_bind_t[]){db_bind_i64(pid), db_bind_i64(sid)}, 2, &err)) {
          db_tx_rollback(db, NULL);
          return -1;
      }
  }

  if (!db_tx_commit(db, &err)) return -1;
  return 0;
}


int
db_avoid_list (db_t *db, int64_t pid, db_res_t **it)
{
  db_error_t err; if (!db || !it)
    {
      return -1;
    }
  char sql[512];
  sql_build (db,
             "SELECT sector_id FROM player_avoid WHERE player_id = {1} ORDER BY sector_id;",
             sql, sizeof (sql));
  if (!db_query (db,
                 sql,
                 (db_bind_t[]){db_bind_i64 (pid)},
                 1,
                 it,
                 &err))
    {
      return -1;
    }
  return 0;
}


int
db_avoid_remove (db_t *db, int64_t pid, int64_t sid)
{
  db_error_t err; if (!db)
    {
      return -1;
    }
  char sql[512];
  sql_build (db,
             "DELETE FROM player_avoid WHERE player_id = {1} AND sector_id = {2};",
             sql, sizeof (sql));
  if (!db_exec (db,
                sql,
                (db_bind_t[]){db_bind_i64 (pid), db_bind_i64 (sid)},
                2,
                &err))
    {
      return -1;
    }
  return 0;
}


int
db_note_set (db_t *db,
             int64_t pid,
             const char *scope,
             const char *key,
             const char *note)
{
  if (!db) return -1;
  const char *q_upd = "UPDATE player_notes SET note = {4} WHERE player_id = {1} AND scope = {2} AND key = {3};";
  const char *q_ins = "INSERT INTO player_notes (player_id, scope, key, note) VALUES ({1}, {2}, {3}, {4});";

  char sql_upd[512]; sql_build(db, q_upd, sql_upd, sizeof(sql_upd));
  char sql_ins[512]; sql_build(db, q_ins, sql_ins, sizeof(sql_ins));

  db_bind_t params[] = {
      db_bind_i64(pid),
      db_bind_text(scope),
      db_bind_text(key),
      db_bind_text(note)
  };

  return h_upsert(db, sql_upd, sql_ins, params, 4);
}


int
db_note_delete (db_t *db, int64_t pid, const char *scope, const char *key)
{
  db_error_t err; if (!db)
    {
      return -1;
    }
  char sql[512];
  sql_build (db,
             "DELETE FROM player_notes WHERE player_id = {1} AND scope = {2} AND key = {3};",
             sql, sizeof (sql));
  if (!db_exec (db,
                sql,
                (db_bind_t[]){db_bind_i64 (pid), db_bind_text (scope),
                              db_bind_text (key)},
                3,
                &err))
    {
      return -1;
    }
  return 0;
}


int
db_note_list (db_t *db, int64_t pid, const char *scope, db_res_t **it)
{
  db_error_t err; if (!db || !it)
    {
      return -1;
    }
  char sql_tmpl[512];
  if (scope)
    {
      strncpy(sql_tmpl, "SELECT scope, key, note FROM player_notes WHERE player_id = {1} AND scope = {2} ORDER BY key;", sizeof(sql_tmpl));
    }
  else
    {
      strncpy(sql_tmpl, "SELECT scope, key, note FROM player_notes WHERE player_id = {1} ORDER BY scope, key;", sizeof(sql_tmpl));
    }
  char sql[512];
  sql_build (db, sql_tmpl, sql, sizeof (sql));
  db_bind_t params[2]; params[0] = db_bind_i64 (pid); if (scope)
    {
      params[1] = db_bind_text (scope);
    }
  if (!db_query (db, sql, params, scope ? 2 : 1, it, &err))
    {
      return -1;
    }
  return 0;
}


int
db_for_each_subscriber (db_t *db, const char *event, player_id_cb cb, void *arg)
{
  db_res_t *res = NULL; db_error_t err; if (!db || !event || !cb)
    {
      return -1;
    }
  char sql[512];
  sql_build (db,
             "SELECT DISTINCT player_id FROM subscriptions WHERE event_type = {1} AND enabled = {2};",
             sql, sizeof (sql));
  if (db_query (db,
                sql,
                (db_bind_t[]){db_bind_text (event), db_bind_bool(true)},
                2,
                &res,
                &err))
    {
      while (db_res_step (res, &err))
        {
          if (cb (db_res_col_i32 (res, 0, &err), arg) != 0)
            {
              break;
            }
        }
      db_res_finalize (res); return 0;
    }
  return -1;
}


int
db_prefs_get_all (db_t *db, int64_t pid, db_res_t **it)
{
  db_error_t err; if (!db || !it)
    {
      return -1;
    }
  char sql[512];
  sql_build (db,
             "SELECT key, value FROM player_prefs WHERE player_prefs_id = {1};",
             sql, sizeof (sql));
  if (!db_query (db,
                 sql,
                 (db_bind_t[]){db_bind_i64 (pid)},
                 1,
                 it,
                 &err))
    {
      return -1;
    }
  return 0;
}


int
db_prefs_get_one (db_t *db, int64_t pid, const char *key, char **out)
{
  db_res_t *res = NULL; db_error_t err; if (!db || !key || !out)
    {
      return -1;
    }
  char sql[512];
  sql_build (db,
             "SELECT value FROM player_prefs WHERE player_prefs_id = {1} AND key = {2} LIMIT 1;",
             sql, sizeof (sql));
  if (db_query (db,
                sql,
                (db_bind_t[]){db_bind_i64 (pid), db_bind_text (key)},
                2,
                &res,
                &err))
    {
      if (db_res_step (res, &err))
        {
          *out = strdup (db_res_col_text (res, 0, &err) ?: "");
        }
      db_res_finalize (res); return 0;
    }
  return -1;
}


int
db_prefs_set_one (db_t *db,
                  int64_t pid,
                  const char *key,
                  pref_type t,
                  const char *val)
{
  if (!db) return -1;
  const char *q_upd = "UPDATE player_prefs SET value = {3} WHERE player_prefs_id = {1} AND key = {2};";
  const char *q_ins = "INSERT INTO player_prefs (player_prefs_id, key, type, value) VALUES ({1}, {2}, 'string', {3});";

  char sql_upd[512]; sql_build(db, q_upd, sql_upd, sizeof(sql_upd));
  char sql_ins[512]; sql_build(db, q_ins, sql_ins, sizeof(sql_ins));

  db_bind_t params[] = {
      db_bind_i64(pid),
      db_bind_text(key),
      db_bind_text(val)
  };

  (void)t;
  return h_upsert(db, sql_upd, sql_ins, params, 3);
}


int
db_get_player_pref_int (db_t *db, int pid, const char *key, int def)
{
  char *s = NULL; if (db_prefs_get_one (db, pid, key, &s) == 0 && s)
    {
      int v = atoi (s); free (s); return v;
    }
  return def;
}


int
db_get_player_pref_string (db_t *db,
                           int pid,
                           const char *key,
                           const char *def,
                           char *buf,
                           size_t sz)
{
  char *s = NULL; if (db_prefs_get_one (db, pid, key, &s) == 0 && s)
    {
      strncpy (buf, s, sz - 1); buf[sz - 1] = '\0'; free (s); return 0;
    }
  if (def)
    {
      strncpy (buf, def, sz - 1); buf[sz - 1] = '\0';
    }
  return 0;
}