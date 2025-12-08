#include <sqlite3.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <jansson.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <stdio.h>
#include <sqlite3.h>
#include <stdarg.h>
#include <inttypes.h> // For PRId64
// local include
#include "server_universe.h"
#include "server_ports.h"
#include "database.h"
#include "server_cmds.h"
#include "server_rules.h"
#include "server_bigbang.h"
#include "server_news.h"
#include "common.h"
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
#include "server_ports.h"

typedef enum
{
  FER_STATE_ROAM = 0,
  FER_STATE_RETURNING = 1
} fer_state_t;
typedef struct
{
  int id;                       /* 0..N-1 */
  int ship_id;                  /* Real DB ship ID */
  int sector;                   /* current location */
  int home_sector;              /* Ferringhi homeworld sector */
  int trades_done;              /* trades since last refill */
  fer_state_t state;            /* ROAM or RETURNING */
} fer_trader_t;


#define SECTOR_LIST_BUF_SIZE 256
/* cache DB so fer_tick signature matches ISS style */
extern sqlite3 *g_db;           /* <- global DB handle used elsewhere (ISS) */
static sqlite3 *g_fer_db = NULL;        /* <- cached here for trader helpers */
#define NPC_TRADE_PLAYER_ID 0
/* Fallback logging macros  */
#ifndef FER_TRADER_COUNT
#define FER_TRADER_COUNT            5
#endif
#ifndef FER_TRADES_BEFORE_RETURN
#define FER_TRADES_BEFORE_RETURN    5
#endif
#ifndef FER_MAX_HOLD
#define FER_MAX_HOLD                100
#endif

#define FERENGI_CORP_TAG "FENG"

static fer_trader_t g_fer[FER_TRADER_COUNT];
static int g_fer_inited = 0;
static int g_fer_corp_id = 0;
static int g_fer_player_id = 0;
static const int kIssPatrolBudget = 8;  /* hops before we drift home */
static const char *kIssName = "Imperial Starship";
static const char *kIssNoticePrefix = "[ISS • Capt. Zyrain]";
/* Private state (kept in this .c only) */
static int g_iss_inited = 0;
static int g_iss_id = 0;
static int g_iss_sector = 0;
static int g_stardock_sector = 0;
static int g_patrol_budget = 0;
/* pending summon (if set, next tick warps immediately) */
static int g_summon_sector = 0;
static int g_summon_offender = 0;


typedef struct
{
  int sector;
  int budget;
} IssState;
/* forward statics (defined later in this file) */
static int db_pick_adjacent (int sector);
static void post_iss_notice_move (int from, int to, const char *kind,
                                  const char *extra);
static void iss_log_event_move (int from, int to, const char *kind,
                                const char *extra);
static void attach_sector_asset_counts (sqlite3 *db,
                                        int sector_id,
                                        json_t *data_out);
static int ferengi_trade_at_port(sqlite3 *db, fer_trader_t *trader, int port_id);



#ifndef UNUSED
#define UNUSED(x) (void)(x)
#endif
// Adjust these if you like
#define SEARCH_DEFAULT_LIMIT 20
#define SEARCH_MAX_LIMIT     100
// If your schema uses a separate class table, set this to 1 and ensure names below match.
//  - ports(class_id) -> port_classes(id, name)
// If ports has a text column 'class' already, leave this as 0.
#ifndef PORTS_HAVE_CLASS_TABLE
#define PORTS_HAVE_CLASS_TABLE 0
#endif
json_t *build_sector_info_json (int sector_id);
////////////////////// ORION NPC ////////////////////////////////
/* Static globals for Orion state */
static bool ori_initialized = false;
static sqlite3 *ori_db = NULL;
static int ori_owner_id = -1;
static int ori_home_sector_id = -1;


/* Private function to move all Orion ships */
static void ori_move_all_ships (void);


/* Helper to execute a single SELECT query and return an int value */
static int
get_int_value (sqlite3 *db, const char *sql)
{
  sqlite3_stmt *stmt = NULL;
  int value = -1;
  if (sqlite3_prepare_v2 (db, sql, -1, &stmt, NULL) == SQLITE_OK)
    {
      if (sqlite3_step (stmt) == SQLITE_ROW)
        {
          value = sqlite3_column_int (stmt, 0);
        }
      sqlite3_finalize (stmt);
    }
  else
    {
      LOGE ("ORI_DB_HELPER: Failed to prepare statement: %s",
            sqlite3_errmsg (db));
    }
  return value;
}


/* Attaches the database handle (called by server_cron.c) */
void
ori_attach_db (sqlite3 *db)
{
  ori_db = db;
}


/* Initializes Orion NPC state and finds the owner/home sector ID. */
int
ori_init_once (void)
{
  if (ori_initialized || ori_db == NULL)
    {
      return (ori_owner_id != -1);
    }
  // --- Step 1: Find the Orion Syndicate owner ID (from the corporation tag) ---
  const char *sql_find_owner =
    "SELECT owner_id FROM corporations WHERE tag='ORION';";


  ori_owner_id = get_int_value (ori_db, sql_find_owner);
  if (ori_owner_id == -1)
    {
      LOGW ("ORI_INIT: Failed to find Orion Syndicate owner. Skipping.");
      ori_initialized = true;
      return 0;
    }
  // --- Step 2: Find the Black Market home sector ID (from Port ID 10) ---
  const char *sql_find_sector =
    "SELECT sector FROM ports WHERE id=10 AND name='Orion Black Market Dock';";


  ori_home_sector_id = get_int_value (ori_db, sql_find_sector);
  if (ori_home_sector_id == -1)
    {
      LOGW
      (
        "ORI_INIT: Failed to find Black Market home sector (Port ID 10). Movement will be random.");
    }
  LOGI ("ORI_INIT: Orion Syndicate owner ID is %d, Home Sector is %d",
        ori_owner_id, ori_home_sector_id);
  ori_initialized = true;
  return 1;
}


/* Executes the core Orion movement logic on every cron tick. */
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


/* Loops through all Orion ships and executes their movement logic. */
static void
ori_move_all_ships (void)
{
  sqlite3_stmt *select_stmt = NULL;
  int rc;
  // Select all ships owned by the Orion Syndicate
  const char *sql_select_orion_ships =
    "SELECT id, sector, target_sector FROM ships WHERE owner_id = ?;";
  rc =
    sqlite3_prepare_v2 (ori_db, sql_select_orion_ships, -1, &select_stmt,
                        NULL);
  if (rc != SQLITE_OK)
    {
      LOGE ("ORI_MOVE: Failed to prepare select statement: %s",
            sqlite3_errmsg (ori_db));
      return;
    }
  sqlite3_bind_int (select_stmt, 1, ori_owner_id);
  while ((rc = sqlite3_step (select_stmt)) == SQLITE_ROW)
    {
      int ship_id = sqlite3_column_int (select_stmt, 0);
      int current_sector = sqlite3_column_int (select_stmt, 1);
      int target_sector = sqlite3_column_int (select_stmt, 2);
      int new_target = target_sector;


      // --- Core Orion Movement Strategy ---
      // If the ship has no current target (target_sector == 0) or has reached its target:
      if (new_target == 0 || new_target == current_sector)
        {
          // 60% chance to target the Black Market home sector for resupply/patrol
          if (rand () % 10 < 6 && ori_home_sector_id != -1)
            {
              new_target = ori_home_sector_id;
            }
          else
            {
              // 40% chance to target a random, unprotected sector for piracy
              // MIN_UNPROTECTED_SECTOR=11, MAX_UNPROTECTED_SECTOR=999
              new_target = (rand () % (999 - 11 + 1)) + 11;
            }
          // Don't target the current sector
          if (new_target == current_sector)
            {
              new_target = (new_target % 999) + 1;
            }
        }
      // --- Execute Movement ---
      // NOTE: The 'move_ship' function should handle updating the ship's location
      // and checking for players/mines in the new sector.
      // Placeholder for the actual move_ship call
      // if (move_ship(ship_id, new_target) == 0) {
      //     LOGI("ORI_MOVE: Ship %d moved to sector %d.", ship_id, new_target);
      // }
      // Update the target directly for the next tick
      sqlite3_stmt *update_stmt = NULL;
      const char *sql_update_target =
        "UPDATE ships SET target_sector = ?1 WHERE id = ?2;";


      if (sqlite3_prepare_v2
            (ori_db, sql_update_target, -1, &update_stmt, NULL) == SQLITE_OK)
        {
          sqlite3_bind_int (update_stmt, 1, new_target);
          sqlite3_bind_int (update_stmt, 2, ship_id);
          if (sqlite3_step (update_stmt) != SQLITE_DONE)
            {
              LOGE ("ORI_MOVE: Failed to update target for ship %d: %s",
                    ship_id, sqlite3_errmsg (ori_db));
            }
          sqlite3_finalize (update_stmt);
        }
      else
        {
          LOGE
            ("ORI_MOVE: Failed to prepare update statement for ship %d: %s",
            ship_id, sqlite3_errmsg (ori_db));
        }
      LOGI ("ORI_MOVE: Ship %d targeting sector %d (from %d).",
            ship_id, new_target, current_sector);
    }
  if (rc != SQLITE_DONE)
    {
      LOGE ("ORI_MOVE: SELECT processing failed: %s",
            sqlite3_errmsg (ori_db));
    }
  sqlite3_finalize (select_stmt);
}


////////////////////// ORION NPC ////////////////////////////////


/* --- minimal event writer for tests (engine_events) --- */
static void
fer_event_json (const char *type, int sector_id, const char *fmt, ...)
{
  // Assumed extern definition: extern pthread_mutex_t db_mutex;
  //LOGI("fer_event_json: Starting process for event type '%s' at sector %d.", type, sector_id);
  if (!g_fer_db)
    {
      // If the caller failed to provide the DB handle, we can't do anything
      LOGE ("fer_event_json: Received NULL DB handle. Cannot proceed.");
      return;
    }
  //LOGI("fer_event_json: past g_fer_db check");
  if (!type)
    {
      LOGE ("fer_event_json: Event type is NULL. Cannot proceed.");
      return;
    }
  //LOGI("fer_event_json: past type check");
  /* format JSON payload from ... */
  char payload[512];
  va_list ap;


  va_start (ap, fmt);
  vsnprintf (payload, sizeof payload, fmt, ap);
  va_end (ap);
  // LOGI("fer_event_json: Payload formatted: %s", payload);
  // 1. ACQUIRE LOCK (This should now correctly recurse)
  //LOGI("fer_event_json: Attempting to acquire db_mutex.");
  db_mutex_lock ();
  //LOGI("fer_event_json: Mutex acquired.");
  /* INSERT into engine_events(type, sector_id, payload, ts) */
  const char *sql =
    "INSERT INTO engine_events(type, sector_id, payload, ts) "
    "VALUES (?1, ?2, ?3, strftime('%s','now'))";
  sqlite3_stmt *st = NULL;
  // 2. Prepare Statement: Using the passed 'db' handle
  int rc = sqlite3_prepare_v2 (g_fer_db, sql, -1, &st, NULL);


  if (rc != SQLITE_OK)
    {
      LOGE ("fer_event_json: SQL Prepare FAILED (rc=%d).", rc);
      LOGE ("fer_event_json: Error message: %s", sqlite3_errmsg (g_fer_db));
      sqlite3_finalize (st);    // Clean up even if prepare failed
      db_mutex_unlock ();
      LOGI ("fer_event_json: Mutex released on prepare failure.");
      return;
    }
  //LOGI("fer_event_json: SQL prepared successfully.");
  // 3. Bind Parameters
  sqlite3_bind_text (st, 1, type, -1, SQLITE_TRANSIENT);
  sqlite3_bind_int (st, 2, sector_id);
  sqlite3_bind_text (st, 3, payload, -1, SQLITE_TRANSIENT);
  // 4. Step/Execute Statement
  rc = sqlite3_step (st);
  if (rc != SQLITE_DONE)
    {
      LOGE
        ("fer_event_json: SQL Step FAILED (rc=%d). Expected SQLITE_DONE (%d).",
        rc, SQLITE_DONE);
      LOGE ("fer_event_json: Step error message: %s",
            sqlite3_errmsg (g_fer_db));
    }
  else
    {
      //LOGI("fer_event_json: SQL step successful (Event logged).");
    }
  // 5. Finalize Statement
  sqlite3_finalize (st);
  //LOGI("fer_event_json: Statement finalized.");
  // 6. RELEASE LOCK
  db_mutex_unlock ();
  //LOGI("fer_event_json: Mutex released. Function complete.");
}


/**
 * Build a nested player object { id, name? } for event payloads.
 * Name lookup is best-effort; if not found, we emit only {id}.
 */
json_t *
make_player_object (int64_t player_id)
{
  json_t *player = json_object ();
  json_object_set_new (player, "id", json_integer (player_id));
  char *pname = NULL;


  if (db_player_name (player_id, &pname) == 0 && pname)
    {
      json_object_set_new (player, "name", json_string (pname));
      free (pname);             /* allocated in db_player_name with malloc */
    }
  return player;
}


static int
parse_sector_search_input (json_t *root,
                           char **q_out,
                           int *type_any, int *type_sector, int *type_port,
                           int *limit_out, int *offset_out)
{
  *q_out = NULL;
  *type_any = *type_sector = *type_port = 0;
  *limit_out = SEARCH_DEFAULT_LIMIT;
  *offset_out = 0;
  json_t *data = json_object_get (root, "data");


  if (!json_is_object (data))
    {
      return -1;
    }
  // q (optional, empty means “match all”)
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
  // type
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
  // limit
  json_t *jlimit = json_object_get (data, "limit");


  if (json_is_integer (jlimit))
    {
      int lim = (int) json_integer_value (jlimit);


      if (lim <= 0)
        {
          lim = SEARCH_DEFAULT_LIMIT;
        }
      if (lim > SEARCH_MAX_LIMIT)
        {
          lim = SEARCH_MAX_LIMIT;
        }
      *limit_out = lim;
    }
  // cursor (offset)
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
      // allow stringified integers too
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


//////////////////////////////
int
cmd_sector_search (client_ctx_t *ctx, json_t *root)
{
  UNUSED (ctx);
  if (!root)
    {
      return -1;
    }
  char *q = NULL;
  int type_any = 0, type_sector = 0, type_port = 0;
  int limit = 0, offset = 0;
  int prc =
    parse_sector_search_input (root, &q, &type_any, &type_sector, &type_port,
                               &limit, &offset);


  if (prc != 0)
    {
      free (q);
      send_enveloped_error (ctx->fd, root, 400, "Expected data { ... }");
      return 0;
    }
  sqlite3 *db = db_get_handle ();


  if (!db)
    {
      free (q);
      send_enveloped_error (ctx->fd, root, 500, "No database handle.");
      return 0;
    }
  char likepat[512];


  snprintf (likepat, sizeof (likepat), "%%%s%%", q ? q : "");
  int fetch = limit + 1;
  // --- This SQL is now simple and robust ---
  char sql[1024];
  const char *base_sql =
    "SELECT kind, id, name, sector_id, sector_name FROM sector_search_index ";
  const char *order_limit_sql = " ORDER BY kind, name, id LIMIT ?2 OFFSET ?3";
  // Build the WHERE clause
  char where_sql[256];


  if (type_any)
    {
      // Match 'q' against the search term, and don't filter by kind
      snprintf (where_sql, sizeof (where_sql),
                "WHERE ( (?1 = '') OR (search_term_1 LIKE ?1 COLLATE NOCASE) )");
    }
  else if (type_sector)
    {
      // Match 'q' AND kind = 'sector'
      snprintf (where_sql,
                sizeof (where_sql),
                "WHERE kind = 'sector' AND ( (?1 = '') OR (search_term_1 LIKE ?1 COLLATE NOCASE) )");
    }
  else
    {                           // type_port
      // Match 'q' AND kind = 'port'
      snprintf (where_sql,
                sizeof (where_sql),
                "WHERE kind = 'port' AND ( (?1 = '') OR (search_term_1 LIKE ?1 COLLATE NOCASE) )");
    }
  // Combine the query
  snprintf (sql, sizeof (sql), "%s %s %s", base_sql, where_sql,
            order_limit_sql);
  // --- End of new SQL logic ---
  // This will print the *much simpler* SQL query for debugging
  // // fprintf(stderr, "\n[DEBUG] SQL (View): [%s]\n\n", sql);
  sqlite3_stmt *st = NULL;
  int rc = sqlite3_prepare_v2 (db, sql, -1, &st, NULL);


  if (rc != SQLITE_OK)
    {
      free (q);
      send_enveloped_error (ctx->fd, root, 500, sqlite3_errmsg (db));
      return 0;
    }
  // Bind parameters
  sqlite3_bind_text (st, 1, likepat, -1, SQLITE_TRANSIENT);
  sqlite3_bind_int (st, 2, fetch);
  sqlite3_bind_int (st, 3, offset);
  // (The rest of your function: sqlite3_step loop, JSON packing, etc.)
  // ... (This part of your code was already correct) ...
  json_t *items = json_array ();
  int row_count = 0;


  while ((rc = sqlite3_step (st)) == SQLITE_ROW)
    {
      const char *kind = (const char *) sqlite3_column_text (st, 0);
      int id = sqlite3_column_int (st, 1);
      const char *name = (const char *) sqlite3_column_text (st, 2);
      int sector_id = sqlite3_column_int (st, 3);
      const char *sector_name = (const char *) sqlite3_column_text (st, 4);


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
      if (row_count >= fetch)
        {
          break;
        }
    }
  sqlite3_finalize (st);
  free (q);
  json_t *jdata = json_object ();


  json_object_set_new (jdata, "items", items);
  if (row_count > limit)
    {
      json_object_set_new (jdata, "next_cursor",
                           json_integer (offset + limit));
    }
  else
    {
      json_object_set_new (jdata, "next_cursor", json_null ());
    }
  send_enveloped_ok (ctx->fd, root, "sector.search_results_v1", jdata);
  return 0;
}


json_t *
build_sector_scan_json (int sector_id, int player_id,
                        bool holo_scanner_active)
{
  json_t *root = json_object ();
  if (!root)
    {
      return NULL;
    }
  json_object_set_new (root, "server_tick", json_integer (g_server_tick));
  /* Basic sector info (id/name) */
  json_t *basic = NULL;


  if (db_sector_basic_json (sector_id, &basic) == SQLITE_OK && basic)
    {
      json_object_set_new (root, "sector_id", json_integer (sector_id));
      json_object_set_new (root, "name", json_object_get (basic, "name"));
      json_decref (basic);
    }
  else
    {
      json_object_set_new (root, "sector_id", json_integer (sector_id));
    }
  /* Adjacent warps */
  json_t *adj = NULL;


  if (db_adjacent_sectors_json (sector_id, &adj) == SQLITE_OK && adj)
    {
      json_object_set_new (root, "adjacent", adj);
      json_object_set_new (root, "adjacent_count",
                           json_integer ((int) json_array_size (adj)));
    }
  else
    {
      json_object_set_new (root, "adjacent", json_array ());
      json_object_set_new (root, "adjacent_count", json_integer (0));
    }
  /* Ships (Filtered by Holo-Scanner) */
  json_t *ships = NULL;
  int rc = db_ships_at_sector_json (player_id, sector_id, &ships);


  if (rc == SQLITE_OK)
    {
      json_object_set_new (root, "ships", ships ? ships : json_array ());
      json_object_set_new (root, "ships_count",
                           json_integer ((int) json_array_size (ships)));
    }
  else
    {
      json_object_set_new (root, "ships", json_array ());
      json_object_set_new (root, "ships_count", json_integer (0));
    }
  json_object_set_new (root, "holo_scanner_active",
                       holo_scanner_active ? json_true () : json_false ());
  /* Ports */
  json_t *ports = NULL;


  if (db_ports_at_sector_json (sector_id, &ports) == SQLITE_OK && ports)
    {
      json_object_set_new (root, "ports", ports);
      json_object_set_new (root, "has_port",
                           json_array_size (ports) >
                           0 ? json_true () : json_false ());
    }
  else
    {
      json_object_set_new (root, "ports", json_array ());
      json_object_set_new (root, "has_port", json_false ());
    }
  /* Planets */
  json_t *planets = NULL;


  if (db_planets_at_sector_json (sector_id, &planets) == SQLITE_OK && planets)
    {
      json_object_set_new (root, "planets", planets);
      json_object_set_new (root, "has_planet",
                           json_array_size (planets) >
                           0 ? json_true () : json_false ());
      json_object_set_new (root, "planets_count",
                           json_integer ((int) json_array_size (planets)));
    }
  else
    {
      json_object_set_new (root, "planets", json_array ());
      json_object_set_new (root, "has_planet", json_false ());
      json_object_set_new (root, "planets_count", json_integer (0));
    }
  /* Players (excluding those only visible by cloaked ships already filtered above) */
  json_t *players = NULL;


  if (db_players_at_sector_json (sector_id, &players) == SQLITE_OK && players)
    {
      json_object_set_new (root, "players", players);
      json_object_set_new (root, "players_count",
                           json_integer ((int) json_array_size (players)));
    }
  else
    {
      json_object_set_new (root, "players", json_array ());
      json_object_set_new (root, "players_count", json_integer (0));
    }
  /* Beacons */
  json_t *beacons = NULL;


  if (db_beacons_at_sector_json (sector_id, &beacons) == SQLITE_OK && beacons)
    {
      json_object_set_new (root, "beacons", beacons);
      json_object_set_new (root, "beacons_count",
                           json_integer ((int) json_array_size (beacons)));
    }
  else
    {
      json_object_set_new (root, "beacons", json_array ());
      json_object_set_new (root, "beacons_count", json_integer (0));
    }
  // You can attach other asset counts here if needed, similar to attach_sector_asset_counts(db, sector_id, root);
  // Attach Fighters
  json_t *fighters = NULL;


  if (db_fighters_at_sector_json (sector_id,
                                  &fighters) == SQLITE_OK && fighters)
    {
      json_object_set_new (root, "fighters", fighters);
      json_object_set_new (root, "fighters_count",
                           json_integer ((int) json_array_size (fighters)));
    }
  else
    {
      json_object_set_new (root, "fighters", json_array ());
      json_object_set_new (root, "fighters_count", json_integer (0));
    }
  // Attach Mines
  json_t *mines = NULL;


  if (db_mines_at_sector_json (sector_id, &mines) == SQLITE_OK && mines)
    {
      json_object_set_new (root, "mines", mines);
      json_object_set_new (root, "mines_count",
                           json_integer ((int) json_array_size (mines)));
    }
  else
    {
      json_object_set_new (root, "mines", json_array ());
      json_object_set_new (root, "mines_count", json_integer (0));
    }
  // Original call - keep it as it handles other asset types.
  attach_sector_asset_counts (db_get_handle (), sector_id, root);
  return root;
}


// ====================================================================
// --- PRIMARY COMMAND HANDLER: cmd_sector_scan (Replaces Mock) ---


/**
 * @brief Executes a detailed tactical scan of the current sector.
 * * Implements the "sector.scan" command.
 * * @param ctx The client context containing player, ship, and database info.
 * @param root The root JSON request object.
 */
void
cmd_sector_scan (client_ctx_t *ctx, json_t *root)
{
  sqlite3 *db = db_get_handle ();
  int player_id = ctx->player_id;
  int ship_id = h_get_active_ship_id (db, player_id);
  int sector_id;
  bool holo_scanner_active = false;
  // 1. Basic Checks
  if (ship_id <= 0)
    {
      send_enveloped_error (ctx->fd, root, 1501,
                            "You must be in a ship to perform a sector scan.");
      return;
    }
  // 2. Get Sector ID
  sector_id = db_get_ship_sector_id (db, ship_id);
  if (sector_id <= 0)
    {
      send_enveloped_error (ctx->fd, root, 1502,
                            "Could not determine your current sector.");
      return;
    }
  // 3. Holo-Scanner Capability Check
  // --- Holo-Scanner Check Placeholder ---
  // The db_ship_has_holo_scanner() stub currently returns false.
  // Uncomment the line below once the DB function is properly implemented
  // to query the 'ships' table for the holo_scanner flag.
  // holo_scanner_active = db_ship_has_holo_scanner(db, ship_id);
  // --- End Placeholder ---
  // 4. Build JSON Payload
  json_t *payload =
    build_sector_scan_json (sector_id, player_id, holo_scanner_active);


  if (!payload)
    {
      send_enveloped_error (ctx->fd, root, 1503,
                            "Out of memory building sector scan info");
      return;
    }
  // 5. Send Response
  send_enveloped_ok (ctx->fd, root, "sector.scan", payload);
  json_decref (payload);
}


////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////
// "sector.scan.density"
// "sector.scan.density"
void
cmd_sector_scan_density (void *ctx_in, json_t *root)
{
  // FIX: Use the correct structure name provided by the user (client_ctx_t)
  client_ctx_t *ctx = (client_ctx_t *) ctx_in;
  // --- START: INITIALIZATION LOGIC ---
  // Determine the target sector_id from context or request data
  int msector = ctx->sector_id > 0 ? ctx->sector_id : 0;
  json_t *jdata = json_object_get (root, "data");
  json_t *jsec =
    json_is_object (jdata) ? json_object_get (jdata, "sector_id") : NULL;
  if (json_is_integer (jsec))
    {
      msector = (int) json_integer_value (jsec);
    }
  if (msector <= 0)
    {
      msector = 1;
    }
  // --- END: INITIALIZATION LOGIC ---
  sqlite3 *db = db_get_handle ();


  h_decloak_ship (db, h_get_active_ship_id (db, ctx->player_id));
  TurnConsumeResult tc = h_consume_player_turn (db, ctx,1);


  if (tc != TURN_CONSUME_SUCCESS)
    {
      handle_turn_consumption_error (ctx, tc, "sector.scan.density", root,
                                     NULL);
      return;
    }
  const char *sql_adj =
    "SELECT to_sector FROM sector_warps WHERE from_sector = ?1 ORDER BY to_sector";
  const char *sql_density_template =
    "SELECT sector_id, total_density_score FROM sector_ops WHERE sector_id IN (%s) "
    "ORDER BY (sector_id = %d) DESC, sector_id ASC;";
  sqlite3_stmt *st = NULL;
  sqlite3_stmt *st2 = NULL;
  char *sql_formatted = NULL;
  json_t *payload = NULL;
  int rc = SQLITE_ERROR;
  // Array to hold the current sector + adjacent sectors
  // Assuming MAX_WARPS_PER_SECTOR and SECTOR_LIST_BUF_SIZE are defined
  int adjacent[MAX_WARPS_PER_SECTOR + 1] = { 0 };
  int count = 0;


  db_mutex_lock ();
  // --- 1. Get Current and Adjacent Sectors ---
  adjacent[count++] = msector;  // Add the current sector ID first
  rc = sqlite3_prepare_v2 (db, sql_adj, -1, &st, NULL);
  if (rc != SQLITE_OK)
    {
      // Preparation failed, rc is the error code
      goto done;
    }
  sqlite3_bind_int (st, 1, msector);
  while ((rc = sqlite3_step (st)) == SQLITE_ROW
         && count < (MAX_WARPS_PER_SECTOR + 1))
    {
      adjacent[count++] = sqlite3_column_int (st, 0);
    }
  // If the loop broke because the buffer was full, drain remaining rows
  if (rc == SQLITE_ROW)
    {
      while ( (rc = sqlite3_step (st)) == SQLITE_ROW)
        {
          // Do nothing, just drain
        }
    }
  // CHECKPOINT: Check if the loop terminated due to an error other than SQLITE_DONE.
  if (rc != SQLITE_DONE)
    {
      // An error occurred during sqlite3_step (rc is the error code)
      goto done;
    }
  // Set rc back to OK since we successfully retrieved the list (even if the list was empty)
  rc = SQLITE_OK;
  // st is now finalized in the 'done' block, we no longer need to finalize it here.
  // sqlite3_finalize (st);
  // st = NULL;
  // --- 2. Build Comma-Separated Sector List (list) ---
  char list[SECTOR_LIST_BUF_SIZE] = "";
  int current_len = 0;


  for (int a = 0; a < count; a++)
    {
      int appended_len = snprintf (list + current_len,
                                   SECTOR_LIST_BUF_SIZE - current_len,
                                   "%d,",
                                   adjacent[a]);


      if (appended_len > 0
          && current_len + appended_len < SECTOR_LIST_BUF_SIZE)
        {
          current_len += appended_len;
        }
      else
        {
          rc = ERROR_INTERNAL;  // Buffer overflow
          goto done;
        }
    }
  // Remove the trailing comma
  if (current_len > 0)
    {
      list[current_len - 1] = '\0';
    }
  else
    {
      // This should only happen if the sector list is empty, which shouldn't be possible
      // as msector is always added, but we handle it just in case.
      if (count == 0)
        {
          rc = SQLITE_OK;       // No warps, no density to show, but no critical error
        }
      else
        {
          rc = ERROR_INTERNAL;
        }
      goto done;
    }
  // --- 3. Prepare and Execute Density Query (FIXED) ---
  // Dynamically generate the SQL string using sqlite3_mprintf
  sql_formatted = sqlite3_mprintf (sql_density_template, list, msector);
  if (!sql_formatted)
    {
      rc = SQLITE_NOMEM;
      goto done;
    }
  rc = sqlite3_prepare_v2 (db, sql_formatted, -1, &st2, NULL);
  // Free the result of sqlite3_mprintf immediately after use
  sqlite3_free (sql_formatted);
  if (rc != SQLITE_OK)
    {
      goto done;
    }
  // --- 4. Process Results and Build JSON Payload ---
  payload = json_array ();
  while ((rc = sqlite3_step (st2)) == SQLITE_ROW)
    {
      int sector_id = sqlite3_column_int (st2, 0);
      int density = sqlite3_column_int (st2, 1);


      // Append sector ID and density score pair
      json_array_append_new (payload, json_integer (sector_id));
      json_array_append_new (payload, json_integer (density));
    }
  if (rc == SQLITE_DONE)
    {
      // --- FINAL USER-REQUESTED RESPONSE LOGIC ---
      send_enveloped_ok (ctx->fd, root, "sector.density.scan", payload);
      json_decref (payload);
      // --- FINAL USER-REQUESTED RESPONSE LOGIC ---
      rc = SQLITE_OK;
    }
  else
    {
      // An error occurred in the last sqlite3_step of st2
      json_decref (payload);
      // rc already holds the error code
    }
done:
  // Finalize all statements and unlock mutex
  // *MUST* finalize st and st2 if they were successfully prepared (st != NULL)
  if (st)
    {
      sqlite3_finalize (st);
    }
  if (st2)
    {
      sqlite3_finalize (st2);
    }
  db_mutex_unlock ();
  // Handle errors if the final RC is not OK
  if (rc != SQLITE_OK && rc != SQLITE_DONE)
    {
      // Assuming ERR_DB and send_enveloped_error are defined
      send_enveloped_error (ctx->fd, root, ERR_DB, sqlite3_errstr (rc));
    }
}


/*
 * Checks if a warp link exists between two sectors.
 * Returns 1 if a warp exists, 0 otherwise.
 */
int
h_warp_exists (sqlite3 *db, int from_sector_id, int to_sector_id)
{
  sqlite3_stmt *st = NULL;
  int has_warp = 0;
  const char *sql =
    "SELECT 1 FROM sector_warps WHERE from_sector = ?1 AND to_sector = ?2 LIMIT 1;";
  if (sqlite3_prepare_v2 (db, sql, -1, &st, NULL) != SQLITE_OK)
    {
      LOGE ("Failed to prepare h_warp_exists statement: %s",
            sqlite3_errmsg (db));
      return 0;
    }
  sqlite3_bind_int (st, 1, from_sector_id);
  sqlite3_bind_int (st, 2, to_sector_id);
  if (sqlite3_step (st) == SQLITE_ROW)
    {
      has_warp = 1;
    }
  sqlite3_finalize (st);
  return has_warp;
}


int
universe_init (void)
{
  sqlite3 *handle = db_get_handle ();   /* <-- accessor */
  sqlite3_stmt *stmt;
  const char *sql = "SELECT COUNT(*) FROM sectors;";
  if (sqlite3_prepare_v2 (handle, sql, -1, &stmt, NULL) != SQLITE_OK)
    {
      LOGE ( "DB universe_init error: %s\n",
             sqlite3_errmsg (handle));
      return -1;
    }
  int count = 0;


  if (sqlite3_step (stmt) == SQLITE_ROW)
    {
      count = sqlite3_column_int (stmt, 0);
    }
  sqlite3_finalize (stmt);
  if (count == 0)
    {
      LOGE ( "Universe empty, running bigbang...\n");
      return bigbang ();
    }
  return 0;
}


/* Shutdown universe (hook for cleanup) */
void
universe_shutdown (void)
{
  /* At present nothing to do, placeholder for later */
}


/* -------- helpers -------- */


/* Return REFUSED(1402) if no link; otherwise OK. */
decision_t
validate_warp_rule (int from_sector, int to_sector)
{
  if (to_sector <= 0)
    {
      return err (ERR_BAD_REQUEST, "Missing required field");
    }
  if (from_sector <= 0)
    {
      return err (ERR_BAD_STATE, "Unknown current sector");
    }
  if (from_sector == to_sector)
    {
      return ok ();             /* no-op warp is fine (cheap “success”) */
    }
  sqlite3 *db = db_get_handle ();


  if (!db)
    {
      return err (ERR_DB, "No database handle");
    }
  int has = 0;
  sqlite3_stmt *st = NULL;


  /* Fast adjacency check */
  db_mutex_lock ();
  int rc = sqlite3_prepare_v2 (db,
                               "SELECT 1 FROM sector_warps WHERE from_sector = ?1 AND to_sector = ?2 LIMIT 1",
                               -1,
                               &st,
                               NULL);


  if (rc == SQLITE_OK)
    {
      sqlite3_bind_int (st, 1, from_sector);
      sqlite3_bind_int (st, 2, to_sector);
      if (sqlite3_step (st) == SQLITE_ROW)
        {
          has = 1;
        }
    }
  if (st)
    {
      sqlite3_finalize (st);
    }
  db_mutex_unlock ();
  if (!has)
    {
      return refused (REF_NO_WARP_LINK, "No warp link");
    }
  return ok ();
}


/* -------- MOVEMENT -------- */
int
cmd_move_describe_sector (client_ctx_t *ctx, json_t *root)
{
  int sector_id = ctx->sector_id > 0 ? ctx->sector_id : 0;
  json_t *jdata = json_object_get (root, "data");
  json_t *jsec =
    json_is_object (jdata) ? json_object_get (jdata, "sector_id") : NULL;
  if (json_is_integer (jsec))
    {
      sector_id = (int) json_integer_value (jsec);
    }
  if (sector_id <= 0)
    {
      sector_id = 1;
    }
  (void) cmd_sector_info (ctx->fd, root, sector_id, ctx->player_id);
  return 0;
}


int
cmd_move_warp (client_ctx_t *ctx, json_t *root)
{
  LOGI ("cmd_move_warp: ctx->player_id=%d, ctx->sector_id=%d",
        ctx->player_id,
        ctx->sector_id);
  sqlite3 *db_handle = db_get_handle ();


  h_decloak_ship (db_handle,
                  h_get_active_ship_id (db_handle, ctx->player_id));
  TurnConsumeResult tc = h_consume_player_turn (db_handle, ctx,1);


  if (tc != TURN_CONSUME_SUCCESS)
    {
      return handle_turn_consumption_error (ctx, tc, "move.warp", root, NULL);
    }
  json_t *jdata = json_object_get (root, "data");
  int to = 0;


  if (json_is_object (jdata))
    {
      json_t *jto = json_object_get (jdata, "to_sector_id");


      if (json_is_integer (jto))
        {
          to = (int) json_integer_value (jto);
        }
    }
  decision_t d = validate_warp_rule (ctx->sector_id, to);


  if (d.status == DEC_ERROR)
    {
      send_enveloped_error (ctx->fd, root, d.code, d.message);
      return 0;
    }
  if (d.status == DEC_REFUSED)
    {
      json_t *meta = json_pack ("{s:i,s:i,s:s}",
                                "from", ctx->sector_id,
                                "to", to,
                                "reason",
                                (d.code ==
                                 REF_NO_WARP_LINK ? "no_warp_link" :
                                 "refused"));


      send_enveloped_refused (ctx->fd, root, d.code, d.message, meta);
      json_decref (meta);
      return 0;
    }

  /* START TRANSACTION */
  char *errmsg = NULL;


  if (sqlite3_exec (db_handle, "BEGIN IMMEDIATE;", NULL, NULL,
                    &errmsg) != SQLITE_OK)
    {
      if (errmsg)
        {
          sqlite3_free (errmsg);
        }
      send_enveloped_error (ctx->fd, root, 1500, "DB Transaction Error");
      return 0;
    }

  /* 1. Verify Current Sector (Authoritative) */
  int auth_from = 0;
  sqlite3_stmt *st_chk = NULL;


  if (sqlite3_prepare_v2 (db_handle,
                          "SELECT sector FROM players WHERE id=?1",
                          -1,
                          &st_chk,
                          NULL) == SQLITE_OK)
    {
      sqlite3_bind_int (st_chk, 1, ctx->player_id);
      if (sqlite3_step (st_chk) == SQLITE_ROW)
        {
          auth_from = sqlite3_column_int (st_chk, 0);
        }
      sqlite3_finalize (st_chk);
    }

  int from = ctx->sector_id;


  if (auth_from != from)
    {
      LOGW ("Warp mismatch: Client thinks %d, DB says %d. Using DB.",
            from,
            auth_from);
      from = auth_from;
      ctx->sector_id = auth_from;

      /* Re-validate with authoritative source */
      decision_t d2 = validate_warp_rule (from, to);


      if (d2.status != DEC_OK)
        {
          sqlite3_exec (db_handle, "ROLLBACK;", NULL, NULL, NULL);
          send_enveloped_error (ctx->fd, root, d2.code, d2.message);
          return 0;
        }
    }

  int turns_spent = 1;          // Assuming 1 turn for a single warp, as per typical game mechanics.


  LOGI
    ("cmd_move_warp: Player %d (fd %d) attempting to warp from sector %d to %d",
    ctx->player_id, ctx->fd, from, to);
  /* Persist & update session */
  int prc = db_player_set_sector (ctx->player_id, to);


  if (prc != SQLITE_OK)
    {
      sqlite3_exec (db_handle, "ROLLBACK;", NULL, NULL, NULL);
      LOGE
      (
        "cmd_move_warp: db_player_set_sector failed for player %d, ship_id %d, to sector %d. Error code: %d",
        ctx->player_id,
        h_get_active_ship_id (db_handle, ctx->player_id),
        to,
        prc);
      send_enveloped_error (ctx->fd, root, 1502,
                            "Failed to persist player sector");
      return 0;
    }
  LOGI
  (
    "cmd_move_warp: Player %d (fd %d) successfully warped from sector %d to %d. db_player_set_sector returned %d. Rows updated: %d",
    ctx->player_id,
    ctx->fd,
    from,
    to,
    prc,
    sqlite3_changes (db_handle));
  ctx->sector_id = to;
  // Apply Armid mines on entry
  armid_encounter_t armid_enc = { 0 };


  if (apply_armid_mines_on_entry (ctx, to, &armid_enc) != 0)
    {
      sqlite3_exec (db_handle, "ROLLBACK;", NULL, NULL, NULL);
      send_enveloped_error (ctx->fd, root, 1500, "Mine trigger error");
      return 0;
    }

  sqlite3_exec (db_handle, "COMMIT;", NULL, NULL, NULL);
  g_warps_performed++; // Increment atomic counter

  /* 1) Send the direct reply for the actor */
  json_t *resp = json_object ();


  json_object_set_new (resp, "player_id", json_integer (ctx->player_id));
  json_object_set_new (resp, "from_sector_id", json_integer (from));
  json_object_set_new (resp, "to_sector_id", json_integer (to));
  json_object_set_new (resp, "turns_spent", json_integer (turns_spent));
  if (armid_enc.armid_triggered > 0)
    {
      json_t *damage_obj = json_object ();


      json_object_set_new (damage_obj, "shields_lost",
                           json_integer (armid_enc.shields_lost));
      json_object_set_new (damage_obj, "fighters_lost",
                           json_integer (armid_enc.fighters_lost));
      json_object_set_new (damage_obj, "hull_lost",
                           json_integer (armid_enc.hull_lost));
      json_object_set_new (damage_obj, "destroyed",
                           json_boolean (armid_enc.destroyed));
      json_t *encounter_obj = json_object ();


      json_object_set_new (encounter_obj, "kind", json_string ("mines"));
      json_object_set_new (encounter_obj, "armid_triggered",
                           json_integer (armid_enc.armid_triggered));
      json_object_set_new (encounter_obj, "armid_remaining",
                           json_integer (armid_enc.armid_remaining));
      json_object_set_new (encounter_obj, "damage", damage_obj);
      json_object_set_new (resp, "encounter", encounter_obj);
    }
  send_enveloped_ok (ctx->fd, root, "move.result", resp);
  /* 2) Broadcast LEFT (from) then ENTERED (to) to subscribers */
  /* LEFT event: sector = 'from' */
  json_t *left = json_object ();


  json_object_set_new (left, "player_id", json_integer (ctx->player_id));
  json_object_set_new (left, "sector_id", json_integer (from));
  json_object_set_new (left, "to_sector_id", json_integer (to));
  json_object_set_new (left, "player", make_player_object (ctx->player_id));
  comm_publish_sector_event (from, "sector.player_left", left);
  /* ENTERED event: sector = 'to' */
  json_t *entered = json_object ();


  json_object_set_new (entered, "player_id", json_integer (ctx->player_id));
  json_object_set_new (entered, "sector_id", json_integer (to));
  json_object_set_new (entered, "from_sector_id", json_integer (from));
  json_object_set_new (entered, "player",
                       make_player_object (ctx->player_id));
  comm_publish_sector_event (to, "sector.player_entered", entered);
  return 0;
}


/* -------- move.pathfind: BFS path A->B with avoid list -------- */
int
cmd_move_pathfind (client_ctx_t *ctx, json_t *root)
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
      send_enveloped_error (ctx->fd, root, 1401,
                            "Target sector not specified");
      return 1;
    }
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
      send_enveloped_error (ctx->fd, root, 1401, "No sectors");
      return 1;
    }
  /* Clamp from/to to valid range quickly */
  if (from <= 0 || from > max_id || to > max_id)
    {
      send_enveloped_error (ctx->fd, root, 1401, "Sector not found");
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
      send_enveloped_error (ctx->fd, root, 1500, "Out of memory");
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
      send_enveloped_error (ctx->fd, root, 1406, "Path not found");
      return 1;
    }
  /* Trivial path */
  if (from == to)
    {
      json_t *steps = json_array ();


      json_array_append_new (steps, json_integer (from));
      json_t *out = json_object ();


      json_object_set_new (out, "steps", steps);
      json_object_set_new (out, "total_cost", json_integer (0));
      send_enveloped_ok (ctx->fd, root, "move.path_v1", out);
      free (avoid);
      free (prev);
      free (seen);
      free (queue);
      return 1;
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
      send_enveloped_error (ctx->fd, root, 1500, "Pathfind init failed");
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
              /* still finish stepping rows to keep stmt sane */
              /* break after unlock */
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
      send_enveloped_error (ctx->fd, root, 1406, "Path not found");
      return 1;
    }
  /* Reconstruct path */
  json_t *steps = json_array ();
  int cur = to;
  int hops = 0;
  /* backtrack into a simple stack (we can append to a temp C array then JSON) */
  int *stack = (int *) malloc (N * sizeof (int));


  if (!stack)
    {
      free (avoid);
      free (prev);
      free (seen);
      free (queue);
      send_enveloped_error (ctx->fd, root, 1500, "Out of memory");
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
      send_enveloped_error (ctx->fd, root, 1406, "Path not found");
      return 1;
    }
  /* reverse into JSON steps: from .. to */
  for (int i = sp - 1; i >= 0; --i)
    {
      json_array_append_new (steps, json_integer (stack[i]));
    }
  hops = sp - 1;
  free (stack);
  /* Build rootponse */
  json_t *out = json_object ();


  json_object_set_new (out, "steps", steps);
  json_object_set_new (out, "total_cost", json_integer (hops));
  send_enveloped_ok (ctx->fd, root, "move.path_v1", out);
  free (avoid);
  free (prev);
  free (seen);
  free (queue);
  return 0;
}


static const char *SQL_SECTOR_ASSET_COUNTS =
  "SELECT asset_type, COALESCE(SUM(quantity),0) AS qty "
  "FROM sector_assets WHERE sector = ?1 " "GROUP BY asset_type;";


static void
attach_sector_asset_counts (sqlite3 *db, int sector_id, json_t *data_out)
{
  int ftrs = 0, armid = 0, limpet = 0;
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2 (db, SQL_SECTOR_ASSET_COUNTS, -1, &st, NULL) ==
      SQLITE_OK)
    {
      sqlite3_bind_int (st, 1, sector_id);
      while (sqlite3_step (st) == SQLITE_ROW)
        {
          int type = sqlite3_column_int (st, 0);
          int qty = sqlite3_column_int (st, 1);


          if (type == 2)
            {
              ftrs += qty;      /* ASSET_FIGHTER */
            }
          else if (type == 1)
            {
              armid += qty;     /* ASSET_MINE (Armid) */
            }
          else if (type == 4)
            {
              limpet += qty;    /* ASSET_LIMPET_MINE */
            }
        }
    }
  if (st)
    {
      sqlite3_finalize (st);
    }
  json_t *counts = json_object ();


  json_object_set_new (counts, "fighters", json_integer (ftrs));
  json_object_set_new (counts, "mines_armid", json_integer (armid));
  json_object_set_new (counts, "mines_limpet", json_integer (limpet));
  json_object_set_new (counts, "mines", json_integer (armid + limpet));

  /* If you also track ships/planets elsewhere and already had a counts obj,
     merge instead of overwrite. Otherwise, just set it: */
  json_object_set_new (data_out, "counts", counts);
}


void
cmd_sector_info (int fd, json_t *root, int sector_id, int player_id)
{
  sqlite3 *db = db_get_handle ();
  json_t *payload = build_sector_info_json (sector_id);
  if (!payload)
    {
      send_enveloped_error (fd, root, 1500,
                            "Out of memory building sector info");
      return;
    }
  // Add beacon info
  char *btxt = NULL;


  if (db_sector_beacon_text (sector_id, &btxt) == SQLITE_OK && btxt && *btxt)
    {
      json_object_set_new (payload, "beacon", json_string (btxt));
      json_object_set_new (payload, "has_beacon", json_true ());
    }
  else
    {
      json_object_set_new (payload, "beacon", json_null ());
      json_object_set_new (payload, "has_beacon", json_false ());
    }
  free (btxt);
  // Add ships info
  json_t *ships = NULL;
  int rc = db_ships_at_sector_json (player_id, sector_id, &ships);


  if (rc == SQLITE_OK)
    {
      json_object_set_new (payload, "ships", ships ? ships : json_array ());
      json_object_set_new (payload, "ships_count",
                           json_integer (json_array_size (ships)));
    }
  // Add port info
  json_t *ports = NULL;
  int pt = db_ports_at_sector_json (sector_id, &ports);


  if (pt == SQLITE_OK)
    {
      json_object_set_new (payload, "ports", ports ? ports : json_array ());
      json_object_set_new (payload, "ports_count",
                           json_integer (json_array_size (ports)));
    }
  // Add planet info
  json_t *planets = NULL;
  int plt = db_planets_at_sector_json (sector_id, &planets);


  if (plt == SQLITE_OK)
    {
      json_object_set_new (payload, "planets",
                           planets ? planets : json_array ());
      json_object_set_new (payload, "planets_count",
                           json_integer (json_array_size (planets)));
    }
  // Add planet info
  json_t *players = NULL;
  int py = db_players_at_sector_json (sector_id, &players);


  if (py == SQLITE_OK)
    {
      json_object_set_new (payload, "players",
                           players ? players : json_array ());
      json_object_set_new (payload, "players_count",
                           json_integer (json_array_size (players)));
    }
  attach_sector_asset_counts (db, sector_id, payload);
  send_enveloped_ok (fd, root, "sector.info", payload);
  json_decref (payload);
}


/* /\* Build a full sector snapshot for sector.info *\/ */
json_t *
build_sector_info_json (int sector_id)
{
  json_t *root = json_object ();
  if (!root)
    {
      return NULL;
    }
  json_object_set_new (root, "server_tick", json_integer (g_server_tick));
  /* Basic info (id/name) */
  json_t *basic = NULL;


  if (db_sector_basic_json (sector_id, &basic) == SQLITE_OK && basic)
    {
      json_t *sid = json_object_get (basic, "sector_id");
      json_t *name = json_object_get (basic, "name");


      if (sid)
        {
          json_object_set (root, "sector_id", sid);
        }
      if (name)
        {
          json_object_set (root, "name", name);
        }
      json_decref (basic);
    }
  else
    {
      json_object_set_new (root, "sector_id", json_integer (sector_id));
    }
  /* Adjacent warps */
  json_t *adj = NULL;


  if (db_adjacent_sectors_json (sector_id, &adj) == SQLITE_OK && adj)
    {
      json_object_set_new (root, "adjacent", adj);
      json_object_set_new (root, "adjacent_count",
                           json_integer ((int) json_array_size (adj)));
    }
  else
    {
      json_object_set_new (root, "adjacent", json_array ());
      json_object_set_new (root, "adjacent_count", json_integer (0));
    }
/* Ports */
  json_t *ports = NULL;


  if (db_ports_at_sector_json (sector_id, &ports) == SQLITE_OK && ports)
    {
      json_object_set_new (root, "ports", ports);
      json_object_set_new (root, "has_port",
                           json_array_size (ports) >
                           0 ? json_true () : json_false ());
    }
  else
    {
      json_object_set_new (root, "ports", json_array ());
      json_object_set_new (root, "has_port", json_false ());
    }
  /* Players */
  json_t *players = NULL;


  if (db_players_at_sector_json (sector_id, &players) == SQLITE_OK && players)
    {
      json_object_set_new (root, "players", players);
      json_object_set_new (root, "players_count",
                           json_integer ((int) json_array_size (players)));
    }
  else
    {
      json_object_set_new (root, "players", json_array ());
      json_object_set_new (root, "players_count", json_integer (0));
    }
  /* Beacons (always include array) */
  json_t *beacons = NULL;


  if (db_beacons_at_sector_json (sector_id, &beacons) == SQLITE_OK && beacons)
    {
      json_object_set_new (root, "beacons", beacons);
      json_object_set_new (root, "beacons_count",
                           json_integer ((int) json_array_size (beacons)));
    }
  else
    {
      json_object_set_new (root, "beacons", json_array ());
      json_object_set_new (root, "beacons_count", json_integer (0));
    }
  /* Planets */
  json_t *planets = NULL;


  if (db_planets_at_sector_json (sector_id, &planets) == SQLITE_OK && planets)
    {
      json_object_set_new (root, "planets", planets);   /* takes ownership */
      json_object_set_new (root, "has_planet",
                           json_array_size (planets) >
                           0 ? json_true () : json_false ());
      json_object_set_new (root, "planets_count",
                           json_integer ((int) json_array_size (planets)));
    }
  else
    {
      json_object_set_new (root, "planets", json_array ());
      json_object_set_new (root, "has_planet", json_false ());
      json_object_set_new (root, "planets_count", json_integer (0));
    }
  /* Beacons (always include array) */
  // json_t *beacons = NULL;
  if (db_beacons_at_sector_json (sector_id, &beacons) == SQLITE_OK && beacons)
    {
      json_object_set_new (root, "beacons", beacons);
      json_object_set_new (root, "beacons_count",
                           json_integer ((int) json_array_size (beacons)));
    }
  else
    {
      json_object_set_new (root, "beacons", json_array ());
      json_object_set_new (root, "beacons_count", json_integer (0));
    }
  return root;
}


/* -------- move.scan: fast, side-effect-free snapshot (defensive build) -------- */
int
cmd_move_scan (client_ctx_t *ctx, json_t *root)
{
  if (!ctx)
    {
      return 1;
    }
  sqlite3 *db_handle = db_get_handle ();


  h_decloak_ship (db_handle,
                  h_get_active_ship_id (db_handle, ctx->player_id));
  TurnConsumeResult tc = h_consume_player_turn (db_handle, ctx,1);


  if (tc != TURN_CONSUME_SUCCESS)
    {
      return handle_turn_consumption_error (ctx, tc, "move.scan", root, NULL);
    }
  /* Resolve sector id (default to 1 if session is unset) */
  int sector_id = (ctx->sector_id > 0) ? ctx->sector_id : 1;


  LOGI ("[move.scan] sector_id=%d\n", sector_id);
  /* 1) Core snapshot from DB (uses sectors.name/beacon; ports.location; ships.location; planets.sector) */
  json_t *core = NULL;


  if (db_sector_scan_core (sector_id, &core) != SQLITE_OK || !core)
    {
      send_enveloped_error (ctx->fd, root, 1401, "Sector not found");
      return 0;
    }
  /* 2) Adjacent IDs (array) */
  json_t *adj = NULL;


  if (db_adjacent_sectors_json (sector_id, &adj) != SQLITE_OK || !adj)
    {
      adj = json_array ();      /* never null */
    }
  /* 3) Security flags */
  int in_fed = (sector_id >= 1 && sector_id <= 10);
  int safe_zone = json_integer_value (json_object_get (core, "safe_zone"));     /* 0 with your schema */
  json_t *security = json_object ();


  if (!security)
    {
      json_decref (core);
      json_decref (adj);
      send_enveloped_error (ctx->fd, root, 1500, "OOM");
      return 0;
    }
  json_object_set_new (security, "fedspace", json_boolean (in_fed));
  json_object_set_new (security, "safe_zone",
                       json_boolean (in_fed ? 1 : (safe_zone != 0)));
  json_object_set_new (security, "combat_locked",
                       json_boolean (in_fed ? 1 : 0));
  /* 4) Port summary (presence only) */
  int port_cnt = json_integer_value (json_object_get (core, "port_count"));
  json_t *port = json_object ();


  if (!port)
    {
      json_decref (core);
      json_decref (adj);
      json_decref (security);
      send_enveloped_error (ctx->fd, root, 1500, "OOM");
      return 0;
    }
  json_object_set_new (port, "present", json_boolean (port_cnt > 0));
  json_object_set_new (port, "class", json_null ());
  json_object_set_new (port, "stance", json_null ());
  /* 5) Counts object */
  int ships = json_integer_value (json_object_get (core, "ship_count"));
  int planets = json_integer_value (json_object_get (core, "planet_count"));
  json_t *counts = json_object ();


  if (!counts)
    {
      json_decref (core);
      json_decref (adj);
      json_decref (security);
      json_decref (port);
      send_enveloped_error (ctx->fd, root, 1500, "OOM");
      return 0;
    }
  json_object_set_new (counts, "ships", json_integer (ships));
  json_object_set_new (counts, "planets", json_integer (planets));
  json_object_set_new (counts, "mines", json_integer (0));
  json_object_set_new (counts, "fighters", json_integer (0));
  /* 6) Beacon (string or null) */
  const char *btxt =
    json_string_value (json_object_get (core, "beacon_text"));
  json_t *beacon = (btxt && *btxt) ? json_string (btxt) : json_null ();


  if (!beacon)
    {                           /* json_string can OOM */
      beacon = json_null ();
    }
  /* 7) Name */
  const char *name = json_string_value (json_object_get (core, "name"));
  /* 8) Build data object explicitly (no json_pack; no chance of NULL from format mismatch) */
  json_t *data = json_object ();


  if (!data)
    {
      json_decref (core);
      json_decref (adj);
      json_decref (security);
      json_decref (port);
      json_decref (counts);
      if (beacon)
        {
          json_decref (beacon);
        }
      send_enveloped_error (ctx->fd, root, 1500, "OOM");
      return 0;
    }
  json_object_set_new (data, "sector_id", json_integer (sector_id));
  json_object_set_new (data, "name", json_string (name ? name : "Unknown"));
  json_object_set_new (data, "security", security);     /* transfers ownership */
  json_object_set_new (data, "adjacent", adj);  /* transfers ownership */
  json_object_set_new (data, "port", port);     /* transfers ownership */
  json_object_set_new (data, "counts", counts); /* transfers ownership */
  json_object_set_new (data, "beacon", beacon); /* transfers ownership */
  /* Optional debug: confirm non-NULL before sending */
  LOGD ("[move.scan] built data=%p (sector_id=%d)\n",
        (void *) data, sector_id);
  /* 9) Send envelope (your send_enveloped_ok steals the 'data' ref via _set_new) */
  send_enveloped_ok (ctx->fd, root, "sector.scan_v1", data);
  /* 10) Clean up */
  json_decref (core);
  /* 'data' members already owned by 'data' -> envelope stole 'data' */
  return 0;
}


int
cmd_sector_set_beacon (client_ctx_t *ctx, json_t *root)
{
  if (!ctx || !root)
    {
      return 1;
    }
  sqlite3 *db_handle = db_get_handle ();


  h_decloak_ship (db_handle,
                  h_get_active_ship_id (db_handle, ctx->player_id));
  json_t *jdata = json_object_get (root, "data");
  json_t *jsector_id = json_object_get (jdata, "sector_id");
  json_t *jtext = json_object_get (jdata, "text");


  /* Guard 0: schema */
  if (!json_is_integer (jsector_id) || !json_is_string (jtext))
    {
      send_enveloped_error (ctx->fd, root, 1300, "Invalid request schema");
      return 1;
    }
  /* Guard 1: player must be in that sector */
  int req_sector_id = (int) json_integer_value (jsector_id);


  if (ctx->sector_id != req_sector_id)
    {
      send_enveloped_error (ctx->fd, root, 1400,
                            "Player is not in the specified sector.");
      return 1;
    }
  /* Guard 2: FedSpace 1–10 is forbidden */
  if (req_sector_id >= 1 && req_sector_id <= 10)
    {
      send_enveloped_error (ctx->fd, root, 1403,
                            "Cannot set a beacon in FedSpace.");
      return 1;
    }
  /* Guard 3: player must have a beacon on the ship */
  if (!db_player_has_beacon_on_ship (ctx->player_id))
    {
      send_enveloped_error (ctx->fd, root, 1401,
                            "Player does not have a beacon on their ship.");
      return 1;
    }

  /* NOTE: Canon behavior: if a beacon already exists, launching another destroys BOTH.
     So we DO NOT reject here. We only check 'had_beacon' to craft a user message. */
  int had_beacon = db_sector_has_beacon (req_sector_id);
  /* Text length guard (<=80) */
  const char *beacon_text = json_string_value (jtext);


  if (!beacon_text)
    {
      beacon_text = "";
    }
  if ((int) strlen (beacon_text) > 80)
    {
      send_enveloped_error (ctx->fd, root, 1400,
                            "Beacon text is too long (max 80 characters).");
      return 1;
    }

  /* Perform the update:
     - if none existed → set text
     - if one existed  → clear (explode both) */
  int rc = db_sector_set_beacon (req_sector_id, beacon_text, ctx->player_id);


  if (rc != SQLITE_OK)
    {
      send_enveloped_error (ctx->fd, root, 1500,
                            "Database error updating beacon.");
      return 1;
    }
  /* Consume the player's beacon (canon: you used it either way) */
  db_player_decrement_beacon_count (ctx->player_id);
  /* ===== Build sector.info payload (same fields as handle_sector_info) ===== */
  json_t *payload = build_sector_info_json (req_sector_id);


  if (!payload)
    {
      send_enveloped_error (ctx->fd, root, 1500,
                            "Out of memory building sector info");
      return 1;
    }
  /* Beacon text */
  char *btxt = NULL;


  if (db_sector_beacon_text (req_sector_id, &btxt) == SQLITE_OK && btxt
      && *btxt)
    {
      json_object_set_new (payload, "beacon", json_string (btxt));
      json_object_set_new (payload, "has_beacon", json_true ());
    }
  else
    {
      json_object_set_new (payload, "beacon", json_null ());
      json_object_set_new (payload, "has_beacon", json_false ());
    }
  free (btxt);
  /* Ships */
  json_t *ships = NULL;


  rc = db_ships_at_sector_json (ctx->player_id, req_sector_id, &ships);
  if (rc == SQLITE_OK)
    {
      json_object_set_new (payload, "ships", ships ? ships : json_array ());
      json_object_set_new (payload, "ships_count",
                           json_integer ((int) json_array_size (ships)));
    }
  /* Ports */
  json_t *ports = NULL;
  int pt = db_ports_at_sector_json (req_sector_id, &ports);


  if (pt == SQLITE_OK)
    {
      json_object_set_new (payload, "ports", ports ? ports : json_array ());
      json_object_set_new (payload, "ports_count",
                           json_integer ((int) json_array_size (ports)));
    }
  /* Planets */
  json_t *planets = NULL;
  int plt = db_planets_at_sector_json (req_sector_id, &planets);


  if (plt == SQLITE_OK)
    {
      json_object_set_new (payload, "planets",
                           planets ? planets : json_array ());
      json_object_set_new (payload, "planets_count",
                           json_integer ((int) json_array_size (planets)));
    }
  /* Players */
  json_t *players = NULL;
  int py = db_players_at_sector_json (req_sector_id, &players);


  if (py == SQLITE_OK)
    {
      json_object_set_new (payload, "players",
                           players ? players : json_array ());
      json_object_set_new (payload, "players_count",
                           json_integer ((int) json_array_size (players)));
    }
  /* ===== Send envelope with a nice meta.message ===== */
  json_t *env = make_base_envelope (root, "sector.info");


  json_object_set_new (env, "status", json_string ("ok"));
  json_object_set_new (env, "type", json_string ("sector.info"));
  json_object_set_new (env, "data", payload);   /* take ownership */
  json_t *meta = json_object ();


  json_object_set_new (meta, "message",
                       json_string (had_beacon
                                    ?
                                    "Two marker beacons collided and exploded — the sector now has no beacon."
                                    : "Beacon deployed."));
  json_object_set_new (env, "meta", meta);
  attach_rate_limit_meta (env, ctx);
  rl_tick (ctx);
  send_all_json (ctx->fd, env);
  json_decref (env);
  send_enveloped_ok (ctx->fd, root, "sector.set_beacon", NULL);
  return 0;
}



/* ===== Imperial Starship (ISS) — internal state + helpers ============ */

void
iss_log_event_move (int from, int to, const char *kind, const char *extra)
{
  json_t *evt =
    json_pack ("{s:i, s:i, s:s}", "old_sector", from, "new_sector", to,
               "reason", kind);
  if (extra)
    {
      json_object_set_new (evt, "extra", json_string (extra));
    }
  db_log_engine_event ((long long) time (NULL), kIssNoticePrefix, NULL,
                       0 /*system actor */, to, evt, NULL);
}


/* --- tiny DB helpers --- */
static int
db_get_stardock_sector (void)
{
  sqlite3 *db = db_get_handle ();
  sqlite3_stmt *st = NULL;
  int sector = 0;
  if (sqlite3_prepare_v2 (db,
                          "SELECT sector_id FROM stardock_location LIMIT 1;",
                          -1, &st, NULL) == SQLITE_OK
      && sqlite3_step (st) == SQLITE_ROW)
    {
      sector = sqlite3_column_int (st, 0);
    }
  sqlite3_finalize (st);
  return sector;
}


static int
db_get_iss_player (int *out_player_id, int *out_sector)
{
  sqlite3 *db = db_get_handle ();
  sqlite3_stmt *st = NULL;
  int ok = 0;
  if (sqlite3_prepare_v2 (db,
                          "SELECT id, COALESCE(sector,0) FROM players "
                          "WHERE type=1 AND name=?1 LIMIT 1;", -1, &st,
                          NULL) == SQLITE_OK)
    {
      sqlite3_bind_text (st, 1, kIssName, -1, SQLITE_STATIC);
      if (sqlite3_step (st) == SQLITE_ROW)
        {
          *out_player_id = sqlite3_column_int (st, 0);
          *out_sector = sqlite3_column_int (st, 1);
          ok = 1;
        }
    }
  sqlite3_finalize (st);
  return ok;
}


static int
db_pick_adjacent (int sector)
{
  int next = sector;
  sqlite3 *db = db_get_handle ();
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2 (db,
                          "SELECT to_sector FROM sector_warps WHERE from_sector=?1 "
                          "ORDER BY RANDOM() LIMIT 1;",
                          -1,
                          &st,
                          NULL) == SQLITE_OK)
    {
      sqlite3_bind_int (st, 1, sector);
      if (sqlite3_step (st) == SQLITE_ROW)
        {
          next = sqlite3_column_int (st, 0);
        }
    }
  sqlite3_finalize (st);
  return next;
}


static void
post_iss_notice_move (int from, int to, const char *kind, const char *extra)
{
  (void) from;
  (void) kind;
  (void) extra;
  char news_body[256];


  // In the future, the ISS name could be dynamic. Using "Warrior" for now.
  snprintf (news_body, sizeof (news_body),
            "Federation Starship ISS Warrior has been sighted in sector %d!",
            to);
  // Post to the news system. Category "ISS", author 0 (system).
  news_post (news_body, "ISS", 0);
}


/* Move the ISS; warp==1 means “blink”. */
static void
iss_move_to (int next_sector, int warp, const char *extra)
{
  if (next_sector <= 0 || next_sector == g_iss_sector)
    {
      return;
    }
  sqlite3 *db = db_get_handle ();
  sqlite3_stmt *up = NULL;


  if (sqlite3_prepare_v2 (db,
                          "UPDATE players SET sector=?1, intransit=0, movingto=NULL, beginmove=NULL "
                          "WHERE id=?2;",
                          -1,
                          &up,
                          NULL) == SQLITE_OK)
    {
      sqlite3_bind_int (up, 1, next_sector);
      sqlite3_bind_int (up, 2, g_iss_id);
      sqlite3_step (up);
    }
  sqlite3_finalize (up);
  iss_log_event_move (g_iss_sector, next_sector, warp ? "warp" : "move",
                      extra);
  post_iss_notice_move (g_iss_sector, next_sector, warp ? "warp" : "move",
                        extra);
  g_iss_sector = next_sector;
  /* decay/refresh budget */
  if (!warp)
    {
      g_patrol_budget--;
      if (g_patrol_budget <= 0 || g_iss_sector == g_stardock_sector)
        {
          g_patrol_budget = kIssPatrolBudget;
        }
    }
  else
    {
      g_patrol_budget = kIssPatrolBudget / 2;   /* linger a bit, then drift home */
    }
}


/* One patrol step: slight bias to wander, but budget forces eventual return. */
static void
iss_patrol_step (void)
{
  if (g_iss_sector <= 0)
    {
      g_iss_sector = g_stardock_sector;
    }
  int next;


  /* ~30% chance move “toward” home by picking an adjacent at random anyway;
     keep it simple (pathing can be improved later). */
  if ((rand () % 10) < 3)
    {
      next = db_pick_adjacent (g_iss_sector);
    }
  else
    {
      next = db_pick_adjacent (g_iss_sector);
    }
  if (next != g_iss_sector)
    {
      iss_move_to (next, /*warp= */ 0, /*extra= */ NULL);
    }
}


/* Consume a pending summon (set by iss_summon()), return 1 if we warped. */
static int
iss_try_consume_summon (void)
{
  if (g_summon_sector > 0)
    {
      char extra[64];


      snprintf (extra, sizeof (extra), "summoned to sector %d (offender %d)",
                g_summon_sector, g_summon_offender);
      iss_move_to (g_summon_sector, /*warp= */ 1, extra);
      g_summon_sector = 0;
      g_summon_offender = 0;
      return 1;
    }
  return 0;
}


int
iss_init_once (void)
{
  if (g_iss_inited)
    {
      return 1;
    }
  g_stardock_sector = db_get_stardock_sector ();
  if (g_stardock_sector <= 0)
    {
      return 0;
    }
  int sector = 0;


  if (!db_get_iss_player (&g_iss_id, &sector))
    {
      return 0;
    }
  if (sector <= 0)
    {
      /* park at Stardock on first discovery */
      g_iss_sector = g_stardock_sector;
      iss_move_to (g_stardock_sector, /*warp= */ 1, "initialization");
    }
  else
    {
      g_iss_sector = sector;
    }
  g_patrol_budget = kIssPatrolBudget;
  srand ((unsigned) time (NULL));
  g_iss_inited = 1;
  return 1;
}


void
iss_summon (int sector_id, int offender_id)
{
  if (!g_iss_inited)
    {
      return;
    }
  g_summon_sector = sector_id;
  g_summon_offender = offender_id;
}


void
iss_tick (int64_t now_ms)
{
  (void) now_ms;                /* reserved for future timing/backoff logic */
  if (!g_iss_inited)
    {
      return;
    }
  if (iss_try_consume_summon ())
    {
      return;
    }
  iss_patrol_step ();
}


/* static int
   iss_should_broadcast_now (int force)
   {
   // read engine_state
   sqlite3 *db = db_get_handle ();
   sqlite3_stmt *q = NULL;
   int64_t now = time (NULL), last = 0;
   int broadcast_moves = 0;

   if (sqlite3_prepare_v2 (db,
                          "SELECT state_key, state_val FROM engine_state "
                          "WHERE state_key IN ('iss.broadcast_moves','iss.last_notice_ts');",
                          -1, &q, NULL) == SQLITE_OK)
    {
      while (sqlite3_step (q) == SQLITE_ROW)
        {
          const char *k = (const char *) sqlite3_column_text (q, 0);
          const char *v = (const char *) sqlite3_column_text (q, 1);
          if (!k || !v)
            continue;
          if (!strcmp (k, "iss.broadcast_moves"))
            broadcast_moves = atoi (v);
          if (!strcmp (k, "iss.last_notice_ts"))
            last = atoll (v);
        }
    }
   sqlite3_finalize (q);

   if (force)
    return 1;			// always for warps/summons

   if (!broadcast_moves)
    return 0;			// default off

   // rate-limit to once per 60s
   if (now - last < 60)
    return 0;

   // update last_notice_ts
   sqlite3_stmt *u = NULL;
   if (sqlite3_prepare_v2 (db,
                          "INSERT INTO engine_state(state_key,state_val) VALUES('iss.last_notice_ts',?1) "
                          "ON CONFLICT(state_key) DO UPDATE SET state_val=excluded.state_val;",
                          -1, &u, NULL) == SQLITE_OK)
    {
      char buf[32];
      snprintf (buf, sizeof (buf), "%lld", (long long) now);
      sqlite3_bind_text (u, 1, buf, -1, SQLITE_STATIC);
      sqlite3_step (u);
    }
   sqlite3_finalize (u);

   return 1;
   }
*/


int
sector_has_port (int sector)
{
  if (!g_fer_db || sector <= 0)
    {
      return 0;
    }
  const char *sql = "SELECT 1 FROM ports WHERE sector=?1 LIMIT 1;";
  sqlite3_stmt *st = NULL;
  int ok = 0;


  if (sqlite3_prepare_v2 (g_fer_db, sql, -1, &st, NULL) != SQLITE_OK)
    {
      return 0;
    }
  sqlite3_bind_int (st, 1, sector);
  if (sqlite3_step (st) == SQLITE_ROW)
    {
      ok = 1;
    }
  sqlite3_finalize (st);
  return ok;
}


/* ---------- Graph helpers over sector_warps ---------- */
int
nav_random_neighbor (int sector)
{
  if (!g_fer_db || sector <= 0)
    {
      return 0;
    }
  const char *sql =
    "SELECT to_sector FROM sector_warps WHERE from_sector=?1 "
    "ORDER BY RANDOM() LIMIT 1;";
  sqlite3_stmt *st = NULL;
  int next = 0;


  if (sqlite3_prepare_v2 (g_fer_db, sql, -1, &st, NULL) != SQLITE_OK)
    {
      return 0;
    }
  sqlite3_bind_int (st, 1, sector);
  if (sqlite3_step (st) == SQLITE_ROW)
    {
      next = sqlite3_column_int (st, 0);
    }
  sqlite3_finalize (st);
  return next;
}


/* returns the first hop from start toward goal (BFS in a bounded ring) */
int
nav_next_hop (int start, int goal)
{
  if (!g_fer_db || start <= 0 || goal <= 0 || start == goal)
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


  auto int
  seen_get (int key)
  {
    for (int i = 0; i < seen_n; ++i)
      {
        if (seen[i].key == key)
          {
            return i;
          }
      }
    return -1;
  }


  auto int
  seen_put (int key, int prev)
  {
    if (seen_n >= MAX_SEEN)
      {
        return -1;
      }
    seen[seen_n].key = key;
    seen[seen_n].prev = prev;
    return seen_n++;
  }


  q[tail = head = 0] = start;
  tail = 1;
  seen_put (start, -1);
  const char *sql =
    "SELECT to_sector FROM sector_warps WHERE from_sector=?1;";
  sqlite3_stmt *st = NULL;


  if (sqlite3_prepare_v2 (g_fer_db, sql, -1, &st, NULL) != SQLITE_OK)
    {
      return 0;
    }
  int found = -1;


  while (head != tail && found == -1)
    {
      int cur = q[head++ % MAX_Q];


      sqlite3_reset (st);
      sqlite3_clear_bindings (st);
      sqlite3_bind_int (st, 1, cur);
      while (sqlite3_step (st) == SQLITE_ROW)
        {
          int nb = sqlite3_column_int (st, 0);


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
    }
  sqlite3_finalize (st);
  if (found == -1)
    {
      return 0;
    }
  /* reconstruct one hop toward goal */
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
          break;                /* step == start */
        }
      if (prev == start)
        {
          return step;          /* first hop away from start */
        }
      step = prev;
    }
  return step;                  /* neighbour fallback */
}


/* ---------- (optional) internal event emitters ---------- */
/* If you have a helper already for engine_events, call it here.
   Otherwise keep these INFO_LOGs for visibility and add event writes later. */

void
fer_attach_db (sqlite3 *db)
{
  // fprintf(stderr, "[engine] fer_attach_db enter\n");
  g_fer_db = db;
  // fprintf(stderr, "[engine] fer_attach_db exit\n");
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

  sqlite3_stmt *st = NULL;

  // 1. Find Ferengi Corp ID and its CEO player ID
  const char *sql_find_corp_info = "SELECT id, owner_id FROM corporations WHERE tag='FENG' LIMIT 1;";
  if (sqlite3_prepare_v2(g_fer_db, sql_find_corp_info, -1, &st, NULL) == SQLITE_OK) {
      if (sqlite3_step(st) == SQLITE_ROW) {
          g_fer_corp_id = sqlite3_column_int(st, 0);
          g_fer_player_id = sqlite3_column_int(st, 1); // This player owns the corp
          if (g_fer_player_id == 0) {
              g_fer_player_id = 1; // Fallback to System player if no owner assigned
          }
      }
      sqlite3_finalize(st);
  }

  if (g_fer_corp_id == 0) {
      LOGW("[fer] Ferrengi Alliance corporation not found. Traders disabled.");
      return 0;
  }
  
  // 2. Find Home Sector (Ferengi Homeworld Planet)
  int home = 0;
  const char *sql_find_home_sector = "SELECT sector FROM planets WHERE id=2 LIMIT 1;"; // Assumed Ferengi Homeworld is Planet ID 2
  if (sqlite3_prepare_v2(g_fer_db, sql_find_home_sector, -1, &st, NULL) == SQLITE_OK) {
      if (sqlite3_step(st) == SQLITE_ROW) {
          home = sqlite3_column_int(st, 0);
      }
      sqlite3_finalize(st);
  }

  if (home <= 0) {
      LOGW("[fer] no 'Ferringhi' homeworld planet found; disabling traders");
      return 0;
  }

  // 3. Get ship type ID for "Ferrengi Warship" (or a suitable fallback)
  int ship_type_id = 0;
  const char *sql_find_shiptype = "SELECT id FROM shiptypes WHERE name='Ferrengi Warship' LIMIT 1;";
  if (sqlite3_prepare_v2(g_fer_db, sql_find_shiptype, -1, &st, NULL) == SQLITE_OK) {
      if (sqlite3_step(st) == SQLITE_ROW) {
          ship_type_id = sqlite3_column_int(st, 0);
      }
      sqlite3_finalize(st);
  }
  if (ship_type_id == 0) { // Fallback if Ferengi Warship is missing
      sql_find_shiptype = "SELECT id FROM shiptypes WHERE name='Scout Marauder' LIMIT 1;";
      if (sqlite3_prepare_v2(g_fer_db, sql_find_shiptype, -1, &st, NULL) == SQLITE_OK) {
          if (sqlite3_step(st) == SQLITE_ROW) {
              ship_type_id = sqlite3_column_int(st, 0);
          }
          sqlite3_finalize(st);
      }
  }
  if (ship_type_id == 0) {
      LOGE("[fer] No suitable shiptype found for Ferengi traders. Disabling.");
      return 0;
  }

  // 4. Initialize Traders: Create/Find persistent ships for each Ferengi trader
  for (int i = 0; i < FER_TRADER_COUNT; ++i)
    {
      g_fer[i].id = i;
      g_fer[i].home_sector = home;
      g_fer[i].trades_done = 0;
      g_fer[i].state = FER_STATE_ROAM;
      
      char ship_name[64];
      snprintf(ship_name, sizeof(ship_name), "Ferengi Trader %d", i + 1);
      
      int ship_id = 0;
      int current_sector = home; // Default to home, will update from DB if ship found

      // Check if ship already exists for this NPC player/name
      const char *sql_find_ship = 
        "SELECT s.id, s.sector FROM ships s "
        "JOIN ship_ownership so ON s.id = so.ship_id "
        "WHERE so.player_id = ? AND s.name = ?;";
      if (sqlite3_prepare_v2(g_fer_db, sql_find_ship, -1, &st, NULL) == SQLITE_OK) {
          sqlite3_bind_int(st, 1, g_fer_player_id);
          sqlite3_bind_text(st, 2, ship_name, -1, SQLITE_STATIC);
          if (sqlite3_step(st) == SQLITE_ROW) {
              ship_id = sqlite3_column_int(st, 0);
              current_sector = sqlite3_column_int(st, 1); // Sync sector from DB
          }
          sqlite3_finalize(st);
      }

      if (ship_id == 0) {
          // Ship doesn't exist, create it
          const char *sql_create_ship = 
            "INSERT INTO ships (name, type_id, sector, ported, onplanet, holds, ore, organics, equipment) "
            "VALUES (?, ?, ?, 0, 0, ?, ?, ?, ?);"; // Set initial cargo, holds
          if (sqlite3_prepare_v2(g_fer_db, sql_create_ship, -1, &st, NULL) == SQLITE_OK) {
              sqlite3_bind_text(st, 1, ship_name, -1, SQLITE_STATIC);
              sqlite3_bind_int(st, 2, ship_type_id);
              sqlite3_bind_int(st, 3, home); // Start at home sector
              sqlite3_bind_int(st, 4, FER_MAX_HOLD); // Max holds
              sqlite3_bind_int(st, 5, FER_MAX_HOLD / 2); // Initial cargo
              sqlite3_bind_int(st, 6, FER_MAX_HOLD / 2); 
              sqlite3_bind_int(st, 7, FER_MAX_HOLD / 2); 
              if (sqlite3_step(st) != SQLITE_DONE) {
                  LOGE("[fer] Failed to create ship %s: %s", ship_name, sqlite3_errmsg(g_fer_db));
                  sqlite3_finalize(st);
                  return 0; // Fatal error for this trader
              }
              ship_id = sqlite3_last_insert_rowid(g_fer_db);
              sqlite3_finalize(st);
              
              // Assign ownership
              const char *sql_create_ownership = "INSERT INTO ship_ownership (ship_id, player_id, role_id, is_primary) VALUES (?, ?, 1, 0);"; // role_id 1 = owner
              if (sqlite3_prepare_v2(g_fer_db, sql_create_ownership, -1, &st, NULL) == SQLITE_OK) {
                  sqlite3_bind_int(st, 1, ship_id);
                  sqlite3_bind_int(st, 2, g_fer_player_id);
                  if (sqlite3_step(st) != SQLITE_DONE) {
                      LOGE("[fer] Failed to assign ownership for ship %s: %s", ship_name, sqlite3_errmsg(g_fer_db));
                      sqlite3_finalize(st);
                      return 0; // Fatal
                  }
                  sqlite3_finalize(st);
              } else {
                  LOGE("[fer] Failed to prepare ownership statement for ship %s: %s", ship_name, sqlite3_errmsg(g_fer_db));
                  return 0; // Fatal
              }
          } else {
              LOGE("[fer] Failed to prepare create ship statement for %s: %s", ship_name, sqlite3_errmsg(g_fer_db));
              return 0; // Fatal
          }
      }
      g_fer[i].ship_id = ship_id;
      g_fer[i].sector = current_sector; // Ensure struct reflects DB
    }

  fer_event_json ("npc.online", 0,
                  "{ \"kind\":\"ferringhi\", \"count\": %d }",
                  FER_TRADER_COUNT);
  g_fer_inited = 1;
  return g_fer_inited;
}


void
fer_tick (int64_t now_ms)
{
  (void) now_ms;                /* reserved for rate logic later */
  if (!g_fer_inited)
    {
      if (!fer_init_once ())
        {
          return;
        }
    }

  sqlite3 *db = g_fer_db; // Use the attached DB handle

  for (int i = 0; i < FER_TRADER_COUNT; ++i)
    {
      fer_trader_t *t = &g_fer[i];

      if (t->home_sector <= 0 || t->ship_id <= 0)
        {
          continue; // Skip if no home sector or ship assigned
        }
      
      // Update the trader's current sector from DB (authoritative)
      int current_db_sector = db_get_ship_sector_id(db, t->ship_id);
      if (current_db_sector > 0) {
          t->sector = current_db_sector;
      } else {
          LOGW("Ferengi Trader %d (ship %d) has invalid sector in DB. Skipping.", t->id, t->ship_id);
          continue;
      }

      /* choose goal: roam to random port, or return home */
      int goal = (t->state == FER_STATE_RETURNING) ? t->home_sector : 0;

      if (goal == 0)
        {
          // Select random port from DB
          const char *sql_random_port_sector =
            "SELECT sector FROM ports ORDER BY RANDOM() LIMIT 1;";
          sqlite3_stmt *st = NULL;
          if (sqlite3_prepare_v2 (db, sql_random_port_sector, -1, &st, NULL) == SQLITE_OK)
            {
              if (sqlite3_step (st) == SQLITE_ROW)
                {
                  goal = sqlite3_column_int (st, 0);
                }
              sqlite3_finalize (st);
            }
        }
      if (goal <= 0)
        {
          continue; // No valid goal found
        }
      
      /* one hop; drift if no path */
      int next_sector = nav_next_hop (t->sector, goal);

      if (next_sector <= 0 || next_sector == t->sector)
        {
          // If no path or stuck, try a random neighbor
          next_sector = nav_random_neighbor (t->sector);
        }
      
      if (next_sector <= 0 || next_sector == t->sector)
	{
          continue; // Still no valid move
	}	

      // Update ship's sector in DB
      db_player_set_sector(g_fer_player_id, next_sector); // Use Ferengi player ID for player-ship link
      // Also update the ship's sector directly in the ships table (db_player_set_sector only sets player sector)
      sqlite3_stmt *update_ship_st = NULL;
      if (sqlite3_prepare_v2(db, "UPDATE ships SET sector = ? WHERE id = ?;", -1, &update_ship_st, NULL) == SQLITE_OK)
	{
          sqlite3_bind_int(update_ship_st, 1, next_sector);
          sqlite3_bind_int(update_ship_st, 2, t->ship_id);
          sqlite3_step(update_ship_st);
          sqlite3_finalize(update_ship_st);
	}
      t->sector = next_sector; // Update in-memory cache

      /* trade only when actually on a port sector */
      if (sector_has_port (t->sector))
        {
          int port_id = db_get_port_id_by_sector (t->sector);
          if (port_id > 0)
	    {
              ferengi_trade_at_port(db, t, port_id);
              t->trades_done++;
              if (t->trades_done >= FER_TRADES_BEFORE_RETURN)
		{
                  t->state = FER_STATE_RETURNING;
		}
	    }
        }
      
      /* reached home while returning → reset state */
      if (t->state == FER_STATE_RETURNING && t->sector == t->home_sector)
        {
          t->trades_done = 0;
          t->state = FER_STATE_ROAM;
	}
    }
}


int ferengi_trade_at_port(sqlite3 *db, fer_trader_t *trader, int port_id) {
    int rc = SQLITE_OK;
    char tx_group_id[UUID_STR_LEN];
    h_generate_hex_uuid(tx_group_id, sizeof(tx_group_id));

    // Get current Ferengi ship state
    int fer_ore = 0, fer_organics = 0, fer_equipment = 0;
    int fer_holds = 0; // Current total holds, not empty
    h_get_ship_cargo_and_holds(db, trader->ship_id, &fer_ore, &fer_organics, &fer_equipment, &fer_holds, NULL, NULL, NULL, NULL);
    int fer_current_cargo_total = fer_ore + fer_organics + fer_equipment;
    int fer_empty_holds = fer_holds - fer_current_cargo_total;

    long long fer_credits = 0;
    db_get_corp_bank_balance(g_fer_corp_id, &fer_credits); // Get Ferengi Corp balance

    // Get Port's stock and prices
    int port_ore_qty = 0, port_org_qty = 0, port_equ_qty = 0;
    int dummy_cap = 0; bool dummy_bool = false; // For h_get_port_commodity_details
    
    h_get_port_commodity_details(db, port_id, "ORE", &port_ore_qty, &dummy_cap, &dummy_bool, &dummy_bool);
    h_get_port_commodity_details(db, port_id, "ORG", &port_org_qty, &dummy_cap, &dummy_bool, &dummy_bool);
    h_get_port_commodity_details(db, port_id, "EQU", &port_equ_qty, &dummy_cap, &dummy_bool, &dummy_bool);
    
    int port_ore_buy_price = h_calculate_port_buy_price(db, port_id, "ORE");
    int port_ore_sell_price = h_calculate_port_sell_price(db, port_id, "ORE");
    int port_org_buy_price = h_calculate_port_buy_price(db, port_id, "ORG");
    int port_org_sell_price = h_calculate_port_sell_price(db, port_id, "ORG");
    int port_equ_buy_price = h_calculate_port_buy_price(db, port_id, "EQU");
    int port_equ_sell_price = h_calculate_port_sell_price(db, port_id, "EQU");

    const char *commodities[] = {"ORE", "ORG", "EQU"};
    int fer_hold_values[] = {fer_ore, fer_organics, fer_equipment}; // Use values directly
    int port_buy_prices[] = {port_ore_buy_price, port_org_buy_price, port_equ_buy_price};
    int port_sell_prices[] = {port_ore_sell_price, port_org_sell_price, port_equ_sell_price};
    int port_quantities[] = {port_ore_qty, port_org_qty, port_equ_qty};

    int best_sell_idx = -1;
    int best_buy_idx = -1;

    // Determine best commodity to SELL
    for (int c_idx = 0; c_idx < 3; ++c_idx) {
        if (fer_hold_values[c_idx] > 0 && port_buy_prices[c_idx] > 0) {
            if (best_sell_idx == -1 || port_buy_prices[c_idx] > port_buy_prices[best_sell_idx]) { // Prioritize highest buy price
                best_sell_idx = c_idx;
            }
        }
    }

    // Determine best commodity to BUY
    for (int c_idx = 0; c_idx < 3; ++c_idx) {
        if (fer_empty_holds > 0 && port_quantities[c_idx] > 0 && port_sell_prices[c_idx] > 0) {
            if (best_buy_idx == -1 || port_sell_prices[c_idx] < port_sell_prices[best_buy_idx]) { // Prioritize lowest sell price
                best_buy_idx = c_idx;
            }
        }
    }

    // Start transaction
    sqlite3_exec(db, "BEGIN IMMEDIATE;", NULL, NULL, NULL);

    if (best_sell_idx != -1) {
        const char *commodity = commodities[best_sell_idx];
        int price_per_unit = port_buy_prices[best_sell_idx];
        
        // Quantity to sell: limited by Ferengi stock, port capacity to buy, and port's available credits
        int max_sell_to_port = port_quantities[best_sell_idx] / 2; // Port won't buy more than half its stock
        if (max_sell_to_port == 0) max_sell_to_port = 1;

        int qty_to_trade = MIN(fer_hold_values[best_sell_idx], max_sell_to_port);
        qty_to_trade = MIN(qty_to_trade, (int)(db_get_port_bank_balance(port_id, NULL) / price_per_unit)); // Port can afford it

        if (qty_to_trade > 0 && price_per_unit > 0) {
            long long total_credits = (long long)qty_to_trade * price_per_unit;
            rc = h_bank_transfer_unlocked(db, "port", port_id, "corp", g_fer_corp_id, total_credits, "TRADE_SELL", tx_group_id);
            if (rc == SQLITE_OK) {
                rc = h_update_ship_cargo(db, trader->ship_id, commodity, -qty_to_trade);
                if (rc == SQLITE_OK) {
                    rc = h_market_move_port_stock(db, port_id, commodity, qty_to_trade);
                }
            }
            if (rc == SQLITE_OK) {
                fer_event_json("npc.trade", trader->sector,
                               "{ \"kind\":\"ferrengi_sell\", \"ship_id\":%d, \"port_id\":%d, \"commodity\":\"%s\", \"qty\":%d, \"price\":%d, \"total_credits\":%" PRId64 " }",
                               trader->ship_id, port_id, commodity, qty_to_trade, price_per_unit, total_credits);
                LOGI("Ferengi %d (ship %d) SOLD %d %s to Port %d for %lld credits.", trader->id, trader->ship_id, qty_to_trade, commodity, port_id, total_credits);
            } else {
                LOGW("Ferengi %d failed to sell %d %s to Port %d (trade error %d).", trader->id, qty_to_trade, commodity, port_id, rc);
            }
        }
    } 
    
    // If no selling, or selling was done, try buying
    if (rc == SQLITE_OK && best_buy_idx != -1 && fer_empty_holds > 0) {
        const char *commodity = commodities[best_buy_idx];
        int price_per_unit = port_sell_prices[best_buy_idx];
        
        // Quantity to buy: limited by empty holds, port stock, and Ferengi credits
        int qty_to_trade = MIN(fer_empty_holds, port_quantities[best_buy_idx]);
        if (qty_to_trade <= 0) qty_to_trade = 1; // Ensure minimal trade attempt

        long long total_credits = (long long)qty_to_trade * price_per_unit;

        if (qty_to_trade > 0 && total_credits > 0 && fer_credits >= total_credits) {
            // Corp buys from Port
            rc = h_bank_transfer_unlocked(db, "corp", g_fer_corp_id, "port", port_id, total_credits, "TRADE_BUY", tx_group_id);
            if (rc == SQLITE_OK) {
                rc = h_update_ship_cargo(db, trader->ship_id, commodity, qty_to_trade);
                if (rc == SQLITE_OK) {
                    rc = h_market_move_port_stock(db, port_id, commodity, -qty_to_trade);
                }
            }
            if (rc == SQLITE_OK) {
                fer_event_json("npc.trade", trader->sector,
                               "{ \"kind\":\"ferrengi_buy\", \"ship_id\":%d, \"port_id\":%d, \"commodity\":\"%s\", \"qty\":%d, \"price\":%d, \"total_credits\":%" PRId64 " }",
                               trader->ship_id, port_id, commodity, qty_to_trade, price_per_unit, total_credits);
                LOGI("Ferengi %d (ship %d) BOUGHT %d %s from Port %d for %lld credits.", trader->id, trader->ship_id, qty_to_trade, commodity, port_id, total_credits);
            } else {
                LOGW("Ferengi %d failed to buy %d %s from Port %d (bank transfer error %d).", trader->id, qty_to_trade, commodity, port_id, rc);
            }
        }
    }

    if (rc == SQLITE_OK) {
        sqlite3_exec(db, "COMMIT;", NULL, NULL, NULL); // End transaction
    } else {
        sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL); // Rollback on error
        LOGE("Ferengi trade transaction rolled back due to error %d.", rc);
    }
    return rc;
}


 int
   cmd_move_transwarp (client_ctx_t *ctx, json_t *root)
 {
   sqlite3 *db_handle = db_get_handle ();

   if (!db_handle)
     {
       send_enveloped_error (ctx->fd, root, ERR_DB, "No database handle");
       return 0;
     }

   if (!ctx || ctx->player_id <= 0)
     {
       send_enveloped_refused (ctx->fd,
			       root,
			       ERR_NOT_AUTHENTICATED,
			       "Not authenticated",
			       NULL);
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
       send_enveloped_refused (ctx->fd,
			       root,
			       ERR_INVALID_ARG,
			       "Target sector not specified",
			       NULL);
       return 0;
     }

   // 1. Capability Check (TransWarp Drive)
   int ship_id = h_get_active_ship_id (db_handle, ctx->player_id);


   if (ship_id <= 0)
     {
       send_enveloped_refused (ctx->fd,
			       root,
			       ERR_NO_ACTIVE_SHIP,
			       "No active ship found.",
			       NULL);
       return 0;
     }

   // Check if player is towing a ship
   int towing_ship_id = 0;
   sqlite3_stmt *stmt_towing_status = NULL;
   const char *sql_towing_status =
     "SELECT towing_ship_id FROM ships WHERE id = ?;";
   int rc_tow_check = sqlite3_prepare_v2 (db_handle,
					  sql_towing_status,
					  -1,
					  &stmt_towing_status,
					  NULL);


   if (rc_tow_check == SQLITE_OK)
     {
       sqlite3_bind_int (stmt_towing_status, 1, ship_id);
       if (sqlite3_step (stmt_towing_status) == SQLITE_ROW)
	 {
	   towing_ship_id = sqlite3_column_int (stmt_towing_status, 0);
	 }
       sqlite3_finalize (stmt_towing_status);
     }
   else
     {
       LOGE ("cmd_move_transwarp: Failed to prepare towing status check: %s",
	     sqlite3_errmsg (db_handle));
       send_enveloped_error (ctx->fd, root, ERR_DB_QUERY_FAILED,
			     "Database error");
       return 0;
     }

   if (towing_ship_id != 0)
     {
       send_enveloped_refused (ctx->fd,
			       root,
			       REF_CANNOT_TRANSWARP_WHILE_TOWING,
			       "Cannot TransWarp while towing another ship.",
			       NULL);
       return 0;
     }

   int has_transwarp = 0;
   sqlite3_stmt *stmt = NULL;
   const char *sql_check_transwarp =
     "SELECT has_transwarp FROM ships WHERE id = ?;";
   int rc = sqlite3_prepare_v2 (db_handle,
				sql_check_transwarp,
				-1,
				&stmt,
				NULL);


   if (rc == SQLITE_OK)
     {
       sqlite3_bind_int (stmt, 1, ship_id);
       if (sqlite3_step (stmt) == SQLITE_ROW)
	 {
	   has_transwarp = sqlite3_column_int (stmt, 0);
	 }
       sqlite3_finalize (stmt);
     }
   else
     {
       LOGE (
	     "cmd_move_transwarp: Failed to prepare transwarp check statement: %s",
	     sqlite3_errmsg (db_handle));
       send_enveloped_error (ctx->fd, root, ERR_DB_QUERY_FAILED,
			     "Database error");
       return 0;
     }

   if (has_transwarp == 0)
     {
       send_enveloped_refused (ctx->fd,
			       root,
			       REF_TRANSWARP_UNAVAILABLE,
			       "You do not have a TransWarp drive.",
			       NULL);
       return 0;
     }

   // 2. Destination Validation
   int max_sector_id = 0;
   const char *sql_max_sector = "SELECT MAX(id) FROM sectors;";


   rc = sqlite3_prepare_v2 (db_handle,
			    sql_max_sector,
			    -1,
			    &stmt,
			    NULL);
   if (rc == SQLITE_OK)
     {
       if (sqlite3_step (stmt) == SQLITE_ROW)
	 {
	   max_sector_id = sqlite3_column_int (stmt, 0);
	 }
       sqlite3_finalize (stmt);
     }
   else
     {
       LOGE ("cmd_move_transwarp: Failed to prepare max sector check: %s",
	     sqlite3_errmsg (db_handle));
       send_enveloped_error (ctx->fd, root, ERR_DB_QUERY_FAILED,
			     "Database error");
       return 0;
     }

   if (to_sector_id <= 0 || to_sector_id > max_sector_id)
     {
       send_enveloped_refused (ctx->fd,
			       root,
			       ERR_INVALID_ARG,
			       "Invalid TransWarp coordinates: Sector does not exist.",
			       NULL);
       return 0;
     }

   if (to_sector_id == ctx->sector_id)
     {
       send_enveloped_ok (ctx->fd, root, "move.transwarp.result",
			  json_pack ("{s:s}",
				     "message",
				     "You transwarp to your current sector. Nothing happens."));
       return 0;
     }

   // 3. Turn Cost & Consumption
   const int TRANSWARP_TURN_COST = 3;
   TurnConsumeResult tc = h_consume_player_turn (db_handle,
						 ctx,
						 TRANSWARP_TURN_COST);


   if (tc != TURN_CONSUME_SUCCESS)
     {
       return handle_turn_consumption_error (ctx,
					     tc,
					     "move.transwarp",
					     root,
					     NULL);
     }

   // Execute TransWarp
   int from_sector_id = ctx->sector_id;
   int prc = db_player_set_sector (ctx->player_id, to_sector_id);


   if (prc != SQLITE_OK)
     {
       LOGE
	 (
	  "cmd_move_warp: db_player_set_sector failed for player %d, ship_id %d, to sector %d. Error code: %d",
	  ctx->player_id,
	  h_get_active_ship_id (db_handle, ctx->player_id),
	  to_sector_id,
	  prc);
       send_enveloped_error (ctx->fd, root, 1502,
			     "Failed to persist player sector");
       return 0;
     }

   // If player is towing a ship, update its sector as well
   int player_ship_id_for_towing = h_get_active_ship_id (db_handle,
							 ctx->player_id);
   int towed_ship_id = 0;
   sqlite3_stmt *stmt_towed_ship = NULL;
   const char *sql_get_towed_ship =
     "SELECT towing_ship_id FROM ships WHERE id = ?;";
   int rc_get_towed = sqlite3_prepare_v2 (db_handle,
					  sql_get_towed_ship,
					  -1,
					  &stmt_towed_ship,
					  NULL);


   if (rc_get_towed == SQLITE_OK)
     {
       sqlite3_bind_int (stmt_towed_ship, 1, player_ship_id_for_towing);
       if (sqlite3_step (stmt_towed_ship) == SQLITE_ROW)
	 {
	   towed_ship_id = sqlite3_column_int (stmt_towed_ship, 0);
	 }
       sqlite3_finalize (stmt_towed_ship);
     }
   else
     {
       LOGE ("cmd_move_warp: Failed to prepare towed ship check: %s",
	     sqlite3_errmsg (db_handle));
       // Log error but don't stop movement
     }

   if (towed_ship_id > 0)
     {
       const char *sql_update_towed_ship =
	 "UPDATE ships SET sector = ? WHERE id = ?;";
       sqlite3_stmt *stmt_update_towed = NULL;
       int rc_update_towed = sqlite3_prepare_v2 (db_handle,
						 sql_update_towed_ship,
						 -1,
						 &stmt_update_towed,
						 NULL);


       if (rc_update_towed == SQLITE_OK)
	 {
	   sqlite3_bind_int (stmt_update_towed, 1, to_sector_id);
	   sqlite3_bind_int (stmt_update_towed, 2, towed_ship_id);
	   if (sqlite3_step (stmt_update_towed) != SQLITE_DONE)
	     {
	       LOGE (
		     "cmd_move_warp: Failed to update sector for towed ship %d: %s",
		     towed_ship_id,
		     sqlite3_errmsg (db_handle));
	     }
	   sqlite3_finalize (stmt_update_towed);
	 }
       else
	 {
	   LOGE (
		 "cmd_move_warp: Failed to prepare update towed ship statement: %s",
		 sqlite3_errmsg (db_handle));
	 }
       LOGI ("cmd_move_warp: Towed ship %d moved to sector %d.",
	     towed_ship_id,
	     to_sector_id);
     }
   LOGI (
	 "cmd_move_warp: Player %d (fd %d) successfully warped from sector %d to %d."
	 " db_player_set_sector returned %d. Rows updated: %d",
	 ctx->player_id,
	 ctx->player_id,
	 from_sector_id,
	 to_sector_id,
	 to_sector_id,
	 1);

   // Blind Entry - Apply sector effects immediately
   armid_encounter_t armid_enc = { 0 };


   apply_armid_mines_on_entry (ctx, to_sector_id, &armid_enc);
   // Future: Trigger combat checks, quasar cannons, etc.

   // 1) Send the direct reply for the actor
   json_t *resp = json_object ();


   json_object_set_new (resp, "player_id", json_integer (ctx->player_id));
   json_object_set_new (resp, "from_sector_id", json_integer (from_sector_id));
   json_object_set_new (resp, "to_sector_id", json_integer (to_sector_id));
   json_object_set_new (resp, "turns_spent", json_integer (TRANSWARP_TURN_COST));
   if (armid_enc.armid_triggered > 0)
     {
       json_t *damage_obj = json_object ();


       json_object_set_new (damage_obj, "shields_lost",
			    json_integer (armid_enc.shields_lost));
       json_object_set_new (damage_obj, "fighters_lost",
			    json_integer (armid_enc.fighters_lost));
       json_object_set_new (damage_obj, "hull_lost",
			    json_integer (armid_enc.hull_lost));
       json_object_set_new (damage_obj, "destroyed",
			    json_boolean (armid_enc.destroyed));
       json_t *encounter_obj = json_object ();


       json_object_set_new (encounter_obj, "kind", json_string ("mines"));
       json_object_set_new (encounter_obj, "armid_triggered",
			    json_integer (armid_enc.armid_triggered));
       json_object_set_new (encounter_obj, "armid_remaining",
			    json_integer (armid_enc.armid_remaining));
       json_object_set_new (encounter_obj, "damage", damage_obj);
       json_object_set_new (resp, "encounter", encounter_obj);
     }
   send_enveloped_ok (ctx->fd, root, "move.transwarp.result", resp);

   // 2) Broadcast LEFT (from) then ENTERED (to) to subscribers
   json_t *left = json_object ();


   json_object_set_new (left, "player_id", json_integer (ctx->player_id));
   json_object_set_new (left, "sector_id", json_integer (from_sector_id));
   json_object_set_new (left, "to_sector_id", json_integer (to_sector_id));
   json_object_set_new (left, "player", make_player_object (ctx->player_id));
   comm_publish_sector_event (from_sector_id, "sector.player_left", left);

   json_t *entered = json_object ();


   json_object_set_new (entered, "player_id", json_integer (ctx->player_id));
   json_object_set_new (entered, "sector_id", json_integer (to_sector_id));
   json_object_set_new (entered, "from_sector_id",
			json_integer (from_sector_id));
   json_object_set_new (entered, "player", make_player_object (ctx->player_id));
   comm_publish_sector_event (to_sector_id, "sector.player_entered", entered);

   return 0;
 }
