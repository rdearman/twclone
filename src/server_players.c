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



/* ---------- forward decls for settings section builders (stubs for now) ---------- */
static json_t* players_build_settings   (client_ctx_t *ctx, json_t *req);
static json_t* players_get_prefs        (client_ctx_t *ctx);
static json_t* players_get_subscriptions(client_ctx_t *ctx);
static json_t* players_list_bookmarks   (client_ctx_t *ctx);
static json_t* players_list_avoid       (client_ctx_t *ctx);
static json_t* players_list_notes       (client_ctx_t *ctx, json_t *req);

/* ==================================================================== */
/*                  YOUR ORIGINAL INFO/ONLINE HANDLERS                   */
/* ==================================================================== */

int
cmd_player_my_info (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id <= 0) {
    send_enveloped_refused (ctx->fd, root, 1401, "Not authenticated", NULL);
    return 0;
  }

  json_t *pinfo = NULL;
  int prc = db_player_info_json (ctx->player_id, &pinfo);
  if (prc != SQLITE_OK || !pinfo) {
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
  if (ctx->player_id > 0) {
    json_array_append_new (arr, json_pack ("{s:i}", "player_id", ctx->player_id));
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
cmd_player_get_settings(client_ctx_t *ctx, json_t *root)
{
  json_t *resp = players_build_settings(ctx, root);
  if (!resp)  send_enveloped_error(ctx->fd, root, 500, "settings_build_failed");
  send_enveloped_ok(ctx, root, "player.settings", resp);
  json_decref(resp);
  return 0;
}

/* ==================================================================== */
/*                             PREFS                                     */
/* ==================================================================== */

int
cmd_player_set_prefs (client_ctx_t *ctx, json_t *root)
{
  STUB_NIY (ctx, root, "player.set_prefs");
}

int
cmd_player_get_prefs(client_ctx_t *ctx, json_t *root)
{
  json_t *prefs = players_get_prefs(ctx);
  if (!prefs)  send_enveloped_error(ctx, root, 500, "prefs_load_failed");

  json_t *out = json_object();
  json_object_set_new(out, "prefs", prefs);
  send_enveloped_ok(ctx, root, "player.prefs", out);
  json_decref(out);
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
cmd_player_get_topics(client_ctx_t *ctx, json_t *root)
{
  json_t *topics = players_get_subscriptions(ctx);
  if (!topics)  send_enveloped_error(ctx, root, 500, "subs_load_failed");

  json_t *out = json_object();
  json_object_set_new(out, "topics", topics);
  send_enveloped_ok(ctx, root, "player.subscriptions", out);
  json_decref(out);
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
cmd_player_get_bookmarks(client_ctx_t *ctx, json_t *root)
{
  json_t *bookmarks = players_list_bookmarks(ctx);
  if (!bookmarks)  send_enveloped_error(ctx, root, 500, "bookmarks_load_failed");

  json_t *out = json_object();
  json_object_set_new(out, "bookmarks", bookmarks);
  send_enveloped_ok(ctx, root, "player.bookmarks", out);
  json_decref(out);
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
cmd_player_get_avoids(client_ctx_t *ctx, json_t *root)
{
  json_t *avoid = players_list_avoid(ctx);
  if (!avoid)  send_enveloped_error(ctx, root, 500, "avoid_load_failed");

  json_t *out = json_object();
  json_object_set_new(out, "avoid", avoid);
  send_enveloped_ok(ctx, root, "player.avoids", out);
  json_decref(out);
  return 0;
}

/* ==================================================================== */
/*                               NOTES                                   */
/* ==================================================================== */

int
cmd_player_get_notes(client_ctx_t *ctx, json_t *root)
{
  json_t *notes = players_list_notes(ctx, root); /* supports {"scope":...,"key":...} */
  if (!notes)  send_enveloped_error(ctx, root, 500, "notes_load_failed");

  json_t *out = json_object();
  json_object_set_new(out, "notes", notes);
  send_enveloped_ok(ctx, root, "player.notes", out);
  json_decref(out);
  return 0;
}

/* ==================================================================== */
/*                       SECTION BUILDERS (STUBS)                        */
/* ==================================================================== */

static int _include_wanted(json_t *data, const char *key) {
  json_t *inc = data ? json_object_get(data, "include") : NULL;
  if (!inc || !json_is_array(inc)) return 1; /* no filter → include all */
  size_t i, n = json_array_size(inc);
  for (i = 0; i < n; i++) {
    const char *s = json_string_value(json_array_get(inc, i));
    if (s && 0 == strcmp(s, key)) return 1;
  }
  return 0;
}

static json_t*
players_build_settings(client_ctx_t *ctx, json_t *req)
{
  json_t *out = json_object();

  if (_include_wanted(req, "prefs")) {
    json_t *prefs = players_get_prefs(ctx); if (!prefs) prefs = json_object();
    json_object_set_new(out, "prefs", prefs);
  }
  if (_include_wanted(req, "subscriptions")) {
    json_t *subs = players_get_subscriptions(ctx); if (!subs) subs = json_array();
    json_object_set_new(out, "subscriptions", subs);
  }
  if (_include_wanted(req, "bookmarks")) {
    json_t *bm = players_list_bookmarks(ctx); if (!bm) bm = json_array();
    json_object_set_new(out, "bookmarks", bm);
  }
  if (_include_wanted(req, "avoid")) {
    json_t *av = players_list_avoid(ctx); if (!av) av = json_array();
    json_object_set_new(out, "avoid", av);
  }
  if (_include_wanted(req, "notes")) {
    json_t *nt = players_list_notes(ctx, req); if (!nt) nt = json_array();
    json_object_set_new(out, "notes", nt);
  }
  return out;
}

/* Replace these stubs with DB-backed implementations as you land #189+ */
static json_t* players_get_prefs(client_ctx_t *ctx) {
  (void)ctx;
  json_t *prefs = json_object();
  json_object_set_new(prefs, "ui.ansi", json_true());
  json_object_set_new(prefs, "ui.clock_24h", json_true());
  json_object_set_new(prefs, "ui.locale", json_string("en-GB"));
  json_object_set_new(prefs, "ui.page_length", json_integer(20));
  json_object_set_new(prefs, "privacy.dm_allowed", json_true());
  return prefs;
}

static json_t* players_get_subscriptions(client_ctx_t *ctx) {
  (void)ctx;
  json_t *arr = json_array();
  json_t *a = json_object();
  json_object_set_new(a, "topic", json_string("system.notice"));
  json_object_set_new(a, "locked", json_true());
  json_array_append_new(arr, a);
  json_t *b = json_object();
  json_object_set_new(b, "topic", json_string("sector.*"));
  json_object_set_new(b, "locked", json_false());
  json_array_append_new(arr, b);
  return arr;
}

static json_t* players_list_bookmarks(client_ctx_t *ctx) {
  (void)ctx;
  return json_array();
}

static json_t* players_list_avoid(client_ctx_t *ctx) {
  (void)ctx;
  return json_array();
}

static json_t* players_list_notes(client_ctx_t *ctx, json_t *req) {
  (void)ctx; (void)req;
  return json_array();
}
