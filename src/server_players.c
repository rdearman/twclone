#include "server_players.h"
#include "server_envelope.h"
#include "database.h"
#include "jansson.h"
#include <string.h>
#include "server_players.h"
#include "database.h"		// play_login, user_create, db_player_info_json, db_player_get_sector, db_session_*
#include "errors.h"
#include "config.h"
#include <string.h>
#include "server_cmds.h"
#include "server_rules.h"
#include <sqlite3.h>
#include <stdlib.h>		/* for strtol */
#include "db_player_settings.h"
#include "server_envelope.h"
#include "server_auth.h"
#include "errors.h"



/* ---------- forward decls for settings section builders (stubs for now) ---------- */
static json_t *players_build_settings (client_ctx_t * ctx, json_t * req);
static json_t *players_get_prefs (client_ctx_t * ctx);
static json_t *players_get_subscriptions (client_ctx_t * ctx);
static json_t *players_list_bookmarks (client_ctx_t * ctx);
static json_t *players_list_avoid (client_ctx_t * ctx);
static json_t *players_list_notes (client_ctx_t * ctx, json_t * req);

/* ==================================================================== */
/*                  YOUR ORIGINAL INFO/ONLINE HANDLERS                   */
/* ==================================================================== */

int
cmd_player_my_info (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id <= 0)
    {
      send_enveloped_refused (ctx->fd, root, 1401, "Not authenticated", NULL);
      return 0;
    }

  json_t *pinfo = NULL;
  int prc = db_player_info_json (ctx->player_id, &pinfo);
  if (prc != SQLITE_OK || !pinfo)
    {
      send_enveloped_error (ctx->fd, root, 1503, "Database error");
      return 0;
    }

  send_enveloped_ok (ctx->fd, root, "player.info", pinfo);
  json_decref (pinfo);
  return 0;
}

int
cmd_player_list_online (client_ctx_t *ctx, json_t *root)
{
  /* Until a global connection registry exists, return current player only. */
  json_t *arr = json_array ();
  if (ctx->player_id > 0)
    {
      json_array_append_new (arr,
			     json_pack ("{s:i}", "player_id",
					ctx->player_id));
    }
  json_t *data = json_pack ("{s:o}", "players", arr);
  send_enveloped_ok (ctx->fd, root, "player.list_online", data);
  json_decref (data);
  return 0;
}

/* ==================================================================== */
/*                         SETTINGS AGGREGATE                            */
/* ==================================================================== */

int
cmd_player_set_settings (client_ctx_t *ctx, json_t *root)
{
  STUB_NIY (ctx, root, "player.set_settings");
}

int
cmd_player_get_settings (client_ctx_t *ctx, json_t *root)
{
  json_t *resp = players_build_settings (ctx, root);
  if (!resp)
    send_enveloped_error (ctx->fd, root, 500, "settings_build_failed");
  send_enveloped_ok (ctx->fd, root, "player.settings", resp);
  json_decref (resp);
  return 0;
}

/* ==================================================================== */
/*                             PREFS                                     */
/* ==================================================================== */


int
cmd_player_get_prefs (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id <= 0)
    {
      send_enveloped_refused (ctx->fd, root, 1401, "Not authenticated", NULL);
      return 0;
    }

  sqlite3_stmt *it = NULL;
  if (db_prefs_get_all (ctx->player_id, &it) != 0)
    {
      send_enveloped_error (ctx->fd, root, 1503, "Database error");
      return 0;
    }

  json_t *prefs = json_array ();
  while (sqlite3_step (it) == SQLITE_ROW)
    {
      const char *k = (const char *) sqlite3_column_text (it, 0);
      const char *t = (const char *) sqlite3_column_text (it, 1);
      const char *v = (const char *) sqlite3_column_text (it, 2);
      json_t *o = json_object ();
      json_object_set_new (o, "key", json_string (k ? k : ""));
      json_object_set_new (o, "type", json_string (t ? t : "string"));
      json_object_set_new (o, "value", json_string (v ? v : ""));
      json_array_append_new (prefs, o);
    }
  sqlite3_finalize (it);

  json_t *data = json_object ();
  json_object_set_new (data, "prefs", prefs);
  send_enveloped_ok (ctx->fd, root, "player.prefs_v1", data);
  return 0;
}



/* ==================================================================== */
/*                           SUBSCRIPTIONS                               */
/* ==================================================================== */

int
cmd_player_set_topics (client_ctx_t *ctx, json_t *root)
{
  STUB_NIY (ctx, root, "player.set_topics");
}

int
cmd_player_get_topics (client_ctx_t *ctx, json_t *root)
{
  json_t *topics = players_get_subscriptions (ctx);
  if (!topics)
    send_enveloped_error (ctx->fd, root, 500, "subs_load_failed");

  json_t *out = json_object ();
  json_object_set_new (out, "topics", topics);
  send_enveloped_ok (ctx->fd, root, "player.subscriptions", out);
  json_decref (out);
  return 0;
}

/* ==================================================================== */
/*                           BOOKMARKS                                   */
/* ==================================================================== */

int
cmd_player_set_bookmarks (client_ctx_t *ctx, json_t *root)
{
  STUB_NIY (ctx, root, "player.set_bookmarks");
}

int
cmd_player_get_bookmarks (client_ctx_t *ctx, json_t *root)
{
  json_t *bookmarks = players_list_bookmarks (ctx);
  if (!bookmarks)
    send_enveloped_error (ctx->fd, root, 500, "bookmarks_load_failed");
  json_t *out = json_object ();
  json_object_set_new (out, "bookmarks", bookmarks);
  send_enveloped_ok (ctx->fd, root, "player.bookmarks", out);
  json_decref (out);
  return 0;
}

/* ==================================================================== */
/*                              AVOIDS                                   */
/* ==================================================================== */

int
cmd_player_set_avoids (client_ctx_t *ctx, json_t *root)
{
  STUB_NIY (ctx, root, "player.set_avoids");
}

int
cmd_player_get_avoids (client_ctx_t *ctx, json_t *root)
{
  json_t *avoid = players_list_avoid (ctx);
  if (!avoid)
    send_enveloped_error (ctx->fd, root, 500, "avoid_load_failed");

  json_t *out = json_object ();
  json_object_set_new (out, "avoid", avoid);
  send_enveloped_ok (ctx->fd, root, "avoids", out);
  json_decref (out);
  return 0;
}

/* ==================================================================== */
/*                               NOTES                                   */
/* ==================================================================== */

int
cmd_player_get_notes (client_ctx_t *ctx, json_t *root)
{
  json_t *notes = players_list_notes (ctx, root);	/* supports {"scope":...,"key":...} */
  if (!notes)
    send_enveloped_error (ctx->fd, root, 500, "notes_load_failed");

  json_t *out = json_object ();
  json_object_set_new (out, "notes", notes);
  send_enveloped_ok (ctx->fd, root, "player.notes", out);
  json_decref (out);
  return 0;
}

/* ==================================================================== */
/*                       SECTION BUILDERS (STUBS)                        */
/* ==================================================================== */

static int
_include_wanted (json_t *data, const char *key)
{
  json_t *inc = data ? json_object_get (data, "include") : NULL;
  if (!inc || !json_is_array (inc))
    return 1;			/* no filter → include all */
  size_t i, n = json_array_size (inc);
  for (i = 0; i < n; i++)
    {
      const char *s = json_string_value (json_array_get (inc, i));
      if (s && 0 == strcmp (s, key))
	return 1;
    }
  return 0;
}

static json_t *
players_build_settings (client_ctx_t *ctx, json_t *req)
{
  json_t *out = json_object ();

  if (_include_wanted (req, "prefs"))
    {
      json_t *prefs = players_get_prefs (ctx);
      if (!prefs)
	prefs = json_object ();
      json_object_set_new (out, "prefs", prefs);
    }
  if (_include_wanted (req, "subscriptions"))
    {
      json_t *subs = players_get_subscriptions (ctx);
      if (!subs)
	subs = json_array ();
      json_object_set_new (out, "subscriptions", subs);
    }
  if (_include_wanted (req, "bookmarks"))
    {
      json_t *bm = players_list_bookmarks (ctx);
      if (!bm)
	bm = json_array ();
      json_object_set_new (out, "bookmarks", bm);
    }
  if (_include_wanted (req, "avoid"))
    {
      json_t *av = players_list_avoid (ctx);
      if (!av)
	av = json_array ();
      json_object_set_new (out, "avoid", av);
    }
  if (_include_wanted (req, "notes"))
    {
      json_t *nt = players_list_notes (ctx, req);
      if (!nt)
	nt = json_array ();
      json_object_set_new (out, "notes", nt);
    }
  return out;
}

/* Replace these stubs with DB-backed implementations as you land #189+ */
static json_t *
players_get_prefs (client_ctx_t *ctx)
{
  (void) ctx;
  json_t *prefs = json_object ();
  json_object_set_new (prefs, "ui.ansi", json_true ());
  json_object_set_new (prefs, "ui.clock_24h", json_true ());
  json_object_set_new (prefs, "ui.locale", json_string ("en-GB"));
  json_object_set_new (prefs, "ui.page_length", json_integer (20));
  json_object_set_new (prefs, "privacy.dm_allowed", json_true ());
  return prefs;
}

static json_t *
players_get_subscriptions (client_ctx_t *ctx)
{
  (void) ctx;
  json_t *arr = json_array ();
  json_t *a = json_object ();
  json_object_set_new (a, "topic", json_string ("system.notice"));
  json_object_set_new (a, "locked", json_true ());
  json_array_append_new (arr, a);
  json_t *b = json_object ();
  json_object_set_new (b, "topic", json_string ("sector.*"));
  json_object_set_new (b, "locked", json_false ());
  json_array_append_new (arr, b);
  return arr;
}

static json_t *
players_list_bookmarks (client_ctx_t *ctx)
{
  (void) ctx;
  return json_array ();
}

static json_t *
players_list_avoid (client_ctx_t *ctx)
{
  (void) ctx;
  return json_array ();
}

static json_t *
players_list_notes (client_ctx_t *ctx, json_t *req)
{
  (void) ctx;
  (void) req;
  return json_array ();
}

/* --- local helpers for type mapping/validation --- */
static int
map_pt (const char *s)
{
  if (!s)
    return PT_STRING;
  if (strcmp (s, "bool") == 0)
    return PT_BOOL;
  if (strcmp (s, "int") == 0)
    return PT_INT;
  if (strcmp (s, "json") == 0)
    return PT_JSON;
  return PT_STRING;
}

static int
validate_value (int pt, const char *v)
{
  if (!v)
    return 0;
  char *end = NULL;
  switch (pt)
    {
    case PT_BOOL:
      return (!strcmp (v, "true") || !strcmp (v, "false") || !strcmp (v, "0")
	      || !strcmp (v, "1"));
    case PT_INT:
      strtol (v, &end, 10);
      return (end && *end == '\0');
    case PT_JSON:
      return v[0] == '{' || v[0] == '[';	/* lightweight check */
    case PT_STRING:
    default:
      return 1;
    }
}

int
cmd_player_set_prefs (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id <= 0)
    {
      send_enveloped_refused (ctx->fd, root, 1401, "Not authenticated", NULL);
      return 0;
    }

  json_t *data = json_object_get (root, "data");
  if (!json_is_object (data))
    {
      send_enveloped_refused (ctx->fd, root, 1402, "Bad arguments", NULL);
      return 0;
    }

  /* Accept either single item or array in data.items */
  json_t *items = json_object_get (data, "items");
  if (!json_is_array (items))
    {
      json_t *one = json_object ();
      json_object_set (one, "key", json_object_get (data, "key"));
      json_object_set (one, "type", json_object_get (data, "type"));
      json_object_set (one, "value", json_object_get (data, "value"));
      items = json_array ();
      json_array_append_new (items, one);
    }

  if (!json_is_array (items))
    {
      send_enveloped_refused (ctx->fd, root, 1402, "Bad arguments", NULL);
      return 0;
    }

  size_t i, n_ok = 0;
  json_t *it;
  json_array_foreach (items, i, it)
  {
    if (!json_is_object (it))
      continue;
    const char *key = json_string_value (json_object_get (it, "key"));
    const char *typ = json_string_value (json_object_get (it, "type"));
    const char *val = json_string_value (json_object_get (it, "value"));
    int pt = map_pt (typ);
    if (!key || !val || !validate_value (pt, val))
      continue;
    if (db_prefs_set_one (ctx->player_id, key, pt, val) == 0)
      n_ok++;
  }

  json_t *ack = json_object ();
  json_object_set_new (ack, "updated", json_integer ((json_int_t) n_ok));
  send_enveloped_ok (ctx->fd, root, "player.pref.ack_v1", ack);
  return 0;
}


/* ---------- nav.bookmark.add ---------- */
void
cmd_nav_bookmark_add(client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id <= 0) { send_enveloped_refused(ctx->fd, root, 1401, "Not authenticated", NULL); return; }

  json_t *data = json_object_get(root, "data");
  const char *name = data ? json_string_value(json_object_get(data, "name")) : NULL;
  int64_t sector_id = data ? (int64_t)json_integer_value(json_object_get(data, "sector_id")) : 0;

  if (!name || !*name || sector_id <= 0) {
    send_enveloped_refused(ctx->fd, root, 1402, "Bad arguments", NULL);
    return;
  }

  int rc = db_bookmark_upsert((int64_t)ctx->player_id, name, sector_id);
  if (rc != 0) { send_enveloped_error(ctx->fd, root, 1503, "Database error"); return; }

  json_t *ack = json_pack("{s:s,s:I}", "name", name, "sector_id", (json_int_t)sector_id);
  send_enveloped_ok(ctx->fd, root, "nav.bookmark.ack_v1", ack);
  json_decref(ack);
}

/* ---------- nav.bookmark.remove ---------- */
void
cmd_nav_bookmark_remove(client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id <= 0) { send_enveloped_refused(ctx->fd, root, 1401, "Not authenticated", NULL); return; }

  json_t *data = json_object_get(root, "data");
  const char *name = data ? json_string_value(json_object_get(data, "name")) : NULL;

  if (!name || !*name) {
    send_enveloped_refused(ctx->fd, root, 1402, "Bad arguments", NULL);
    return;
  }

  int rc = db_bookmark_remove((int64_t)ctx->player_id, name);
  if (rc != 0) { send_enveloped_error(ctx->fd, root, 1503, "Database error"); return; }

  json_t *ack = json_pack("{s:s}", "name", name);
  send_enveloped_ok(ctx->fd, root, "nav.bookmark.ack_v1", ack);
  json_decref(ack);
}

/* ---------- nav.bookmark.list ---------- */
void
cmd_nav_bookmark_list(client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id <= 0) { send_enveloped_refused(ctx->fd, root, 1401, "Not authenticated", NULL); return; }

  sqlite3_stmt *it = NULL;
  int rc = db_bookmark_list((int64_t)ctx->player_id, &it);
  if (rc != 0 || !it) { send_enveloped_error(ctx->fd, root, 1503, "Database error"); return; }

  json_t *arr = json_array();
  /* cols: name (0), sector_id (1) */
  for (;;) {
    rc = sqlite3_step(it);
    if (rc == SQLITE_ROW) {
      const unsigned char *nm = sqlite3_column_text(it, 0);
      int64_t sector_id       = (int64_t)sqlite3_column_int64(it, 1);
      json_t *row = json_pack("{s:s,s:I}",
                              "name", nm ? (const char*)nm : "",
                              "sector_id", (json_int_t)sector_id);
      json_array_append_new(arr, row);
    } else if (rc == SQLITE_DONE) {
      break;
    } else {
      sqlite3_finalize(it);
      json_decref(arr);
      send_enveloped_error(ctx->fd, root, 1503, "Database error");
      return;
    }
  }
  sqlite3_finalize(it);

  json_t *payload = json_pack("{s:o}", "bookmarks", arr); /* takes ownership of arr */
  send_enveloped_ok(ctx->fd, root, "nav.bookmark.list_v1", payload);
  json_decref(payload);
}

/* ---------- nav.avoid.add ---------- */
void
cmd_nav_avoid_add(client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id <= 0) { send_enveloped_refused(ctx->fd, root, 1401, "Not authenticated", NULL); return; }

  json_t *data = json_object_get(root, "data");
  int64_t sector_id = data ? (int64_t)json_integer_value(json_object_get(data, "sector_id")) : 0;

  if (sector_id <= 0) {
    send_enveloped_refused(ctx->fd, root, 1402, "Bad arguments", NULL);
    return;
  }

  int rc = db_avoid_add((int64_t)ctx->player_id, sector_id);
  if (rc != 0) { send_enveloped_error(ctx->fd, root, 1503, "Database error"); return; }

  json_t *ack = json_pack("{s:I}", "sector_id", (json_int_t)sector_id);
  send_enveloped_ok(ctx->fd, root, "nav.avoid.ack_v1", ack);
  json_decref(ack);
}

/* ---------- nav.avoid.remove ---------- */
void
cmd_nav_avoid_remove(client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id <= 0) { send_enveloped_refused(ctx->fd, root, 1401, "Not authenticated", NULL); return; }

  json_t *data = json_object_get(root, "data");
  int64_t sector_id = data ? (int64_t)json_integer_value(json_object_get(data, "sector_id")) : 0;

  if (sector_id <= 0) {
    send_enveloped_refused(ctx->fd, root, 1402, "Bad arguments", NULL);
    return;
  }

  int rc = db_avoid_remove((int64_t)ctx->player_id, sector_id);
  if (rc != 0) { send_enveloped_error(ctx->fd, root, 1503, "Database error"); return; }

  json_t *ack = json_pack("{s:I}", "sector_id", (json_int_t)sector_id);
  send_enveloped_ok(ctx->fd, root, "nav.avoid.ack_v1", ack);
  json_decref(ack);
}

/* ---------- nav.avoid.list ---------- */
void
cmd_nav_avoid_list(client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id <= 0) { send_enveloped_refused(ctx->fd, root, 1401, "Not authenticated", NULL); return; }

  sqlite3_stmt *it = NULL;
  int rc = db_avoid_list((int64_t)ctx->player_id, &it);
  if (rc != 0 || !it) { send_enveloped_error(ctx->fd, root, 1503, "Database error"); return; }

  json_t *arr = json_array();
  /* cols: sector_id (0) */
  for (;;) {
    rc = sqlite3_step(it);
    if (rc == SQLITE_ROW) {
      int64_t sector_id = (int64_t)sqlite3_column_int64(it, 0);
      json_t *row = json_pack("{s:I}", "sector_id", (json_int_t)sector_id);
      json_array_append_new(arr, row);
    } else if (rc == SQLITE_DONE) {
      break;
    } else {
      sqlite3_finalize(it);
      json_decref(arr);
      send_enveloped_error(ctx->fd, root, 1503, "Database error");
      return;
    }
  }
  sqlite3_finalize(it);

  json_t *payload = json_pack("{s:o}", "sectors", arr); /* takes ownership */
  send_enveloped_ok(ctx->fd, root, "nav.avoid.list_v1", payload);
  json_decref(payload);
}

