#include "db/repo/repo_autopilot.h"
#include "db/repo/repo_player_settings.h"
#include <string.h>
#include <stdlib.h>
#include <time.h>
//local includes
#include "server_autopilot.h"
#include "server_config.h"
#include "server_envelope.h"
#include "server_universe.h"
#include "db/repo/repo_database.h"
#include "db/db_api.h"
#include "game_db.h"
#include "common.h"
#include "server_log.h"

#define AUTOPILOT_ROUTE_PREF "movement.autopilot.route.v1"

static int
autopilot_load_route (db_t *db, int player_id, json_t **route_out)
{
  *route_out = NULL;
  char *raw = NULL;
  if (db_prefs_get_one (db, player_id, AUTOPILOT_ROUTE_PREF, &raw) != 0)
    {
      free (raw);
      return -1;
    }
  if (!raw)
    {
      return 0;
    }
  json_error_t error;
  json_t *route = json_loads (raw, 0, &error);
  free (raw);
  if (!json_is_object (route) || !json_is_array (json_object_get (route, "path")))
    {
      json_decref (route);
      return -1;
    }
  *route_out = route;
  return 0;
}

static int
autopilot_save_route (db_t *db, int player_id, json_t *route)
{
  char *raw = json_dumps (route, JSON_COMPACT | JSON_ENSURE_ASCII);
  if (!raw)
    {
      return -1;
    }
  int rc = db_prefs_set_one (db, player_id, AUTOPILOT_ROUTE_PREF, PT_JSON, raw);
  free (raw);
  return rc;
}

/* Match the persisted cursor to the player's authoritative current sector. */
static int
autopilot_reconcile_route (json_t *route, int current_sector)
{
  const char *state = json_string_value (json_object_get (route, "state"));
  if (state && strcmp (state, "complete") == 0)
    {
      return 1;
    }
  json_t *path = json_object_get (route, "path");
  int next_index = (int) json_integer_value (json_object_get (route, "next_index"));
  size_t count = json_array_size (path);
  size_t first = next_index > 0 ? (size_t) next_index - 1 : 0;
  if (first >= count)
    {
      first = count ? count - 1 : 0;
    }
  for (size_t i = first; i < count; ++i)
    {
      json_t *sector = json_array_get (path, i);
      if (json_is_integer (sector)
	  && json_integer_value (sector) == current_sector)
	{
	  json_object_set_new (route, "next_index", json_integer ((json_int_t) i + 1));
	  if (i + 1 >= count)
	    {
	      json_object_set_new (route, "state", json_string ("complete"));
	    }
	  return 1;
	}
    }
  json_object_set_new (route, "state", json_string ("reconcile_required"));
  return 0;
}

int
server_autopilot_on_warp (db_t *db, int player_id, int sector_id)
{
  if (!db || player_id <= 0 || sector_id <= 0)
    {
      return 0;
    }
  json_t *route = NULL;
  if (autopilot_load_route (db, player_id, &route) != 0)
    {
      return -1;
    }
  if (!route)
    {
      return 0;
    }
  if (autopilot_reconcile_route (route, sector_id))
    {
      const char *mode = json_string_value (json_object_get (route, "mode"));
      const char *state = json_string_value (json_object_get (route, "state"));
      if (mode && strcmp (mode, "stop_at_next") == 0
	  && state && strcmp (state, "complete") != 0)
	{
	  json_object_set_new (route, "state", json_string ("stopped"));
	}
    }
  int rc = autopilot_save_route (db, player_id, route);
  json_decref (route);
  return rc;
}

static int
autopilot_store_new_route (db_t *db, int player_id, int from, int to,
			   json_t *path)
{
  json_t *route = json_object ();
  json_object_set_new (route, "version", json_integer (1));
  json_object_set_new (route, "path", json_deep_copy (path));
  json_object_set_new (route, "target_sector_id", json_integer (to));
  json_object_set_new (route, "next_index", json_integer (1));
  json_object_set_new (route, "mode", json_string ("continue"));
  json_object_set_new (route, "state",
		       from == to ? json_string ("complete") : json_string ("running"));
  int rc = autopilot_save_route (db, player_id, route);
  json_decref (route);
  return rc;
}


int
cmd_move_autopilot_start (db_t *db, client_ctx_t *ctx, json_t *root)
{
  if (!ctx || !db)
    {
      return 1;
    }

  json_t *data = root ? json_object_get (root, "data") : NULL;

  /* default from = current sector */
  int from = (ctx->sector_id > 0) ? ctx->sector_id : 1;


  if (data)
    {
      int tmp;


      if (json_get_int_flexible (data, "from", &tmp)
	  || json_get_int_flexible (data, "from_sector_id", &tmp))
	{
	  from = tmp;
	}
    }

  /* to = required */
  int to = -1;


  if (data)
    {
      int tmp;


      if (json_get_int_flexible (data, "to", &tmp)
	  || json_get_int_flexible (data, "to_sector_id", &tmp))
	{
	  to = tmp;
	}
    }

  if (to <= 0)
    {
      send_response_error (ctx, root, ERR_SECTOR_NOT_FOUND,
			   "Target sector not specified");
      return 1;
    }

  /* --- Query MAX(id) from sectors --- */
  int max_id = 0;
  if (repo_autopilot_get_max_sector_id (db, &max_id) != 0 || max_id <= 0)
    {
      send_response_error (ctx, root, ERR_SECTOR_NOT_FOUND, "No sectors");
      return 1;
    }


  /* Clamp from/to */
  if (from <= 0 || from > max_id || to <= 0 || to > max_id)
    {
      send_response_error (ctx, root, ERR_SECTOR_NOT_FOUND,
			   "Sector not found");
      return 1;
    }

  /* allocate arrays sized max_id+1 */
  size_t N = (size_t) max_id + 1;
  unsigned char *avoid = (unsigned char *) calloc (N, 1);
  unsigned char *seen = (unsigned char *) calloc (N, 1);
  int *prev = (int *) malloc (N * sizeof (int));
  int *queue = (int *) malloc (N * sizeof (int));


  if (!avoid || !seen || !prev || !queue)
    {
      free (avoid);
      free (seen);
      free (prev);
      free (queue);
      send_response_error (ctx, root, ERR_PLANET_NOT_FOUND, "Out of memory");
      return 1;
    }

  for (int i = 0; i <= max_id; ++i)
    {
      prev[i] = -1;
    }

  /* Fill avoid[] from JSON */
  if (data)
    {
      json_t *javoid = json_object_get (data, "avoid");


      if (javoid && json_is_array (javoid))
	{
	  size_t i, len = json_array_size (javoid);


	  for (i = 0; i < len; ++i)
	    {
	      json_t *v = json_array_get (javoid, i);


	      if (json_is_integer (v))
		{
		  int sid = (int) json_integer_value (v);


		  if (sid > 0 && sid <= max_id)
		    {
		      avoid[sid] = 1;
		    }
		}
	    }
	}
    }

  if (avoid[from] || avoid[to])
    {
      free (avoid);
      free (seen);
      free (prev);
      free (queue);
      send_response_error (ctx, root, REF_SAFE_ZONE_ONLY, "Path not found");
      return 1;
    }

  if (from == to)
    {
      json_t *steps = json_array ();


      json_array_append_new (steps, json_integer (from));

      json_t *out = json_object ();


      json_object_set_new (out, "from_sector_id", json_integer (from));
      json_object_set_new (out, "to_sector_id", json_integer (to));
      json_object_set_new (out, "path", steps);
      json_object_set_new (out, "hops", json_integer (0));

      if (autopilot_store_new_route (db, ctx->player_id, from, to, steps) != 0)
	{
	  json_decref (out);
	  free (avoid);
	  free (seen);
	  free (prev);
	  free (queue);
	  send_response_error (ctx, root, ERR_DB_QUERY_FAILED,
			       "Failed to persist autopilot route");
	  return 1;
	}

      send_response_ok_take (ctx, root, "move.autopilot.route_v1", &out);

      free (avoid);
      free (seen);
      free (prev);
      free (queue);
      return 0;
    }

  /* --- Load entire warp graph into adjacency lists --- */
  int *head = NULL;
  int *to_v = NULL;
  int *next = NULL;
  int edges = 0;


  head = (int *) malloc (N * sizeof (int));
  if (!head)
    {
      free (avoid);
      free (seen);
      free (prev);
      free (queue);
      send_response_error (ctx, root, ERR_PLANET_NOT_FOUND, "Out of memory");
      return 1;
    }
  for (int i = 0; i <= max_id; ++i)
    {
      head[i] = -1;
    }

  /* pass 1: count edges */
  if (repo_autopilot_get_warp_count (db, &edges) != 0 || edges < 0)
    {
      free (head);
      free (avoid);
      free (seen);
      free (prev);
      free (queue);
      send_response_error (ctx,
			   root,
			   ERR_PLANET_NOT_FOUND, "Pathfind init failed");
      return 1;
    }

  to_v = (int *) malloc ((size_t) edges * sizeof (int));
  next = (int *) malloc ((size_t) edges * sizeof (int));
  if ((edges > 0) && (!to_v || !next))
    {
      free (to_v);
      free (next);
      free (head);
      free (avoid);
      free (seen);
      free (prev);
      free (queue);
      send_response_error (ctx, root, ERR_PLANET_NOT_FOUND, "Out of memory");
      return 1;
    }

  /* pass 2: read edges and build adjacency */
  {
    db_res_t *res = NULL;
    db_error_t err;
    if (repo_autopilot_get_all_warps (db, &res) != 0)
      {
	free (to_v);
	free (next);
	free (head);
	free (avoid);
	free (seen);
	free (prev);
	free (queue);
	send_response_error (ctx,
			     root,
			     ERR_PLANET_NOT_FOUND, "Pathfind init failed");
	return 1;
      }

    int e = 0;


    while (db_res_step (res, &err))
      {
	int u = (int) db_res_col_i64 (res, 0, &err);
	int v = (int) db_res_col_i64 (res, 1, &err);


	if (e >= edges)
	  {
	    break;		/* safety if count lied */
	  }
	if (u <= 0 || u > max_id || v <= 0 || v > max_id)
	  {
	    continue;
	  }

	to_v[e] = v;
	next[e] = head[u];
	head[u] = e;
	e++;
      }

    db_res_finalize (res);

    if (err.code != 0)
      {
	free (to_v);
	free (next);
	free (head);
	free (avoid);
	free (seen);
	free (prev);
	free (queue);
	send_response_error (ctx,
			     root,
			     ERR_PLANET_NOT_FOUND, "Pathfind init failed");
	return 1;
      }

    /* shrink edges to actual loaded count */
    edges = e;
  }

  /* --- BFS on in-memory adjacency --- */
  int qh = 0, qt = 0;


  queue[qt++] = from;
  seen[from] = 1;

  int found = 0;


  while (qh < qt)
    {
      int u = queue[qh++];


      for (int ei = head[u]; ei != -1; ei = next[ei])
	{
	  int v = to_v[ei];


	  if (avoid[v] || seen[v])
	    {
	      continue;
	    }

	  seen[v] = 1;
	  prev[v] = u;
	  queue[qt++] = v;

	  if (v == to)
	    {
	      found = 1;
	      break;
	    }
	}

      if (found)
	{
	  break;
	}
    }

  free (to_v);
  free (next);
  free (head);

  if (!found)
    {
      free (avoid);
      free (seen);
      free (prev);
      free (queue);
      send_response_error (ctx, root, REF_SAFE_ZONE_ONLY, "Path not found");
      return 1;
    }

  /* reconstruct path */
  int *stack = (int *) malloc (N * sizeof (int));


  if (!stack)
    {
      free (avoid);
      free (seen);
      free (prev);
      free (queue);
      send_response_error (ctx, root, ERR_PLANET_NOT_FOUND, "Out of memory");
      return 1;
    }

  int sp = 0;
  int cur = to;


  while (cur != -1)
    {
      stack[sp++] = cur;
      if (cur == from)
	{
	  break;
	}
      cur = prev[cur];
    }

  if (sp <= 0 || stack[sp - 1] != from)
    {
      free (stack);
      free (avoid);
      free (seen);
      free (prev);
      free (queue);
      send_response_error (ctx, root, REF_SAFE_ZONE_ONLY, "Path not found");
      return 1;
    }

  json_t *steps = json_array ();


  for (int i = sp - 1; i >= 0; --i)
    {
      json_array_append_new (steps, json_integer (stack[i]));
    }

  int hops = sp - 1;


  free (stack);
  free (avoid);
  free (seen);
  free (prev);
  free (queue);

  json_t *out = json_object ();


  json_object_set_new (out, "to_sector_id", json_integer (to));
  json_object_set_new (out, "from_sector_id", json_integer (from));
  json_object_set_new (out, "path", steps);
  json_object_set_new (out, "hops", json_integer (hops));

  if (autopilot_store_new_route (db, ctx->player_id, from, to, steps) != 0)
    {
      json_decref (out);
      send_response_error (ctx, root, ERR_DB_QUERY_FAILED,
			   "Failed to persist autopilot route");
      return 1;
    }

  send_response_ok_take (ctx, root, "move.autopilot.route_v1", &out);
  return 0;
}


int
cmd_move_autopilot_status (client_ctx_t *ctx, json_t *root)
{
  if (!ctx || ctx->player_id <= 0)
    {
      send_response_error (ctx, root, ERR_NOT_AUTHENTICATED,
			   "Not authenticated");
      return 0;
    }
  db_t *db = game_db_get_handle ();
  if (!db)
    {
      send_response_error (ctx, root, ERR_DB, "No database handle");
      return 0;
    }
  json_t *route = NULL;
  if (autopilot_load_route (db, ctx->player_id, &route) != 0)
    {
      send_response_error (ctx, root, ERR_DB_QUERY_FAILED,
			   "Failed to load autopilot route");
      return 0;
    }
  json_t *out = json_object ();
  json_object_set_new (out, "current_sector_id",
		       json_integer (ctx->sector_id));
  json_object_set_new (out, "last_error", json_string (""));
  if (!route)
    {
      json_object_set_new (out, "state", json_string ("idle"));
      json_object_set_new (out, "mode", json_string ("manual"));
      json_object_set_new (out, "path", json_array ());
      json_object_set_new (out, "next_sector_id", json_null ());
      json_object_set_new (out, "next", json_null ());
    }
  else
    {
      autopilot_reconcile_route (route, ctx->sector_id);
      if (autopilot_save_route (db, ctx->player_id, route) != 0)
	{
	  json_decref (route);
	  json_decref (out);
	  send_response_error (ctx, root, ERR_DB_QUERY_FAILED,
			       "Failed to reconcile autopilot route");
	  return 0;
	}
      json_t *path = json_object_get (route, "path");
      int next_index = (int) json_integer_value (json_object_get (route,
								 "next_index"));
      json_object_set_new (out, "state", json_string (
		json_string_value (json_object_get (route, "state")) ?: "running"));
      json_object_set_new (out, "mode", json_string (
		json_string_value (json_object_get (route, "mode")) ?: "continue"));
      json_object_set_new (out, "path", json_deep_copy (path));
      json_object_set_new (out, "target_sector_id",
			   json_object_get (route, "target_sector_id")
			   ? json_incref (json_object_get (route, "target_sector_id"))
			   : json_null ());
      json_t *next = next_index >= 0 ? json_array_get (path, (size_t) next_index) : NULL;
      json_object_set_new (out, "next_sector_id",
			   json_is_integer (next) ? json_incref (next) : json_null ());
      json_object_set_new (out, "next",
			   json_is_integer (next) ? json_incref (next) : json_null ());
    }
  json_decref (route);
  send_response_ok_take (ctx, root, "move.autopilot.status_v1", &out);
  return 0;
}

int
cmd_move_autopilot_stop (client_ctx_t *ctx, json_t *root)
{
  if (!ctx || ctx->player_id <= 0)
    {
      send_response_error (ctx, root, ERR_NOT_AUTHENTICATED,
			   "Not authenticated");
      return 0;
    }
  db_t *db = game_db_get_handle ();
  if (!db)
    {
      send_response_error (ctx, root, ERR_DB, "No database handle");
      return 0;
    }
  json_t *route = NULL;
  if (autopilot_load_route (db, ctx->player_id, &route) != 0)
    {
      send_response_error (ctx, root, ERR_DB_QUERY_FAILED,
			   "Failed to load autopilot route");
      return 0;
    }
  if (route)
    {
      json_object_set_new (route, "state", json_string ("stopped"));
      json_object_set_new (route, "mode", json_string ("manual"));
      if (autopilot_save_route (db, ctx->player_id, route) != 0)
	{
	  json_decref (route);
	  send_response_error (ctx, root, ERR_DB_QUERY_FAILED,
			       "Failed to stop autopilot");
	  return 0;
	}
    }
  json_t *out = json_object ();
  json_object_set_new (out, "current_sector_id",
		       json_integer (ctx->sector_id));
  json_object_set_new (out, "stopped_at",
		       json_integer ((json_int_t) time (NULL)));
  json_object_set_new (out, "state", json_string ("stopped"));
  json_decref (route);
  send_response_ok_take (ctx, root, "move.autopilot.stopped_v1", &out);
  return 0;
}

int
cmd_move_autopilot_control (client_ctx_t *ctx, json_t *root)
{
  if (!ctx || ctx->player_id <= 0)
    {
      send_response_error (ctx, root, ERR_NOT_AUTHENTICATED,
			   "Not authenticated");
      return 0;
    }
  json_t *data = json_object_get (root, "data");
  const char *action = json_string_value (json_object_get (data, "action"));
  if (!action || (strcmp (action, "stop_at_next") != 0
		  && strcmp (action, "continue") != 0
		  && strcmp (action, "express") != 0))
    {
      send_response_error (ctx, root, ERR_INVALID_ARG, "Invalid action");
      return 0;
    }
  db_t *db = game_db_get_handle ();
  if (!db)
    {
      send_response_error (ctx, root, ERR_DB, "No database handle");
      return 0;
    }
  json_t *route = NULL;
  if (autopilot_load_route (db, ctx->player_id, &route) != 0)
    {
      send_response_error (ctx, root, ERR_DB_QUERY_FAILED,
			   "Failed to load autopilot route");
      return 0;
    }
  if (!route)
    {
      send_response_error (ctx, root, ERR_BAD_STATE,
			   "No resumable autopilot route");
      return 0;
    }
  if (!autopilot_reconcile_route (route, ctx->sector_id))
    {
      autopilot_save_route (db, ctx->player_id, route);
      json_decref (route);
      send_response_error (ctx, root, ERR_BAD_STATE,
			   "Current sector is outside the stored route");
      return 0;
    }
  json_t *path = json_object_get (route, "path");
  int next_index = (int) json_integer_value (json_object_get (route,
							   "next_index"));
  if (next_index >= (int) json_array_size (path))
    {
      json_decref (route);
      send_response_error (ctx, root, ERR_BAD_STATE, "Route is complete");
      return 0;
    }
  json_object_set_new (route, "mode", json_string (action));
  json_object_set_new (route, "state", json_string ("running"));
  if (autopilot_save_route (db, ctx->player_id, route) != 0)
    {
      json_decref (route);
      send_response_error (ctx, root, ERR_DB_QUERY_FAILED,
			   "Failed to persist autopilot control");
      return 0;
    }
  json_t *out = json_object ();
  json_object_set_new (out, "action", json_string (action));
  json_object_set_new (out, "state", json_string ("running"));
  json_object_set_new (out, "current_sector_id", json_integer (ctx->sector_id));
  json_object_set_new (out, "next_sector_id",
		       json_incref (json_array_get (path, (size_t) next_index)));
  json_decref (route);
  send_response_ok_take (ctx, root, "move.autopilot.controlled_v1", &out);
  return 0;
}
