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
#include "db/sql_driver.h"

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

/* ============ Orion (Orion Syndicate) Globals ============ */
static db_t *ori_db = NULL;
static bool ori_initialized = false;
static int ori_owner_id = -1;
static int ori_home_sector_id = -1;

/* ============ ISS Helper Function Forward Declarations ============ */
static int db_get_stardock_sector (db_t *db);
static int db_get_iss_player (db_t *db, int *out_player_id, int *out_sector);
static void iss_move_to (db_t *db, int sector_id, int warp_enabled, const char *reason);
static int iss_try_consume_summon (db_t *db);
static void iss_patrol_step (db_t *db);

/* ============ Ferengi Event Logging ============ */
/* Log events to engine_events table with formatted payload */
static void
fer_event_json (const char *type, int sector_id, const char *fmt, ...)
{
  if (!g_fer_db)
    {
      LOGE ("fer_event_json: Received NULL DB handle. Cannot proceed.");
      return;
    }
  if (!type)
    {
      LOGE ("fer_event_json: Event type is NULL. Cannot proceed.");
      return;
    }

  /* Format payload from variadic args */
  char payload[512];
  va_list ap;
  va_start (ap, fmt);
  vsnprintf (payload, sizeof payload, fmt, ap);
  va_end (ap);

  /* INSERT into engine_events(type, sector_id, payload, ts) */
  db_error_t err;
  db_error_clear (&err);
  
  const char *sql = "INSERT INTO engine_events(type, sector_id, payload, ts) "
                    "VALUES ({1}, {2}, {3}, NOW())";
  db_bind_t params[] = {
    db_bind_text (type),
    db_bind_i32 (sector_id),
    db_bind_text (payload)
  };

  char sql_converted[512];
  sql_build(g_fer_db, sql, sql_converted, sizeof(sql_converted));
  if (!db_exec (g_fer_db, sql_converted, params, 3, &err))
    {
      LOGE ("fer_event_json: Failed to insert event: %s", err.message);
    }
}

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
      const char *sql = "SELECT to_sector FROM sector_warps WHERE from_sector={1};";
      db_bind_t params[] = { db_bind_i32 (cur) };
      db_res_t *res = NULL;

      char sql_converted[512];
      sql_build(db, sql, sql_converted, sizeof(sql_converted));
      if (!db_query (db, sql_converted, params, 1, &res, &err))
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
    "SELECT to_sector FROM sector_warps WHERE from_sector={1} ORDER BY RANDOM() LIMIT 1;";
  db_bind_t params[] = { db_bind_i32 (sector) };
  db_res_t *res = NULL;

  char sql_converted[512];
  sql_build(db, sql, sql_converted, sizeof(sql_converted));
  if (!db_query (db, sql_converted, params, 1, &res, &err))
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
      const char *sql = "UPDATE ships SET sector_id = {1} WHERE ship_id = {2};";


      char sql_converted[512];
      sql_build(db, sql, sql_converted, sizeof(sql_converted));
      if (!db_exec (db, sql_converted,
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
  ori_db = db;
}


/* Loops through all Orion ships and executes their movement logic */
static void
ori_move_all_ships (void)
{
  db_error_t err;
  db_error_clear (&err);

  /* Select all ships owned by the Orion Syndicate */
  const char *sql_select_orion_ships =
    "SELECT s.id, s.sector, s.target_sector "
    "FROM ships s "
    "JOIN ship_ownership so ON s.id = so.ship_id "
    "WHERE so.player_id = {1};";

  db_bind_t params[] = { db_bind_i32 (ori_owner_id) };
  db_res_t *res = NULL;

  char sql_converted[1024];
  sql_build(ori_db, sql_select_orion_ships, sql_converted, sizeof(sql_converted));
  if (!db_query (ori_db, sql_converted, params, 1, &res, &err))
    {
      LOGE ("ORI_MOVE: Failed to query Orion ships: %s", err.message);
      return;
    }

  while (db_res_step (res, &err))
    {
      int ship_id = db_res_col_i32 (res, 0, &err);
      int current_sector = db_res_col_i32 (res, 1, &err);
      int target_sector = db_res_col_i32 (res, 2, &err);
      int new_target = target_sector;

      /* Core Orion Movement Strategy:
         60% chance to target Black Market home sector for resupply/patrol
         40% chance to target a random unprotected sector for piracy */
      if (new_target == 0 || new_target == current_sector)
        {
          if (rand () % 10 < 6 && ori_home_sector_id != -1)
            {
              new_target = ori_home_sector_id;
            }
          else
            {
              /* Random sector in unprotected range (11..999) */
              new_target = (rand () % (999 - 11 + 1)) + 11;
            }
          /* Don't target the current sector */
          if (new_target == current_sector)
            {
              new_target = (new_target % 999) + 1;
            }
        }

      /* Update the target for the next tick */
      const char *sql_update_target =
        "UPDATE ships SET target_sector = {1} WHERE ship_id = {2};";

      db_bind_t update_params[] = {
        db_bind_i32 (new_target),
        db_bind_i32 (ship_id)
      };

      db_error_clear (&err);
      char sql_converted[512];
      sql_build(ori_db, sql_update_target, sql_converted, sizeof(sql_converted));
      if (!db_exec (ori_db, sql_converted, update_params, 2, &err))
        {
          LOGE ("ORI_MOVE: Failed to update target for ship %d: %s",
                ship_id, err.message);
        }
      else
        {
          LOGI ("ORI_MOVE: Ship %d targeting sector %d (from %d).",
                ship_id, new_target, current_sector);
        }
    }

  db_res_finalize (res);
}


int
ori_init_once (void)
{
  if (ori_initialized || ori_db == NULL)
    {
      return (ori_owner_id != -1);
    }

  db_error_t err;
  db_error_clear (&err);

  /* Step 1: Find the Orion Syndicate owner ID from corporation tag */
  const char *sql_find_owner = "SELECT owner_id FROM corporations WHERE tag={1};";
  db_bind_t params_owner[] = { db_bind_text ("ORION") };
  db_res_t *res = NULL;

  char sql_converted[512];
  sql_build(ori_db, sql_find_owner, sql_converted, sizeof(sql_converted));
  if (!db_query (ori_db, sql_converted, params_owner, 1, &res, &err))
    {
      LOGW ("ORI_INIT: Failed to find Orion Syndicate owner (query error: %s). Skipping.",
            err.message);
      ori_initialized = true;
      return 0;
    }

  if (db_res_step (res, &err))
    {
      ori_owner_id = db_res_col_i32 (res, 0, &err);
    }
  db_res_finalize (res);

  if (ori_owner_id == -1)
    {
      LOGW ("ORI_INIT: Failed to find Orion Syndicate owner. Skipping.");
      ori_initialized = true;
      return 0;
    }

  /* Step 2: Find the Black Market home sector ID (from Port ID 10) */
  const char *sql_find_sector = "SELECT sector FROM ports WHERE id={1} AND name={2};";
  db_bind_t params_sector[] = {
    db_bind_i32 (10),
    db_bind_text ("Orion Black Market Dock")
  };
  res = NULL;
  db_error_clear (&err);

  char sql_converted2[512];
  sql_build(ori_db, sql_find_sector, sql_converted2, sizeof(sql_converted2));
  if (!db_query (ori_db, sql_converted2, params_sector, 2, &res, &err))
    {
      LOGW ("ORI_INIT: Failed to find Black Market sector (query error: %s). Movement will be random.",
            err.message);
    }
  else if (db_res_step (res, &err))
    {
      ori_home_sector_id = db_res_col_i32 (res, 0, &err);
      db_res_finalize (res);
    }
  else
    {
      LOGW ("ORI_INIT: Failed to find Black Market home sector (Port ID 10). Movement will be random.");
      db_res_finalize (res);
    }

  LOGI ("ORI_INIT: Orion Syndicate owner ID is %d, Home Sector is %d",
        ori_owner_id, ori_home_sector_id);
  ori_initialized = true;
  return 1;
}


void
ori_tick (int64_t now_ms)
{
  if (!ori_initialized || ori_owner_id == -1 || ori_db == NULL)
    {
      return;
    }
  LOGI ("ORI_TICK: Running movement logic for Orion Syndicate... @%ld",
        now_ms);
  ori_move_all_ships ();
  LOGI ("ORI_TICK: Complete.");
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
parse_sector_search_input (json_t *root,
                           char **q_out,
                           int *type_any, int *type_sector, int *type_port,
                           int *limit_out, int *offset_out)
{
  *q_out = NULL;
  *type_any = *type_sector = *type_port = 0;
  *limit_out = 20;  /* SEARCH_DEFAULT_LIMIT */
  *offset_out = 0;
  json_t *data = json_object_get (root, "data");

  if (!json_is_object (data))
    {
      return -1;
    }
  /* q (optional, empty means "match all") */
  json_t *jq = json_object_get (data, "q");

  if (json_is_string (jq))
    {
      const char *qs = json_string_value (jq);
      *q_out = strdup (qs ? qs : "");
    }
  else
    {
      *q_out = strdup ("");
    }
  if (!*q_out)
    {
      return -2;
    }
  /* type */
  const char *type = "any";
  json_t *jtype = json_object_get (data, "type");

  if (json_is_string (jtype))
    {
      type = json_string_value (jtype);
    }
  if (!type || strcasecmp (type, "any") == 0)
    {
      *type_any = 1;
    }
  else if (strcasecmp (type, "sector") == 0)
    {
      *type_sector = 1;
    }
  else if (strcasecmp (type, "port") == 0)
    {
      *type_port = 1;
    }
  else
    {
      free (*q_out);
      return -3;
    }
  /* limit */
  json_t *jlimit = json_object_get (data, "limit");

  if (json_is_integer (jlimit))
    {
      int lim = (int) json_integer_value (jlimit);
      if (lim <= 0)
        {
          lim = 20;
        }
      if (lim > 100)  /* SEARCH_MAX_LIMIT */
        {
          lim = 100;
        }
      *limit_out = lim;
    }
  /* cursor (offset) */
  json_t *jcur = json_object_get (data, "cursor");

  if (json_is_integer (jcur))
    {
      *offset_out = (int) json_integer_value (jcur);
      if (*offset_out < 0)
        {
          *offset_out = 0;
        }
    }
  else if (json_is_string (jcur))
    {
      /* allow stringified integers too */
      const char *s = json_string_value (jcur);
      if (s && *s)
        {
          *offset_out = atoi (s);
          if (*offset_out < 0)
            {
              *offset_out = 0;
            }
        }
    }
  return 0;
}

int
cmd_sector_search (client_ctx_t *ctx, json_t *root)
{
  if (!root)
    {
      return -1;
    }
  char *q = NULL;
  int type_any = 0, type_sector = 0, type_port = 0;
  int limit = 0, offset = 0;
  int prc = parse_sector_search_input (root, &q, &type_any, &type_sector, 
                                        &type_port, &limit, &offset);

  if (prc != 0)
    {
      free (q);
      send_response_error (ctx, root, ERR_BAD_REQUEST, "Expected data { ... }");
      return 0;
    }
  
  db_t *db = game_db_get_handle ();
  if (!db)
    {
      free (q);
      send_response_error (ctx, root, ERR_SERVER_ERROR, "No database handle.");
      return 0;
    }
  
  /* Build SQL query based on search type */
  char sql[512];
  int params_count = 3;
  db_bind_t params[3];
  
  const char *base_sql = 
    "SELECT kind, id, name, sector_id, sector_name FROM sector_search_index ";
  const char *order_limit = " ORDER BY kind, name, id LIMIT {2} OFFSET {3}";
  
  /* Build WHERE clause */
  if (type_any)
    {
      snprintf (sql, sizeof(sql), 
                "%s WHERE (({1} = '') OR (search_term_1 ILIKE {1})) %s",
                base_sql, order_limit);
    }
  else if (type_sector)
    {
      snprintf (sql, sizeof(sql),
                "%s WHERE kind = 'sector' AND (({1} = '') OR (search_term_1 ILIKE {1})) %s",
                base_sql, order_limit);
    }
  else
    {  /* type_port */
      snprintf (sql, sizeof(sql),
                "%s WHERE kind = 'port' AND (({1} = '') OR (search_term_1 ILIKE {1})) %s",
                base_sql, order_limit);
    }
  
  /* Bind parameters */
  params[0] = db_bind_text(q ? q : "");
  params[1] = db_bind_i32(limit + 1);
  params[2] = db_bind_i32(offset);
  
  db_error_t err;
  db_res_t *res = NULL;
  
  char sql_converted[512];
  sql_build(db, sql, sql_converted, sizeof(sql_converted));
  if (!db_query (db, sql_converted, params, params_count, &res, &err))
    {
      free (q);
      send_response_error (ctx, root, ERR_SERVER_ERROR, "Search query failed");
      return 0;
    }
  
  json_t *items = json_array ();
  int row_count = 0;
  
  while (db_res_step (res, &err))
    {
      const char *kind = db_res_col_text (res, 0, &err);
      int id = (int)db_res_col_i64 (res, 1, &err);
      const char *name = db_res_col_text (res, 2, &err);
      int sector_id = (int)db_res_col_i64 (res, 3, &err);
      const char *sector_name = db_res_col_text (res, 4, &err);
      
      if (row_count < limit)
        {
          json_t *it = json_object ();
          json_object_set_new (it, "kind", json_string (kind ? kind : ""));
          json_object_set_new (it, "id", json_integer (id));
          json_object_set_new (it, "name", json_string (name ? name : ""));
          json_object_set_new (it, "sector_id", json_integer (sector_id));
          json_object_set_new (it, "sector_name", 
                              json_string (sector_name ? sector_name : ""));
          json_array_append_new (items, it);
        }
      row_count++;
      if (row_count >= limit + 1)
        {
          break;
        }
    }
  db_res_finalize (res);
  free (q);
  
  json_t *jdata = json_object ();
  json_object_set_new (jdata, "items", items);
  
  if (row_count > limit)
    {
      json_object_set_new (jdata, "next_cursor", json_integer (offset + limit));
    }
  else
    {
      json_object_set_new (jdata, "next_cursor", json_null ());
    }
  
  send_response_ok_take (ctx, root, "sector.search_results_v1", &jdata);
  return 0;
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
      json_object_set (root, "name", json_object_get (basic, "name"));
      json_object_set (root, "beacon", json_object_get (basic, "beacon"));
      json_decref (basic);
    }
  json_t *adj = NULL;


  if (db_adjacent_sectors_json (db, sector_id, &adj) == 0 && adj)
    {
      json_object_set_new (root, "adjacent_sectors", adj);
    }
  json_t *ships = NULL; if (db_ships_at_sector_json (db,
                                                     player_id,
                                                     sector_id,
                                                     &ships) == 0)
    {
      json_object_set_new (root, "ships_present", ships ?: json_array ());
    }
  json_t *ports = NULL;


  if (db_ports_at_sector_json (db, sector_id, &ports) == 0)
    {
      json_object_set_new (root, "ports", ports ?: json_array ());
    }
  json_t *planets = NULL;


  if (db_planets_at_sector_json (db, sector_id, &planets) == 0)
    {
      json_object_set_new (root, "celestial_objects", planets ?: json_array ());
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
    "  SELECT {1}::int as sector_id "
    "  UNION "
    "  SELECT to_sector FROM sector_warps WHERE from_sector = {1} "
    ") "
    "SELECT DISTINCT sector_id FROM sector_list ORDER BY sector_id;";
  
  char sql_converted[1024];
  sql_build(db, sql_sectors, sql_converted, sizeof(sql_converted));
  if (!db_query(db, sql_converted, (db_bind_t[]){db_bind_i32(target_sector)}, 1, &res, &err))
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
         Ports: 100 per
      */
      const char *density_sql = 
        "SELECT "
        "  COALESCE((SELECT SUM(quantity) FROM sector_assets WHERE sector_id = {1} AND asset_type = 2), 0) + "
        "  COALESCE((SELECT SUM(quantity) FROM sector_assets WHERE sector_id = {1} AND (asset_type = 1 OR asset_type = 4)), 0) + "
        "  CASE WHEN EXISTS(SELECT 1 FROM sectors WHERE sector_id = {1} AND beacon IS NOT NULL) THEN 1 ELSE 0 END + "
        "  (SELECT COALESCE(COUNT(*), 0) * 10 FROM ships WHERE sector_id = {1}) + "
        "  (SELECT COALESCE(COUNT(*), 0) * 100 FROM planets WHERE sector_id = {1}) + "
        "  (SELECT COALESCE(COUNT(*), 0) * 100 FROM ports WHERE sector_id = {1}) "
        "as total_density;";
      
      db_res_t *density_res = NULL;
      char sql_converted2[1024];
      sql_build(db, density_sql, sql_converted2, sizeof(sql_converted2));
      if (db_query(db, sql_converted2, (db_bind_t[]){db_bind_i32(sector_id)}, 1, &density_res, &err))
        {
          int density = 0;
          if (db_res_step(density_res, &err))
            {
              density = (int)db_res_col_i64(density_res, 0, &err);
            }
          db_res_finalize(density_res);
          
          /* Include all sectors found, even with 0 density */
          json_array_append_new (sectors, json_integer (sector_id));
          json_array_append_new (sectors, json_integer (density));
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


  const char *sql = "SELECT 1 FROM sector_warps WHERE from_sector = {1} AND to_sector = {2} LIMIT 1;";
  char sql_converted[512];
  sql_build(db, sql, sql_converted, sizeof(sql_converted));
  if (db_query (db,
                sql_converted,
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

/* Check if sector has hostile interdictors */
int
h_check_interdiction (db_t *db, int sector_id, int player_id, int corp_id)
{
  if (!db || sector_id <= 0)
    {
      return 0;
    }

  db_error_t err;
  db_res_t *res = NULL;

  /* Query citadels with interdictor capability */
  const char *sql = "SELECT p.owner_id, p.owner_type "
    "FROM planets p "
    "JOIN citadels c ON p.planet_id = c.planet_id "
    "WHERE p.sector_id = {1} AND c.level >= 6 AND c.interdictor > 0;";

  db_bind_t params[] = { db_bind_i32 (sector_id) };

  char sql_converted[1024];
  sql_build(db, sql, sql_converted, sizeof(sql_converted));
  if (!db_query (db, sql_converted, params, 1, &res, &err))
    {
      LOGE ("h_check_interdiction: query failed: %s", err.message);
      return 0; /* Fail open */
    }

  int blocked = 0;
  while (db_res_step (res, &err))
    {
      int owner_id = db_res_col_int (res, 0, &err);
      const char *owner_type = db_res_col_text (res, 1, &err);

      int p_corp_id = 0;
      if (owner_type && (strcasecmp (owner_type, "corp") == 0
                         || strcasecmp (owner_type, "corporation") == 0))
        {
          p_corp_id = owner_id;
        }

      if (is_asset_hostile (owner_id, p_corp_id, player_id, corp_id))
        {
          blocked = 1;
          break;
        }
    }

  db_res_finalize (res);
  return blocked;
}

/* Check if sector has a port */
int
sector_has_port (db_t *db, int sector)
{
  if (!db || sector <= 0)
    {
      return 0;
    }

  db_error_t err;
  db_res_t *res = NULL;
  const char *sql = "SELECT 1 FROM ports WHERE sector_id={1} LIMIT 1;";
  db_bind_t params[] = { db_bind_i32 (sector) };

  char sql_converted[512];
  sql_build(db, sql, sql_converted, sizeof(sql_converted));
  if (!db_query (db, sql_converted, params, 1, &res, &err))
    {
      return 0;
    }

  int has_port = db_res_step (res, &err) ? 1 : 0;
  db_res_finalize (res);
  return has_port;
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

/* Validate that a warp link exists between two sectors */
int
validate_warp_rule (int from_sector, int to_sector)
{
  if (to_sector <= 0)
    {
      return ERR_BAD_REQUEST;
    }
  if (from_sector <= 0)
    {
      return ERR_BAD_STATE;
    }
  if (from_sector == to_sector)
    {
      return 0;             /* no-op warp is fine (cheap "success") */
    }

  db_t *db = game_db_get_handle ();
  if (!db)
    {
      return ERR_DB;
    }

  if (!h_warp_exists (db, from_sector, to_sector))
    {
      return REF_NO_WARP_LINK;
    }

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
  
  /* Check warp link exists */
  if (!h_warp_exists (db, ctx->sector_id, to))
    {
      send_response_error (ctx, root, REF_NO_WARP_LINK, "No link"); 
      return 0;
    }
  
  /* Check for hostile interdictors */
  if (h_check_interdiction (db, to, ctx->player_id, ctx->corp_id))
    {
      send_response_error (ctx, root, REF_TURN_COST_EXCEEDS, 
                          "Warp interdicted by hostile planetary defences");
      return 0;
    }
  
  /* Consume turn */
  TurnConsumeResult tc = h_consume_player_turn (db, ctx, 1);
  if (tc != TURN_CONSUME_SUCCESS)
    {
      return handle_turn_consumption_error (ctx, tc, "move.warp", root, NULL);
    }
  
  if (db_player_set_sector (ctx->player_id, to) == 0)
    {
      ctx->sector_id = to;
      json_t *resp = json_object (); 
      json_object_set_new (resp, "to_sector_id", json_integer (to));
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
    "SELECT asset_type, SUM(quantity) FROM sector_assets WHERE sector_id = {1} GROUP BY asset_type;";
  char sql_converted[512];
  sql_build(db, sql, sql_converted, sizeof(sql_converted));
  if (db_query (db, sql_converted, (db_bind_t[]){db_bind_i32 (sid)}, 1, &res, &err))
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
  const char *sql = "UPDATE sectors SET beacon = {1} WHERE sector_id = {2};";
  db_bind_t binds[] = {
    db_bind_text(beacon_text),
    db_bind_i32(req_sector_id)
  };
  
  char sql_converted[512];
  sql_build(db, sql, sql_converted, sizeof(sql_converted));
  if (!db_exec (db, sql_converted, binds, 2, &err))
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
  const char *sql = "SELECT 1 FROM ships WHERE ship_id = {1} AND transwarp_enabled = true LIMIT 1;";
  
  int has_transwarp = 0;
  char sql_converted[512];
  sql_build(db, sql, sql_converted, sizeof(sql_converted));
  if (db_query (db, sql_converted, (db_bind_t[]){db_bind_i32(ship_id)}, 1, &res, &err))
    {
      if (res && db_res_step(res, &err))
        {
          has_transwarp = 1;
        }
      if (res) db_res_finalize(res);
    }

  if (!has_transwarp)
    {
      send_response_refused_steal (ctx, root, REF_TRANSWARP_UNAVAILABLE,
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
  sql = "UPDATE players SET sector_id = {1} WHERE player_id = {2};";
  char sql_converted2[512];
  sql_build(db, sql, sql_converted2, sizeof(sql_converted2));
  if (!db_exec (db, sql_converted2, (db_bind_t[]){db_bind_i32(to_sector_id), 
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


/* Ferengi Trading at Port - Core NPC trading logic */
static int
ferengi_trade_at_port (db_t *db, int trader_id, int ship_id, int port_id,
                       int sector_id)
{
  if (!db || ship_id <= 0 || port_id <= 0)
    return 1;

  db_error_t err;
  char tx_group_id[UUID_STR_LEN];
  h_generate_hex_uuid (tx_group_id, sizeof(tx_group_id));

  int fer_ore = 0, fer_organics = 0, fer_equipment = 0;
  int fer_holds = 0;

  if (h_get_ship_cargo_and_holds (db, ship_id,
                                  &fer_ore, &fer_organics, &fer_equipment,
                                  &fer_holds, NULL, NULL, NULL, NULL) != 0)
    {
      LOGW ("[fer] Failed to get ship %d cargo", ship_id);
      return 1;
    }

  int fer_current_cargo_total = fer_ore + fer_organics + fer_equipment;
  int fer_empty_holds = fer_holds - fer_current_cargo_total;

  long long fer_credits = 0;
  db_error_clear (&err);
  db_get_corp_bank_balance (db, g_fer_corp_id, &fer_credits);

  int port_ore_qty = 0, port_org_qty = 0, port_equ_qty = 0;
  int dummy_cap = 0;
  bool dummy_bool = false;

  h_get_port_commodity_details (db, port_id, "ORE", &port_ore_qty, &dummy_cap, &dummy_bool, &dummy_bool);
  h_get_port_commodity_details (db, port_id, "ORG", &port_org_qty, &dummy_cap, &dummy_bool, &dummy_bool);
  h_get_port_commodity_details (db, port_id, "EQU", &port_equ_qty, &dummy_cap, &dummy_bool, &dummy_bool);

  int port_ore_buy = h_calculate_port_buy_price (db, port_id, "ORE");
  int port_ore_sell = h_calculate_port_sell_price (db, port_id, "ORE");
  int port_org_buy = h_calculate_port_buy_price (db, port_id, "ORG");
  int port_org_sell = h_calculate_port_sell_price (db, port_id, "ORG");
  int port_equ_buy = h_calculate_port_buy_price (db, port_id, "EQU");
  int port_equ_sell = h_calculate_port_sell_price (db, port_id, "EQU");

  const char *commodities[] = {"ORE", "ORG", "EQU"};
  int fer_hold_values[] = {fer_ore, fer_organics, fer_equipment};
  int port_buy_prices[] = {port_ore_buy, port_org_buy, port_equ_buy};
  int port_sell_prices[] = {port_ore_sell, port_org_sell, port_equ_sell};
  int port_quantities[] = {port_ore_qty, port_org_qty, port_equ_qty};

  int best_sell_idx = -1;
  int best_buy_idx = -1;

  for (int c_idx = 0; c_idx < 3; ++c_idx)
    {
      if (fer_hold_values[c_idx] > 0 && port_buy_prices[c_idx] > 0)
        {
          if (best_sell_idx == -1 || port_buy_prices[c_idx] > port_buy_prices[best_sell_idx])
            best_sell_idx = c_idx;
        }
    }

  for (int c_idx = 0; c_idx < 3; ++c_idx)
    {
      if (fer_empty_holds > 0 && port_quantities[c_idx] > 0 && port_sell_prices[c_idx] > 0)
        {
          if (best_buy_idx == -1 || port_sell_prices[c_idx] < port_sell_prices[best_buy_idx])
            best_buy_idx = c_idx;
        }
    }

  int rc = 0;

  if (best_sell_idx != -1)
    {
      const char *commodity = commodities[best_sell_idx];
      int price_per_unit = port_buy_prices[best_sell_idx];
      int max_sell_to_port = port_quantities[best_sell_idx] / 2;
      if (max_sell_to_port == 0)
        max_sell_to_port = 1;

      int qty_to_trade = MIN (fer_hold_values[best_sell_idx], max_sell_to_port);
      long long port_balance = 0;
      db_get_port_bank_balance (db, port_id, &port_balance);
      qty_to_trade = MIN (qty_to_trade, (int)(port_balance / price_per_unit));

      if (qty_to_trade > 0 && price_per_unit > 0)
        {
          long long total_credits = (long long)qty_to_trade * price_per_unit;

          db_error_clear (&err);
          rc = h_bank_transfer_unlocked (db, "port", port_id, "corp", g_fer_corp_id,
                                         total_credits, "TRADE_SELL", tx_group_id);
          if (rc == 0)
            {
              rc = h_update_ship_cargo (db, ship_id, commodity, -qty_to_trade, NULL);
              if (rc == 0)
                rc = h_market_move_port_stock (db, port_id, commodity, qty_to_trade);
            }

          if (rc == 0)
            {
              fer_event_json ("npc.trade", sector_id,
                            "{ \"kind\":\"ferrengi_sell\", \"ship_id\":%d, \"port_id\":%d, "
                            "\"commodity\":\"%s\", \"qty\":%d, \"price\":%d, \"total_credits\":%"PRId64" }",
                            ship_id, port_id, commodity, qty_to_trade, price_per_unit, total_credits);
              LOGI ("[fer] Sold %d %s to Port %d for %lld credits.",
                    qty_to_trade, commodity, port_id, total_credits);
            }
          else
            {
              LOGW ("[fer] Failed to sell %d %s to Port %d (rc=%d)",
                    qty_to_trade, commodity, port_id, rc);
            }
        }
    }

  if (rc == 0 && best_buy_idx != -1 && fer_empty_holds > 0)
    {
      const char *commodity = commodities[best_buy_idx];
      int price_per_unit = port_sell_prices[best_buy_idx];
      int qty_to_trade = MIN (fer_empty_holds, port_quantities[best_buy_idx]);
      if (qty_to_trade <= 0)
        qty_to_trade = 1;

      long long total_credits = (long long)qty_to_trade * price_per_unit;

      if (qty_to_trade > 0 && total_credits > 0 && fer_credits >= total_credits)
        {
          db_error_clear (&err);
          rc = h_bank_transfer_unlocked (db, "corp", g_fer_corp_id, "port", port_id,
                                         total_credits, "TRADE_BUY", tx_group_id);
          if (rc == 0)
            {
              rc = h_update_ship_cargo (db, ship_id, commodity, qty_to_trade, NULL);
              if (rc == 0)
                rc = h_market_move_port_stock (db, port_id, commodity, -qty_to_trade);
            }

          if (rc == 0)
            {
              fer_event_json ("npc.trade", sector_id,
                            "{ \"kind\":\"ferrengi_buy\", \"ship_id\":%d, \"port_id\":%d, "
                            "\"commodity\":\"%s\", \"qty\":%d, \"price\":%d, \"total_credits\":%"PRId64" }",
                            ship_id, port_id, commodity, qty_to_trade, price_per_unit, total_credits);
              LOGI ("[fer] Bought %d %s from Port %d for %lld credits.",
                    qty_to_trade, commodity, port_id, total_credits);
            }
          else
            {
              LOGW ("[fer] Failed to buy %d %s from Port %d (rc=%d)",
                    qty_to_trade, commodity, port_id, rc);
            }
        }
    }

  return rc;
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
    "SELECT corporation_id, owner_id FROM corporations WHERE tag='FENG' LIMIT 1;";

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
          LOGD ("[fer] Found Ferengi corp_id=%d, player_id=%d", g_fer_corp_id, g_fer_player_id);
        }
      else
        {
          LOGW ("[fer] db_res_step failed: %s", err.message);
        }
      if (res) db_res_finalize (res);
    }
  else
    {
      LOGW ("[fer] db_query failed: %s", err.message);
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
    "SELECT shiptypes_id FROM shiptypes WHERE name='Ferengi Warship' LIMIT 1;";

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
  const char *sql = "SELECT to_sector_id FROM wormholes WHERE from_sector_id = {1} ORDER BY RANDOM() LIMIT 1;";
  
  char sql_converted[512];
  sql_build(db, sql, sql_converted, sizeof(sql_converted));
  if (!db_query (db, sql_converted, (db_bind_t[]){db_bind_i32(sid)}, 1, &res, &err))
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

/* ============ NPC Encounter Handler ============ */

void
h_handle_npc_encounters (db_t *db, client_ctx_t *ctx, int new_sector_id)
{
  if (!db || !ctx || new_sector_id <= 0)
    {
      LOGE ("h_handle_npc_encounters: Invalid input.");
      return;
    }

  /* 10% chance of a generic NPC encounter */
  if (rand () % 10 == 0)
    {
      json_t *payload = json_object ();

      if (!payload)
        {
          LOGE ("h_handle_npc_encounters: Out of memory for event payload.");
          return;
        }
      json_object_set_new (payload, "player_id", json_integer (ctx->player_id));
      json_object_set_new (payload, "sector_id", json_integer (new_sector_id));
      json_object_set_new (payload, "npc_type", json_string ("Generic NPC"));
      json_object_set_new (payload, "message",
                           json_string ("A generic NPC has been encountered!"));

      db_log_engine_event (time (NULL),
                           "npc.encounter",
                           "player",
                           ctx->player_id,
                           new_sector_id,
                           payload,
                           NULL);
      LOGI (
        "h_handle_npc_encounters: Generic NPC encounter logged for player %d in sector %d.",
        ctx->player_id,
        new_sector_id);
    }
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

