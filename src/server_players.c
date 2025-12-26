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
                               json_integer (db_res_col_int (res, 1, &err)));
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
                                 json_integer (db_res_col_int (res, 0, &err)));
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
}


void
cmd_nav_avoid_remove (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id <= 0)
    {
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
}


void
cmd_nav_avoid_list (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id <= 0)
    {
      return;
    }
  json_t *items = avoid_as_array (ctx->player_id);
  json_t *resp = json_object ();


  json_object_set_new (resp, "items", items);
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
    "SELECT p.name, p.credits, t.turns_remaining, p.sector, p.ship, "
    "p.alignment, p.experience, cm.corp_id "
    "FROM players p "
    "LEFT JOIN turns t ON t.player = p.id "
    "LEFT JOIN corp_members cm ON cm.player_id = p.id "
    "WHERE p.id = $1";

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

