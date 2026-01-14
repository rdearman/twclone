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


int
db_subscribe_upsert (db_t *db,
                     int64_t pid,
                     const char *topic,
                     const char *filter,
                     int locked)
{
  db_error_t err; if (!db)
    {
      return -1;
    }
  char sql[512];
  sql_build (db,
             "INSERT INTO subscriptions (player_id, event_type, filter_json, locked, enabled) VALUES ({1}, {2}, {3}, {4}, 1) "
             "ON CONFLICT(player_id, event_type) DO UPDATE SET filter_json = {3}, locked = {4}, enabled = 1;",
             sql, sizeof (sql));
  if (!db_exec (db,
                sql,
                (db_bind_t[]){db_bind_i64 (pid), db_bind_text (topic),
                              db_bind_text (filter), db_bind_i32 (locked)},
                4,
                &err))
    {
      return -1;
    }
  return 0;
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
             "UPDATE subscriptions SET enabled = 0 WHERE player_id = {1} AND event_type = {2} AND locked = 0;",
             sql, sizeof (sql));
  if (!db_exec (db,
                sql,
                (db_bind_t[]){db_bind_i64 (pid), db_bind_text (topic)},
                2,
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
  db_error_t err; if (!db)
    {
      return -1;
    }
  char sql[512];
  sql_build (db,
             "INSERT INTO player_bookmarks (player_id, name, sector_id) VALUES ({1}, {2}, {3}) "
             "ON CONFLICT(player_id, name) DO UPDATE SET sector_id = {3};",
             sql, sizeof (sql));
  if (!db_exec (db, sql,
                (db_bind_t[]){db_bind_i64 (pid), db_bind_text (name),
                              db_bind_i64 (sid)}, 3, &err))
    {
      return -1;
    }
  return 0;
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
  if (!db)
    {
      return -1;
    }
  const char *conflict_clause = sql_insert_ignore_clause(db);
  if (!conflict_clause)
    {
      return -1;
    }
  char sql_buf[256];
  snprintf(sql_buf, sizeof(sql_buf),
      "INSERT INTO player_avoid (player_id, sector_id) VALUES ({1}, {2}) %s;",
      conflict_clause);
  char sql[512];
  sql_build (db, sql_buf, sql, sizeof (sql));
  if (!db_exec (db, sql,
                (db_bind_t[]){db_bind_i64 (pid), db_bind_i64 (sid)},
                2,
                &err))
    {
      return -1;
    }
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
  db_error_t err; if (!db)
    {
      return -1;
    }
  char sql[512];
  sql_build (db,
             "INSERT INTO player_notes (player_id, scope, key, note) VALUES ({1}, {2}, {3}, {4}) "
             "ON CONFLICT(player_id, scope, key) DO UPDATE SET note = {4};",
             sql, sizeof (sql));
  if (!db_exec (db, sql,
                (db_bind_t[]){db_bind_i64 (pid), db_bind_text (scope),
                              db_bind_text (key), db_bind_text (note)}, 4,
                &err))
    {
      return -1;
    }
  return 0;
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
             "SELECT DISTINCT player_id FROM subscriptions WHERE event_type = {1} AND enabled = 1;",
             sql, sizeof (sql));
  if (db_query (db,
                sql,
                (db_bind_t[]){db_bind_text (event)},
                1,
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
             "SELECT key, value FROM player_prefs WHERE player_id = {1};",
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
             "SELECT value FROM player_prefs WHERE player_id = {1} AND key = {2} LIMIT 1;",
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
  db_error_t err; if (!db || !key)
    {
      return -1;
    }
  char sql[512];
  sql_build (db,
             "INSERT INTO player_prefs (player_id, key, value) VALUES ({1}, {2}, {3}) ON CONFLICT(player_id, key) DO UPDATE SET value = {3};",
             sql, sizeof (sql));
  if (!db_exec (db, sql,
                (db_bind_t[]){db_bind_i64 (pid), db_bind_text (key),
                              db_bind_text (val)}, 3, &err))
    {
      return -1;
    }
  (void)t; return 0;
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