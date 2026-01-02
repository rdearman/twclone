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

/* ============ Ferengi Trader Globals ============ */
static db_t *g_fer_db = NULL;
static int g_fer_inited = 0;
static int g_fer_corp_id = 0;
static int g_fer_player_id = 0;

/* ============ ISS (Interpolar Security Station) Globals ============ */
static int g_iss_inited = 0;
static int g_iss_id = 0;
static int g_iss_sector = 0;
static int g_stardock_sector = 0;
static int g_patrol_budget = 0;
#define kIssPatrolBudget 5000

/* ============ Navigation Helper Functions ============ */

/* Breadth-first search to find one hop toward goal sector from start sector */
int
nav_next_hop (db_t *db, int start, int goal)
{
  if (!db || start <= 0 || goal <= 0 || start == goal)
    {
      return 0;
    }

  enum
  { MAX_Q = 4096, MAX_SEEN = 8192 };
  int q[MAX_Q], head = 0, tail = 0;

  typedef struct
  {
    int key, prev;
  } kv_t;
  kv_t seen[MAX_SEEN];
  int seen_n = 0;

  /* Helper: lookup seen table */
  auto int seen_get (int key)
  {
    for (int i = 0; i < seen_n; ++i)
      {
        if (seen[i].key == key)
          {
            return i;
          }
      }
    return -1;
  };

  /* Helper: insert into seen table */
  auto int seen_put (int key, int prev)
  {
    if (seen_n >= MAX_SEEN)
      {
        return -1;
      }
    seen[seen_n].key = key;
    seen[seen_n].prev = prev;
    return seen_n++;
  };

  /* Initialize BFS queue with start sector */
  q[0] = start;
  head = 0;
  tail = 1;
  seen_put (start, -1);

  db_error_t err;
  int found = -1;

  /* BFS to find goal */
  while (head != tail && found == -1)
    {
      int cur = q[head++ % MAX_Q];

      /* Query adjacent sectors from sector_warps table */
      const char *sql = "SELECT to_sector FROM sector_warps WHERE from_sector=$1;";
      db_bind_t params[] = { db_bind_i32 (cur) };
      db_res_t *res = NULL;

      if (!db_query (db, sql, params, 1, &res, &err))
        {
          /* Query failed; return 0 to indicate no path */
          return 0;
        }

      while (db_res_step (res, &err))
        {
          int nb = db_res_col_int (res, 0, &err);

          if (seen_get (nb) != -1)
            {
              continue;
            }
          seen_put (nb, cur);
          if (nb == goal)
            {
              found = nb;
              break;
            }
          if ((tail - head) < (MAX_Q - 1))
            {
              q[tail++ % MAX_Q] = nb;
            }
        }

      db_res_finalize (res);
    }

  if (found == -1)
    {
      return 0;
    }

  /* Reconstruct one hop toward goal */
  int step = found, prev = -2;

  for (;;)
    {
      int i = seen_get (step);

      if (i < 0)
        {
          break;
        }
      prev = seen[i].prev;
      if (prev == -1)
        {
          break; /* step == start */
        }
      if (prev == start)
        {
          return step; /* first hop away from start */
        }
      step = prev;
    }
  return step; /* neighbour fallback */
}

/* Get random adjacent sector */
int
nav_random_neighbor (db_t *db, int sector)
{
  if (!db || sector <= 0)
    {
      return 0;
    }

  db_error_t err;
  const char *sql =
    "SELECT to_sector FROM sector_warps WHERE from_sector=$1 ORDER BY RANDOM() LIMIT 1;";
  db_bind_t params[] = { db_bind_i32 (sector) };
  db_res_t *res = NULL;

  if (!db_query (db, sql, params, 1, &res, &err))
    {
      return 0;
    }

  int result = 0;
  if (db_res_step (res, &err))
    {
      result = db_res_col_int (res, 0, &err);
    }
  db_res_finalize (res);

  return result;
}

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
      const char *sql = "UPDATE ships SET sector_id = $1 WHERE ship_id = $2;";


      if (!db_exec (db, sql,
                    (db_bind_t[]){db_bind_i32 (set_sector),
                                  db_bind_i32 (ship_id)}, 2, &err))
        {
          return err.code;
        }
      return 0;
    }
  const char *sql_mass = (db_backend (db) == DB_BACKEND_POSTGRES) ?
                         "UPDATE ships SET sector_id = floor(random() * 90) + 11 WHERE sector_id = 0;"
  :
                         "UPDATE ships SET sector_id = ABS(RANDOM() % 90) + 11 WHERE sector_id = 0;";


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
  client_ctx_t *ctx = (client_ctx_t *)ctx_in;
  
  /* Get target sector from context or request data */
  int target_sector = ctx->sector_id;
  json_t *jdata = json_object_get(root, "data");
  json_t *jsec = (jdata && json_is_object(jdata)) ? 
                 json_object_get(jdata, "sector_id") : NULL;
  if (jsec && json_is_integer(jsec))
    target_sector = (int)json_integer_value(jsec);
  
  if (target_sector <= 0)
    target_sector = ctx->sector_id;
  
  db_t *db = game_db_get_handle();
  if (!db)
    {
      send_response_error(ctx, root, ERR_DB, "Database connection failed");
      return;
    }
  
  db_error_t err;
  db_res_t *res = NULL;
  json_t *payload = json_object();
  json_t *sectors = json_array();
  
  /* Build sector list: current + adjacent */
  const char *sql_sectors = 
    "WITH sector_list AS ("
    "  SELECT $1::int as sector_id "
    "  UNION "
    "  SELECT to_sector FROM sector_warps WHERE from_sector = $1 "
    ") "
    "SELECT DISTINCT sector_id FROM sector_list ORDER BY sector_id;";
  
  if (!db_query(db, sql_sectors, (db_bind_t[]){db_bind_i32(target_sector)}, 1, &res, &err))
    {
      send_response_error(ctx, root, ERR_DB, "Failed to get sector list");
      json_decref(payload);
      json_decref(sectors);
      return;
    }
  
  /* For each sector, calculate total density */
  while (db_res_step(res, &err))
    {
      int sector_id = (int)db_res_col_i64(res, 0, &err);
      
      /* Calculate density for this sector:
         Fighters: 1 per
         Mines: 1 per
         Beacons: 1
         Ships: 10 per
         Planets: 100 per
      */
      const char *density_sql = 
        "SELECT "
        "  COALESCE((SELECT fighters FROM sector_assets WHERE sector_id = $1), 0) + "
        "  COALESCE((SELECT mines FROM sector_assets WHERE sector_id = $1), 0) + "
        "  CASE WHEN EXISTS(SELECT 1 FROM sectors WHERE sector_id = $1 AND beacon IS NOT NULL) THEN 1 ELSE 0 END + "
        "  (SELECT COALESCE(COUNT(*), 0) * 10 FROM ships WHERE sector_id = $1) + "
        "  (SELECT COALESCE(COUNT(*), 0) * 100 FROM planets WHERE sector_id = $1) "
        "as total_density;";
      
      db_res_t *density_res = NULL;
      if (db_query(db, density_sql, (db_bind_t[]){db_bind_i32(sector_id)}, 1, &density_res, &err))
        {
          int density = 0;
          if (db_res_step(density_res, &err))
            {
              density = (int)db_res_col_i64(density_res, 0, &err);
            }
          db_res_finalize(density_res);
          
          /* Only include sectors with non-zero density */
          if (density > 0)
            {
              json_array_append_new(sectors, json_integer(sector_id));
              json_array_append_new(sectors, json_integer(density));
            }
        }
    }
  db_res_finalize(res);
  
  json_object_set_new(payload, "sectors", sectors);
  
  send_response_ok_take(ctx, root, "sector.density.scan", &payload);
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


  if (!db_query (db, "SELECT MAX(sector_id) FROM sectors;", NULL, 0, &res,
                 &err))
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
    "SELECT asset_type, SUM(quantity) FROM sector_assets WHERE sector_id = $1 GROUP BY asset_type;";
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
  (void)fd;
  db_t *db = game_db_get_handle();
  if (!db)
    {
      send_response_error (ctx, root, ERR_DB_CLOSED, "Database connection failed");
      return;
    }

  json_t *payload = build_sector_info_json (db, sid);
  if (!payload)
    {
      send_response_error (ctx, root, ERR_PLANET_NOT_FOUND,
                           "Out of memory building sector info");
      return;
    }

  send_response_ok_take (ctx, root, "sector.info", &payload);
}


json_t *
build_sector_info_json (db_t *db, int sid)
{
  return build_sector_scan_json (db, sid, 0, false);
}


int
cmd_move_scan (client_ctx_t *ctx, json_t *root)
{
  if (!ctx)
    {
      return 1;
    }
  db_t *db = game_db_get_handle();
  if (!db)
    {
      send_response_error (ctx, root, ERR_DB_CLOSED, "Database connection failed");
      return 0;
    }

  h_decloak_ship (db, h_get_active_ship_id (db, ctx->player_id));
  TurnConsumeResult tc = h_consume_player_turn (db, ctx, 1);

  if (tc != TURN_CONSUME_SUCCESS)
    {
      return handle_turn_consumption_error (ctx, tc, "move.scan", root, NULL);
    }

  int sector_id = (ctx->sector_id > 0) ? ctx->sector_id : 1;
  LOGI ("[move.scan] sector_id=%d\n", sector_id);

  json_t *payload = build_sector_scan_json (db, sector_id, ctx->player_id, false);
  if (!payload)
    {
      send_response_error (ctx, root, ERR_SECTOR_NOT_FOUND, "Sector not found");
      return 0;
    }

  send_response_ok_take (ctx, root, "sector.scan", &payload);
  return 0;
}


int
cmd_sector_set_beacon (client_ctx_t *ctx, json_t *root)
{
  if (!ctx || !root)
    {
      return 1;
    }
  
  db_t *db = game_db_get_handle();
  if (!db)
    {
      send_response_error (ctx, root, ERR_DB_CLOSED, "Database connection failed");
      return 1;
    }

  h_decloak_ship (db, h_get_active_ship_id (db, ctx->player_id));
  
  json_t *jdata = json_object_get (root, "data");
  json_t *jsector_id = json_object_get (jdata, "sector_id");
  json_t *jtext = json_object_get (jdata, "text");

  if (!json_is_integer (jsector_id) || !json_is_string (jtext))
    {
      send_response_error (ctx, root, ERR_INVALID_SCHEMA, "Invalid request schema");
      return 1;
    }

  int req_sector_id = (int) json_integer_value (jsector_id);

  if (ctx->sector_id != req_sector_id)
    {
      send_response_error (ctx, root, REF_NOT_IN_SECTOR,
                           "Player is not in the specified sector.");
      return 1;
    }

  if (req_sector_id >= 1 && req_sector_id <= 10)
    {
      send_response_error (ctx, root, REF_TURN_COST_EXCEEDS,
                           "Cannot set a beacon in FedSpace.");
      return 1;
    }

  const char *beacon_text = json_string_value (jtext);
  if (!beacon_text)
    {
      beacon_text = "";
    }
  
  if ((int) strlen (beacon_text) > 80)
    {
      send_response_error (ctx, root, REF_NOT_IN_SECTOR,
                           "Beacon text is too long (max 80 characters).");
      return 1;
    }

  /* Update beacon */
  db_error_t err;
  const char *sql = "UPDATE sectors SET beacon_text = $1, beacon_owner_id = $2 WHERE sector_id = $3;";
  db_bind_t binds[] = {
    db_bind_text(beacon_text),
    db_bind_i32(ctx->player_id),
    db_bind_i32(req_sector_id)
  };
  
  if (!db_exec (db, sql, binds, 3, &err))
    {
      send_response_error (ctx, root, ERR_DB, "Database error updating beacon.");
      return 1;
    }

  json_t *payload = build_sector_info_json (db, req_sector_id);
  if (!payload)
    {
      send_response_error (ctx, root, ERR_PLANET_NOT_FOUND,
                           "Out of memory building sector info");
      return 1;
    }

  send_response_ok_take (ctx, root, "sector.beacon_set", &payload);
  return 0;
}


int
cmd_move_transwarp (client_ctx_t *ctx, json_t *root)
{
  db_t *db = game_db_get_handle();
  if (!db)
    {
      send_response_error (ctx, root, ERR_DB_CLOSED, "No database handle");
      return 0;
    }

  if (!ctx || ctx->player_id <= 0)
    {
      send_response_refused_steal (ctx, root, ERR_NOT_AUTHENTICATED,
                                   "Not authenticated", NULL);
      return 0;
    }

  json_t *jdata = json_object_get (root, "data");
  int to_sector_id = 0;

  if (json_is_object (jdata))
    {
      json_t *jto = json_object_get (jdata, "to_sector_id");
      if (json_is_integer (jto))
        {
          to_sector_id = (int) json_integer_value (jto);
        }
    }

  if (to_sector_id <= 0)
    {
      send_response_refused_steal (ctx, root, ERR_INVALID_ARG,
                                   "Target sector not specified", NULL);
      return 0;
    }

  int ship_id = h_get_active_ship_id (db, ctx->player_id);
  if (ship_id <= 0)
    {
      send_response_refused_steal (ctx, root, ERR_NO_ACTIVE_SHIP,
                                   "No active ship found.", NULL);
      return 0;
    }

  /* Check transwarp capability */
  db_error_t err;
  db_res_t *res = NULL;
  const char *sql = "SELECT 1 FROM ships WHERE ship_id = $1 AND transwarp_enabled = true LIMIT 1;";
  
  int has_transwarp = 0;
  if (db_query (db, sql, (db_bind_t[]){db_bind_i32(ship_id)}, 1, &res, &err))
    {
      if (res && db_res_step(res, &err))
        {
          has_transwarp = 1;
        }
      if (res) db_res_finalize(res);
    }

  if (!has_transwarp)
    {
      send_response_refused_steal (ctx, root, 1453,
                                   "Ship does not have transwarp capability.", NULL);
      return 0;
    }

  /* Try to perform transwarp */
  TurnConsumeResult tc = h_consume_player_turn (db, ctx, 1);
  if (tc != TURN_CONSUME_SUCCESS)
    {
      return handle_turn_consumption_error (ctx, tc, "move.transwarp", root, NULL);
    }

  /* Update player sector */
  sql = "UPDATE players SET sector_id = $1 WHERE player_id = $2;";
  if (!db_exec (db, sql, (db_bind_t[]){db_bind_i32(to_sector_id), 
                                        db_bind_i32(ctx->player_id)}, 2, &err))
    {
      send_response_error (ctx, root, ERR_DB, "Database error during transwarp");
      return 0;
    }

  ctx->sector_id = to_sector_id;
  
  json_t *data = json_object();
  json_object_set_new(data, "sector_id", json_integer(to_sector_id));
  json_object_set_new(data, "status", json_string("transwarp_complete"));

  send_response_ok_take (ctx, root, "move.transwarp", &data);
  return 0;
}


int
fer_init_once (void)
{
  if (g_fer_inited)
    {
      return 1;
    }
  if (!g_fer_db)
    {
      LOGW ("[fer] no DB handle; traders disabled");
      return 0;
    }

  db_error_t err;
  db_res_t *res = NULL;

  const char *sql_find_corp_info =
    "SELECT id, owner_id FROM corporations WHERE tag='FENG' LIMIT 1;";

  if (db_query (g_fer_db, sql_find_corp_info, NULL, 0, &res, &err))
    {
      if (res && db_res_step (res, &err))
        {
          g_fer_corp_id = db_res_col_i32 (res, 0, &err);
          g_fer_player_id = db_res_col_i32 (res, 1, &err);
          if (g_fer_player_id == 0)
            {
              g_fer_player_id = 1;
            }
        }
      if (res) db_res_finalize (res);
    }

  if (g_fer_corp_id == 0)
    {
      LOGW ("[fer] Ferengi Alliance corporation not found. Traders disabled.");
      return 0;
    }

  int home = 0;
  const char *sql_find_home_sector =
    "SELECT sector_id FROM planets WHERE planet_id=2 LIMIT 1;";

  res = NULL;
  if (db_query (g_fer_db, sql_find_home_sector, NULL, 0, &res, &err))
    {
      if (res && db_res_step (res, &err))
        {
          home = db_res_col_i32 (res, 0, &err);
        }
      if (res) db_res_finalize (res);
    }

  if (home <= 0)
    {
      LOGW ("[fer] Ferengi homeworld not found; disabling traders");
      return 0;
    }

  int ship_type_id = 0;
  const char *sql_find_shiptype =
    "SELECT ship_type_id FROM ship_types WHERE name='Ferengi Warship' LIMIT 1;";

  res = NULL;
  if (db_query (g_fer_db, sql_find_shiptype, NULL, 0, &res, &err))
    {
      if (res && db_res_step (res, &err))
        {
          ship_type_id = db_res_col_i32 (res, 0, &err);
        }
      if (res) db_res_finalize (res);
    }

  if (ship_type_id == 0)
    {
      LOGE ("[fer] No suitable shiptype found. Disabling.");
      return 0;
    }

  g_fer_inited = 1;
  LOGI ("[fer] Ferengi traders initialized");
  return 1;
}


void
fer_tick (db_t *db, int64_t now_ms)
{
  (void) now_ms;
  if (!g_fer_inited)
    {
      if (!fer_init_once ())
        {
          return;
        }
    }
  
  if (!db)
    {
      db = g_fer_db;
      if (!db) return;
    }

  LOGD ("[fer] tick: traders system active");
}


void
fer_attach_db (db_t *db)
{
  g_fer_db = db;
}


void
iss_init (db_t *db)
{
  if (!db)
    {
      return;
    }
  if (!iss_init_once ())
    {
      LOGW ("[iss] ISS initialization failed");
    }
}


void
iss_tick (db_t *db, int64_t now_ms)
{
  (void) now_ms;
  if (!g_iss_inited)
    {
      if (!iss_init_once ())
        {
          return;
        }
    }
  if (iss_try_consume_summon (db))
    {
      return;
    }
  iss_patrol_step (db);
}


int
db_pick_adjacent (db_t *db, int sid)
{
  if (!db || sid <= 0)
    {
      return 0;
    }

  db_error_t err;
  db_res_t *res = NULL;
  const char *sql = "SELECT to_sector_id FROM wormholes WHERE from_sector_id = $1 ORDER BY RANDOM() LIMIT 1;";
  
  if (!db_query (db, sql, (db_bind_t[]){db_bind_i32(sid)}, 1, &res, &err))
    {
      return 0;
    }

  if (!res)
    {
      return 0;
    }

  int adjacent_id = 0;
  if (db_res_step(res, &err))
    {
      adjacent_id = (int) db_res_col_i32(res, 0, &err);
    }
  
  db_res_finalize(res);
  return adjacent_id;
}


int
iss_init_once (void)
{
  if (g_iss_inited)
    {
      return 1;
    }
  db_t *db = game_db_get_handle();
  if (!db)
    {
      return 0;
    }
  g_stardock_sector = db_get_stardock_sector (db);
  if (g_stardock_sector <= 0)
    {
      LOGW ("[iss] Stardock sector not found");
      return 0;
    }
  int sector = 0;

  if (!db_get_iss_player (db, &g_iss_id, &sector))
    {
      LOGW ("[iss] ISS player not found in database");
      return 0;
    }
  if (sector <= 0)
    {
      g_iss_sector = g_stardock_sector;
      iss_move_to (db, g_stardock_sector, 1, "initialization");
    }
  else
    {
      g_iss_sector = sector;
    }
  g_patrol_budget = kIssPatrolBudget;
  srand ((unsigned) time (NULL));
  g_iss_inited = 1;
  LOGI ("[iss] ISS initialization complete (sector=%d)", g_iss_sector);
  return 1;
}

/* ============ ISS Helper Stubs (TODO: Full implementation) ============ */

int
db_get_stardock_sector (db_t *db)
{
  (void)db;
  /* TODO: Query database for stardock sector */
  return 1;
}

int
db_get_iss_player (db_t *db, int *out_player_id, int *out_sector)
{
  (void)db;
  if (out_player_id) *out_player_id = 1;
  if (out_sector) *out_sector = 1;
  /* TODO: Query database for ISS player */
  return 1;
}

void
iss_move_to (db_t *db, int sector_id, int warp_enabled, const char *reason)
{
  (void)db; (void)sector_id; (void)warp_enabled; (void)reason;
  /* TODO: Move ISS to sector */
}

int
iss_try_consume_summon (db_t *db)
{
  (void)db;
  /* TODO: Check if ISS should respond to summon */
  return 0;
}

void
iss_patrol_step (db_t *db)
{
  (void)db;
  /* TODO: ISS patrol movement logic */
}

