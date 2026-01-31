#include "db/repo/repo_players.h"
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
#include "db/repo/repo_database.h"
#include "game_db.h"
#include "repo_cmd.h"
#include "errors.h"
#include "config.h"
#include "server_cmds.h"
#include "server_rules.h"
#include "repo_player_settings.h"
#include "server_envelope.h"
#include "server_auth.h"
#include "server_log.h"
#include "common.h"
#include "server_ships.h"
#include "server_loop.h"
#include "server_bank.h"
#include "db/db_api.h"
#include "db/sql_driver.h"

/* Constants */
enum
{ MAX_BOOKMARKS = 64, MAX_BM_NAME = 64 };
enum
{ MAX_AVOIDS = 64 };

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
  json_t *payload = json_object ();
  json_object_set_new (payload, "rankings", json_array ());
  send_response_ok_take (ctx, root, "player.rankings.response", &payload);
  return 0;
}

static int
route_compare (const void *a, const void *b)
{
  const trade_route_t *ra = (const trade_route_t *) a;
  const trade_route_t *rb = (const trade_route_t *) b;

  /* Prefer two-way loops */
  if (ra->is_two_way != rb->is_two_way)
    {
      return rb->is_two_way - ra->is_two_way;
    }
  /* Then shorter overall distance */
  int dist_a = ra->hops_between + ra->hops_from_player;
  int dist_b = rb->hops_between + rb->hops_from_player;
  return dist_a - dist_b;
}

int
cmd_player_computer_recommend_routes (client_ctx_t *ctx, json_t *root)
{
  db_t *db = game_db_get_handle ();
  if (!db)
    {
      send_response_error (ctx, root, ERR_DB, "No database handle");
      return 0;
    }

  json_t *data = json_object_get (root, "data");
  int max_hops_between = 10;
  int max_hops_from_player = 20;
  int require_two_way = 0;
  int limit = 10;

  if (json_is_object (data))
    {
      json_get_int_flexible (data, "max_hops_between", &max_hops_between);
      json_get_int_flexible (data, "max_hops_from_player",
			     &max_hops_from_player);
      json_get_int_flexible (data, "require_two_way", &require_two_way);
      json_get_int_flexible (data, "limit", &limit);
    }

  trade_route_t *routes = NULL;
  int count = 0;
  int truncated = 0;
  int pairs_checked = 0;
  int rc = repo_players_get_recommended_routes (db, ctx->player_id,
						ctx->sector_id,
						max_hops_between,
						max_hops_from_player,
						require_two_way, &routes,
						&count, &truncated, &pairs_checked);

  if (rc != 0)
    {
      send_response_error (ctx, root, 500, "Failed to calculate routes");
      return 0;
    }

  if (count > 0)
    {
      qsort (routes, count, sizeof (trade_route_t), route_compare);
    }

  json_t *j_routes = json_array ();
  for (int i = 0; i < count && i < limit; i++)
    {
      json_t *r = json_object ();
      json_object_set_new (r, "port_a_id", json_integer (routes[i].port_a_id));
      json_object_set_new (r, "port_b_id", json_integer (routes[i].port_b_id));
      json_object_set_new (r, "sector_a_id",
			   json_integer (routes[i].sector_a_id));
      json_object_set_new (r, "sector_b_id",
			   json_integer (routes[i].sector_b_id));
      json_object_set_new (r, "hops_between",
			   json_integer (routes[i].hops_between));
      json_object_set_new (r, "hops_from_player",
			   json_integer (routes[i].hops_from_player));
      json_object_set_new (r, "is_two_way",
			   routes[i].is_two_way ? json_true () :
			   json_false ());
      json_array_append_new (j_routes, r);
    }

  repo_players_free_routes (routes, count);

  json_t *payload = json_object ();
  json_object_set_new (payload, "routes", j_routes);
  json_object_set_new (payload, "pathing_model", json_string ("full_graph"));
  json_object_set_new (payload, "truncated", truncated ? json_true () : json_false ());
  json_object_set_new (payload, "pairs_checked", json_integer (pairs_checked));
  send_response_ok_take (ctx, root, "player.computer.trade_routes", &payload);
  return 0;
}


/* ==================================================================== */
/* STATIC HELPER DEFINITIONS                                            */
/* ==================================================================== */


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
		    const char *key, const char *type, const char *value)
{
  LOGD ("h_db_prefs_set_one: pid=%d key=%s val=%s", player_id, key, value);
  db_t *db = game_db_get_handle ();
  return repo_players_set_pref (db, player_id, key, type, value);
}


static int
h_db_bookmark_upsert (int player_id, const char *name, int sector_id)
{
  db_t *db = game_db_get_handle ();
  return repo_players_upsert_bookmark (db, player_id, name, sector_id);
}


static int
h_db_bookmark_remove (int player_id, const char *name)
{
  db_t *db = game_db_get_handle ();
  return repo_players_delete_bookmark (db, player_id, name);
}


static int
h_db_avoid_add (int player_id, int sector_id)
{
  db_t *db = game_db_get_handle ();
  return repo_players_add_avoid (db, player_id, sector_id);
}


static int
h_db_avoid_remove (int player_id, int sector_id)
{
  db_t *db = game_db_get_handle ();
  return repo_players_delete_avoid (db, player_id, sector_id);
}


static int
h_db_subscribe_upsert (int player_id,
		       const char *topic,
		       const char *delivery, const char *filter)
{
  db_t *db = game_db_get_handle ();
  return repo_players_upsert_subscription (db, player_id, topic, delivery,
					   filter);
}


/* --- Logic Helpers --- */


static json_t *
prefs_as_object (int64_t pid)
{
  db_t *db = game_db_get_handle ();
  db_res_t *res = NULL;
  db_error_t err;
  json_t *root = json_object ();


  if ((res = repo_players_get_prefs (db, pid, &err)) != NULL)
    {
      while (db_res_step (res, &err))
	{
	  const char *k = db_res_col_text (res, 0, &err);
	  const char *t = db_res_col_text (res, 1, &err);
	  const char *v = db_res_col_text (res, 2, &err);


	  if (!k || !v)
	    continue;

	  if (t && strcmp (t, "bool") == 0)
	    {
	      json_object_set_new (root, k,
				   json_boolean (atoi (v) ||
						 strcasecmp (v,
						     "true") == 0));
	    }
	  else if (t && strcmp (t, "int") == 0)
	    {
	      json_object_set_new (root, k, json_integer (atoll (v)));
	    }
	  else
	    {
	      json_object_set_new (root, k, json_string (v));
	    }
	}
      db_res_finalize (res);
    }
  return root;
}


static json_t *
prefs_as_array (int64_t pid)
{
  db_t *db = game_db_get_handle ();
  db_res_t *res = NULL;
  db_error_t err;

  json_t *arr = json_array ();
  if ((res = repo_players_get_prefs (db, pid, &err)) != NULL)
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
  db_res_t *res = NULL;
  db_error_t err;
  json_t *arr = json_array ();
  if ((res = repo_players_get_bookmarks (db, pid, &err)) != NULL)
    {
      while (db_res_step (res, &err))
	{
	  const char *name = db_res_col_text (res, 0, &err);
	  json_t *bm_obj = json_object ();


	  json_object_set_new (bm_obj, "name",
			       json_string (name ? name : ""));
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
  db_res_t *res = NULL;
  db_error_t err;
  json_t *arr = json_array ();
  if ((res = repo_players_get_avoids (db, pid, &err)) != NULL)
    {
      while (db_res_step (res, &err))
	{
	  json_array_append_new (arr,
				 json_integer (db_res_col_i32
					       (res, 0, &err)));
	}
      db_res_finalize (res);
    }
  return arr;
}


static json_t *
subscriptions_as_array (int64_t pid)
{
  db_t *db = game_db_get_handle ();
  db_res_t *res = NULL;
  db_error_t err;
  json_t *arr = json_array ();
  if ((res = repo_players_get_subscriptions (db, pid, &err)) != NULL)
    {
      while (db_res_step (res, &err))
	{
	  json_t *one = json_object ();


	  json_object_set_new (one,
			       "topic",
			       json_string (db_res_col_text (res, 0, &err)));
	  json_object_set_new (one, "locked",
			       json_boolean (db_res_col_int (res, 1, &err)));
	  json_object_set_new (one, "enabled",
			       json_boolean (db_res_col_int (res, 2, &err)));
	  json_object_set_new (one, "delivery",
			       json_string (db_res_col_text (res, 3, &err)));
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
  LOGD ("h_set_prefs: start");
  if (!json_is_object (prefs))
    {
      LOGD ("h_set_prefs: not an object");
      return -1;
    }

  /* Handle V3 "items": [...] array if present */
  json_t *items = json_object_get (prefs, "items");
  if (json_is_array (items))
    {
      LOGD ("h_set_prefs: found items array");
      size_t idx;
      json_t *item;
      json_array_foreach (items, idx, item)
      {
	const char *key = json_string_value (json_object_get (item, "key"));
	json_t *val = json_object_get (item, "value");
	if (key && val)
	  {
	    LOGD ("h_set_prefs: item key=%s", key);
	    char buf[512] = { 0 };
	    const char *sval = "";
	    if (json_is_string (val))
	      {
		sval = json_string_value (val);
	      }
	    else if (json_is_integer (val))
	      {
		snprintf (buf, sizeof (buf), "%lld",
			  (long long) json_integer_value (val));
		sval = buf;
	      }
	    else if (json_is_boolean (val))
	      {
		sval = json_is_true (val) ? "1" : "0";
	      }
	    else
	      continue;
	    h_db_prefs_set_one (ctx->player_id, key, "string", sval);
	  }
      }
      return 0;
    }

  const char *key;
  json_t *val;


  json_object_foreach (prefs, key, val)
  {
    LOGD ("h_set_prefs: foreach key=%s", key);
    if (!is_valid_key (key, 64))
      {
	LOGD ("h_set_prefs: invalid key %s", key);
	continue;
      }
    char buf[512] = { 0 };
    const char *sval = "";


    if (json_is_string (val))
      {
	sval = json_string_value (val);
      }
    else if (json_is_integer (val))
      {
	snprintf (buf, sizeof (buf), "%lld",
		  (long long) json_integer_value (val));
	sval = buf;
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
  size_t idx;
  json_t *val;


  json_array_foreach (list, idx, val)
  {
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
  size_t idx;
  json_t *val;


  json_array_foreach (list, idx, val)
  {
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
  size_t idx;
  json_t *val;


  json_array_foreach (list, idx, val)
  {
    if (json_is_string (val))
      {
	h_db_subscribe_upsert (ctx->player_id,
			       json_string_value (val), NULL, NULL);
      }
    else if (json_is_object (val))
      {
	const char *topic =
	  json_string_value (json_object_get (val, "topic"));


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
  json_object_set_new (data, "bookmarks",
		       bookmarks_as_array (ctx->player_id));
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
			   "data object required");
      return 0;
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


  json_object_set_new (data, "prefs", prefs_as_object (ctx->player_id));
  send_response_ok_take (ctx, root, "player.prefs", &data);
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
  const char *name =
    json_string_value (json_object_get (json_object_get (root,
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
  h_set_avoids (ctx,
		json_object_get (json_object_get (root, "data"), "avoid"));
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
  int sid =
    json_integer_value (json_object_get (json_object_get (root, "data"),
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
  int sid =
    json_integer_value (json_object_get (json_object_get (root, "data"),
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
  json_object_set_new (out, "topics",
		       subscriptions_as_array (ctx->player_id));
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
  if (ctx->player_id <= 0)
    {
      send_response_error (ctx, root, ERR_NOT_AUTHENTICATED, "Auth required");
      return 0;
    }

  db_t *db = game_db_get_handle ();
  db_res_t *res = NULL;
  db_error_t err;
  json_t *data = json_object_get (root, "data");
  const char *scope = json_string_value (json_object_get (data, "scope"));

  if (db_note_list (db, ctx->player_id, scope, &res) != 0)
    {
      send_response_error (ctx, root, ERR_SERVER_ERROR, "db error");
      return 0;
    }

  json_t *items = json_array ();
  while (db_res_step (res, &err))
    {
      json_t *row = json_object ();
      json_object_set_new (row, "scope",
			   json_string (db_res_col_text (res, 0, &err)));
      json_object_set_new (row, "key",
			   json_string (db_res_col_text (res, 1, &err)));
      json_object_set_new (row, "note",
			   json_string (db_res_col_text (res, 2, &err)));
      json_array_append_new (items, row);
    }
  db_res_finalize (res);

  json_t *resp = json_object ();
  json_object_set_new (resp, "notes", items);
  send_response_ok_take (ctx, root, "player.notes_v1", &resp);
  return 0;
}


int
cmd_player_set_note (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id <= 0)
    {
      send_response_error (ctx, root, ERR_NOT_AUTHENTICATED, "Auth required");
      return 0;
    }

  json_t *data = json_object_get (root, "data");
  const char *scope = json_string_value (json_object_get (data, "scope"));
  const char *key = json_string_value (json_object_get (data, "key"));
  const char *note = json_string_value (json_object_get (data, "note"));

  if (!scope || !key || !note)
    {
      send_response_error (ctx, root, ERR_INVALID_ARG,
			   "scope, key, and note required");
      return 0;
    }

  db_t *db = game_db_get_handle ();
  if (db_note_set (db, ctx->player_id, scope, key, note) != 0)
    {
      send_response_error (ctx, root, ERR_SERVER_ERROR, "db error");
      return 0;
    }

  json_t *resp = json_object ();
  json_object_set_new (resp, "ok", json_true ());
  send_response_ok_take (ctx, root, "player.note.set_v1", &resp);
  return 0;
}


int
cmd_player_delete_note (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id <= 0)
    {
      send_response_error (ctx, root, ERR_NOT_AUTHENTICATED, "Auth required");
      return 0;
    }

  json_t *data = json_object_get (root, "data");
  const char *scope = json_string_value (json_object_get (data, "scope"));
  const char *key = json_string_value (json_object_get (data, "key"));

  if (!scope || !key)
    {
      send_response_error (ctx, root, ERR_INVALID_ARG, "scope and key required");
      return 0;
    }

  db_t *db = game_db_get_handle ();
  if (db_note_delete (db, ctx->player_id, scope, key) != 0)
    {
      send_response_error (ctx, root, ERR_SERVER_ERROR, "db error");
      return 0;
    }

  json_t *resp = json_object ();
  json_object_set_new (resp, "ok", json_true ());
  send_response_ok_take (ctx, root, "player.note.deleted_v1", &resp);
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
				   "Not authenticated", NULL);
      return 0;
    }
  db_t *db = game_db_get_handle ();
  db_res_t *res = NULL;
  db_error_t err;


  if ((res = repo_players_get_my_info (db, ctx->player_id, &err)) != NULL)
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


	  json_object_set_new (player_obj, "id",
			       json_integer (ctx->player_id));
	  json_object_set_new (player_obj, "username",
			       json_string (name ? name : "Unknown"));

	  char credits_str[64];


	  snprintf (credits_str, sizeof (credits_str), "%lld.00", credits);
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
      send_response_error (ctx, root, ERR_NOT_AUTHENTICATED, "Auth required");
      return 0;
    }

  json_t *data = json_object_get (root, "data");
  if (!json_is_object (data))
    {
      send_response_error (ctx, root, ERR_INVALID_SCHEMA,
			   "data object required");
      return 0;
    }

  json_t *pref = json_object_get (data, "prefer_bank");
  if (!pref)
    {
      pref = json_object_get (data, "preference");
    }

  bool prefer_bank = false;
  bool valid = false;

  if (json_is_boolean (pref))
    {
      prefer_bank = json_is_true (pref);
      valid = true;
    }
  else if (json_is_integer (pref))
    {
      prefer_bank = (json_integer_value (pref) != 0);
      valid = true;
    }
  else if (json_is_string (pref))
    {
      const char *s = json_string_value (pref);
      if (strcasecmp (s, "bank") == 0 || strcasecmp (s, "1") == 0
	  || strcasecmp (s, "true") == 0)
	{
	  prefer_bank = true;
	  valid = true;
	}
      else if (strcasecmp (s, "petty_cash") == 0 || strcasecmp (s, "0") == 0
	       || strcasecmp (s, "false") == 0)
	{
	  prefer_bank = false;
	  valid = true;
	}
    }

  if (valid)
    {
      h_db_prefs_set_one (ctx->player_id,
			  "trade.prefer_bank",
			  "bool", prefer_bank ? "1" : "0");
      json_t *resp = json_object ();
      json_object_set_new (resp, "ok", json_true ());
      send_response_ok_take (ctx, root, "player.prefs.updated", &resp);
    }
  else
    {
      send_response_error (ctx, root, ERR_INVALID_ARG,
			   "Invalid preference value. Expected boolean, 0/1, or 'bank'/'petty_cash'");
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

  db_res_t *res = NULL;
  db_error_t err;


  db_error_clear (&err);


  if ((res = repo_players_get_title_info (db, player_id, &err)) == NULL)
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

  align = (int) db_res_col_i32 (res, 0, &err);
  exp = db_res_col_i64 (res, 1, &err);
  comm_id = (int) db_res_col_i32 (res, 2, &err);
  db_res_finalize (res);

  char *band_code = NULL, *band_name = NULL;
  int is_good = 0, is_evil = 0, can_iss = 0, can_rob = 0;


  db_alignment_band_for_value (db, align, NULL, &band_code, &band_name,
			       &is_good, &is_evil, &can_iss, &can_rob);

  int det_comm_id = 0, comm_is_evil = 0;
  char *comm_title = NULL;


  db_commission_for_player (db,
			    is_evil,
			    exp, &det_comm_id, &comm_title, &comm_is_evil);

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
			  const char *subject, const char *message)
{
  if (!db || !subject || !message)
    {
      return 1;
    }

  return repo_players_send_mail (db, sender_id, recipient_id, subject,
				 message) == 0 ? 0 : 1;
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

  return repo_players_get_cargo_free (db, player_id, free_out);
}


int
h_player_is_npc (db_t *db, int player_id)
{
  if (!db)
    {
      return 0;
    }

  int is_npc = 0;
  if (repo_players_is_npc (db, player_id, &is_npc) == 0)
    {
      return is_npc;
    }
  return 0;
}


int
spawn_starter_ship (db_t *db, int player_id, int sector_id)
{
  if (!db)
    {
      return -1;
    }

  int ship_type_id = 0, holds = 0, fighters = 0, shields = 0;
  if (repo_players_get_shiptype_by_name
      (db, "Scout Marauder", &ship_type_id, &holds, &fighters, &shields) != 0)
    {
      return -1;
    }

  int ship_id = 0;
  if (repo_players_insert_ship
      (db, "Starter Ship", ship_type_id, holds, fighters, shields, sector_id,
       &ship_id) != 0)
    {
      return -1;
    }

  if (repo_players_set_ship_ownership (db, ship_id, player_id) != 0)
    {
      return -1;
    }

  if (repo_players_update_ship_and_sector (db, player_id, ship_id, sector_id)
      != 0)
    {
      return -1;
    }

  if (repo_players_update_podded_status (db, player_id, "alive") != 0)
    {
      return -1;
    }

  return 0;
}


int
h_get_player_petty_cash (db_t *db, int player_id, long long *bal)
{
  if (!db || player_id <= 0 || !bal)
    {
      return -1;
    }

  return repo_players_get_credits (db, player_id, bal);
}


int
h_deduct_ship_credits (db_t *db, int player_id, int amount, int *new_balance)
{
  long long new_balance_ll = 0;
  int rc = h_deduct_credits (db,
			     "player",
			     player_id,
			     amount,
			     "WITHDRAWAL",
			     NULL,
			     &new_balance_ll);
  if (rc == 0 && new_balance)
    {
      *new_balance = (int) new_balance_ll;
    }
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

  return repo_players_deduct_credits_returning (db, player_id, amount,
						new_balance_out);
}


int
h_add_player_petty_cash (db_t *db,
			 int player_id,
			 long long amount, long long *new_balance_out)
{
  if (!db || amount < 0)
    {
      return -1;
    }

  return repo_players_add_credits_returning (db, player_id, amount,
					     new_balance_out);
}


TurnConsumeResult
h_consume_player_turn (db_t *db, client_ctx_t *ctx, int turns)
{
  if (!db || !ctx || turns <= 0)
    {
      return TURN_CONSUME_ERROR_INVALID_AMOUNT;
    }

  int player_id = ctx->player_id;

  int turns_remaining = 0;
  if (repo_players_get_turns (db, player_id, &turns_remaining) != 0)
    {
      LOGE ("h_consume_player_turn: failed to get turns for player_id=%d",
	    player_id);
      return TURN_CONSUME_ERROR_DB_FAIL;
    }

  if (turns_remaining < turns)
    {
      return TURN_CONSUME_ERROR_NO_TURNS;
    }

  if (repo_players_consume_turns (db, player_id, turns) != 0)
    {
      return TURN_CONSUME_ERROR_DB_FAIL;
    }

  return TURN_CONSUME_SUCCESS;
}


int
handle_turn_consumption_error (client_ctx_t *ctx,
			       TurnConsumeResult res,
			       const char *cmd, json_t *root, json_t *meta)
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
				   "Insufficient turns.", NULL);


      json_decref (meta_obj);
    }


  return 0;
}


int
h_player_apply_progress (db_t *db,
			 int player_id,
			 long long delta_xp,
			 int delta_align, const char *reason)
{
  if (!db || player_id <= 0)
    {
      return -1;
    }

  int cur_align = 0;
  long long cur_xp = 0;

  if (repo_players_get_align_exp (db, player_id, &cur_align, &cur_xp) != 0)
    {
      return -1;
    }

  // Calculate new values
  long long new_xp = cur_xp + delta_xp;
  if (new_xp < 0)
    new_xp = 0;

  int new_align = cur_align + delta_align;
  if (new_align > 2000)
    new_align = 2000;
  if (new_align < -2000)
    new_align = -2000;

  if (repo_players_update_align_exp (db, player_id, new_align, new_xp) != 0)
    {
      return -1;
    }

  // Update commission (call the DB function)
  db_player_update_commission (db, player_id);

  LOGD ("Player %d progress updated. Reason: %s",
	player_id, reason ? reason : "N/A");

  return 0;
}


int
h_get_player_sector (db_t *db, int player_id)
{
  if (!db)
    {
      return 0;
    }

  int sector = 0;
  if (repo_players_get_sector (db, player_id, &sector) == 0)
    {
      return sector;
    }
  return 0;
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

  return repo_players_add_credits_returning (db, player_id, amount,
					     new_balance_out);
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

  int rc =
    repo_players_update_credits_safe (db, player_id, delta, new_balance_out);
  if (rc == 0)
    {
      return 0;
    }

  /* Could be: player missing OR insufficient funds. Distinguish minimally. */
  int exists = 0;
  if (repo_players_check_exists (db, player_id, &exists) == 0)
    {
      if (!exists)
	return ERR_DB_NOT_FOUND;
      return ERR_DB_CONSTRAINT;
    }

  return rc;
}
