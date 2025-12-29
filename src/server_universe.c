/* src/server_universe.c */
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <jansson.h>
#include <string.h>
#include <strings.h>
#include <inttypes.h>

/* local includes */
#include "server_universe.h"
#include "server_ports.h"
#include "database.h"
#include "game_db.h"
#include "server_cmds.h"
#include "server_rules.h"
// #include "server_bigbang.h"
#include "server_news.h"
#include "server_envelope.h"
#include "server_loop.h"
#include "server_communication.h"
#include "schemas.h"
#include "common.h"
#include "errors.h"
#include "globals.h"
#include "server_players.h"
#include "server_log.h"
#include "server_combat.h"
#include "database_cmd.h"
#include "server_ships.h"
#include "server_corporation.h"
#include "database_market.h"
#include "db/db_api.h"

/* Forward statics */
static void attach_sector_asset_counts (db_t *db,
                                        int sector_id,
                                        json_t *data_out);


int
no_zero_ship (db_t *db, int set_sector, int ship_id)
{
  if (!db)
    {
      return ERR_DB_CLOSED;
    }
  db_error_t err;


  if (set_sector > 0 && ship_id > 0)
    {
      const char *sql = "UPDATE ships SET sector = $1 WHERE id = $2;";


      if (!db_exec (db, sql,
                    (db_bind_t[]){db_bind_i32 (set_sector),
                                  db_bind_i32 (ship_id)}, 2, &err))
        {
          return err.code;
        }
      return 0;
    }
  const char *sql_mass = (db_backend (db) == DB_BACKEND_POSTGRES) ?
                         "UPDATE ships SET sector = floor(random() * 90) + 11 WHERE sector = 0;"
  :
                         "UPDATE ships SET sector = ABS(RANDOM() % 90) + 11 WHERE sector = 0;";


  if (!db_exec (db, sql_mass, NULL, 0, &err))
    {
      return err.code;
    }
  return 0;
}


void
ori_attach_db (db_t *db)
{
  (void)db;
}


int
ori_init_once (void)
{
  return 0;
}


void
ori_tick (int64_t now_ms)
{
  (void)now_ms;
}


json_t *
make_player_object (int64_t player_id)
{
  db_t *db = game_db_get_handle ();
  json_t *player = json_object (); json_object_set_new (player,
                                                        "id",
                                                        json_integer (
                                                          player_id));
  char *pname = NULL;
  if (db_player_name (db, player_id, &pname) == 0 && pname)
    {
      json_object_set_new (player, "name", json_string (pname)); free (pname);
    }
  return player;
}


int
cmd_sector_search (client_ctx_t *ctx, json_t *root)
{
  (void)ctx; (void)root; return 0;
}


json_t *
build_sector_scan_json (db_t *db, int sector_id, int player_id, bool holo)
{
  json_t *root = json_object (); if (!root)
    {
      return NULL;
    }
  json_object_set_new (root,
                       "server_tick",
                       json_integer (g_server_tick));
  json_t *basic = NULL;


  if (db_sector_basic_json (db, sector_id, &basic) == 0 && basic)
    {
      json_object_set_new (root, "sector_id", json_integer (sector_id));
      json_object_set_new (root, "name", json_object_get (basic, "name"));
      json_decref (basic);
    }
  json_t *adj = NULL;


  if (db_adjacent_sectors_json (db, sector_id, &adj) == 0 && adj)
    {
      json_object_set_new (root, "adjacent", adj);
    }
  json_t *ships = NULL; if (db_ships_at_sector_json (db,
                                                     player_id,
                                                     sector_id,
                                                     &ships) == 0)
    {
      json_object_set_new (root, "ships", ships ?: json_array ());
    }
  json_t *ports = NULL;


  if (db_ports_at_sector_json (db, sector_id, &ports) == 0)
    {
      json_object_set_new (root, "ports", ports ?: json_array ());
    }
  json_t *planets = NULL;


  if (db_planets_at_sector_json (db, sector_id, &planets) == 0)
    {
      json_object_set_new (root, "planets", planets ?: json_array ());
    }


  attach_sector_asset_counts (db, sector_id, root);
  (void)holo; return root;
}


void
cmd_sector_scan (client_ctx_t *ctx, json_t *root)
{
  db_t *db = game_db_get_handle (); int ship_id = h_get_active_ship_id (db,
                                                                        ctx->
                                                                        player_id);
  if (ship_id <= 0)
    {
      send_response_error (ctx, root, ERR_SHIP_NOT_FOUND, "No ship"); return;
    }
  int sid = db_get_ship_sector_id (db, ship_id);
  json_t *payload = build_sector_scan_json (db, sid, ctx->player_id, false);


  if (!payload)
    {
      send_response_error (ctx, root, ERR_NOMEM, "OOM"); return;
    }
  send_response_ok_take (ctx, root, "sector.scan", &payload);
}


void
cmd_sector_scan_density (void *ctx_in, json_t *root)
{
  (void)ctx_in; (void)root;
}


int
h_warp_exists (db_t *db, int from, int to)
{
  if (!db)
    {
      return 0;
    }
  db_res_t *res = NULL; db_error_t err; int has = 0;


  if (db_query (db,
                "SELECT 1 FROM sector_warps WHERE from_sector = $1 AND to_sector = $2 LIMIT 1;",
                (db_bind_t[]){db_bind_i32 (from), db_bind_i32 (to)},
                2,
                &res,
                &err))
    {
      if (db_res_step (res, &err))
        {
          has = 1;
        }
      db_res_finalize (res);
    }
  return has;
}


int
universe_init (void)
{
  return 0;
}


void
universe_shutdown (void)
{
}


int
cmd_move_describe_sector (client_ctx_t *ctx,
                          json_t *root)
{
  db_t *db = game_db_get_handle ();
  if (!db)
    {
      send_response_error (ctx, root, ERR_DB, "Database connection failed");
      return 0;
    }

  /* Default: caller's current sector */
  int sector_id = ctx->sector_id;

  json_t *data = json_object_get (root, "data");


  if (data && json_is_object (data))
    {
      json_t *j_sid = json_object_get (data, "sector_id");


      if (j_sid && json_is_integer (j_sid))
        {
          sector_id = (int) json_integer_value (j_sid);
        }
    }

  if (sector_id <= 0)
    {
      send_response_error (ctx, root, ERR_INVALID_SCHEMA, "Invalid sector_id");
      return 0;
    }

  /* Helper already exists; use it. */
  json_t *payload = build_sector_scan_json (db, sector_id, ctx->player_id,
                                            false);


  if (!payload)
    {
      /* This covers: sector not found, DB error, allocation failure. */
      send_response_error (ctx,
                           root,
                           ERR_NOT_FOUND,
                           "Sector not found or could not be described");
      return 0;
    }

  send_response_ok_take (ctx, root, "sector.info", &payload);
  return 0;
}


int
cmd_move_warp (client_ctx_t *ctx, json_t *root)
{
  db_t *db = game_db_get_handle (); if (!db)
    {
      return -1;
    }
  json_t *data = json_object_get (root, "data");
  int to = (int)json_integer_value (json_object_get (data, "to_sector_id"));
  if (!h_warp_exists (db, ctx->sector_id, to))
    {
      send_response_error (ctx, root, REF_NO_WARP_LINK, "No link"); return 0;
    }
  if (db_player_set_sector ( ctx->player_id, to) == 0)
    {
      ctx->sector_id = to;
      json_t *resp = json_object (); json_object_set_new (resp,
                                                          "to_sector_id",
                                                          json_integer (to));


      send_response_ok_take (ctx, root, "move.result", &resp);
    }
  else
    {
      send_response_error (ctx, root, ERR_DB, "Failed");
    }
  return 0;
}


int
cmd_move_pathfind (client_ctx_t *ctx, json_t *root)
{
  db_t *db = game_db_get_handle ();
  if (!db)
    {
      send_response_error (ctx, root, ERR_DB, "Database connection failed");
      return 0;
    }

  json_t *data = json_object_get (root, "data");
  int from = ctx->sector_id;
  int to = -1;


  if (data)
    {
      json_get_int_flexible (data, "from", &from);
      json_get_int_flexible (data, "to", &to);
    }

  if (to <= 0)
    {
      send_response_error (ctx,
                           root,
                           ERR_INVALID_SCHEMA,
                           "Target sector 'to' not specified");
      return 0;
    }

  int max_id = 0;
  db_error_t err;
  db_res_t *res = NULL;


  if (!db_query (db, "SELECT MAX(id) FROM sectors;", NULL, 0, &res, &err))
    {
      send_response_error (ctx, root, ERR_DB, "Failed to query universe size");
      return 0;
    }
  if (db_res_step (res, &err))
    {
      max_id = (int)db_res_col_i64 (res, 0, &err);
    }
  db_res_finalize (res);

  if (max_id <= 0)
    {
      send_response_error (ctx, root, ERR_NOT_FOUND, "No sectors in universe");
      return 0;
    }

  if (from <= 0 || from > max_id || to <= 0 || to > max_id)
    {
      send_response_error (ctx, root, ERR_SECTOR_NOT_FOUND, "Sector not found");
      return 0;
    }

  size_t N = (size_t)max_id + 1;
  unsigned char *avoid = calloc (N, 1);
  unsigned char *seen = calloc (N, 1);
  int *prev = malloc (N * sizeof(int));
  int *queue = malloc (N * sizeof(int));


  if (!avoid || !seen || !prev || !queue)
    {
      free (avoid); free (seen); free (prev); free (queue);
      send_response_error (ctx, root, ERR_NOMEM, "Out of memory");
      return 0;
    }
  for (size_t i = 0; i < N; ++i)
    {
      prev[i] = -1;
    }

  if (from == to)
    {
      json_t *steps = json_array ();


      json_array_append_new (steps, json_integer (from));
      json_t *out = json_object ();


      json_object_set_new (out, "path", steps);
      json_object_set_new (out, "hops", json_integer (0));
      send_response_ok_take (ctx, root, "move.pathfind", &out);
      free (avoid); free (seen); free (prev); free (queue);
      return 0;
    }

  int *head = malloc (N * sizeof(int));
  int *to_v = NULL;
  int *next = NULL;
  int edges = 0;


  for (size_t i = 0; i < N; i++)
    {
      head[i] = -1;
    }

  if (!db_query (db, "SELECT COUNT(*) FROM sector_warps;", NULL, 0, &res, &err))
    {
      // Memory cleanup
      free (head); free (avoid); free (seen); free (prev); free (queue);
      send_response_error (ctx,
                           root,
                           ERR_DB,
                           "Pathfind init failed (edge count)");
      return 0;
    }
  if (db_res_step (res, &err))
    {
      edges = (int)db_res_col_i64 (res, 0, &err);
    }
  db_res_finalize (res);

  if (edges > 0)
    {
      to_v = malloc ((size_t)edges * sizeof(int));
      next = malloc ((size_t)edges * sizeof(int));
      if (!to_v || !next)
        {
          free (to_v); free (next); free (head);
          free (avoid); free (seen); free (prev); free (queue);
          send_response_error (ctx, root, ERR_NOMEM, "Out of memory");
          return 0;
        }

      if (!db_query (db,
                     "SELECT from_sector, to_sector FROM sector_warps;",
                     NULL,
                     0,
                     &res,
                     &err))
        {
          free (to_v); free (next); free (head);
          free (avoid); free (seen); free (prev); free (queue);
          send_response_error (ctx,
                               root,
                               ERR_DB,
                               "Pathfind init failed (edge read)");
          return 0;
        }
      int e = 0;


      while (db_res_step (res, &err))
        {
          if (e >= edges)
            {
              break;
            }
          int u = (int)db_res_col_i64 (res, 0, &err);
          int v = (int)db_res_col_i64 (res, 1, &err);


          if (u > 0 && u <= max_id && v > 0 && v <= max_id)
            {
              to_v[e] = v;
              next[e] = head[u];
              head[u] = e++;
            }
        }
      db_res_finalize (res);
    }

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

  free (to_v); free (next); free (head);

  if (!found)
    {
      free (avoid); free (seen); free (prev); free (queue);
      send_response_error (ctx, root, ERR_NOT_FOUND, "Path not found");
      return 0;
    }

  int *stack = malloc (N * sizeof(int));
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
      free (stack); free (avoid); free (seen); free (prev); free (queue);
      send_response_error (ctx,
                           root,
                           ERR_SERVER_ERROR,
                           "Path reconstruction failed");
      return 0;
    }

  json_t *steps = json_array ();


  for (int i = sp - 1; i >= 0; --i)
    {
      json_array_append_new (steps, json_integer (stack[i]));
    }

  json_t *out = json_object ();


  json_object_set_new (out, "path", steps);
  json_object_set_new (out, "hops", json_integer (sp - 1));
  send_response_ok_take (ctx, root, "move.pathfind", &out);

  free (stack); free (avoid); free (seen); free (prev); free (queue);
  return 0;
}


static void
attach_sector_asset_counts (db_t *db, int sid, json_t *out)
{
  int ftrs = 0, armid = 0, limpet = 0;
  db_res_t *res = NULL; db_error_t err;
  const char *sql =
    "SELECT asset_type, SUM(quantity) FROM sector_assets WHERE sector = $1 GROUP BY asset_type;";
  if (db_query (db, sql, (db_bind_t[]){db_bind_i32 (sid)}, 1, &res, &err))
    {
      while (db_res_step (res, &err))
        {
          int type = db_res_col_i32 (res, 0, &err);
          int qty = db_res_col_i32 (res,
                                    1,
                                    &err);


          if (type == 2)
            {
              ftrs += qty;
            }
          else if (type == 1)
            {
              armid += qty;
            }
          else if (type == 4)
            {
              limpet += qty;
            }
        }
      db_res_finalize (res);
    }
  json_t *c = json_object ();


  json_object_set_new (c, "fighters", json_integer (ftrs));


  json_object_set_new (c, "mines", json_integer (armid + limpet));
  json_object_set_new (out, "counts", c);
}


void
cmd_sector_info (client_ctx_t *ctx, int fd, json_t *root, int sid, int pid)
{
  (void)ctx; (void)fd; (void)root; (void)sid; (void)pid;
}


json_t *
build_sector_info_json (db_t *db, int sid)
{
  return build_sector_scan_json (db, sid, 0, false);
}


int
cmd_move_scan (client_ctx_t *ctx, json_t *root)
{
  (void)ctx; (void)root; return 0;
}


int
cmd_sector_set_beacon (client_ctx_t *ctx, json_t *root)
{
  (void)ctx; (void)root; return 0;
}


int
cmd_move_transwarp (client_ctx_t *ctx, json_t *root)
{
  (void)ctx; (void)root; return 0;
}


int
fer_init_once (void)
{
  return 0;
}


void
fer_tick (int64_t now_ms)
{
  (void)now_ms;
}


void
fer_attach_db (db_t *db)
{
  (void)db;
}


void
iss_init (db_t *db)
{
  (void)db;
}


void
iss_tick (db_t *db, int64_t now_ms)
{
  (void)db; (void)now_ms;
}


int
db_pick_adjacent (db_t *db, int sid)
{
  (void)db; (void)sid; return 0;
}


int
iss_init_once (void)
{
  return 1;
}

