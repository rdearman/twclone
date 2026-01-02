#include <string.h>
#include <stdlib.h>
#include <time.h>
//local includes
#include "server_autopilot.h"
#include "server_config.h"
#include "server_envelope.h"
#include "server_universe.h"
#include "database.h"
#include "db/db_api.h"
#include "common.h"
#include "server_log.h"


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
  {
    db_error_t err;


    db_error_clear (&err);

    db_res_t *res = NULL;


    if (!db_query (db, "SELECT MAX(sector_id) FROM sectors;", NULL, 0, &res, &err))
      {
        LOGE (
          "cmd_move_autopilot_start: MAX(sectors.sector_id) failed: %s (code=%d backend=%d)",
          err.message,
          err.code,
          err.backend_code);
        send_response_error (ctx, root, ERR_SECTOR_NOT_FOUND, "No sectors");
        return 1;
      }

    if (db_res_step (res, &err))
      {
        max_id = (int) db_res_col_i64 (res, 0, &err);
      }

    db_res_finalize (res);

    if (err.code != 0 || max_id <= 0)
      {
        send_response_error (ctx, root, ERR_SECTOR_NOT_FOUND, "No sectors");
        return 1;
      }
  }


  /* Clamp from/to */
  if (from <= 0 || from > max_id || to <= 0 || to > max_id)
    {
      send_response_error (ctx, root, ERR_SECTOR_NOT_FOUND, "Sector not found");
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
      free (avoid); free (seen); free (prev); free (queue);
      send_response_error (ctx, root, ERR_PLANET_NOT_FOUND, "Out of memory");
      return 1;
    }
  for (int i = 0; i <= max_id; ++i)
    {
      head[i] = -1;
    }

  /* pass 1: count edges */
  {
    db_error_t err;


    db_error_clear (&err);
    db_res_t *res = NULL;


    if (!db_query (db, "SELECT COUNT(*) FROM sector_warps;", NULL, 0, &res,
                   &err))
      {
        LOGE (
          "cmd_move_autopilot_start: COUNT(sector_warps) failed: %s (code=%d backend=%d)",
          err.message,
          err.code,
          err.backend_code);
        free (head); free (avoid); free (seen); free (prev); free (queue);
        send_response_error (ctx,
                             root,
                             ERR_PLANET_NOT_FOUND,
                             "Pathfind init failed");
        return 1;
      }

    if (db_res_step (res, &err))
      {
        edges = (int) db_res_col_i64 (res, 0, &err);
      }
    db_res_finalize (res);

    if (err.code != 0 || edges < 0)
      {
        free (head); free (avoid); free (seen); free (prev); free (queue);
        send_response_error (ctx,
                             root,
                             ERR_PLANET_NOT_FOUND,
                             "Pathfind init failed");
        return 1;
      }
  }

  to_v = (int *) malloc ((size_t) edges * sizeof (int));
  next = (int *) malloc ((size_t) edges * sizeof (int));
  if ((edges > 0) && (!to_v || !next))
    {
      free (to_v); free (next); free (head);
      free (avoid); free (seen); free (prev); free (queue);
      send_response_error (ctx, root, ERR_PLANET_NOT_FOUND, "Out of memory");
      return 1;
    }

  /* pass 2: read edges and build adjacency */
  {
    db_error_t err;


    db_error_clear (&err);
    db_res_t *res = NULL;


    if (!db_query (db,
                   "SELECT from_sector, to_sector FROM sector_warps;",
                   NULL,
                   0,
                   &res,
                   &err))
      {
        LOGE (
          "cmd_move_autopilot_start: sector_warps read failed: %s (code=%d backend=%d)",
          err.message,
          err.code,
          err.backend_code);
        free (to_v); free (next); free (head);
        free (avoid); free (seen); free (prev); free (queue);
        send_response_error (ctx,
                             root,
                             ERR_PLANET_NOT_FOUND,
                             "Pathfind init failed");
        return 1;
      }

    int e = 0;


    while (db_res_step (res, &err))
      {
        int u = (int) db_res_col_i64 (res, 0, &err);
        int v = (int) db_res_col_i64 (res, 1, &err);


        if (e >= edges)
          {
            break;             /* safety if count lied */
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
        free (to_v); free (next); free (head);
        free (avoid); free (seen); free (prev); free (queue);
        send_response_error (ctx,
                             root,
                             ERR_PLANET_NOT_FOUND,
                             "Pathfind init failed");
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
      free (avoid); free (seen); free (prev); free (queue);
      send_response_error (ctx, root, REF_SAFE_ZONE_ONLY, "Path not found");
      return 1;
    }

  /* reconstruct path */
  int *stack = (int *) malloc (N * sizeof (int));


  if (!stack)
    {
      free (avoid); free (seen); free (prev); free (queue);
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
      free (avoid); free (seen); free (prev); free (queue);
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
  json_object_set_new (out, "path", steps);
  json_object_set_new (out, "hops", json_integer (hops));

  send_response_ok_take (ctx, root, "move.autopilot.route_v1", &out);
  return 0;
}


/*
 * cmd_move_autopilot_status
 *
 * Reports client-side only status.
 */
int
cmd_move_autopilot_status (client_ctx_t *ctx, json_t *root)
{
  if (!ctx)
    {
      return 1;
    }
  json_t *out = json_object ();


  json_object_set_new (out, "current_sector_id",
                       json_integer (ctx->sector_id));
  json_object_set_new (out, "last_error", json_string (""));
  send_response_ok_take (ctx, root, "move.autopilot.status_v1", &out);
  return 0;
}


/*
 * cmd_move_autopilot_stop
 *
 * Acknowledges the stop command (no server state to clear).
 */
int
cmd_move_autopilot_stop (client_ctx_t *ctx, json_t *root)
{
  if (!ctx)
    {
      return 1;
    }
  json_t *out = json_object ();


  json_object_set_new (out, "current_sector_id",
                       json_integer (ctx->sector_id));
  json_object_set_new (out, "stopped_at",
                       json_integer ((json_int_t) time (NULL)));
  send_response_ok_take (ctx, root, "move.autopilot.stopped_v1", &out);
  return 0;
}

