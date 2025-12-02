#include "db_player_settings.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "database.h"           // db_get_handle()
#include "db_player_settings.h" // our prototypes
static sqlite3 *g_db_ps = NULL;
static bool
player_is_sysop (sqlite3 *db, int player_id)
{
  bool is_sysop = false;
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2 (db,
                          "SELECT COALESCE(type,2), COALESCE(flags,0) FROM players WHERE id=?1",
                          -1,
                          &st,
                          NULL) == SQLITE_OK)
    {
      sqlite3_bind_int (st, 1, player_id);
      if (sqlite3_step (st) == SQLITE_ROW)
        {
          int type = sqlite3_column_int (st, 0);
          int flags = sqlite3_column_int (st, 1);
          if (type == 1 || (flags & 0x1))
            {
              is_sysop = true;  /* adjust rule if needed */
            }
        }
    }
  if (st)
    {
      sqlite3_finalize (st);
    }
  return is_sysop;
}


/* Prepared statements (kept simple; one-shot prepare each call to avoid lifetime headaches) */
static int
prep (sqlite3 *db, sqlite3_stmt **st, const char *sql)
{
  return sqlite3_prepare_v2 (db, sql, -1, st, NULL);
}


int
db_player_settings_init (sqlite3 *db)
{
  g_db_ps = db;
  return (g_db_ps ? 0 : -1);
}


/* ---------- Prefs ---------- */
/* Upsert a single preference (typed) */
int
db_prefs_set_one (int64_t pid, const char *key, pref_type t,
                  const char *value)
{
  if (!key || !value)
    {
      return SQLITE_MISUSE;
    }
  /* map enum to on-disk textual type */
  const char *type_str =
    (t == PT_BOOL) ? "bool" :
    (t == PT_INT) ? "int" :
    (t == PT_STRING) ? "string" : (t == PT_JSON) ? "json" : "string";
  static const char *SQL =
    "INSERT INTO player_prefs(player_id,key,type,value,updated_at) "
    "VALUES(?1,?2,?3,?4,strftime('%s','now')) "
    "ON CONFLICT(player_id,key) DO UPDATE SET "
    "  type=excluded.type,"
    "  value=excluded.value," "  updated_at=excluded.updated_at;";
  sqlite3_stmt *st = NULL;
  if (prep (g_db_ps, &st, SQL) != SQLITE_OK)
    {
      return -1;
    }
  sqlite3_bind_int64 (st, 1, pid);
  sqlite3_bind_text (st, 2, key, -1, SQLITE_STATIC);
  sqlite3_bind_text (st, 3, type_str, -1, SQLITE_STATIC);
  sqlite3_bind_text (st, 4, value, -1, SQLITE_TRANSIENT);
  int rc = sqlite3_step (st);
  sqlite3_finalize (st);
  return (rc == SQLITE_DONE) ? 0 : -1;
}


int
db_prefs_get_one (int64_t player_id, const char *key, char **out_value)
{
  if (out_value)
    {
      *out_value = NULL;
    }
  if (!key || !out_value)
    {
      return SQLITE_MISUSE;
    }
  sqlite3_stmt *st = NULL;
  if (prep (g_db_ps,
            &st,
            "SELECT value FROM player_prefs WHERE player_id=?1 AND key=?2 LIMIT 1;")
      != SQLITE_OK)
    {
      return -1;
    }
  sqlite3_bind_int64 (st, 1, player_id);
  sqlite3_bind_text (st, 2, key, -1, SQLITE_STATIC);
  int rc = sqlite3_step (st);
  if (rc == SQLITE_ROW)
    {
      const unsigned char *txt = sqlite3_column_text (st, 0);
      if (txt)
        {
          *out_value = strdup ((const char *) txt);     // caller frees
        }
      rc = SQLITE_OK;
    }
  else if (rc == SQLITE_DONE)
    {
      rc = SQLITE_OK;           // not found → *out_value stays NULL
    }
  sqlite3_finalize (st);
  return rc;
}


int
db_prefs_get_all (int64_t pid, sqlite3_stmt **it)
{
  static const char *SQL =
    "SELECT key, type, value FROM player_prefs WHERE player_id=?1;";
  if (prep (g_db_ps, it, SQL) != SQLITE_OK)
    {
      return -1;
    }
  sqlite3_bind_int64 (*it, 1, pid);
  return 0;
}


int
db_get_player_pref_int (int player_id, const char *key, int default_value)
{
  char *value_str = NULL;
  if (db_prefs_get_one (player_id, key, &value_str) != 0 || !value_str)
    {
      return default_value;
    }
  int value = atoi (value_str);
  free (value_str);
  return value;
}


int
db_get_player_pref_string (int player_id, const char *key,
                           const char *default_value, char *out_buffer,
                           size_t buffer_size)
{
  char *value_str = NULL;
  if (db_prefs_get_one (player_id, key, &value_str) != 0 || !value_str)
    {
      if (default_value)
        {
          strncpy (out_buffer, default_value, buffer_size - 1);
          out_buffer[buffer_size - 1] = '\0';
        }
      else
        {
          out_buffer[0] = '\0';
        }
      return 0;
    }
  strncpy (out_buffer, value_str, buffer_size - 1);
  out_buffer[buffer_size - 1] = '\0';
  free (value_str);
  return 0;
}


static const char *
type_to_s (pref_type t)
{
  switch (t)
    {
      case PT_BOOL:
        return "bool";
      case PT_INT:
        return "int";
      case PT_STRING:
        return "string";
      case PT_JSON:
        return "json";
    }
  return "string";
}


/* ---------- Subscriptions ---------- */
int
db_subscribe_upsert (int64_t pid, const char *topic, const char *filter_json,
                     int locked)
{
  static const char *SQL =
    "INSERT INTO subscriptions(player_id,event_type,delivery,filter_json,locked,enabled)"
    "VALUES(?1,?2,'push',?3,?4,1) "
    "ON CONFLICT(player_id,event_type) DO UPDATE SET "
    " enabled=1, locked=MAX(subscriptions.locked,excluded.locked), filter_json=excluded.filter_json;";
  sqlite3_stmt *st = NULL;
  if (prep (g_db_ps, &st, SQL) != SQLITE_OK)
    {
      return -1;
    }
  sqlite3_bind_int64 (st, 1, pid);
  sqlite3_bind_text (st, 2, topic, -1, SQLITE_TRANSIENT);
  if (filter_json)
    {
      sqlite3_bind_text (st, 3, filter_json, -1, SQLITE_TRANSIENT);
    }
  else
    {
      sqlite3_bind_null (st, 3);
    }
  sqlite3_bind_int (st, 4, locked ? 1 : 0);
  int rc = sqlite3_step (st);
  sqlite3_finalize (st);
  return (rc == SQLITE_DONE) ? 0 : -1;
}


int
db_subscribe_disable (int64_t pid, const char *topic, int *was_locked)
{
  static const char *SQL_LOCK =
    "SELECT locked FROM subscriptions WHERE player_id=?1 AND event_type=?2;";
  static const char *SQL_DISABLE =
    "UPDATE subscriptions SET enabled=0 WHERE player_id=?1 AND event_type=?2 AND locked=0;";
  sqlite3_stmt *st = NULL;
  if (prep (g_db_ps, &st, SQL_LOCK) != SQLITE_OK)
    {
      return -1;
    }
  sqlite3_bind_int64 (st, 1, pid);
  sqlite3_bind_text (st, 2, topic, -1, SQLITE_TRANSIENT);
  int rc = sqlite3_step (st);
  int locked = 0;
  if (rc == SQLITE_ROW)
    {
      locked = sqlite3_column_int (st, 0);
    }
  sqlite3_finalize (st);
  if (was_locked)
    {
      *was_locked = locked;
    }
  if (locked)
    {
      return +1;                /* signal LOCKED to caller */
    }
  if (prep (g_db_ps, &st, SQL_DISABLE) != SQLITE_OK)
    {
      return -1;
    }
  sqlite3_bind_int64 (st, 1, pid);
  sqlite3_bind_text (st, 2, topic, -1, SQLITE_TRANSIENT);
  rc = sqlite3_step (st);
  sqlite3_finalize (st);
  return (rc == SQLITE_DONE) ? 0 : -1;
}


int
db_subscribe_list (int64_t pid, sqlite3_stmt **it)
{
  static const char *SQL =
    "SELECT event_type AS topic, locked, enabled, delivery, filter_json "
    "FROM subscriptions WHERE player_id=?1 ORDER BY locked DESC, event_type;";
  if (prep (g_db_ps, it, SQL) != SQLITE_OK)
    {
      return -1;
    }
  sqlite3_bind_int64 (*it, 1, pid);
  return 0;
}


/* ---------- Bookmarks ---------- */
int
db_bookmark_upsert (int64_t pid, const char *name, int64_t sector_id)
{
  static const char *SQL =
    "INSERT INTO player_bookmarks(player_id,name,sector_id,updated_at)"
    "VALUES(?1,?2,?3,strftime('%s','now')) "
    "ON CONFLICT(player_id,name) DO UPDATE SET sector_id=excluded.sector_id,updated_at=excluded.updated_at;";
  sqlite3_stmt *st = NULL;
  if (prep (g_db_ps, &st, SQL) != SQLITE_OK)
    {
      return -1;
    }
  sqlite3_bind_int64 (st, 1, pid);
  sqlite3_bind_text (st, 2, name, -1, SQLITE_TRANSIENT);
  sqlite3_bind_int64 (st, 3, sector_id);
  int rc = sqlite3_step (st);
  sqlite3_finalize (st);
  return (rc == SQLITE_DONE) ? 0 : -1;
}


int
db_bookmark_remove (int64_t pid, const char *name)
{
  static const char *SQL =
    "DELETE FROM player_bookmarks WHERE player_id=?1 AND name=?2;";
  sqlite3_stmt *st = NULL;
  if (prep (g_db_ps, &st, SQL) != SQLITE_OK)
    {
      return -1;
    }
  sqlite3_bind_int64 (st, 1, pid);
  sqlite3_bind_text (st, 2, name, -1, SQLITE_TRANSIENT);
  int rc = sqlite3_step (st);
  sqlite3_finalize (st);
  return (rc == SQLITE_DONE) ? 0 : -1;
}


int
db_bookmark_list (int64_t pid, sqlite3_stmt **it)
{
  static const char *SQL =
    "SELECT name, sector_id FROM player_bookmarks WHERE player_id=?1 ORDER BY name;";
  if (prep (g_db_ps, it, SQL) != SQLITE_OK)
    {
      return -1;
    }
  sqlite3_bind_int64 (*it, 1, pid);
  return 0;
}


/* ---------- Avoid ---------- */
int
db_avoid_add (int64_t pid, int64_t sector_id)
{
  static const char *SQL =
    "INSERT OR IGNORE INTO player_avoid(player_id,sector_id) VALUES(?1,?2);";
  sqlite3_stmt *st = NULL;
  if (prep (g_db_ps, &st, SQL) != SQLITE_OK)
    {
      return -1;
    }
  sqlite3_bind_int64 (st, 1, pid);
  sqlite3_bind_int64 (st, 2, sector_id);
  int rc = sqlite3_step (st);
  sqlite3_finalize (st);
  return (rc == SQLITE_DONE) ? 0 : -1;
}


int
db_avoid_remove (int64_t pid, int64_t sector_id)
{
  static const char *SQL =
    "DELETE FROM player_avoid WHERE player_id=?1 AND sector_id=?2;";
  sqlite3_stmt *st = NULL;
  if (prep (g_db_ps, &st, SQL) != SQLITE_OK)
    {
      return -1;
    }
  sqlite3_bind_int64 (st, 1, pid);
  sqlite3_bind_int64 (st, 2, sector_id);
  int rc = sqlite3_step (st);
  sqlite3_finalize (st);
  return (rc == SQLITE_DONE) ? 0 : -1;
}


int
db_avoid_list (int64_t pid, sqlite3_stmt **it)
{
  static const char *SQL =
    "SELECT sector_id FROM player_avoid WHERE player_id=?1 ORDER BY sector_id;";
  if (prep (g_db_ps, it, SQL) != SQLITE_OK)
    {
      return -1;
    }
  sqlite3_bind_int64 (*it, 1, pid);
  return 0;
}


/* ---------- Notes ---------- */
int
db_note_set (int64_t pid, const char *scope, const char *key,
             const char *note)
{
  static const char *SQL =
    "INSERT INTO player_notes(player_id,scope,key,note,updated_at)"
    "VALUES(?1,?2,?3,?4,strftime('%s','now')) "
    "ON CONFLICT(player_id,scope,key) DO UPDATE SET note=excluded.note,updated_at=excluded.updated_at;";
  sqlite3_stmt *st = NULL;
  if (prep (g_db_ps, &st, SQL) != SQLITE_OK)
    {
      return -1;
    }
  sqlite3_bind_int64 (st, 1, pid);
  sqlite3_bind_text (st, 2, scope, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text (st, 3, key, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text (st, 4, note, -1, SQLITE_TRANSIENT);
  int rc = sqlite3_step (st);
  sqlite3_finalize (st);
  return (rc == SQLITE_DONE) ? 0 : -1;
}


int
db_note_delete (int64_t pid, const char *scope, const char *key)
{
  static const char *SQL =
    "DELETE FROM player_notes WHERE player_id=?1 AND scope=?2 AND key=?3;";
  sqlite3_stmt *st = NULL;
  if (prep (g_db_ps, &st, SQL) != SQLITE_OK)
    {
      return -1;
    }
  sqlite3_bind_int64 (st, 1, pid);
  sqlite3_bind_text (st, 2, scope, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text (st, 3, key, -1, SQLITE_TRANSIENT);
  int rc = sqlite3_step (st);
  sqlite3_finalize (st);
  return (rc == SQLITE_DONE) ? 0 : -1;
}


int
db_note_list (int64_t pid, const char *scope, sqlite3_stmt **it)
{
  if (scope)
    {
      static const char *SQL =
        "SELECT scope,key,note FROM player_notes WHERE player_id=?1 AND scope=?2 ORDER BY key;";
      if (prep (g_db_ps, it, SQL) != SQLITE_OK)
        {
          return -1;
        }
      sqlite3_bind_int64 (*it, 1, pid);
      sqlite3_bind_text (*it, 2, scope, -1, SQLITE_TRANSIENT);
    }
  else
    {
      static const char *SQL =
        "SELECT scope,key,note FROM player_notes WHERE player_id=?1 ORDER BY scope,key;";
      if (prep (g_db_ps, it, SQL) != SQLITE_OK)
        {
          return -1;
        }
      sqlite3_bind_int64 (*it, 1, pid);
    }
  return 0;
}


int
db_for_each_subscriber (sqlite3 *db, const char *event_type, player_id_cb cb,
                        void *arg)
{
  if (!db || !event_type || !cb)
    {
      return -1;
    }
  // Compute domain and domain.* for the SQL fast-path
  // We support: exact match and single-level wildcard "domain.*"
  const char *dot = strchr (event_type, '.');
  char domain[64] = { 0 };
  char domain_star[70] = { 0 };
  if (dot && (size_t) (dot - event_type) < sizeof (domain))
    {
      size_t n = (size_t) (dot - event_type);
      memcpy (domain, event_type, n);
      domain[n] = '\0';
      snprintf (domain_star, sizeof (domain_star), "%s.*", domain);
    }
  else
    {
      // No dot? Then only exact match makes sense; domain_star becomes "*"
      strncpy (domain_star, "*", sizeof (domain_star) - 1);
    }
  // We purposely avoid LIKE/GLOB for predictable perf and semantics.
  // Unique(player_id, event_type) is already enforced; DISTINCT is for safety.
  const char *SQL =
    "SELECT DISTINCT player_id "
    "FROM subscriptions "
    "WHERE enabled=1 " "  AND (event_type = ?1 OR event_type = ?2)";
  sqlite3_stmt *st = NULL;
  int rc = sqlite3_prepare_v2 (db, SQL, -1, &st, NULL);
  if (rc != SQLITE_OK)
    {
      return -1;
    }
  sqlite3_bind_text (st, 1, event_type, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text (st, 2, domain_star, -1, SQLITE_TRANSIENT);
  while ((rc = sqlite3_step (st)) == SQLITE_ROW)
    {
      int player_id = sqlite3_column_int (st, 0);
      if (cb (player_id, arg) != 0)
        {                       // cb can stop early by returning non-zero
          break;
        }
    }
  int ok = (rc == SQLITE_ROW || rc == SQLITE_DONE) ? 0 : -1;
  sqlite3_finalize (st);
  return ok;
}

