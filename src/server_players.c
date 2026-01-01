/* src/server_players.c */
#include <stdio.h>
#include <stdint.h>
#include <jansson.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <time.h>
#include <errno.h>

/* local includes */
#include "server_players.h"
#include "database.h"
#include "game_db.h"
#include "database_cmd.h"
#include "errors.h"
#include "config.h"
#include "server_cmds.h"
#include "server_rules.h"
#include "db_player_settings.h"
#include "server_envelope.h"
#include "server_auth.h"
#include "server_log.h"
#include "common.h"
#include "server_ships.h"
#include "server_loop.h"
#include "server_bank.h"
#include "db/db_api.h"

/* Constants */
enum { MAX_BOOKMARKS = 64, MAX_BM_NAME = 64 };
enum { MAX_AVOIDS = 64 };

static const char *DEFAULT_PLAYER_FIELDS[] = {
  "id", "username", "credits", "sector", "faction", NULL
};

/* Externs */
extern client_node_t *g_clients;
extern pthread_mutex_t g_clients_mu;


int
cmd_player_list_online (client_ctx_t *ctx, json_t *root)
{
  send_response_error (ctx,
                       root,
                       ERR_NOT_IMPLEMENTED,
                       "Not implemented: cmd_player_list_online");
  return 0;
}


int
cmd_player_rankings (client_ctx_t *ctx, json_t *root)
{
  send_response_error (ctx,
                       root,
                       ERR_NOT_IMPLEMENTED,
                       "Not implemented: cmd_player_rankings");
  return 0;
}


/* ==================================================================== */
/* STATIC HELPER DEFINITIONS                                            */
/* ==================================================================== */


static int
is_ascii_printable (const char *s)
{
  if (!s)
    {
      return 0;
    }
  for (const unsigned char *p = (const unsigned char *)s; *p; ++p)
    {
      if (*p < 0x20 || *p > 0x7E)
        {
          return 0;
        }
    }
  return 1;
}


static int
len_leq (const char *s, size_t m)
{
  return s && strlen (s) <= m;
}


static int
is_valid_key (const char *s, size_t max)
{
  if (!s)
    {
      return 0;
    }
  size_t n = strlen (s);


  if (n == 0 || n > max)
    {
      return 0;
    }
  for (size_t i = 0; i < n; ++i)
    {
      char c = s[i];


      if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' ||
            c == '_' || c == '-'))
        {
          return 0;
        }
    }
  return 1;
}


/* --- Local DB Helpers (replacing missing database_cmd functions) --- */


static int
h_db_prefs_set_one (int player_id,
                    const char *key,
                    const char *type,
                    const char *value)
{
  db_t *db = game_db_get_handle ();
  const char *sql =
    "INSERT INTO player_prefs (player_id, key, type, value) VALUES ($1, $2, $3, $4) "
    "ON CONFLICT (player_id, key) DO UPDATE SET type = EXCLUDED.type, value = EXCLUDED.value";
  db_bind_t p[] = { db_bind_i32 (player_id), db_bind_text (key),
                    db_bind_text (type), db_bind_text (value) };
  db_error_t err;
  return db_exec (db, sql, p, 4, &err) ? 0 : -1;
}


static int
h_db_bookmark_upsert (int player_id, const char *name, int sector_id)
{
  db_t *db = game_db_get_handle ();
  const char *sql =
    "INSERT INTO player_bookmarks (player_id, name, sector_id) VALUES ($1, $2, $3) "
    "ON CONFLICT (player_id, name) DO UPDATE SET sector_id = EXCLUDED.sector_id";
  db_bind_t p[] = { db_bind_i32 (player_id), db_bind_text (name),
                    db_bind_i32 (sector_id) };
  db_error_t err;
  return db_exec (db, sql, p, 3, &err) ? 0 : -1;
}


static int
h_db_bookmark_remove (int player_id, const char *name)
{
  db_t *db = game_db_get_handle ();
  const char *sql =
    "DELETE FROM player_bookmarks WHERE player_id = $1 AND name = $2";
  db_bind_t p[] = { db_bind_i32 (player_id), db_bind_text (name) };
  db_error_t err;
  return db_exec (db, sql, p, 2, &err) ? 0 : -1;
}


static int
h_db_avoid_add (int player_id, int sector_id)
{
  db_t *db = game_db_get_handle ();
  const char *sql =
    "INSERT INTO player_avoid (player_id, sector_id) VALUES ($1, $2) ON CONFLICT DO NOTHING";
  db_bind_t p[] = { db_bind_i32 (player_id), db_bind_i32 (sector_id) };
  db_error_t err;
  return db_exec (db, sql, p, 2, &err) ? 0 : -1;
}


static int
h_db_avoid_remove (int player_id, int sector_id)
{
  db_t *db = game_db_get_handle ();
  const char *sql =
    "DELETE FROM player_avoid WHERE player_id = $1 AND sector_id = $2";
  db_bind_t p[] = { db_bind_i32 (player_id), db_bind_i32 (sector_id) };
  db_error_t err;
  return db_exec (db, sql, p, 2, &err) ? 0 : -1;
}


static int
h_db_subscribe_disable (int player_id, const char *topic)
{
  db_t *db = game_db_get_handle ();
  const char *sql =
    "UPDATE player_subscriptions SET enabled = 0 WHERE player_id = $1 AND topic = $2";
  db_bind_t p[] = { db_bind_i32 (player_id), db_bind_text (topic) };
  db_error_t err;
  return db_exec (db, sql, p, 2, &err) ? 0 : -1;
}


static int
h_db_subscribe_upsert (int player_id,
                       const char *topic,
                       const char *delivery,
                       const char *filter)
{
  db_t *db = game_db_get_handle ();
  const char *sql =
    "INSERT INTO player_subscriptions (player_id, topic, enabled, delivery, filter) VALUES ($1, $2, 1, $3, $4) "
    "ON CONFLICT (player_id, topic) DO UPDATE SET enabled = 1, delivery = EXCLUDED.delivery, filter = EXCLUDED.filter";
  db_bind_t p[] = {
    db_bind_i32 (player_id), db_bind_text (topic),
    db_bind_text (delivery ? delivery : "push"),
    db_bind_text (filter ? filter : "")
  };
  db_error_t err;
  return db_exec (db, sql, p, 4, &err) ? 0 : -1;
}


/* --- Logic Helpers --- */


static json_t *
prefs_as_array (int64_t pid)
{
  db_t *db = game_db_get_handle ();
  const char *sql =
    "SELECT key, type, value FROM player_prefs WHERE player_id = $1";
  db_bind_t params[] = { db_bind_i64 (pid) };
  db_res_t *res = NULL;
  db_error_t err;

  json_t *arr = json_array ();
  if (db_query (db, sql, params, 1, &res, &err))
    {
      while (db_res_step (res, &err))
        {
          const char *k = db_res_col_text (res, 0, &err);
          const char *t = db_res_col_text (res, 1, &err);
          const char *v = db_res_col_text (res, 2, &err);
          json_t *pref_obj = json_object ();


          json_object_set_new (pref_obj, "key", json_string (k ? k : ""));
          json_object_set_new (pref_obj, "type",
                               json_string (t ? t : "string"));
          json_object_set_new (pref_obj, "value", json_string (v ? v : ""));
          json_array_append_new (arr, pref_obj);
        }
      db_res_finalize (res);
    }
  return arr;
}


static json_t *
bookmarks_as_array (int64_t pid)
{
  db_t *db = game_db_get_handle ();
  const char *sql =
    "SELECT name, sector_id FROM player_bookmarks WHERE player_id=$1 ORDER BY name";
  db_bind_t params[] = { db_bind_i64 (pid) };
  db_res_t *res = NULL;
  db_error_t err;
  json_t *arr = json_array ();
  if (db_query (db, sql, params, 1, &res, &err))
    {
      while (db_res_step (res, &err))
        {
          const char *name = db_res_col_text (res, 0, &err);
          json_t *bm_obj = json_object ();


          json_object_set_new (bm_obj, "name", json_string (name ? name : ""));
          json_object_set_new (bm_obj, "sector_id",
                               json_integer (db_res_col_i32 (res, 1, &err)));
          json_array_append_new (arr, bm_obj);
        }
      db_res_finalize (res);
    }
  return arr;
}


static json_t *
avoid_as_array (int64_t pid)
{
  db_t *db = game_db_get_handle ();
  const char *sql =
    "SELECT sector_id FROM player_avoid WHERE player_id=$1 ORDER BY sector_id";
  db_bind_t params[] = { db_bind_i64 (pid) };
  db_res_t *res = NULL;
  db_error_t err;
  json_t *arr = json_array ();
  if (db_query (db, sql, params, 1, &res, &err))
    {
      while (db_res_step (res, &err))
        {
          json_array_append_new (arr,
                                 json_integer (db_res_col_i32 (res, 0, &err)));
        }
      db_res_finalize (res);
    }
  return arr;
}


static json_t *
subscriptions_as_array (int64_t pid)
{
  db_t *db = game_db_get_handle ();
  const char *sql =
    "SELECT topic, locked, enabled, delivery, filter FROM player_subscriptions WHERE player_id=$1";
  db_bind_t params[] = { db_bind_i64 (pid) };
  db_res_t *res = NULL;
  db_error_t err;
  json_t *arr = json_array ();
  if (db_query (db, sql, params, 1, &res, &err))
    {
      while (db_res_step (res, &err))
        {
          json_t *one = json_object ();


          json_object_set_new (one,
                               "topic",
                               json_string (db_res_col_text (res,
                                                             0,
                                                             &err)));
          json_object_set_new (one, "locked", json_boolean (db_res_col_int (res,
                                                                            1,
                                                                            &err)));
          json_object_set_new (one, "enabled",
                               json_boolean (db_res_col_int (res,
                                                             2,
                                                             &err)));
          json_object_set_new (one, "delivery",
                               json_string (db_res_col_text (res,
                                                             3,
                                                             &err)));
          const char *f = db_res_col_text (res, 4, &err);


          if (f)
            {
              json_object_set_new (one, "filter", json_string (f));
            }
          json_array_append_new (arr, one);
        }
      db_res_finalize (res);
    }
  return arr;
}


static int
h_set_prefs (client_ctx_t *ctx, json_t *prefs)
{
  if (!json_is_object (prefs))
    {
      return -1;
    }
  const char *key; json_t *val;


  json_object_foreach (prefs, key, val) {
    if (!is_valid_key (key, 64))
      {
        continue;
      }
    char buf[512] = {0};
    const char *sval = "";


    if (json_is_string (val))
      {
        sval = json_string_value (val);
      }
    else if (json_is_integer (val))
      {
        snprintf (buf, sizeof(buf), "%lld",
                  (long long)json_integer_value (val)); sval = buf;
      }
    else if (json_is_boolean (val))
      {
        sval = json_is_true (val) ? "1" : "0";
      }
    else
      {
        continue;
      }
    h_db_prefs_set_one (ctx->player_id, key, "string", sval);
  }
  return 0;
}


static int
h_set_bookmarks (client_ctx_t *ctx, json_t *list)
{
  if (!json_is_array (list))
    {
      return -1;
    }

  /* 1. Clear old (Simplified: Removing items present in list first to avoid dupe errors or just upsert)
     Original logic seemed to remove existing from JSON. We'll stick to Upsert logic. */
  size_t idx; json_t *val;


  json_array_foreach (list, idx, val) {
    const char *name = json_string_value (json_object_get (val, "name"));
    int sid = json_integer_value (json_object_get (val, "sector_id"));
    if (name && sid > 0)
      {
        h_db_bookmark_upsert (ctx->player_id, name, sid);
      }
  }
  return 0;
}


static int
h_set_avoids (client_ctx_t *ctx, json_t *list)
{
  if (!json_is_array (list))
    {
      return -1;
    }
  size_t idx; json_t *val;


  json_array_foreach (list, idx, val) {
    if (json_is_integer (val))
      {
        h_db_avoid_add (ctx->player_id, json_integer_value (val));
      }
  }
  return 0;
}


static int
h_set_subscriptions (client_ctx_t *ctx, json_t *list)
{
  if (!json_is_array (list))
    {
      return -1;
    }
  size_t idx; json_t *val;


  json_array_foreach (list, idx, val) {
    if (json_is_string (val))
      {
        h_db_subscribe_upsert (ctx->player_id,
                               json_string_value (val),
                               NULL,
                               NULL);
      }
    else if (json_is_object (val))
      {
        const char *topic = json_string_value (json_object_get (val, "topic"));


        if (topic)
          {
            h_db_subscribe_upsert (ctx->player_id, topic, NULL, NULL);
          }
      }
  }
  return 0;
}


/* ==================================================================== */


/* COMMAND HANDLERS (FULL IMPLEMENTATION)                               */


/* ==================================================================== */


int
cmd_player_get_settings (client_ctx_t *ctx, json_t *root)
{
  if (!ctx || ctx->player_id <= 0)
    {
      send_response_error (ctx, root, ERR_NOT_AUTHENTICATED, "Auth required");
      return 0;
    }
  json_t *data = json_object ();


  json_object_set_new (data, "prefs", prefs_as_array (ctx->player_id));
  json_object_set_new (data, "bookmarks", bookmarks_as_array (ctx->player_id));
  json_object_set_new (data, "avoid", avoid_as_array (ctx->player_id));
  json_object_set_new (data, "subscriptions",
                       subscriptions_as_array (ctx->player_id));
  send_response_ok_take (ctx, root, "player.settings_v1", &data);
  return 0;
}


int
cmd_player_set_settings (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id <= 0)
    {
      send_response_error (ctx, root, ERR_NOT_AUTHENTICATED, "Auth required");
      return 0;
    }
  json_t *data = json_object_get (root,
                                  "data");


  if (!json_is_object (data))
    {
      send_response_error (ctx, root, ERR_INVALID_SCHEMA,
                           "data object required"); return 0;
    }

  if (json_object_get (data, "prefs"))
    {
      h_set_prefs (ctx, json_object_get (data, "prefs"));
    }
  if (json_object_get (data, "bookmarks"))
    {
      h_set_bookmarks (ctx, json_object_get (data, "bookmarks"));
    }
  if (json_object_get (data, "avoid"))
    {
      h_set_avoids (ctx, json_object_get (data, "avoid"));
    }
  if (json_object_get (data, "subscriptions"))
    {
      h_set_subscriptions (ctx, json_object_get (data, "subscriptions"));
    }

  return cmd_player_get_settings (ctx, root);
}


int
cmd_player_get_prefs (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id <= 0)
    {
      send_response_error (ctx, root, ERR_NOT_AUTHENTICATED, "Auth required");
      return 0;
    }
  json_t *data = json_object ();


  json_object_set_new (data, "prefs", prefs_as_array (ctx->player_id));
  send_response_ok_take (ctx, root, "player.prefs_v1", &data);
  return 0;
}


int
cmd_player_set_prefs (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id <= 0)
    {
      send_response_error (ctx, root, ERR_NOT_AUTHENTICATED, "Auth required");
      return 0;
    }
  json_t *data = json_object_get (root, "data");


  if (h_set_prefs (ctx, data) == 0)
    {
      json_t *resp = json_object ();


      json_object_set_new (resp, "ok", json_true ());
      send_response_ok_take (ctx, root, "player.prefs.updated", &resp);
      return 0;
    }
  return -1;
}


int
cmd_player_get_bookmarks (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id <= 0)
    {
      return 0;
    }
  json_t *out = json_object ();


  json_object_set_new (out, "bookmarks", bookmarks_as_array (ctx->player_id));
  send_response_ok_take (ctx, root, "player.bookmarks", &out);
  return 0;
}


int
cmd_player_set_bookmarks (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id <= 0)
    {
      send_response_error (ctx, root, ERR_NOT_AUTHENTICATED, "Auth required");
      return 0;
    }
  json_t *data = json_object_get (root, "data");


  h_set_bookmarks (ctx, json_object_get (data, "bookmarks"));
  return cmd_player_get_bookmarks (ctx, root);
}


void
cmd_nav_bookmark_add (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id <= 0)
    {
      send_response_error (ctx, root, ERR_NOT_AUTHENTICATED, "Auth required");
      return;
    }
  json_t *data = json_object_get (root, "data");
  const char *name = json_string_value (json_object_get (data, "name"));
  int sid = json_integer_value (json_object_get (data, "sector_id"));


  if (name && sid > 0)
    {
      h_db_bookmark_upsert (ctx->player_id, name, sid);
      json_t *resp = json_object ();


      json_object_set_new (resp, "name", json_string (name));
      json_object_set_new (resp, "sector_id", json_integer (sid));
      send_response_ok_take (ctx, root, "nav.bookmark.added", &resp);
    }
  else
    {
      send_response_error (ctx, root, ERR_INVALID_ARG,
                           "Invalid name or sector");
    }
}


void
cmd_nav_bookmark_remove (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id <= 0)
    {
      return;
    }
  const char *name = json_string_value (json_object_get (json_object_get (root,
                                                                          "data"),
                                                         "name"));


  if (name)
    {
      h_db_bookmark_remove (ctx->player_id, name);
      json_t *resp = json_object ();


      json_object_set_new (resp, "name", json_string (name));
      send_response_ok_take (ctx, root, "nav.bookmark.removed", &resp);
    }
}


void
cmd_nav_bookmark_list (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id <= 0)
    {
      return;
    }
  json_t *items = bookmarks_as_array (ctx->player_id);
  json_t *resp = json_object ();


  json_object_set_new (resp, "items", items);
  send_response_ok_take (ctx, root, "nav.bookmark.list", &resp);
}


int
cmd_player_get_avoids (client_ctx_t *ctx, json_t *root)
{
  json_t *out = json_object ();
  json_object_set_new (out, "avoid", avoid_as_array (ctx->player_id));
  send_response_ok_take (ctx, root, "avoids", &out);
  return 0;
}


int
cmd_player_set_avoids (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id <= 0)
    {
      send_response_error (ctx, root, ERR_NOT_AUTHENTICATED, "Auth required");
      return 0;
    }
  h_set_avoids (ctx, json_object_get (json_object_get (root, "data"), "avoid"));
  return cmd_player_get_avoids (ctx, root);
}


void
cmd_nav_avoid_add (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id <= 0)
    {
      send_response_error (ctx, root, ERR_NOT_AUTHENTICATED, "Auth required");
      return;
    }
  int sid = json_integer_value (json_object_get (json_object_get (root, "data"),
                                                 "sector_id"));


  if (sid > 0)
    {
      h_db_avoid_add (ctx->player_id, sid);
      json_t *resp = json_object ();


      json_object_set_new (resp, "sector_id", json_integer (sid));
      send_response_ok_take (ctx, root, "nav.avoid.added", &resp);
    }
  else
    {
      send_response_error (ctx, root, ERR_BAD_REQUEST, "invalid sector");
    }
}


void
cmd_nav_avoid_remove (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id <= 0)
    {
      send_response_error (ctx, root, ERR_NOT_AUTHENTICATED, "Auth required");
      return;
    }
  int sid = json_integer_value (json_object_get (json_object_get (root, "data"),
                                                 "sector_id"));


  if (sid > 0)
    {
      h_db_avoid_remove (ctx->player_id, sid);
      json_t *resp = json_object ();


      json_object_set_new (resp, "sector_id", json_integer (sid));
      send_response_ok_take (ctx, root, "nav.avoid.removed", &resp);
    }
  else
    {
      send_response_error (ctx, root, ERR_BAD_REQUEST, "invalid sector");
    }
}


void
cmd_nav_avoid_list (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id <= 0)
    {
      send_response_error (ctx, root, ERR_NOT_AUTHENTICATED, "Auth required");
      return;
    }
  json_t *items = avoid_as_array (ctx->player_id);
  json_t *resp = json_object ();


  json_object_set_new (resp, "items", items ? items : json_array ());
  send_response_ok_take (ctx, root, "nav.avoid.list", &resp);
}


int
cmd_player_get_topics (client_ctx_t *ctx, json_t *root)
{
  json_t *out = json_object ();
  json_object_set_new (out, "topics", subscriptions_as_array (ctx->player_id));
  send_response_ok_take (ctx, root, "player.subscriptions", &out);
  return 0;
}


int
cmd_player_set_topics (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id <= 0)
    {
      return 0;
    }
  json_t *data = json_object_get (root, "data");
  json_t *topics = json_object_get (data, "topics");


  if (!topics)
    {
      topics = json_object_get (data, "subscriptions");
    }
  h_set_subscriptions (ctx, topics);
  return cmd_player_get_topics (ctx, root);
}


int
cmd_player_get_notes (client_ctx_t *ctx, json_t *root)
{
  json_t *out = json_object ();
  json_object_set_new (out, "notes", json_array ()); /* Placeholder from original */
  send_response_ok_take (ctx, root, "player.notes", &out);
  return 0;
}


/* Ported cmd_player_my_info */
int
cmd_player_my_info (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id <= 0)
    {
      send_response_refused_steal (ctx,
                                   root,
                                   ERR_NOT_AUTHENTICATED,
                                   "Not authenticated",
                                   NULL);
      return 0;
    }
  db_t *db = game_db_get_handle ();
  const char *sql =
    "SELECT p.name, p.credits, t.turns_remaining, p.sector_id, p.ship_id, "
    "p.alignment, p.experience, cm.corporation_id "
    "FROM players p "
    "LEFT JOIN turns t ON t.player_id = p.player_id "
    "LEFT JOIN corp_members cm ON cm.player_id = p.player_id "
    "WHERE p.player_id = $1";

  db_bind_t params[] = { db_bind_i32 (ctx->player_id) };
  db_res_t *res = NULL;
  db_error_t err;


  if (db_query (db, sql, params, 1, &res, &err))
    {
      if (db_res_step (res, &err))
        {
          const char *name = db_res_col_text (res, 0, &err);
          long long credits = db_res_col_i64 (res, 1, &err);
          int turns = db_res_col_int (res, 2, &err);
          int sector = db_res_col_int (res, 3, &err);
          int ship_id = db_res_col_int (res, 4, &err);
          int align = db_res_col_int (res, 5, &err);
          long long exp = db_res_col_i64 (res, 6, &err);
          int corp_id = db_res_col_int (res, 7, &err);

          json_t *player_obj = json_object ();


          json_object_set_new (player_obj, "id", json_integer (ctx->player_id));
          json_object_set_new (player_obj, "username",
                               json_string (name ? name : "Unknown"));

          char credits_str[64];


          snprintf (credits_str, sizeof(credits_str), "%lld.00", credits);
          json_object_set_new (player_obj, "credits",
                               json_string (credits_str));
          json_object_set_new (player_obj, "turns_remaining",
                               json_integer (turns));
          json_object_set_new (player_obj, "sector", json_integer (sector));
          json_object_set_new (player_obj, "ship_id", json_integer (ship_id));
          json_object_set_new (player_obj, "corp_id", json_integer (corp_id));
          json_object_set_new (player_obj, "alignment", json_integer (align));
          json_object_set_new (player_obj, "experience", json_integer (exp));

          /* Reuse h_player_build_title_payload (make sure it's non-static or copied) */
          /* We will use a simplified version here if the helper isn't available */
          json_t *pinfo = json_object ();


          json_object_set_new (pinfo, "player", player_obj);
          send_response_ok_take (ctx, root, "player.info", &pinfo);
        }
      db_res_finalize (res);
    }
  return 0;
}


int
cmd_player_set_trade_account_preference (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id <= 0)
    {
      return 0;
    }
  json_t *data = json_object_get (root, "data");
  json_t *pref = json_object_get (data, "prefer_bank");


  if (json_is_boolean (pref))
    {
      h_db_prefs_set_one (ctx->player_id,
                          "trade.prefer_bank",
                          "bool",
                          json_is_true (pref) ? "1" : "0");
      json_t *resp = json_object ();


      json_object_set_new (resp, "ok", json_true ());
      send_response_ok_take (ctx, root, "player.prefs.updated", &resp);
    }
  return 0;
}


int
h_player_build_title_payload (db_t *db, int player_id, json_t **out_json)
{
  if (!db || player_id <= 0 || !out_json)
    {
      return -1;
    }

  const char *sql =
    "SELECT alignment, experience, commission_id FROM players WHERE player_id = $1;";
  db_res_t *res = NULL;
  db_error_t err;


  db_error_clear (&err);

  db_bind_t params[] = { db_bind_i32 (player_id) };


  if (!db_query (db, sql, params, 1, &res, &err))
    {
      return -1;
    }

  int align = 0, comm_id = 0;
  long long exp = 0;


  if (!db_res_step (res, &err))
    {
      db_res_finalize (res);
      return -1;
    }

  align = (int)db_res_col_i32 (res, 0, &err);
  exp = db_res_col_i64 (res, 1, &err);
  comm_id = (int)db_res_col_i32 (res, 2, &err);
  db_res_finalize (res);

  char *band_code = NULL, *band_name = NULL;
  int is_good = 0, is_evil = 0, can_iss = 0, can_rob = 0;


  db_alignment_band_for_value (db, align, NULL, &band_code, &band_name,
                               &is_good, &is_evil, &can_iss, &can_rob);

  int det_comm_id = 0, comm_is_evil = 0;
  char *comm_title = NULL;


  db_commission_for_player (db,
                            is_evil,
                            exp,
                            &det_comm_id,
                            &comm_title,
                            &comm_is_evil);

  if (comm_id != det_comm_id)
    {
      db_player_update_commission (db, player_id);
      comm_id = det_comm_id;
    }

  json_t *obj = json_object ();


  json_object_set_new (obj, "title",
                       json_string (comm_title ? comm_title : "Unknown"));
  json_object_set_new (obj, "commission", json_integer (comm_id));
  json_object_set_new (obj, "alignment", json_integer (align));
  json_object_set_new (obj, "experience", json_integer (exp));

  json_t *band = json_object ();


  json_object_set_new (band, "code",
                       json_string (band_code ? band_code : "UNKNOWN"));
  json_object_set_new (band, "name",
                       json_string (band_name ? band_name : "Unknown"));
  json_object_set_new (band, "is_good", json_boolean (is_good));
  json_object_set_new (band, "is_evil", json_boolean (is_evil));
  json_object_set_new (band, "can_buy_iss", json_boolean (can_iss));
  json_object_set_new (band, "can_rob_ports", json_boolean (can_rob));
  json_object_set_new (obj, "alignment_band", band);

  if (band_code)
    {
      free (band_code);
    }
  if (band_name)
    {
      free (band_name);
    }
  if (comm_title)
    {
      free (comm_title);
    }

  *out_json = obj;
  return 0;
}


int
h_send_message_to_player (db_t *db,
                          int recipient_id,
                          int sender_id,
                          const char *subject,
                          const char *message)
{
  if (!db || !subject || !message)
    {
      return 1;
    }

  const char *sql =
    "INSERT INTO mail (sender_id, recipient_id, subject, body) VALUES ($1, $2, $3, $4);";
  db_bind_t params[] = {
    db_bind_i32 (sender_id),
    db_bind_i32 (recipient_id),
    db_bind_text (subject),
    db_bind_text (message)
  };

  db_error_t err;


  db_error_clear (&err);

  if (db_exec (db, sql, params, 4, &err))
    {
      return 0;
    }
  return 1;
}


int
h_get_player_bank_account_id (db_t *db, int player_id)
{
  if (!db || player_id <= 0)
    {
      return -1;
    }

  int account_id = -1;
  int rc = h_get_account_id_unlocked (db, "player", player_id, &account_id);


  if (rc != 0)
    {
      return -1;
    }
  return account_id;
}


int


h_get_cargo_space_free (db_t *db, int player_id, int *free_out)


{
  if (!db || !free_out)


    {
      return -1;
    }


  const char *sql =


    "SELECT (COALESCE(s.holds, 0) - COALESCE(s.colonists + s.equipment + s.organics + s.ore + s.slaves + s.weapons + s.drugs, 0)) "
    "FROM players p "
    "JOIN ships s ON s.ship_id = p.ship_id "
    "WHERE p.player_id = $1;";


  db_res_t *res = NULL;


  db_error_t err;


  db_error_clear (&err);


  db_bind_t params[] = { db_bind_i32 (player_id) };


  if (!db_query (db, sql, params, 1, &res, &err))


    {
      return -1;
    }


  int total = 0;


  if (db_res_step (res, &err))


    {
      total = (int)db_res_col_i32 (res, 0, &err);
    }


  else


    {
      db_res_finalize (res);


      return -1;
    }


  db_res_finalize (res);


  if (total < 0)


    {
      total = 0;
    }


  *free_out = total;


  return 0;
}


int


h_player_is_npc (db_t *db, int player_id)


{
  if (!db)


    {
      return 0;
    }


  const char *sql = "SELECT is_npc FROM players WHERE player_id = $1;";


  db_res_t *res = NULL;


  db_error_t err;


  db_error_clear (&err);


  db_bind_t params[] = { db_bind_i32 (player_id) };


  if (!db_query (db, sql, params, 1, &res, &err))


    {
      return 0;
    }


  int is_npc = 0;


  if (db_res_step (res, &err))


    {
      is_npc = (int) db_res_col_i32 (res, 0, &err);
    }


  db_res_finalize (res);


  return is_npc;
}


int


spawn_starter_ship (db_t *db, int player_id, int sector_id)


{
  if (!db)


    {
      return -1;
    }


  // Get ship type


  const char *sql_type =


    "SELECT id, initialholds, maxfighters, maxshields FROM shiptypes WHERE name = $1;";


  db_res_t *res = NULL;


  db_error_t err;


  db_error_clear (&err);


  db_bind_t type_params[] = { db_bind_text ("Scout Marauder") };


  if (!db_query (db, sql_type, type_params, 1, &res, &err))


    {
      return -1;
    }


  int ship_type_id = 0, holds = 0, fighters = 0, shields = 0;


  if (db_res_step (res, &err))


    {
      ship_type_id = (int)db_res_col_i32 (res, 0, &err);


      holds = (int)db_res_col_i32 (res, 1, &err);


      fighters = (int)db_res_col_i32 (res, 2, &err);


      shields = (int)db_res_col_i32 (res, 3, &err);
    }


  db_res_finalize (res);


  if (ship_type_id == 0)


    {
      return -1;
    }


  // Insert ship with RETURNING to get ID


  const char *sql_ins =


    "INSERT INTO ships (name, type_id, holds, fighters, shields, sector) VALUES ($1, $2, $3, $4, $5, $6) RETURNING id;";


  db_bind_t ins_params[] = {
    db_bind_text ("Starter Ship"),


    db_bind_i32 (ship_type_id),


    db_bind_i32 (holds),


    db_bind_i32 (fighters),


    db_bind_i32 (shields),


    db_bind_i32 (sector_id)
  };


  res = NULL;


  db_error_clear (&err);


  if (!db_query (db, sql_ins, ins_params, 6, &res, &err))


    {
      return -1;
    }


  int ship_id = 0;


  if (db_res_step (res, &err))


    {
      ship_id = (int)db_res_col_i32 (res, 0, &err);
    }


  db_res_finalize (res);


  if (ship_id == 0)


    {
      return -1;
    }


  // Set ownership


  const char *sql_own =


    "INSERT INTO ship_ownership (ship_id, player_id, role_id, is_primary) VALUES ($1, $2, 1, 1);";


  db_bind_t own_params[] = {
    db_bind_i32 (ship_id),


    db_bind_i32 (player_id)
  };


  db_error_clear (&err);


  db_exec (db, sql_own, own_params, 2, &err);


  // Update player


  const char *sql_upd =


    "UPDATE players SET ship_id = $1, sector_id = $2 WHERE player_id = $3;";


  db_bind_t upd_params[] = {
    db_bind_i32 (ship_id),


    db_bind_i32 (sector_id),


    db_bind_i32 (player_id)
  };


  db_error_clear (&err);


  db_exec (db, sql_upd, upd_params, 3, &err);


  // Update podded status


  const char *sql_pod =


    "UPDATE podded_status SET status = $1 WHERE player_id = $2;";


  db_bind_t pod_params[] = {
    db_bind_text ("alive"),


    db_bind_i32 (player_id)
  };


  db_error_clear (&err);


  db_exec (db, sql_pod, pod_params, 2, &err);


  return 0;
}


int


h_get_player_petty_cash (db_t *db, int player_id, long long *bal)


{
  if (!db || player_id <= 0 || !bal)


    {
      return -1;
    }


  const char *sql = "SELECT credits FROM players WHERE player_id = $1;";


  db_res_t *res = NULL;


  db_error_t err;


  db_error_clear (&err);


  db_bind_t params[] = { db_bind_i32 (player_id) };


  if (!db_query (db, sql, params, 1, &res, &err))


    {
      return -1;
    }


  int rc = -1;


  if (db_res_step (res, &err))


    {
      *bal = db_res_col_i64 (res, 0, &err);


      rc = 0;
    }


  db_res_finalize (res);


  return rc;
}


int


h_deduct_player_petty_cash_unlocked (db_t *db,


                                     int player_id,


                                     long long amount,


                                     long long *new_balance_out)


{
  if (!db || amount < 0)


    {
      return -1;
    }


  if (new_balance_out)


    {
      *new_balance_out = 0;
    }


  const char *sql =


    "UPDATE players SET credits = credits - $1 WHERE player_id = $2 AND credits >= $1 RETURNING credits;";


  db_bind_t params[] = {
    db_bind_i64 (amount),


    db_bind_i32 (player_id)
  };


  db_res_t *res = NULL;


  db_error_t err;


  db_error_clear (&err);


  if (!db_query (db, sql, params, 2, &res, &err))


    {
      return -1;
    }


  if (db_res_step (res, &err))


    {
      if (new_balance_out)


        {
          *new_balance_out = db_res_col_i64 (res, 0, &err);
        }


      db_res_finalize (res);


      return 0;
    }


  db_res_finalize (res);


  return -1;
}


int


h_add_player_petty_cash (db_t *db,


                         int player_id,


                         long long amount,


                         long long *new_balance_out)


{
  if (!db || amount < 0)


    {
      return -1;
    }


  if (new_balance_out)


    {
      *new_balance_out = 0;
    }


  const char *sql =


    "UPDATE players SET credits = credits + $1 WHERE player_id = $2 RETURNING credits;";


  db_bind_t params[] = {
    db_bind_i64 (amount),


    db_bind_i32 (player_id)
  };


  db_res_t *res = NULL;


  db_error_t err;


  db_error_clear (&err);


  if (!db_query (db, sql, params, 2, &res, &err))


    {
      return -1;
    }


  if (db_res_step (res, &err))


    {
      if (new_balance_out)


        {
          *new_balance_out = db_res_col_i64 (res, 0, &err);
        }


      db_res_finalize (res);


      return 0;
    }


  db_res_finalize (res);


  return -1;
}


TurnConsumeResult


h_consume_player_turn (db_t *db, client_ctx_t *ctx, int turns)


{
  if (!db || !ctx || turns <= 0)


    {
      return TURN_CONSUME_ERROR_INVALID_AMOUNT;
    }


  int player_id = ctx->player_id;


  // Check if player has enough turns


  const char *sql_check =


    "SELECT turns_remaining FROM turns WHERE player_id = $1;";


  db_res_t *res = NULL;


  db_error_t err;


  db_error_clear (&err);


  db_bind_t check_params[] = { db_bind_i32 (player_id) };


  if (!db_query (db, sql_check, check_params, 1, &res, &err))


    {
      LOGE ("h_consume_player_turn: db_query failed for player_id=%d: %s",
            player_id,
            err.message);


      return TURN_CONSUME_ERROR_DB_FAIL;
    }


  int turns_remaining = 0;


  if (db_res_step (res, &err))


    {
      turns_remaining = (int)db_res_col_i32 (res, 0, &err);
    }


  db_res_finalize (res);


  if (turns_remaining < turns)


    {
      return TURN_CONSUME_ERROR_NO_TURNS;
    }


  // Update turns with EXTRACT(EPOCH FROM NOW()) for PostgreSQL compatibility


  const char *sql_update =


    "UPDATE turns SET turns_remaining = turns_remaining - $1, last_update = EXTRACT(EPOCH FROM NOW())::int WHERE player_id = $2 AND turns_remaining >= $1 "
    ";";


  db_bind_t upd_params[] = {
    db_bind_i32 (turns),


    db_bind_i32 (player_id)
  };


  db_error_clear (&err);


  if (!db_exec (db, sql_update, upd_params, 2, &err))


    {
      return TURN_CONSUME_ERROR_DB_FAIL;
    }


  return TURN_CONSUME_SUCCESS;
}


int


handle_turn_consumption_error (client_ctx_t *ctx,


                               TurnConsumeResult res,


                               const char *cmd,


                               json_t *root,


                               json_t *meta)


{
  const char *reason_str = NULL;


  switch (res)


    {
      case TURN_CONSUME_ERROR_DB_FAIL:


        reason_str = "db_failure";


        break;


      case TURN_CONSUME_ERROR_PLAYER_NOT_FOUND:


        reason_str = "player_not_found";


        break;


      case TURN_CONSUME_ERROR_NO_TURNS:


        reason_str = "no_turns_remaining";


        break;


      case TURN_CONSUME_ERROR_INVALID_AMOUNT:


        reason_str = "invalid_amount";


        break;


      default:


        reason_str = "unknown_error";


        break;
    }


  json_t *meta_obj = meta ? json_copy (meta) : json_object ();


  if (meta_obj)


    {
      json_object_set_new (meta_obj, "reason", json_string (reason_str));


      json_object_set_new (meta_obj, "command",


                           json_string (cmd ? cmd : "unknown"));


      send_response_refused_steal (ctx,


                                   root,


                                   ERR_REF_NO_TURNS,


                                   "Insufficient turns.",


                                   NULL);


      json_decref (meta_obj);
    }


  return 0;
}


int


h_player_apply_progress (db_t *db,


                         int player_id,


                         long long delta_xp,


                         int delta_align,


                         const char *reason)


{
  if (!db || player_id <= 0)


    {
      return -1;
    }


  // Get current alignment and experience


  const char *sql_get =


    "SELECT alignment, experience FROM players WHERE player_id = $1;";


  db_res_t *res = NULL;


  db_error_t err;


  db_error_clear (&err);


  db_bind_t get_params[] = { db_bind_i32 (player_id) };


  if (!db_query (db, sql_get, get_params, 1, &res, &err))


    {
      return -1;
    }


  int cur_align = 0;


  long long cur_xp = 0;


  if (db_res_step (res, &err))


    {
      cur_align = (int)db_res_col_i32 (res, 0, &err);


      cur_xp = db_res_col_i64 (res, 1, &err);
    }


  else


    {
      db_res_finalize (res);


      return -1;
    }


  db_res_finalize (res);


  // Calculate new values


  long long new_xp = cur_xp + delta_xp;


  if (new_xp < 0)


    {
      new_xp = 0;
    }


  int new_align = cur_align + delta_align;


  if (new_align > 2000)


    {
      new_align = 2000;
    }


  if (new_align < -2000)


    {
      new_align = -2000;
    }


  // Update player


  const char *sql_upd =


    "UPDATE players SET experience = $1, alignment = $2 WHERE player_id = $3;";


  db_bind_t upd_params[] = {
    db_bind_i64 (new_xp),


    db_bind_i32 (new_align),


    db_bind_i32 (player_id)
  };


  db_error_clear (&err);


  if (!db_exec (db, sql_upd, upd_params, 3, &err))


    {
      return -1;
    }


  // Update commission (call the DB function)


  db_player_update_commission (db, player_id);


  LOGD ("Player %d progress updated. Reason: %s",


        player_id,


        reason ? reason : "N/A");


  return 0;
}


int


h_get_player_sector (db_t *db, int player_id)


{
  if (!db)


    {
      return 0;
    }


  const char *sql =
    "SELECT COALESCE(sector_id, 0) FROM players WHERE player_id = $1;";


  db_res_t *res = NULL;


  db_error_t err;


  db_error_clear (&err);


  db_bind_t params[] = { db_bind_i32 (player_id) };


  if (!db_query (db, sql, params, 1, &res, &err))


    {
      return 0;
    }


  int sector = 0;


  if (db_res_step (res, &err))


    {
      sector = (int)db_res_col_i32 (res, 0, &err);


      if (sector < 0)


        {
          sector = 0;
        }
    }


  db_res_finalize (res);


  return sector;
}


int


h_add_player_petty_cash_unlocked (db_t *db,


                                  int player_id,


                                  long long amount,


                                  long long *new_balance_out)


{
  if (!db || amount < 0)


    {
      return -1;
    }


  if (new_balance_out)


    {
      *new_balance_out = 0;
    }


  const char *sql =


    "UPDATE players SET credits = credits + $1 WHERE player_id = $2 RETURNING credits;";


  db_bind_t params[] = {
    db_bind_i64 (amount),


    db_bind_i32 (player_id)
  };


  db_res_t *res = NULL;


  db_error_t err;


  db_error_clear (&err);


  if (!db_query (db, sql, params, 2, &res, &err))


    {
      return -1;
    }


  if (db_res_step (res, &err))


    {
      if (new_balance_out)


        {
          *new_balance_out = db_res_col_i64 (res, 0, &err);
        }


      db_res_finalize (res);


      return 0;
    }


  db_res_finalize (res);


  return -1;
}


/* players.credits acts as petty cash */


int


h_player_petty_cash_add (db_t *db, int player_id, long long delta,


                         long long *new_balance_out)


{
  if (!db || player_id <= 0 || !new_balance_out)


    {
      return ERR_DB_MISUSE;
    }


  db_error_t err;


  db_error_clear (&err);


  /* Prevent negative balances (closest analogue to your old logic). */


  const char *sql =


    "UPDATE players "


    "SET credits = credits + $2 "


    "WHERE player_id = $1 AND (credits + $2) >= 0 "


    "RETURNING credits;";


  db_bind_t params[] = {
    db_bind_i32 ((int32_t) player_id),


    db_bind_i64 ((int64_t) delta)
  };


  db_res_t *res = NULL;


  if (!db_query (db,
                 sql,
                 params,
                 sizeof (params) / sizeof (params[0]),
                 &res,
                 &err))


    {
      return err.code ? err.code : ERR_DB_QUERY_FAILED;
    }


  long long new_bal = 0;


  bool have_row = db_res_step (res, &err);


  if (have_row && !err.code)


    {
      new_bal = (long long) db_res_col_i64 (res, 0, &err);
    }


  db_res_finalize (res);


  if (err.code)


    {
      return err.code;
    }


  if (!have_row)


    {
      /* Could be: player missing OR insufficient funds. Distinguish minimally. */


      db_error_clear (&err);


      const char *sql_exists =
        "SELECT 1 FROM players WHERE player_id =  LIMIT 1;";


      db_bind_t p2[] = { db_bind_i32 ((int32_t) player_id) };


      res = NULL;


      if (!db_query (db, sql_exists, p2, 1, &res, &err))


        {
          return err.code ? err.code : ERR_DB_QUERY_FAILED;
        }


      bool exists = db_res_step (res, &err);


      db_res_finalize (res);


      if (err.code)


        {
          return err.code;
        }


      return exists ? ERR_DB_CONSTRAINT : ERR_DB_NOT_FOUND;
    }


  *new_balance_out = new_bal;


  return 0;
}

