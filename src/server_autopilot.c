#include <string.h>
#include <stdlib.h>
#include <sqlite3.h>
//local includes
#include "server_autopilot.h"
#include "server_config.h"
#include "server_envelope.h"
#include "server_universe.h"
#include "database.h"


int
cmd_move_autopilot_start (client_ctx_t *ctx, json_t *root)
{
  if (!ctx)
    {
      return 1;
    }
  sqlite3 *db = db_get_handle ();
  int max_id = 0;
  sqlite3_stmt *st = NULL;
  json_t *data = root ? json_object_get (root, "data") : NULL;
  // default from = current sector
  int from = (ctx->sector_id > 0) ? ctx->sector_id : 1;


  if (data)
    {
      int tmp;


      if (json_get_int_flexible (data, "from", &tmp) ||
          json_get_int_flexible (data, "from_sector_id", &tmp))
        {
          from = tmp;
        }
    }
  // to = required
  int to = -1;


  if (data)
    {
      int tmp;


      if (json_get_int_flexible (data, "to", &tmp) ||
          json_get_int_flexible (data, "to_sector_id", &tmp))
        {
          to = tmp;
        }
    }
  if (to <= 0)
    {
      send_response_error (ctx,
                           root,
                           ERR_SECTOR_NOT_FOUND,
                           "Target sector not specified");
      return 1;
    }
  /* Get Max Sector ID to size arrays */
  db_mutex_lock ();
  if (sqlite3_prepare_v2 (db, "SELECT MAX(id) FROM sectors", -1, &st, NULL) ==
      SQLITE_OK && sqlite3_step (st) == SQLITE_ROW)
    {
      max_id = sqlite3_column_int (st, 0);
    }
  if (st)
    {
      sqlite3_finalize (st);
      st = NULL;
    }
  db_mutex_unlock ();
  if (max_id <= 0)
    {
      send_response_error (ctx, root, ERR_SECTOR_NOT_FOUND, "No sectors");
      return 1;
    }
  /* Clamp from/to to valid range quickly */
  if (from <= 0 || from > max_id || to > max_id)
    {
      send_response_error (ctx, root, ERR_SECTOR_NOT_FOUND,
                           "Sector not found");
      return 1;
    }
  /* allocate simple arrays sized max_id+1 */
  size_t N = (size_t) max_id + 1;
  unsigned char *avoid = (unsigned char *) calloc (N, 1);
  int *prev = (int *) malloc (N * sizeof (int));
  unsigned char *seen = (unsigned char *) calloc (N, 1);
  int *queue = (int *) malloc (N * sizeof (int));


  if (!avoid || !prev || !seen || !queue)
    {
      free (avoid);
      free (prev);
      free (seen);
      free (queue);
      send_response_error (ctx, root, ERR_PLANET_NOT_FOUND, "Out of memory");
      return 1;
    }
  /* Fill avoid */
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
  /* If target or source is avoided, unreachable */
  if (avoid[to] || avoid[from])
    {
      free (avoid);
      free (prev);
      free (seen);
      free (queue);
      send_response_error (ctx, root, REF_SAFE_ZONE_ONLY, "Path not found");
      return 1;
    }
  /* Trivial path */
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
      free (prev);
      free (seen);
      free (queue);
      return 0;
    }
  /* Prepare neighbor query once */
  db_mutex_lock ();
  int rc = sqlite3_prepare_v2 (db,
                               "SELECT to_sector FROM sector_warps WHERE from_sector = ?1",
                               -1,
                               &st,
                               NULL);


  db_mutex_unlock ();
  if (rc != SQLITE_OK || !st)
    {
      free (avoid);
      free (prev);
      free (seen);
      free (queue);
      send_response_error (ctx,
                           root,
                           ERR_PLANET_NOT_FOUND, "Pathfind init failed");
      return 1;
    }
  /* BFS */
  for (int i = 0; i <= max_id; ++i)
    {
      prev[i] = -1;
    }
  int qh = 0, qt = 0;


  queue[qt++] = from;
  seen[from] = 1;
  int found = 0;


  while (qh < qt)
    {
      int u = queue[qh++];


      /* fetch neighbors of u */
      db_mutex_lock ();
      sqlite3_reset (st);
      sqlite3_clear_bindings (st);
      sqlite3_bind_int (st, 1, u);
      while ((rc = sqlite3_step (st)) == SQLITE_ROW)
        {
          int v = sqlite3_column_int (st, 0);


          if (v <= 0 || v > max_id)
            {
              continue;
            }
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
              /* still finish stepping rows to keep stmt sane, or break after unlock */
            }
        }
      db_mutex_unlock ();
      if (found)
        {
          break;
        }
    }
  /* finalize stmt */
  db_mutex_lock ();
  sqlite3_finalize (st);
  db_mutex_unlock ();
  if (!found)
    {
      free (avoid);
      free (prev);
      free (seen);
      free (queue);
      send_response_error (ctx, root, REF_SAFE_ZONE_ONLY, "Path not found");
      return 1;
    }
  /* Reconstruct path */
  json_t *steps = json_array ();
  int cur = to;
  int hops = 0;
  /* backtrack into a simple stack */
  int *stack = (int *) malloc (N * sizeof (int));


  if (!stack)
    {
      free (avoid);
      free (prev);
      free (seen);
      free (queue);
      send_response_error (ctx, root, ERR_PLANET_NOT_FOUND, "Out of memory");
      return 1;
    }
  int sp = 0;


  while (cur != -1)
    {
      stack[sp++] = cur;
      if (cur == from)
        {
          break;
        }
      cur = prev[cur];
    }
  /* If we didn’t reach 'from', something’s off */
  if (stack[sp - 1] != from)
    {
      free (stack);
      free (avoid);
      free (prev);
      free (seen);
      free (queue);
      send_response_error (ctx, root, REF_SAFE_ZONE_ONLY, "Path not found");
      return 1;
    }
  /* reverse into JSON steps: from .. to */
  for (int i = sp - 1; i >= 0; --i)
    {
      json_array_append_new (steps, json_integer (stack[i]));
    }
  hops = sp - 1;
  free (stack);
  /* Build response */
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

