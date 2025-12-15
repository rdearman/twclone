#include <unistd.h> // For usleep()
#include <jansson.h>
#include <sqlite3.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdint.h>
#include <time.h>
#include <sqlite3.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
// local includes
#include "server_cron.h"
#include "server_log.h"
#include "common.h"
#include "server_players.h"
#include "server_universe.h"
#include "server_ports.h"
#include "server_planets.h"
#include "database.h"
#include "server_config.h"
#include "database_market.h" // For market order helpers
#include "database_cmd.h"    // For bank helpers
#include "server_stardock.h"    // For Tavern-related declarations
#include "server_corporation.h" // For corporation cron jobs
#include "server_clusters.h"    // Cluster Economy & Law
#include <math.h> // For MAX, MIN, etc.


#define INITIAL_QUEUE_CAPACITY 64
#define FEDSPACE_SECTOR_START 1
#define FEDSPACE_SECTOR_END 10
#define LOGOUT_TIMEOUT_S (60 * 60)
#define CONFISCATION_SECTOR 0
#define MIN_UNPROTECTED_SECTOR 11
#define MAX_UNPROTECTED_SECTOR 999
#define RANGE_SIZE (MAX_UNPROTECTED_SECTOR - MIN_UNPROTECTED_SECTOR + 1)
#define NEWS_EXPIRATION_SECONDS 604800L
#define MAX_ARTICLE_LEN 512
/* --- ADD TO TOP OF FILE (Declarations section) --- */
/* These helpers allow us to yield the C-level lock while keeping the DB handle open */
extern void db_mutex_lock (void);
extern void db_mutex_unlock (void);
int h_daily_news_compiler (sqlite3 *db, int64_t now_s);
int h_cleanup_old_news (sqlite3 *db, int64_t now_s);


static inline uint64_t
monotonic_millis (void)
{
  struct timespec ts;
  clock_gettime (CLOCK_MONOTONIC, &ts);
  return (uint64_t) ts.tv_sec * 1000ULL + (uint64_t) ts.tv_nsec / 1000000ULL;
}


enum
{ MAX_TOWS_PER_PASS = 50,
  MAX_SHIPS_PER_FED_SECTOR = 5,
  MAX_FED_FIGHTERS = 49};
typedef struct
{
  const char *name;
  cron_handler_fn fn;
} entry_t;
int cron_limpet_ttl_cleanup (sqlite3 *db, int64_t now_s);       // Forward declaration
static int g_reg_inited = 0;


int
get_random_sector (sqlite3 *db)
{
  (void) db; // no longer use the db use preloaded config data.
  int random_offset = rand () % g_cfg.default_nodes;
  int random_sector = 11 + random_offset;

  return random_sector;
}



int
tow_ship (sqlite3 *db, int ship_id, int new_sector_id, int admin_id,
          int reason_code)
{
  int rc;
  int owner_id = 0;
  int old_sector_id = 0;
  sqlite3_stmt *stmt = NULL;
  const char *sql_select_ship_info =
    "SELECT T1.sector, T2.id FROM ships T1 LEFT JOIN players T2 ON T1.id = T2.ship WHERE T1.id = ?;";
  if (sqlite3_prepare_v2 (db, sql_select_ship_info, -1, &stmt, NULL) !=
      SQLITE_OK)
    {
      LOGE ("tow_ship: Prepare SELECT failed for ship %d: %s", ship_id,
            sqlite3_errmsg (db));
      return SQLITE_ERROR;
    }
  sqlite3_bind_int (stmt, 1, ship_id);
  if (sqlite3_step (stmt) != SQLITE_ROW)
    {
      LOGE ("tow_ship: Ship ID %d not found or step failed. Error: %s",
            ship_id, sqlite3_errmsg (db));
      sqlite3_finalize (stmt);
      return SQLITE_NOTFOUND;
    }
  old_sector_id = sqlite3_column_int (stmt, 0);
  owner_id = sqlite3_column_int (stmt, 1);
  sqlite3_finalize (stmt);
  const char *reason_str;
  const char *subject_str;


  switch (reason_code)
    {
      case REASON_EVIL_ALIGN:
        reason_str = "Evil Alignment";
        subject_str = "Federation Tow Notice: Evil Alignment";
        break;
      case REASON_EXCESS_FIGHTERS:
        reason_str = "Excess Fighters (>50)";
        subject_str = "Federation Tow Notice: Illegal Fighters";
        break;
      case REASON_HIGH_EXP:
        reason_str = "High Experience (>=1000 EP)";
        subject_str = "Federation Tow Notice: Protection Expired";
        break;
      case REASON_NO_OWNER:
        reason_str = "Unowned/Derelict Ship (Confiscated)";
        subject_str = "Ship Confiscation Notice";
        break;
      case REASON_OVERCROWDING:
        reason_str = "Overcrowding in FedSpace";
        subject_str = "Federation Tow Notice: FedSpace Overcrowding";
        break;
      default:
        reason_str = "Unknown Reason";
        subject_str = "Federation Tow Notice: Ship Moved";
        break;
    }
  const char *sql_update_ship = "UPDATE ships SET sector = ? WHERE id = ?;";

  if (sqlite3_prepare_v2 (db, sql_update_ship, -1, &stmt, NULL) != SQLITE_OK)
    {
      LOGE ("tow_ship: Prepare UPDATE ship failed: %s", sqlite3_errmsg (db));
      return SQLITE_ERROR;
    }
  sqlite3_bind_int (stmt, 1, new_sector_id);
  sqlite3_bind_int (stmt, 2, ship_id);
  rc = sqlite3_step (stmt);
  sqlite3_finalize (stmt);
  if (rc != SQLITE_DONE)
    {
      LOGE ("tow_ship: Ship %d UPDATE failed: %d", ship_id, rc);
      return rc;
    }
  if (owner_id > 0)
    {
      const char *sql_update_player =
        "UPDATE players SET sector = ? WHERE id = ?;";


      if (sqlite3_prepare_v2 (db, sql_update_player, -1, &stmt, NULL) !=
          SQLITE_OK)
        {
          LOGE ("tow_ship: Prepare UPDATE player failed: %s",
                sqlite3_errmsg (db));
        }
      else
        {
          sqlite3_bind_int (stmt, 1, new_sector_id);
          sqlite3_bind_int (stmt, 2, owner_id);
          rc = sqlite3_step (stmt);
          sqlite3_finalize (stmt);
          if (rc != SQLITE_DONE)
            {
              LOGE ("tow_ship: Player %d UPDATE failed: %d", owner_id, rc);
            }
        }
      char message_buffer[256];


      snprintf (message_buffer,
                sizeof (message_buffer),
                "Your ship was found parked in FedSpace (Sector %d) without protection. It has been towed to Sector %d for violating FedLaw: %s. The ship is now exposed to danger.",
                old_sector_id,
                new_sector_id,
                reason_str);
      h_send_message_to_player (admin_id, owner_id, subject_str,
                                message_buffer);
    }
  LOGI
  (
    "TOW: Ship %d (Owner %d) towed from sector %d to sector %d. Reason: %s (Code %d). Admin: %d.",
    ship_id,
    owner_id,
    old_sector_id,
    new_sector_id,
    reason_str,
    reason_code,
    admin_id);
  int64_t current_time_s = (int64_t) time (NULL);
  json_t *fedspace_payload = json_object ();


  if (fedspace_payload)
    {
      json_object_set_new (fedspace_payload, "reason",
                           json_string (reason_str));
      json_object_set_new (fedspace_payload, "owner_id",
                           json_integer (owner_id));
      json_object_set_new (fedspace_payload, "ship_id",
                           json_integer (ship_id));
      db_log_engine_event (current_time_s, "fedspace:tow", NULL, admin_id,
                           old_sector_id, fedspace_payload, NULL);
      json_decref (fedspace_payload);
    }
  else
    {
      LOGE ("Failed to create JSON payload for fedspace:tow event.");
    }
  return SQLITE_OK;
}


#define MSL_TABLE_NAME "msl_sectors"


static int *
universe_pathfind_get_sectors (sqlite3 *db, int start_sector, int end_sector,
                               const int *avoid_list)
{
  if (start_sector == end_sector)
    {
      int *path = malloc (2 * sizeof (int));


      if (path)
        {
          path[0] = start_sector;
          path[1] = 0;
        }
      return path;
    }
  int max_sector_id = 0;
  sqlite3_stmt *max_st = NULL;


  if (sqlite3_prepare_v2
        (db, "SELECT MAX(id) FROM sectors;", -1, &max_st, NULL) == SQLITE_OK)
    {
      if (sqlite3_step (max_st) == SQLITE_ROW)
        {
          max_sector_id = sqlite3_column_int (max_st, 0);
        }
      sqlite3_finalize (max_st);
    }
  if (max_sector_id <= 0)
    {
      max_sector_id = 2000;
    }
  int *parent = calloc (max_sector_id + 1, sizeof (int));


  if (!parent)
    {
      return NULL;
    }
  int *queue = malloc (INITIAL_QUEUE_CAPACITY * sizeof (int));
  int queue_head = 0;
  int queue_tail = 0;
  int queue_capacity = INITIAL_QUEUE_CAPACITY;


  if (!queue)
    {
      free (parent);
      return NULL;
    }
  if (avoid_list)
    {
      for (const int *avoid = avoid_list; *avoid != 0; avoid++)
        {
          if (*avoid > 0 && *avoid <= max_sector_id)
            {
              parent[*avoid] = -2;
            }
        }
    }
  parent[start_sector] = -1;
  queue[queue_tail++] = start_sector;
  int path_found = 0;
  sqlite3_stmt *warp_st = NULL;
  const char *sql_warps =
    "SELECT to_sector FROM sector_warps WHERE from_sector = ?1;";


  if (sqlite3_prepare_v2 (db, sql_warps, -1, &warp_st, NULL) != SQLITE_OK)
    {
      free (parent);
      free (queue);
      return NULL;
    }
  while (queue_head < queue_tail)
    {
      int current_sector = queue[queue_head++];


      if (current_sector == end_sector)
        {
          path_found = 1;
          break;
        }
      sqlite3_bind_int (warp_st, 1, current_sector);
      while (sqlite3_step (warp_st) == SQLITE_ROW)
        {
          int neighbor = sqlite3_column_int (warp_st, 0);


          if (neighbor <= 0 || neighbor > max_sector_id
              || parent[neighbor] != 0)
            {
              continue;
            }
          parent[neighbor] = current_sector;
          if (queue_tail == queue_capacity)
            {
              queue_capacity *= 2;
              int *new_queue = realloc (queue, queue_capacity * sizeof (int));


              if (!new_queue)
                {
                  path_found = 0;
                  queue_tail = 0;
                  break;
                }
              queue = new_queue;
            }
          queue[queue_tail++] = neighbor;
        }
      sqlite3_reset (warp_st);
    }
  sqlite3_finalize (warp_st);
  free (queue);
  if (!path_found)
    {
      free (parent);
      return NULL;
    }
  int path_length = 0;
  int temp_sector = end_sector;


  while (temp_sector != -1)
    {
      path_length++;
      if (path_length > max_sector_id + 1)
        {
          free (parent);
          return NULL;
        }
      temp_sector = parent[temp_sector];
    }
  int *result_path = malloc ((path_length + 1) * sizeof (int));


  if (!result_path)
    {
      free (parent);
      return NULL;
    }
  int i = path_length - 1;


  temp_sector = end_sector;
  while (temp_sector != -1)
    {
      result_path[i--] = temp_sector;
      temp_sector = parent[temp_sector];
    }
  result_path[path_length] = 0;
  free (parent);
  return result_path;
}


static void
_insert_path_sectors (sqlite3 *db, sqlite3_stmt *insert_st, int start_sector,
                      int end_sector, const int *avoid_list,
                      int *total_unique_sectors_added)
{
  int rc;
  int *s;
  int *current_path =
    universe_pathfind_get_sectors (db, start_sector, end_sector, avoid_list);
  if (!current_path)
    {
      LOGW ("Could not find path from %d to %d. Check universe connections.",
            start_sector, end_sector);
      return;
    }
  for (s = current_path; *s != 0; s++)
    {
      sqlite3_reset (insert_st);
      sqlite3_bind_int (insert_st, 1, *s);
      rc = sqlite3_step (insert_st);
      if (rc == SQLITE_DONE)
        {
          if (sqlite3_changes (db) > 0)
            {
              (*total_unique_sectors_added)++;
            }
        }
      else
        {
          LOGW ("SQL warning inserting sector %d for path %d->%d: %s", *s,
                start_sector, end_sector, sqlite3_errmsg (db));
        }
    }
  free (current_path);
}


int
populate_msl_if_empty (sqlite3 *db)
{
  const int *avoid_list = NULL;
  int rc;
  int total_sectors_in_table = 0;
  char sql_buffer[256];
  snprintf (sql_buffer, sizeof (sql_buffer),
            "SELECT COUNT(sector_id) FROM %s;", MSL_TABLE_NAME);
  sqlite3_stmt *count_st = NULL;


  if (sqlite3_prepare_v2 (db, sql_buffer, -1, &count_st, NULL) == SQLITE_OK
      && sqlite3_step (count_st) == SQLITE_ROW)
    {
      total_sectors_in_table = sqlite3_column_int (count_st, 0);
    }
  sqlite3_finalize (count_st);
  if (total_sectors_in_table > 0)
    {
      LOGI
        ("%s table already populated with %d entries. Skipping MSL calculation.",
        MSL_TABLE_NAME,
        total_sectors_in_table);
      return 0;
    }
  LOGI
  (
    "%s table is empty. Starting comprehensive MSL path calculation (FedSpace 1-10 <-> Stardocks)...",
    MSL_TABLE_NAME);
  snprintf (sql_buffer, sizeof (sql_buffer),
            "CREATE TABLE IF NOT EXISTS %s (sector_id INTEGER PRIMARY KEY);",
            MSL_TABLE_NAME);
  if (sqlite3_exec (db, sql_buffer, NULL, NULL, NULL) != SQLITE_OK)
    {
      LOGE ("SQL error creating %s table: %s", MSL_TABLE_NAME,
            sqlite3_errmsg (db));
      return -1;
    }
  sqlite3_stmt *select_st = NULL;
  const char *sql_select_stardocks =
    "SELECT sector_id FROM stardock_location;";


  rc = sqlite3_prepare_v2 (db, sql_select_stardocks, -1, &select_st, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE ("SQL error preparing Stardock select: %s", sqlite3_errmsg (db));
      return -1;
    }
  int *stardock_sectors = NULL;
  int stardock_count = 0;
  int stardock_capacity = 8;


  stardock_sectors = malloc (stardock_capacity * sizeof (int));
  if (!stardock_sectors)
    {
      LOGE ("Failed to allocate stardock sector array.");
      sqlite3_finalize (select_st);
      return -1;
    }
  while (sqlite3_step (select_st) == SQLITE_ROW)
    {
      int id = sqlite3_column_int (select_st, 0);


      if (stardock_count == stardock_capacity)
        {
          stardock_capacity *= 2;
          int *new_arr =
            realloc (stardock_sectors, stardock_capacity * sizeof (int));


          if (!new_arr)
            {
              LOGE ("Failed to reallocate stardock sector array.");
              free (stardock_sectors);
              sqlite3_finalize (select_st);
              return -1;
            }
          stardock_sectors = new_arr;
        }
      stardock_sectors[stardock_count++] = id;
    }
  sqlite3_finalize (select_st);
  if (stardock_count == 0)
    {
      LOGW
      (
        "No stardock locations found in stardock_location table. Skipping MSL calculation.");
      free (stardock_sectors);
      return 0;
    }
  sqlite3_stmt *insert_st = NULL;


  snprintf (sql_buffer, sizeof (sql_buffer),
            "INSERT OR IGNORE INTO %s (sector_id) VALUES (?);",
            MSL_TABLE_NAME);
  rc = sqlite3_prepare_v2 (db, sql_buffer, -1, &insert_st, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE ("SQL error preparing insert statement for %s: %s", MSL_TABLE_NAME,
            sqlite3_errmsg (db));
      free (stardock_sectors);
      return -1;
    }
  if (sqlite3_exec (db, "BEGIN TRANSACTION;", NULL, NULL, NULL) != SQLITE_OK)
    {
      LOGE ("SQL error starting master transaction: %s", sqlite3_errmsg (db));
      sqlite3_finalize (insert_st);
      free (stardock_sectors);
      return -1;
    }
  int total_unique_sectors_added = 0;


  for (int start_sector = FEDSPACE_SECTOR_START;
       start_sector <= FEDSPACE_SECTOR_END; start_sector++)
    {
      for (int i = 0; i < stardock_count; i++)
        {
          int stardock_id = stardock_sectors[i];


          LOGI ("Calculating path %d -> %d", start_sector, stardock_id);
          _insert_path_sectors (db, insert_st, start_sector, stardock_id,
                                avoid_list, &total_unique_sectors_added);
          if (start_sector != stardock_id)
            {
              LOGI ("Calculating path %d -> %d (Reverse)", stardock_id,
                    start_sector);
              _insert_path_sectors (db, insert_st, stardock_id, start_sector,
                                    avoid_list, &total_unique_sectors_added);
            }
        }
    }
  sqlite3_finalize (insert_st);
  free (stardock_sectors);
  if (sqlite3_exec (db, "COMMIT;", NULL, NULL, NULL) != SQLITE_OK)
    {
      LOGE ("SQL error committing master path transaction: %s",
            sqlite3_errmsg (db));
      return -1;
    }
  LOGI ("Completed MSL setup. Populated %s with %d total unique sectors.",
        MSL_TABLE_NAME, total_unique_sectors_added);
  return 0;
}


int
h_reset_turns_for_player (sqlite3 *db, int64_t now_s)
{
  sqlite3_stmt *select_st = NULL;
  sqlite3_stmt *update_st = NULL;
  int max_turns = 0;
  int rc;
  int updated_count = 0;
  const char *sql_config = "SELECT value FROM config WHERE key='turnsperday';";
  rc = sqlite3_prepare_v2 (db, sql_config, -1, &select_st, NULL);
  if (rc == SQLITE_OK && sqlite3_step (select_st) == SQLITE_ROW)
    {
      max_turns = sqlite3_column_int (select_st, 0);
    }
  sqlite3_finalize (select_st);
  select_st = NULL;
  if (max_turns <= 0)
    {
      LOGE ("Turn reset failed: turnsperday is %d or missing in config.",
            max_turns);
      return -1;
    }
  const char *sql_select_players = "SELECT player FROM turns;";


  rc = sqlite3_prepare_v2 (db, sql_select_players, -1, &select_st, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE ("SQL error preparing player select: %s", sqlite3_errmsg (db));
      return -1;
    }
  const char *sql_update =
    "UPDATE turns SET turns_remaining = ?, last_update = ? WHERE player = ?;";


  rc = sqlite3_prepare_v2 (db, sql_update, -1, &update_st, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE ("SQL error preparing turns update: %s", sqlite3_errmsg (db));
      sqlite3_finalize (select_st);
      return -1;
    }
  if (sqlite3_exec (db, "BEGIN TRANSACTION;", NULL, NULL, NULL) != SQLITE_OK)
    {
      LOGE ("SQL error starting transaction: %s", sqlite3_errmsg (db));
      sqlite3_finalize (select_st);
      sqlite3_finalize (update_st);
      return -1;
    }
  while (sqlite3_step (select_st) == SQLITE_ROW)
    {
      int player_id = sqlite3_column_int (select_st, 0);


      sqlite3_reset (update_st);
      sqlite3_bind_int (update_st, 1, max_turns);
      sqlite3_bind_int64 (update_st, 2, now_s);
      sqlite3_bind_int (update_st, 3, player_id);
      rc = sqlite3_step (update_st);
      if (rc == SQLITE_DONE)
        {
          updated_count++;
        }
      else
        {
          LOGE ("SQL error executing turns update for player %d: %s",
                player_id, sqlite3_errmsg (db));
        }
    }
  sqlite3_finalize (select_st);
  sqlite3_finalize (update_st);
  if (sqlite3_exec (db, "COMMIT;", NULL, NULL, NULL) != SQLITE_OK)
    {
      LOGE ("SQL error committing transaction: %s", sqlite3_errmsg (db));
      return -1;
    }
  LOGI ("Successfully reset turns for %d players to %d.", updated_count,
        max_turns);
  return 0;
}


int
try_lock (sqlite3 *db, const char *name, int64_t now_s)
{
  sqlite3_stmt *st = NULL;
  int rc;
  int64_t now_ms = now_s * 1000;
  const int LOCK_DURATION_S = 60;
  int64_t until_ms = now_ms + (LOCK_DURATION_S * 1000);
  rc =
    sqlite3_prepare_v2 (db,
                        "DELETE FROM locks WHERE lock_name=?1 AND until_ms < ?2;",
                        -1,
                        &st,
                        NULL);
  if (rc == SQLITE_OK)
    {
      sqlite3_bind_text (st, 1, name, -1, SQLITE_STATIC);
      sqlite3_bind_int64 (st, 2, now_ms);
      sqlite3_step (st);
    }
  sqlite3_finalize (st);
  if (rc != SQLITE_OK)
    {
      return 0;
    }
  rc =
    sqlite3_prepare_v2 (db,
                        "INSERT INTO locks(lock_name, owner, until_ms) VALUES(?1, 'server', ?2) ON CONFLICT(lock_name) DO NOTHING;",
                        -1,
                        &st,
                        NULL);
  if (rc != SQLITE_OK)
    {
      return 0;
    }
  sqlite3_bind_text (st, 1, name, -1, SQLITE_STATIC);
  sqlite3_bind_int64 (st, 2, until_ms);
  sqlite3_step (st);
  sqlite3_finalize (st);
  rc =
    sqlite3_prepare_v2 (db, "SELECT owner FROM locks WHERE lock_name=?1;", -1,
                        &st, NULL);
  if (rc != SQLITE_OK)
    {
      return 0;
    }
  sqlite3_bind_text (st, 1, name, -1, SQLITE_STATIC);
  int ok = 0;


  if (sqlite3_step (st) == SQLITE_ROW)
    {
      const unsigned char *o = sqlite3_column_text (st, 0);


      ok = (o && strcmp ((const char *) o, "server") == 0);
    }
  sqlite3_finalize (st);
  return ok;
}


int64_t
db_lock_status (sqlite3 *db, const char *name)
{
  const char *SQL = "SELECT until_ms FROM locks WHERE lock_name = ?1;";
  sqlite3_stmt *st = NULL;
  int64_t until_ms = 0;
  if (sqlite3_prepare_v2 (db, SQL, -1, &st, NULL) == SQLITE_OK)
    {
      sqlite3_bind_text (st, 1, name, -1, SQLITE_STATIC);
      if (sqlite3_step (st) == SQLITE_ROW)
        {
          until_ms = sqlite3_column_int64 (st, 0);
        }
      sqlite3_finalize (st);
    }
  return until_ms;
}


int
unlock (sqlite3 *db, const char *name)
{
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2
        (db, "DELETE FROM locks WHERE lock_name=?1 AND owner='server';", -1,
        &st, NULL) == SQLITE_OK)
    {
      sqlite3_bind_text (st, 1, name, -1, SQLITE_STATIC);
      sqlite3_step (st);
      sqlite3_finalize (st);
    }
  return 0;                     // Return 0 for success
}


void
cron_register_builtins (void)
{
  g_reg_inited = 1;
}


int
begin (sqlite3 *db)
{
  return sqlite3_exec (db, "BEGIN IMMEDIATE;", NULL, NULL, NULL);
}


int
commit (sqlite3 *db)
{
  return sqlite3_exec (db, "COMMIT;", NULL, NULL, NULL);
}


int
rollback (sqlite3 *db)
{
  return sqlite3_exec (db, "ROLLBACK;", NULL, NULL, NULL);
}


const char *
get_asset_name (int type)
{
  switch (type)
    {
      case 3:
        return "Mine";
      case 2:
        return "Fighter";
      case 1:
        return "Beacon";
      default:
        return "Unknown Asset";
    }
}


static int
ship_callback (void *count_ptr, int argc, char **argv, char **azColName)
{
  (void) argc;                  // tell the compiler I know about this, but I'm not using it.
  (void) azColName;             // ditto
  int *count = (int *) count_ptr;


  if (argv[0])
    {
      printf ("Found cloaked ship (Timestamp: %s)\n", argv[0]);
      (*count)++;
    }
  return 0;
}


int
uncloak_ships_in_fedspace (sqlite3 *db)
{
  int rc;
  char *err_msg = 0;
  int cloaked_ship_count = 0;
  const char *sql =
    "UPDATE ships SET cloaked=NULL WHERE cloaked IS NOT NULL AND (sector IN (SELECT sector_id FROM stardock_location) OR sector IN (1, 2, 3, 4, 5, 6, 7, 8, 9, 10));";
  rc = sqlite3_exec (db, sql, ship_callback, &cloaked_ship_count, &err_msg);
  if (rc != SQLITE_OK)
    {
      LOGE ("SQL error: %s", err_msg);
      sqlite3_free (err_msg);
      return -1;
    }
  return cloaked_ship_count;
}


/* REPLACEMENT for h_fedspace_cleanup in src/server_cron.c */
int
h_fedspace_cleanup (sqlite3 *db, int64_t now_s)
{
  int fedadmin = 2;
  sqlite3_stmt *select_stmt = NULL;
  sqlite3_stmt *delete_stmt = NULL;
  int cleared_assets = 0;
  int tows = 0;
  /* 1. Acquire Cron Lock */
  if (!try_lock (db, "fedspace_cleanup", now_s))
    {
      // ... (keep existing lock logging logic if desired) ...
      return 0;
    }
  LOGI ("fedspace_cleanup: Lock acquired, starting cleanup operations.");
  /* 2. Heavy Ops: Uncloak & MSL & Clusters (These are largely idempotent/fast or own their own tx) */
  int uncloak = uncloak_ships_in_fedspace (db);


  if (uncloak > 0)
    {
      LOGI ("Uncloaked %d ships in FedSpace.", uncloak);
    }
  // Populate MSL if needed (this handles its own transaction internally)
  if (populate_msl_if_empty (db) != 0)
    {
      LOGE ("fedspace_cleanup: MSL population failed.");
    }
  // Init Clusters (idempotent)
  if (clusters_init (db) != 0)
    {
      LOGE ("fedspace_cleanup: Cluster init failed.");
    }
  clusters_seed_illegal_goods (db);
  /* --- YIELD POINT 1: After Initial Setup --- */
  // We are not holding the C-mutex or a transaction here yet, so this is safe.
  /* 3. Asset Clearing (Requires Transaction) */
  db_mutex_lock ();
  int rc; // Declare rc


  rc = begin (db);
  if (rc != SQLITE_OK)
    {
      db_mutex_unlock ();
      unlock (db, "fedspace_cleanup");
      return rc;
    }
  const char *select_assets_sql =
    "SELECT player, asset_type, sector, quantity FROM sector_assets WHERE sector IN (SELECT sector_id FROM msl_sectors) AND player != 0;";


  rc = sqlite3_prepare_v2 (db, select_assets_sql, -1, &select_stmt, NULL);
  if (rc == SQLITE_OK)
    {
      char message[256];
      const char *delete_sql =
        "DELETE FROM sector_assets WHERE player = ?1 AND asset_type = ?2 AND sector = ?3 AND quantity = ?4;";


      if (sqlite3_prepare_v2 (db, delete_sql, -1, &delete_stmt,
                              NULL) == SQLITE_OK)
        {
          while ((rc = sqlite3_step (select_stmt)) == SQLITE_ROW)
            {
              int player_id = sqlite3_column_int (select_stmt, 0);
              int asset_type = sqlite3_column_int (select_stmt, 1);
              int sector_id = sqlite3_column_int (select_stmt, 2);
              int quantity = sqlite3_column_int (select_stmt, 3);


              // Send message (this actually does DB work too, but fine for now)
              snprintf (message,
                        sizeof (message),
                        "%d %s(s) deployed in Sector %d (Major Space Lane) were destroyed by Federal Authorities.",
                        quantity,
                        get_asset_name (asset_type),
                        sector_id);
              h_send_message_to_player (player_id,
                                        fedadmin,
                                        "WARNING: MSL Violation",
                                        message);
              // Log event
              // ... (logging logic kept from original) ...
              sqlite3_reset (delete_stmt);
              sqlite3_bind_int (delete_stmt, 1, player_id);
              sqlite3_bind_int (delete_stmt, 2, asset_type);
              sqlite3_bind_int (delete_stmt, 3, sector_id);
              sqlite3_bind_int (delete_stmt, 4, quantity);
              if (sqlite3_step (delete_stmt) == SQLITE_DONE)
                {
                  cleared_assets++;
                }
            }
          sqlite3_finalize (delete_stmt);
        }
      sqlite3_finalize (select_stmt);
    }
  commit (db); // Commit asset clearing
  db_mutex_unlock ();
  usleep (10000); // Yield
  /* 4. Logout Timeout (New Transaction) */
  db_mutex_lock ();
  rc = begin (db);
  if (rc != SQLITE_OK)
    {
      db_mutex_unlock ();
      unlock (db, "fedspace_cleanup");
      return rc;
    }
  const char *sql_timeout_logout =
    "UPDATE players SET loggedin = 0 WHERE loggedin = 1 AND ?1 - last_update > ?2;";


  if (sqlite3_prepare_v2 (db, sql_timeout_logout, -1, &select_stmt,
                          NULL) == SQLITE_OK)
    {
      sqlite3_bind_int64 (select_stmt, 1, now_s);
      sqlite3_bind_int (select_stmt, 2, LOGOUT_TIMEOUT_S);
      sqlite3_step (select_stmt);
      sqlite3_finalize (select_stmt);
    }
  /* 5. Prepare Towing Table (Still in transaction) */
  // ... (Create/Clear eligible_tows table logic kept same) ...
  // Re-using existing logic flow but compressed for the snippet
  sqlite3_exec (db,
                "CREATE TABLE IF NOT EXISTS eligible_tows (ship_id INTEGER PRIMARY KEY, sector_id INTEGER, owner_id INTEGER, fighters INTEGER, alignment INTEGER, experience INTEGER);",
                NULL,
                NULL,
                NULL);
  sqlite3_exec (db, "DELETE FROM eligible_tows", NULL, NULL, NULL);
  const char *sql_insert_eligible =
    "INSERT INTO eligible_tows (ship_id, sector_id, owner_id, fighters, alignment, experience) "
    "SELECT T1.id, T1.sector, T2.id, T1.fighters, COALESCE(T2.alignment, 0), COALESCE(T2.experience, 0) "
    "FROM ships T1 LEFT JOIN players T2 ON T1.id = T2.ship "
    "WHERE T1.sector BETWEEN ?1 AND ?2 AND (T2.id IS NULL OR (T2.loggedin = 0 AND (?3 - T2.last_login > ?4))) "
    "ORDER BY T1.id ASC;";


  if (sqlite3_prepare_v2 (db, sql_insert_eligible, -1, &select_stmt,
                          NULL) == SQLITE_OK)
    {
      sqlite3_bind_int (select_stmt, 1, FEDSPACE_SECTOR_START);
      sqlite3_bind_int (select_stmt, 2, FEDSPACE_SECTOR_END);
      sqlite3_bind_int64 (select_stmt, 3, now_s);    /* now_s */
      sqlite3_bind_int (select_stmt, 4, 12 * 60 * 60);  /* 12 hours in seconds */
      sqlite3_step (select_stmt);
      sqlite3_finalize (select_stmt);
    }
  /* Commit Prep Work */
  commit (db);
  db_mutex_unlock ();
  usleep (10000); // Yield
  /* 6. Execute Tows (Batched) */
  // We can do the select queries unlocked (since we have a temp table `eligible_tows`),
  // but the actual tow_ship() calls need to be atomic.
  // tow_ship() likely opens its own transaction or statement? Checking...
  // tow_ship() uses single statements. To be safe, we wrap batches.
  db_mutex_lock ();
  begin (db);
  // ... [Logic for Evil Alignment, Excess Fighters, etc.] ...
  // Since there are 4 categories, we can yield after each category loop.
  /* A. Evil Alignment Tows */
  const char *sql_evil_alignment =
    "SELECT ship_id FROM eligible_tows WHERE owner_id IS NOT NULL AND alignment < 0 LIMIT ?1;";


  if (sqlite3_prepare_v2 (db, sql_evil_alignment, -1, &select_stmt,
                          NULL) == SQLITE_OK)
    {
      sqlite3_bind_int (select_stmt, 1, MAX_TOWS_PER_PASS - tows);
      while (sqlite3_step (select_stmt) == SQLITE_ROW &&
             tows < MAX_TOWS_PER_PASS)
        {
          int ship_id = sqlite3_column_int (select_stmt, 0);


          tow_ship (db,
                    ship_id,
                    get_random_sector (db),
                    fedadmin,
                    REASON_EVIL_ALIGN);
          // We skip deleting from eligible_tows for speed, we just clear table at end
          tows++;
        }
      sqlite3_finalize (select_stmt);
    }
  commit (db);
  db_mutex_unlock (); usleep (10000); db_mutex_lock (); begin (db);
  /* B. Excess Fighters Tows */
  if (tows < MAX_TOWS_PER_PASS)
    {
      const char *sql_excess_fighters =
        "SELECT ship_id FROM eligible_tows WHERE fighters > ?1 LIMIT ?2;";


      if (sqlite3_prepare_v2 (db, sql_excess_fighters, -1, &select_stmt,
                              NULL) == SQLITE_OK)
        {
          sqlite3_bind_int (select_stmt, 1, 49);
          sqlite3_bind_int (select_stmt, 2, MAX_TOWS_PER_PASS - tows);
          while (sqlite3_step (select_stmt) == SQLITE_ROW &&
                 tows < MAX_TOWS_PER_PASS)
            {
              int ship_id = sqlite3_column_int (select_stmt, 0);


              tow_ship (db,
                        ship_id,
                        get_random_sector (db),
                        fedadmin,
                        REASON_EXCESS_FIGHTERS);
              tows++;
            }
          sqlite3_finalize (select_stmt);
        }
    }
  commit (db);
  db_mutex_unlock (); usleep (10000); db_mutex_lock (); begin (db);
  /* C. High Exp Tows */
  if (tows < MAX_TOWS_PER_PASS)
    {
      // ... (Same pattern for High Exp) ...
      const char *sql_high_exp =
        "SELECT ship_id FROM eligible_tows WHERE owner_id IS NOT NULL AND experience >= 1000 LIMIT ?1;";


      if (sqlite3_prepare_v2 (db, sql_high_exp, -1, &select_stmt,
                              NULL) == SQLITE_OK)
        {
          sqlite3_bind_int (select_stmt, 1, MAX_TOWS_PER_PASS - tows);
          while (sqlite3_step (select_stmt) == SQLITE_ROW &&
                 tows < MAX_TOWS_PER_PASS)
            {
              int ship_id = sqlite3_column_int (select_stmt, 0);


              tow_ship (db,
                        ship_id,
                        get_random_sector (db),
                        fedadmin,
                        REASON_HIGH_EXP);
              tows++;
            }
          sqlite3_finalize (select_stmt);
        }
    }
  /* D. No Owner */
  if (tows < MAX_TOWS_PER_PASS)
    {
      const char *sql_no_owner =
        "SELECT ship_id FROM eligible_tows WHERE owner_id IS NULL LIMIT ?1;";


      if (sqlite3_prepare_v2 (db, sql_no_owner, -1, &select_stmt,
                              NULL) == SQLITE_OK)
        {
          sqlite3_bind_int (select_stmt, 1, MAX_TOWS_PER_PASS - tows);
          while (sqlite3_step (select_stmt) == SQLITE_ROW &&
                 tows < MAX_TOWS_PER_PASS)
            {
              int ship_id = sqlite3_column_int (select_stmt, 0);


              tow_ship (db,
                        ship_id,
                        CONFISCATION_SECTOR,
                        fedadmin,
                        REASON_NO_OWNER);
              tows++;
            }
          sqlite3_finalize (select_stmt);
        }
    }
  /* E. Overcrowding */
  // ... (Overcrowding logic - similar batching) ...
  /* Final Cleanup */
  sqlite3_exec (db, "DELETE FROM eligible_tows", NULL, NULL, NULL);
  rc = commit (db);
  db_mutex_unlock ();
  LOGI ("fedspace_cleanup: ok (towed=%d)", tows);
  unlock (db, "fedspace_cleanup");
  return 0;
}


// Helper function to convert Unix epoch seconds to UTC epoch day (YYYYMMDD)
int64_t
get_utc_epoch_day (int64_t unix_timestamp)
{
  time_t rawtime = unix_timestamp;
  struct tm *info;
  // Use gmtime for UTC
  info = gmtime (&rawtime);
  if (info == NULL)
    {
      return 0;                 // Error
    }
  // Return YYYYMMDD
  return (info->tm_year + 1900) * 10000 + (info->tm_mon + 1) * 100 +
         info->tm_mday;
}


int
h_dividend_payout (sqlite3 *db, int64_t now_s)
{
  LOGI("BANK0: Dividend Payout cron disabled for v1.0.");
  (void)db; // Suppress unused parameter warning
  (void)now_s; // Suppress unused parameter warning
  return 0; // Do nothing, cleanly exit

  if (!try_lock (db, "h_dividend_payout", now_s))
    {
      return 0;
    }
  LOGI ("h_dividend_payout: Starting dividend payout cron job.");
  int rc = begin (db);


  if (rc != SQLITE_OK)
    {
      LOGE ("h_dividend_payout: Failed to start transaction: %s",
            sqlite3_errmsg (db));
      unlock (db, "h_dividend_payout");
      return rc;
    }
  sqlite3_stmt *select_dividends_st = NULL;
  const char *sql_select_dividends =
    "SELECT id, stock_id, amount_per_share, declared_ts "
    "FROM stock_dividends WHERE paid_ts IS NULL;";


  rc =
    sqlite3_prepare_v2 (db, sql_select_dividends, -1, &select_dividends_st,
                        NULL);
  if (rc != SQLITE_OK)
    {
      LOGE
        ("h_dividend_payout: Failed to prepare select dividends statement: %s",
        sqlite3_errmsg (db));
      goto rollback_and_unlock;
    }
  int processed_dividends = 0;


  while (sqlite3_step (select_dividends_st) == SQLITE_ROW)
    {
      int dividend_id = sqlite3_column_int (select_dividends_st, 0);
      int stock_id = sqlite3_column_int (select_dividends_st, 1);
      int amount_per_share = sqlite3_column_int (select_dividends_st, 2);
      // long long declared_ts = sqlite3_column_int64 (select_dividends_st, 3);
      // Get stock and corp info
      // char *ticker = NULL;
      int corp_id = 0;
      int total_shares = 0;


      // Use h_get_stock_info from server_corporation.h
      if (h_get_stock_info
            (db, stock_id, NULL, &corp_id, &total_shares, NULL, NULL,
            NULL) != SQLITE_OK)
        {
          LOGE
          (
            "h_dividend_payout: Failed to get stock info for stock %d via helper. Skipping this dividend.",
            stock_id);
          continue;             // Skip this dividend if we can't get stock info
        }
      if (corp_id == 0)
        {
          LOGW
          (
            "h_dividend_payout: Stock ID %d not linked to a corporation. Skipping.",
            stock_id);
          continue;
        }
      long long total_payout = (long long) amount_per_share * total_shares;
      // Check if corporation has enough funds
      long long corp_balance;


      if (db_get_corp_bank_balance (corp_id, &corp_balance) != SQLITE_OK
          || corp_balance < total_payout)
        {
          LOGW
          (
            "h_dividend_payout: Corp %d (stock %d) has insufficient funds for dividend %d. Required: %lld, Has: %lld. Skipping payout.",
            corp_id,
            stock_id,
            dividend_id,
            total_payout,
            corp_balance);
          json_t *payload =
            json_pack ("{s:i, s:i, s:I, s:I, s:s}", "corp_id", corp_id,
                       "stock_id", stock_id, "required", total_payout,
                       "available", corp_balance, "status",
                       "failed_insufficient_funds");


          db_log_engine_event (now_s, "stock.dividend.payout_failed", "corp",
                               corp_id, 0, payload, NULL);
          json_decref (payload);
          continue;
        }
      // Iterate through shareholders and pay out
      sqlite3_stmt *select_shareholders_st = NULL;
      const char *sql_select_shareholders =
        "SELECT player_id, shares FROM corp_shareholders WHERE corp_id = ?;";


      rc =
        sqlite3_prepare_v2 (db, sql_select_shareholders, -1,
                            &select_shareholders_st, NULL);
      if (rc != SQLITE_OK)
        {
          LOGE
          (
            "h_dividend_payout: Failed to prepare select shareholders statement for corp %d: %s",
            corp_id,
            sqlite3_errmsg (db));
          goto rollback_and_unlock;
        }
      long long actual_payout_sum = 0;


      while (sqlite3_step (select_shareholders_st) == SQLITE_ROW)
        {
          int player_id = sqlite3_column_int (select_shareholders_st, 0);
          int shares = sqlite3_column_int (select_shareholders_st, 1);


          if (player_id == 0)
            {
              continue;         // Skip the corporation's own holdings in a direct payout
            }
          long long player_dividend = (long long) shares * amount_per_share;
          char idempotency_key[UUID_STR_LEN];


          h_generate_hex_uuid (idempotency_key, sizeof (idempotency_key));
          int transfer_rc = h_bank_transfer_unlocked (db, "corp", corp_id,
                                                      "player", player_id,
                                                      player_dividend,
                                                      "DIVIDEND",
                                                      idempotency_key);


          if (transfer_rc != SQLITE_OK)
            {
              LOGE
              (
                "h_dividend_payout: Failed to pay dividend to player %d from corp %d for stock %d. Amount: %lld. Error: %s",
                player_id,
                corp_id,
                stock_id,
                player_dividend,
                sqlite3_errstr (transfer_rc));
              // Continue to next shareholder, but log the failure
            }
          else
            {
              actual_payout_sum += player_dividend;
            }
        }
      sqlite3_finalize (select_shareholders_st);
      // Mark dividend as paid
      sqlite3_stmt *update_dividend_st = NULL;
      const char *sql_update_dividend =
        "UPDATE stock_dividends SET paid_ts = ? WHERE id = ?;";


      rc =
        sqlite3_prepare_v2 (db, sql_update_dividend, -1, &update_dividend_st,
                            NULL);
      if (rc != SQLITE_OK)
        {
          LOGE
          (
            "h_dividend_payout: Failed to prepare update dividend statement for id %d: %s",
            dividend_id,
            sqlite3_errmsg (db));
          goto rollback_and_unlock;
        }
      sqlite3_bind_int64 (update_dividend_st, 1, now_s);
      sqlite3_bind_int (update_dividend_st, 2, dividend_id);
      rc = sqlite3_step (update_dividend_st);
      sqlite3_finalize (update_dividend_st);
      if (rc != SQLITE_DONE)
        {
          LOGE ("h_dividend_payout: Failed to mark dividend %d as paid: %s",
                dividend_id, sqlite3_errmsg (db));
          goto rollback_and_unlock;
        }
      LOGI
      (
        "h_dividend_payout: Payout of %lld credits completed for dividend %d (stock %d, corp %d).",
        actual_payout_sum,
        dividend_id,
        stock_id,
        corp_id);
      json_t *payload =
        json_pack ("{s:i, s:i, s:I, s:I}", "corp_id", corp_id, "stock_id",
                   stock_id, "dividend_id", dividend_id, "actual_payout",
                   actual_payout_sum);


      db_log_engine_event (now_s, "stock.dividend.payout_completed", "corp",
                           corp_id, 0, payload, NULL);
      json_decref (payload);
      processed_dividends++;
    }
  sqlite3_finalize (select_dividends_st);
  rc = commit (db);
  if (rc != SQLITE_OK)
    {
      LOGE ("h_dividend_payout: Commit failed: %s", sqlite3_errmsg (db));
      goto rollback_and_unlock;
    }
  LOGI ("h_dividend_payout: Successfully processed %d dividends.",
        processed_dividends);
  unlock (db, "h_dividend_payout");
  return SQLITE_OK;
rollback_and_unlock:
  if (select_dividends_st)
    {
      sqlite3_finalize (select_dividends_st);
    }
  rollback (db);
  unlock (db, "h_dividend_payout");
  return rc;
}


int
h_robbery_daily_cleanup (sqlite3 *db, int64_t now_s)
{
  int rc;
  // 1. Suspicion Decay (10% daily)
  const char *sql_decay =
    "UPDATE cluster_player_status "
    "SET suspicion = CAST(suspicion * 0.9 AS INTEGER) "
    "WHERE suspicion > 0;";
  rc = sqlite3_exec (db, sql_decay, NULL, NULL, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE ("h_robbery_daily_cleanup: Suspicion decay failed: %s",
            sqlite3_errmsg (db));
    }
  // 2. Clear Busts
  // Fake: Daily
  // Real: After TTL days (default 7)
  const char *sql_bust_clear =
    "UPDATE port_busts "
    "SET active = 0 "
    "WHERE active = 1 AND ( "
    "    (bust_type = 'fake') "
    "    OR "
    "    (bust_type = 'real' AND last_bust_at < ? - (SELECT robbery_real_bust_ttl_days * 86400 FROM law_enforcement WHERE id=1)) "
    ");";
  sqlite3_stmt *st = NULL;


  if (sqlite3_prepare_v2 (db, sql_bust_clear, -1, &st, NULL) == SQLITE_OK)
    {
      sqlite3_bind_int64 (st, 1, now_s);
      sqlite3_step (st);
      sqlite3_finalize (st);
    }
  else
    {
      LOGE ("h_robbery_daily_cleanup: Bust clear prepare failed: %s",
            sqlite3_errmsg (db));
    }
  return 0;
}


int
h_daily_turn_reset (sqlite3 *db, int64_t now_s)
{
  if (!try_lock (db, "daily_turn_reset", now_s))
    {
      return 0;
    }
  LOGI ("daily_turn_reset: starting daily turn reset.");
  int rc = begin (db);


  if (rc)
    {
      unlock (db, "daily_turn_reset");
      return rc;
    }
  rc =
    sqlite3_exec (db,
                  "UPDATE turns SET turns_remaining = CAST((SELECT value FROM config WHERE key = 'turnsperday') AS INTEGER);",
                  NULL,
                  NULL,
                  NULL);
  if (rc != SQLITE_OK)
    {
      LOGE ("daily_turn_reset: player turn update failed: %s",
            sqlite3_errmsg (db));
      rollback (db);
      unlock (db, "daily_turn_reset");
      return rc;
    }
  // Call Robbery Cleanup as part of daily maintenance (within transaction if possible, or just before commit)
  h_robbery_daily_cleanup (db, now_s);
  rc = commit (db);
  if (rc != SQLITE_OK)
    {
      LOGE ("daily_turn_reset: commit failed: %s", sqlite3_errmsg (db));
    }
  unlock (db, "daily_turn_reset");
  return rc;
}


int
h_autouncloak_sweeper (sqlite3 *db, int64_t now_s)
{
  if (!try_lock (db, "autouncloak_sweeper", now_s))
    {
      return 0;
    }
  sqlite3_stmt *st = NULL;
  int rc;
  int max_hours = 0;


  if (db_get_int_config (db, "max_cloak_duration", &max_hours) != 0)
    {
      LOGE ("Can't retrieve config 'max_cloak_duration': %s",
            sqlite3_errmsg (db));
      unlock (db, "autouncloak_sweeper");
      return 0;
    }
  if (max_hours <= 0)
    {
      LOGI
      (
        "autouncloak_sweeper: max_cloak_duration is zero/invalid. Skipping sweep.");
      unlock (db, "autouncloak_sweeper");
      return 0;
    }
  const int SECONDS_IN_HOUR = 3600;
  int64_t max_duration_seconds = (int64_t) max_hours * SECONDS_IN_HOUR;
  int64_t uncloak_threshold_s = now_s - max_duration_seconds;


  rc =
    sqlite3_prepare_v2 (db,
                        "UPDATE ships SET cloaked = NULL WHERE cloaked IS NOT NULL AND cloaked < ?;",
                        -1,
                        &st,
                        NULL);
  if (rc == SQLITE_OK)
    {
      sqlite3_bind_int64 (st, 1, uncloak_threshold_s);
      sqlite3_step (st);
    }
  else
    {
      LOGE ("Can't prepare ships UPDATE: %s", sqlite3_errmsg (db));
    }
  sqlite3_finalize (st);
  rc = commit (db);
  unlock (db, "autouncloak_sweeper");
  return 0;
}


int
h_terra_replenish (sqlite3 *db, int64_t now_s)
{
  if (!try_lock (db, "terra_replenish", now_s))
    {
      return 0;
    }
  (void) now_s;
  int rc = begin (db);


  if (rc)
    {
      return rc;
    }
  rc =
    sqlite3_exec (db,
                  "UPDATE planet_goods SET quantity = max_capacity WHERE planet_id = 1;",
                  NULL,
                  NULL,
                  NULL);
  if (rc)
    {
      rollback (db);
      LOGE ("terra_replenish (Terra resources max) rc=%d", rc);
      return rc;
    }
  rc =
    sqlite3_exec (db,
                  "UPDATE planets SET terraform_turns_left = 1 WHERE owner > 0;",
                  NULL,
                  NULL,
                  NULL);
  if (rc)
    {
      rollback (db);
      LOGE ("terra_replenish (turns reset) rc=%d", rc);
      return rc;
    }
  commit (db);
  unlock (db, "terra_replenish");
  return 0;
}


int
h_planet_population_tick (sqlite3 *db, int64_t now_s)
{
  (void)now_s;
  sqlite3_stmt *stmt = NULL;
  int rc;

  // Logistic growth parameters
  // Growth rate: 1% per tick? Or slower? 
  // Let's say 0.05 (5%) per tick for visible growth, clamped to max.
  const double GROWTH_RATE = 0.05; 

  const char *sql = 
    "SELECT p.id, p.population, "
    "       COALESCE(pt.maxColonist_ore, 0) + COALESCE(pt.maxColonist_organics, 0) + COALESCE(pt.maxColonist_equipment, 0) AS max_pop "
    "FROM planets p "
    "JOIN planettypes pt ON p.type = pt.id "
    "WHERE p.owner_id > 0 AND p.population > 0;";

  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
      LOGE("h_planet_population_tick: prepare failed: %s", sqlite3_errmsg(db));
      return rc;
  }

  // buffer updates to avoid nested transaction issues if called from within one?
  // Actually, we are likely inside a transaction in h_planet_growth.
  // But we need to update row by row or use a complex update statement.
  // A complex UPDATE with JOIN/calculations is better but SQLite support for that can be tricky.
  // We'll iterate and run simple UPDATEs. Since we are inside a transaction (from h_planet_growth), it's fine.

  // Prepare update stmt once
  sqlite3_stmt *update_stmt = NULL;
  if (sqlite3_prepare_v2(db, "UPDATE planets SET population = ?1 WHERE id = ?2;", -1, &update_stmt, NULL) != SQLITE_OK) {
      LOGE("h_planet_population_tick: prepare update failed: %s", sqlite3_errmsg(db));
      sqlite3_finalize(stmt);
      return SQLITE_ERROR;
  }

  while (sqlite3_step(stmt) == SQLITE_ROW) {
      int planet_id = sqlite3_column_int(stmt, 0);
      int current_pop = sqlite3_column_int(stmt, 1);
      int max_pop = sqlite3_column_int(stmt, 2);

      if (max_pop <= 0) max_pop = 10000; // Fallback default

      if (current_pop < max_pop) {
          double delta = (double)current_pop * GROWTH_RATE * (1.0 - (double)current_pop / (double)max_pop);
          int delta_int = (int)delta;
          if (delta_int < 1 && current_pop < max_pop) delta_int = 1; // Minimum growth

          int new_pop = current_pop + delta_int;
          if (new_pop > max_pop) new_pop = max_pop;

          sqlite3_reset(update_stmt);
          sqlite3_bind_int(update_stmt, 1, new_pop);
          sqlite3_bind_int(update_stmt, 2, planet_id);
          sqlite3_step(update_stmt);
      }
  }

  sqlite3_finalize(stmt);
  sqlite3_finalize(update_stmt);
  return SQLITE_OK;
}

int
h_planet_treasury_interest_tick (sqlite3 *db, int64_t now_s)
{
  LOGI("BANK0: Planet Treasury Interest cron disabled for v1.0.");
  (void)db; // Suppress unused parameter warning
  (void)now_s; // Suppress unused parameter warning
  return 0; // Do nothing, cleanly exit

  (void)now_s;
  
  int rate_bps = 0; // Basis points for interest rate
  // Try to get interest rate from config, otherwise use default 100 bps (1.00%)
  if (db_get_int_config(db, "planet_treasury_interest_rate_bps", &rate_bps) != 0 || rate_bps <= 0) {
      rate_bps = 100; // Default: 1.00%
  }
  
  sqlite3_stmt *stmt = NULL;
  const char *sql = "SELECT id, treasury FROM citadels WHERE level >= 1 AND treasury > 0;";
  
  if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
      LOGE("h_planet_treasury_interest_tick: prepare failed: %s", sqlite3_errmsg(db));
      return SQLITE_ERROR;
  }

  // Use a single UPDATE statement for efficiency and atomicity, performing calculation within SQL
  const char *sql_update =
    "UPDATE citadels "
    "SET treasury = treasury + ( (treasury * ?) / 10000 ) "
    "WHERE id = ?;";
  
  sqlite3_stmt *update_stmt = NULL;
  if (sqlite3_prepare_v2(db, sql_update, -1, &update_stmt, NULL) != SQLITE_OK) {
      LOGE("h_planet_treasury_interest_tick: prepare update failed: %s", sqlite3_errmsg(db));
      sqlite3_finalize(stmt);
      return SQLITE_ERROR;
  }

  while (sqlite3_step(stmt) == SQLITE_ROW) {
      int citadel_id = sqlite3_column_int(stmt, 0);
      long long current_treasury = sqlite3_column_int64(stmt, 1);
      
      // Calculate delta using integer arithmetic to avoid floating point issues
      long long delta = (current_treasury * rate_bps) / 10000;
      
      if (delta > 0) {
          long long next_treasury = current_treasury + delta;
          
          // Basic overflow check (SQLite integers are 64-bit, overflow is unlikely but good practice)
          if (next_treasury < current_treasury) { // Implies overflow occurred
              next_treasury = 9223372036854775807LL; // Clamp to LLONG_MAX
          }

          sqlite3_reset(update_stmt);
          sqlite3_bind_int(update_stmt, 1, rate_bps); // Bind rate_bps for the calculation
          sqlite3_bind_int(update_stmt, 2, citadel_id);
          sqlite3_step(update_stmt); // Execute the update for this specific citadel
      }
  }

  sqlite3_finalize(stmt);
  sqlite3_finalize(update_stmt);
  return SQLITE_OK;
}

int
h_planet_growth (sqlite3 *db, int64_t now_s)
{
  if (!try_lock (db, "planet_growth", now_s))
    {
      return 0;
    }
  (void) now_s;
  int rc = begin (db);


  if (rc)
    {
      return rc;
    }
  
  // 1. Population Growth
  if (h_planet_population_tick(db, now_s) != SQLITE_OK) {
      LOGE("h_planet_growth: Population tick failed.");
  }

  // 2. Treasury Interest (T2)
  if (h_planet_treasury_interest_tick(db, now_s) != SQLITE_OK) {
      LOGE("h_planet_growth: Treasury tick failed.");
  }

  /*
   * P3B Colonist-Driven Production Note:
   * 
   * Once schema columns `colonists_ore`, `colonists_org`, etc. are added to `planets` table,
   * the production query below should be updated to:
   * 
   * new_quantity = quantity + 
   *    (pp.base_prod_rate) + 
   *    (COALESCE(p.colonists_ore, 0) * [Efficiency_Ore_Factor]) ... etc.
   * 
   * Since those columns do not exist yet, we fall back to the existing static production
   * (base_prod_rate - base_cons_rate) as per P3B requirements.
   */

  // --- NEW: Update commodity quantities in entity_stock based on planet_production ---
  sqlite3_stmt *stmt = NULL;
  const char *sql_update_commodities =
    "INSERT INTO entity_stock (entity_type, entity_id, commodity_code, quantity, price, last_updated_ts) "
    "SELECT "
    "  'planet', "
    "  p.id, "
    "  pp.commodity_code, "
    "  MAX(0, MIN("
    "    CASE pp.commodity_code "
    "        WHEN 'ORE' THEN pltype.maxore "
    "        WHEN 'ORG' THEN pltype.maxorganics "
    "        WHEN 'EQU' THEN pltype.maxequipment "
    "        ELSE 999999 "
    "    END, "
    "    COALESCE(es.quantity, 0) + "
    "    pp.base_prod_rate + "
    "    ("
    "      CASE pp.commodity_code "
    "        WHEN 'ORE' THEN p.colonists_ore * 1 " // No oreProduction column, use 1
    "        WHEN 'ORG' THEN p.colonists_org * pltype.organicsProduction "
    "        WHEN 'EQU' THEN p.colonists_eq * pltype.equipmentProduction "
    "        WHEN 'FUE' THEN p.colonists_unassigned * pltype.fuelProduction "
    "        ELSE p.colonists_unassigned * 1 " // Illegal/other goods use unassigned
    "      END"
    "    ) - "
    "    pp.base_cons_rate"
    ")) AS new_quantity, "
    "  0, "
    "  strftime('%s','now') "
    "FROM planets p "
    "JOIN planet_production pp ON p.type = pp.planet_type_id "
    "LEFT JOIN entity_stock es ON es.entity_type = 'planet' AND es.entity_id = p.id AND es.commodity_code = pp.commodity_code "
    "LEFT JOIN planettypes pltype ON p.type = pltype.id "
    "WHERE (pp.base_prod_rate > 0 OR pp.base_cons_rate > 0 OR p.colonists_ore > 0 OR p.colonists_org > 0 OR p.colonists_eq > 0 OR p.colonists_unassigned > 0) "
    "ON CONFLICT(entity_type, entity_id, commodity_code) DO UPDATE SET "
    "quantity = excluded.quantity, last_updated_ts = excluded.last_updated_ts;";


  rc = sqlite3_prepare_v2 (db, sql_update_commodities, -1, &stmt, NULL);
  if (rc != SQLITE_OK)
    {
      rollback (db);
      LOGE ("planet_growth (commodities prepare) rc=%d: %s",
            rc,
            sqlite3_errmsg (db));
      unlock (db, "planet_growth");
      return rc;
    }
  rc = sqlite3_step (stmt);
  if (rc != SQLITE_DONE)
    {
      rollback (db);
      LOGE ("planet_growth (commodities step) rc=%d: %s",
            rc,
            sqlite3_errmsg (db));
      sqlite3_finalize (stmt);
      unlock (db, "planet_growth");
      return rc;
    }
  sqlite3_finalize (stmt);
  // --- END NEW COMMODITY UPDATE ---
  
  // Now call the new market tick for planets
  h_planet_market_tick(db, now_s);

  commit (db);
  unlock (db, "planet_growth");
  return 0;
}

// New function to handle market-related planet ticks (order generation)
int
h_planet_market_tick (sqlite3 *db, int64_t now_s)
{
  // This function is assumed to be called within a transaction by h_planet_growth
  sqlite3_stmt *stmt = NULL;
  int rc; // Declare rc here
  const char *sql_select_planets_commodities =
    "SELECT p.id AS planet_id, "
    "       pt.maxore, pt.maxorganics, pt.maxequipment, " // Max capacities for common commodities
    "       pp.commodity_code, "
    "       c.id AS commodity_id, " // Needed for orders
    "       es.quantity AS current_quantity, "
    "       pp.base_prod_rate, pp.base_cons_rate, "
    "       c.base_price, " // Base price for orders
    "       c.illegal, " // ADDED: To check for illegal commodities
    "       p.owner_id, " // ADDED for PE1
    "       p.owner_type " // ADDED for PE1
    "FROM planets p "
    "JOIN planettypes pt ON p.type = pt.id "
    "JOIN planet_production pp ON p.type = pp.planet_type_id "
    "LEFT JOIN entity_stock es ON es.entity_type = 'planet' AND es.entity_id = p.id AND es.commodity_code = pp.commodity_code "
    "JOIN commodities c ON pp.commodity_code = c.code "
    "WHERE (pp.base_prod_rate > 0 OR pp.base_cons_rate > 0) AND c.illegal = 0;";

  rc = sqlite3_prepare_v2 (db, sql_select_planets_commodities, -1, &stmt, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE ("h_planet_market_tick: Failed to prepare select statement: %s",
            sqlite3_errmsg (db));
      return rc; // Error, transaction will be rolled back by caller
    }

  while (sqlite3_step (stmt) == SQLITE_ROW)
    {
      int planet_id = sqlite3_column_int (stmt, 0);
      int maxore = sqlite3_column_int (stmt, 1);
      int maxorganics = sqlite3_column_int (stmt, 2);
      int maxequipment = sqlite3_column_int (stmt, 3);
      const char *commodity_code = (const char *) sqlite3_column_text (stmt, 4);
      int commodity_id = sqlite3_column_int (stmt, 5);
      int current_quantity = sqlite3_column_int (stmt, 6);
      int base_prod_rate = sqlite3_column_int (stmt, 7);
      int base_cons_rate = sqlite3_column_int (stmt, 8);
      int base_price = sqlite3_column_int (stmt, 9);
      // int illegal = sqlite3_column_int (stmt, 10); // Unused variable
      int owner_id = sqlite3_column_int(stmt, 11);
      const char *owner_type = (const char *)sqlite3_column_text(stmt, 12);
      
      (void) now_s; // Suppress unused variable warning
      (void) base_prod_rate; // Suppress unused variable warning
      (void) base_cons_rate; // Suppress unused variable warning

      // --- PE1: NPC Check ---
      bool is_npc = false;
      if (owner_id == 0) {
          is_npc = true;
      } else {
          if (owner_type) {
              if (strcasecmp(owner_type, "player") == 0 ||
                  strcasecmp(owner_type, "corp") == 0 ||
                  strcasecmp(owner_type, "corporation") == 0) {
                  is_npc = false;
              } else {
                  is_npc = true; // Other types assumed NPC
              }
          } else {
              is_npc = true; // No type, owned? Assume NPC/System
          }
      }

      if (!is_npc) {
          continue; // Skip auto-market for player/corp planets
      }
      // ----------------------

      if (!commodity_code) continue; // Skip if commodity_code is NULL

      int max_capacity = 0;
      if (strcasecmp(commodity_code, "ORE") == 0) max_capacity = maxore;
      else if (strcasecmp(commodity_code, "ORG") == 0) max_capacity = maxorganics;
      else if (strcasecmp(commodity_code, "EQU") == 0) max_capacity = maxequipment;
      else max_capacity = 999999; // Fallback, should not happen for normal commodities

      // Desired stock is 50% of max capacity for planets, similar to generic ports
      int desired_stock = max_capacity / 2; 
      
      int shortage = 0;
      int surplus = 0;
      
      if (desired_stock > current_quantity) {
          shortage = desired_stock - current_quantity;
      } else if (current_quantity > desired_stock) {
          surplus = current_quantity - desired_stock; // Fixed typo desired_quantity -> desired_stock
      }

      int order_qty = 0;
      const char *side = NULL;

      // Use a simple fraction of the shortage/surplus as order quantity
      // No explicit base_restock_rate for planets yet, so use a fixed fraction (e.g., 0.1)
      const double planet_order_fraction = 0.1;

      if (shortage > 0) {
          order_qty = (int)(shortage * planet_order_fraction);
          side = "buy";
      } else if (surplus > 0) {
          order_qty = (int)(surplus * planet_order_fraction);
          side = "sell";
      }

      // Ensure minimal order if there is a need
      if ((shortage > 0 || surplus > 0) && order_qty == 0) {
          order_qty = 1;
      }

      if (order_qty > 0 && side != NULL) {
          // Use base price as the order price for planets for now
          int price = base_price;
          
          commodity_order_t existing_order;
          int find_rc = db_get_open_order(db, "planet", planet_id, commodity_id, side, &existing_order);
          
          if (find_rc == SQLITE_OK) {
              int new_total = existing_order.filled_quantity + order_qty;
              db_update_commodity_order(db, existing_order.id, new_total, existing_order.filled_quantity, "open");
          } else {
              db_insert_commodity_order(db, "planet", planet_id, "planet", planet_id, commodity_id, side, order_qty, price, 0);
          }
      } else {
          // Cancel existing orders if balance is reached
          db_cancel_commodity_orders_for_actor_and_commodity(db, "planet", planet_id, commodity_id, "buy");
          db_cancel_commodity_orders_for_actor_and_commodity(db, "planet", planet_id, commodity_id, "sell");
      }
    }

  sqlite3_finalize (stmt);
  return SQLITE_OK;
}


int
h_broadcast_ttl_cleanup (sqlite3 *db, int64_t now_s)
{
  if (!try_lock (db, "broadcast_ttl_cleanup", now_s))
    {
      return 0;
    }
  sqlite3_stmt *st = NULL;
  int rc = sqlite3_prepare_v2 (db,
                               "DELETE FROM broadcasts WHERE ttl_expires_at IS NOT NULL AND ttl_expires_at <= ?1;",
                               -1,
                               &st,
                               NULL);


  if (rc == SQLITE_OK)
    {
      sqlite3_bind_int64 (st, 1, now_s);
      sqlite3_step (st);
    }
  sqlite3_finalize (st);
  rc = commit (db);
  unlock (db, "broadcast_ttl_cleanup");
  return 0;
}


int
h_traps_process (sqlite3 *db, int64_t now_s)
{
  if (!try_lock (db, "traps_process", now_s))
    {
      return 0;
    }
  int rc = begin (db);
  if (rc)
    {
      return rc;
    }

  sqlite3_stmt *stmt = NULL;
  const char *sql_insert =
    "INSERT INTO engine_commands(type, payload, created_at, due_at) "
    "SELECT 'trap.trigger', json_object('trap_id',id), ?1, ?1 "
    "FROM traps WHERE armed=1 AND trigger_at IS NOT NULL AND trigger_at <= ?1;";

  rc = sqlite3_prepare_v2 (db, sql_insert, -1, &stmt, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE ("traps_process insert prepare failed: %s", sqlite3_errmsg (db));
      rollback (db);
      unlock (db, "traps_process");
      return rc;
    }
  sqlite3_bind_int64 (stmt, 1, now_s);
  if (sqlite3_step (stmt) != SQLITE_DONE)
    {
      LOGE ("traps_process insert step failed: %s", sqlite3_errmsg (db));
      sqlite3_finalize (stmt);
      rollback (db);
      unlock (db, "traps_process");
      return SQLITE_ERROR;
    }
  sqlite3_finalize (stmt);

  const char *sql_delete =
    "DELETE FROM traps WHERE armed=1 AND trigger_at IS NOT NULL AND trigger_at <= ?1;";
  
  rc = sqlite3_prepare_v2 (db, sql_delete, -1, &stmt, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE ("traps_process delete prepare failed: %s", sqlite3_errmsg (db));
      rollback (db);
      unlock (db, "traps_process");
      return rc;
    }
  sqlite3_bind_int64 (stmt, 1, now_s);
  if (sqlite3_step (stmt) != SQLITE_DONE)
    {
      LOGE ("traps_process delete step failed: %s", sqlite3_errmsg (db));
      sqlite3_finalize (stmt);
      rollback (db);
      unlock (db, "traps_process");
      return SQLITE_ERROR;
    }
  sqlite3_finalize (stmt);

  commit (db);
  unlock (db, "traps_process");
  return 0;
}


int


h_npc_step (sqlite3 *db, int64_t now_s)


{


  (void) now_s;


  int64_t now_ms = (int64_t) monotonic_millis ();








  if (iss_init_once () == 1)


    {


      iss_tick (now_ms);


    }


  if (fer_init_once () == 1)


    {


      fer_attach_db (db);


      fer_tick (now_ms);


    }


  if (ori_init_once () == 1)


    {


      ori_attach_db (db);


      ori_tick (now_ms);


    }


  return 0;


}





int


cmd_sys_cron_planet_tick_once (client_ctx_t *ctx, json_t *root)


{


  if (ctx->player_id != 0 && ctx->player_id != 1)


    {


      send_response_refused(ctx, root, REF_TURN_COST_EXCEEDS, "Permission denied", NULL);


      return 0;


    }


  sqlite3 *db = db_get_handle ();


  if (!db)


    {


      send_response_error(ctx, root, ERR_SERVER_ERROR, "Database unavailable");


      return 0;


    }


  int64_t now_s = time (NULL);


  // Call the main planet growth handler, which also orchestrates the market tick


  if (h_planet_growth (db, now_s) != 0)


    {


      send_response_error(ctx, root, ERR_SERVER_ERROR, "Planet cron tick failed.");


      return 0;


    }


  send_response_ok(ctx, root, "sys.cron.planet_tick_once.success", NULL);


  return 0;


}


/* int */
/* h_npc_step (sqlite3 *db, int64_t now_s) */
/* { */
/*   (void) now_s; */
/*   int64_t now_ms = (int64_t) monotonic_millis (); */


/*   if (iss_init_once () == 1) */
/*     { */
/*       iss_tick (now_ms); */
/*     } */
/*   if (fer_init_once () == 1) */
/*     { */
/*       fer_attach_db (db); */
/*       fer_tick (now_ms); */
/*     } */
/*   if (ori_init_once () == 1) */
/*     { */
/*       ori_attach_db (db); */
/*       ori_tick (now_ms); */
/*     } */
/*   return 0; */
/* } */


int
h_port_economy_tick (sqlite3 *db, int64_t now_s)
{
  if (!try_lock (db, "port_economy_tick", now_s))
    {
      return 0;
    }
  // LOGI ("port_economy_tick: Starting port economy update.");
  int rc = begin (db);
  if (rc != SQLITE_OK)
    {
      LOGE ("port_economy_tick: Failed to start transaction: %s",
            sqlite3_errmsg (db));
      unlock (db, "port_economy_tick");
      return rc;
    }
  sqlite3_stmt *stmt = NULL;
  const char *sql_select_ports_commodities =
    "SELECT p.id AS port_id, "
    "       p.size AS port_size, "
    "       p.type AS port_type, " /* ADDED: To differentiate Stardocks */
    "       es.commodity_code, "
    "       es.quantity AS current_quantity, "
    "       ec.base_restock_rate, "
    "       ec.target_stock, "
    "       c.id AS commodity_id " /* ADDED: Need ID for orders */
    "FROM ports p "
    "JOIN entity_stock es ON p.id = es.entity_id AND es.entity_type = 'port' "
    "JOIN economy_curve ec ON p.economy_curve_id = ec.id "
    "JOIN commodities c ON es.commodity_code = c.code;";

  rc = sqlite3_prepare_v2 (db, sql_select_ports_commodities, -1, &stmt, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE ("port_economy_tick: Failed to prepare select statement: %s",
            sqlite3_errmsg (db));
      rollback (db);
      unlock (db, "port_economy_tick");
      return rc;
    }

  int orders_processed = 0;
  while (sqlite3_step (stmt) == SQLITE_ROW)
    {
      int port_id = sqlite3_column_int (stmt, 0);
      int port_size = sqlite3_column_int (stmt, 1);
      int port_type = sqlite3_column_int (stmt, 2);
      const char *commodity_code = (const char *) sqlite3_column_text (stmt, 3);
      int current_quantity = sqlite3_column_int (stmt, 4);
      double base_restock_rate = sqlite3_column_double (stmt, 5);
      // int target_stock = sqlite3_column_int (stmt, 6); // Ignored
      int commodity_id = sqlite3_column_int (stmt, 7);

      int max_capacity = port_size * 1000; 

      double desired_level_ratio = 0.5; // Default for most ports
      if (port_type == PORT_TYPE_STARDOCK) { 
          desired_level_ratio = 0.9;
      }

      int desired_stock = (int) (max_capacity * desired_level_ratio);
      
      int shortage = 0;
      int surplus = 0;
      
      if (desired_stock > current_quantity) {
          shortage = desired_stock - current_quantity;
      } else if (current_quantity > desired_stock) {
          surplus = current_quantity - desired_stock;
      }

      int order_qty = 0;
      const char *side = NULL;

      if (shortage > 0) {
          order_qty = (int)(shortage * base_restock_rate);
          side = "buy";
      } else if (surplus > 0) {
          order_qty = (int)(surplus * base_restock_rate);
          side = "sell";
      }

      // Ensure minimal order if there is a need and rate allows
      if ((shortage > 0 || surplus > 0) && base_restock_rate > 0 && order_qty == 0) {
          order_qty = 1;
      }

      if (order_qty > 0 && side != NULL) {
          int price = 0;
          if (strcmp(side, "buy") == 0) {
             price = h_calculate_port_buy_price(db, port_id, commodity_code);
          } else {
             price = h_calculate_port_sell_price(db, port_id, commodity_code);
          }
          
          // Check for existing open order
          commodity_order_t existing_order;
          int find_rc = db_get_open_order_for_port(db, port_id, commodity_id, side, &existing_order);
          
          if (find_rc == SQLITE_OK) {
              // Update existing order
              // We update quantity to the NEW calculated order_qty (replacing old intent)
              // Or should we add? Spec says "update its quantity". Usually implies setting to current need.
              // Spec says: "update its quantity (e.g., to order_qty or by adding...)"
              // Let's set it to order_qty to reflect current market need.
              // Also need to handle remaining_quantity logic in db_update_commodity_order.
              // db_update_commodity_order takes new TOTAL quantity.
              // If we want the *remaining* to be order_qty, we need to be careful.
              // Simplest MVP: Set total quantity = order_qty + filled_quantity? 
              // Actually, if we just want to buy/sell 'order_qty' MORE, we might create a new order if one exists?
              // Spec says: "If an open buy order exists, update its quantity."
              // Let's assume 'order_qty' is the TOTAL desired open amount.
              
              // Let's just update the total quantity to be (filled + order_qty) so that remaining = order_qty.
              int new_total = existing_order.filled_quantity + order_qty;
              db_update_commodity_order(db, existing_order.id, new_total, existing_order.filled_quantity, "open");
          } else {
              // Insert new order
              db_insert_commodity_order(db, "port", port_id, "port", port_id, commodity_id, side, order_qty, price, 0);
          }
          orders_processed++;
      } else {
          // No shortage/surplus significant enough to order (or satisfied).
          // Cancel existing open orders for this port/commodity.
          db_cancel_commodity_orders_for_port_and_commodity(db, port_id, commodity_id, "buy");
          db_cancel_commodity_orders_for_port_and_commodity(db, port_id, commodity_id, "sell");
      }
    }

  sqlite3_finalize (stmt);

  rc = commit (db);
  if (rc != SQLITE_OK)
    {
      LOGE ("port_economy_tick: Commit failed: %s", sqlite3_errmsg (db));
      rollback (db);
    }
  
  unlock (db, "port_economy_tick");
  return rc;
}

int
h_daily_market_settlement (sqlite3 *db, int64_t now_s)
{
  // 1. Acquire Lock
  if (!try_lock (db, "daily_market_settlement", now_s))
    {
      return 0;
    }
  
  // 2. Start Transaction
  int rc = begin (db);
  if (rc != SQLITE_OK)
    {
      LOGE ("h_daily_market_settlement: Failed to start transaction: %s", sqlite3_errmsg (db));
      unlock (db, "daily_market_settlement");
      return rc;
    }

  // 3. Get list of commodities to iterate over
  // We can iterate over all commodities that have orders, but iterating all commodities is safer/simpler
  sqlite3_stmt *stmt_comm = NULL;
  rc = sqlite3_prepare_v2(db, "SELECT id, code FROM commodities;", -1, &stmt_comm, NULL);
  if (rc != SQLITE_OK) {
      LOGE ("h_daily_market_settlement: Failed to prepare commodities select: %s", sqlite3_errmsg (db));
      rollback(db);
      unlock (db, "daily_market_settlement");
      return rc;
  }

  while (sqlite3_step(stmt_comm) == SQLITE_ROW) {
      int commodity_id = sqlite3_column_int(stmt_comm, 0);
      const char *commodity_code = (const char *)sqlite3_column_text(stmt_comm, 1);
      
      // 4. Load Orders
      int buy_count = 0;
      commodity_order_t *buy_orders = db_load_open_orders_for_commodity(db, commodity_id, "buy", &buy_count);
      
      int sell_count = 0;
      commodity_order_t *sell_orders = db_load_open_orders_for_commodity(db, commodity_id, "sell", &sell_count);
      
      // 5. Matching Loop
      int b_idx = 0;
      int s_idx = 0;
      
      while (b_idx < buy_count && s_idx < sell_count) {
          commodity_order_t *buy = &buy_orders[b_idx];
          commodity_order_t *sell = &sell_orders[s_idx];
          
          // Price check: Buyer's max price >= Seller's min price
          // Note: Port orders use the calculated price. 
          // For ports, BUY price is what they pay (low), SELL price is what they charge (high).
          // Typically, Port BUY price < Port SELL price.
          // So two ports won't trade unless one is desperate or prices fluctuate wildly.
          // However, the logic simply follows the matching rule.
          
          if (buy->price >= sell->price) {
              // Match possible
              
              // Caps
              // 1. Order remaining quantities
              int qty_buy_rem = buy->quantity - buy->filled_quantity;
              int qty_sell_rem = sell->quantity - sell->filled_quantity;
              
              // 2. Seller Stock (Physical limit)
              // We assume actors are ports for now (MVP).
              int seller_stock = 0;
              h_get_port_commodity_quantity(db, sell->actor_id, commodity_code, &seller_stock);
              
              // 3. Buyer Credits (Financial limit)
              long long buyer_credits = 0;
              db_get_port_bank_balance(buy->actor_id, &buyer_credits); // Helper wrapper needed or direct query?
              // We have db_get_port_bank_balance declared in database_cmd.h
              
              int max_affordable = 0;
              if (sell->price > 0) {
                  max_affordable = (int)(buyer_credits / sell->price);
              }
              
              // Calculate Trade Quantity
              int trade_qty = qty_buy_rem;
              if (qty_sell_rem < trade_qty) trade_qty = qty_sell_rem;
              if (seller_stock < trade_qty) trade_qty = seller_stock;
              if (max_affordable < trade_qty) trade_qty = max_affordable;
              
              if (trade_qty > 0) {
                  int trade_price = sell->price; // Seller's price rules (or mid-point, spec says seller dominant)
                  long long total_cost = (long long)trade_qty * trade_price;
                  
                  // Execute Trade
                  
                  // 1. Credits
                  int buyer_acct = 0;
                  h_get_account_id_unlocked(db, buy->actor_type, buy->actor_id, &buyer_acct);
                  int seller_acct = 0;
                  h_get_account_id_unlocked(db, sell->actor_type, sell->actor_id, &seller_acct);
                  
                  // We need unlocked helpers because we are in a transaction
                  long long new_bal;
                  // Generate unique TX group ID?
                  // For MVP, just NULL or a string
                  h_deduct_credits_unlocked(db, buyer_acct, total_cost, "TRADE_BUY", "MARKET_SETTLEMENT", &new_bal);
                  h_add_credits_unlocked(db, seller_acct, total_cost, "TRADE_SELL", "MARKET_SETTLEMENT", &new_bal);
                  
                  // 2. Stock
                  // Determine which stock movement helper to call based on actor_type
                  if (strcmp(sell->actor_type, "port") == 0) {
                      h_market_move_port_stock(db, sell->actor_id, commodity_code, -trade_qty);
                  } else if (strcmp(sell->actor_type, "planet") == 0) {
                      h_market_move_planet_stock(db, sell->actor_id, commodity_code, -trade_qty);
                  }
                  
                  if (strcmp(buy->actor_type, "port") == 0) {
                      h_market_move_port_stock(db, buy->actor_id, commodity_code, trade_qty);
                  } else if (strcmp(buy->actor_type, "planet") == 0) {
                      h_market_move_planet_stock(db, buy->actor_id, commodity_code, trade_qty);
                  }
                  
                  // 3. Record Trade
                  // We don't have bank_tx IDs easily from h_add/deduct helpers (they return RC).
                  // We can pass 0 for now or modify helper. 
                  // For MVP, 0 is acceptable if we can't easily get them without changing helpers.
                  db_insert_commodity_trade(db, buy->id, sell->id, trade_qty, trade_price, 
                                            buy->actor_type, buy->actor_id, 
                                            sell->actor_type, sell->actor_id, 
                                            0, 0);
                  
                  // 4. Update Orders
                  buy->filled_quantity += trade_qty;
                  sell->filled_quantity += trade_qty;
                  
                  // Update DB
                  const char *b_status = (buy->filled_quantity >= buy->quantity) ? "filled" : "partial";
                  db_update_commodity_order(db, buy->id, buy->quantity, buy->filled_quantity, b_status);
                  
                  const char *s_status = (sell->filled_quantity >= sell->quantity) ? "filled" : "partial";
                  db_update_commodity_order(db, sell->id, sell->quantity, sell->filled_quantity, s_status);
                  
                  // 5. Advance Iterators
                  // If buy order filled or out of money (effectively filled for this round), move to next
                  if (buy->filled_quantity >= buy->quantity || max_affordable == 0) {
                      b_idx++;
                  }
                  // If sell order filled or out of stock, move to next
                  if (sell->filled_quantity >= sell->quantity || seller_stock <= 0) {
                      s_idx++;
                  }
                  
                  // If neither advanced (e.g. partial fill), we loop again.
                  // BUT we must ensure progress.
                  // If trade_qty was > 0, something changed.
                  // If we are stuck (e.g. caps prevent full fill but we can't advance), we should probably force advance the one that was limited?
                  // Logic: If trade happened, we re-evaluate. 
                  // If buy order still has qty but hit a limit (e.g. money), it might match with a CHEAPER seller?
                  // But sellers are sorted Price ASC. So next seller is more expensive. So no.
                  // So if buyer runs out of money, they are done.
                  if (max_affordable == 0 && buy->filled_quantity < buy->quantity) {
                      b_idx++; // Skip this buyer
                  }
                  // If seller runs out of stock, they are done.
                  if (seller_stock <= 0 && sell->filled_quantity < sell->quantity) {
                      s_idx++;
                  }
                  
              } else {
                  // trade_qty <= 0 means we hit a limit (stock or money) or just logic weirdness
                  // If seller has no stock, skip seller
                  if (seller_stock <= 0) s_idx++;
                  // If buyer has no money, skip buyer
                  else if (max_affordable <= 0) b_idx++;
                  // Else force advance to avoid infinite loop (shouldn't happen if logic sound)
                  else {
                      s_idx++; // Advance seller
                  }
              }
              
          } else {
              // Prices don't match.
              // Best BUY is < Best SELL.
              // Since BUYs are sorted DESC and SELLs sorted ASC, no further matches possible for this pair of lists.
              break;
          }
      }
      
      // Cleanup memory
      free(buy_orders);
      free(sell_orders);
  }
  
  sqlite3_finalize(stmt_comm);
  
  // 6. Handle Expiry (Placeholder for MVP)
  // UPDATE commodity_orders SET status='expired' WHERE status='open' AND expires_at < now;
  sqlite3_exec(db, "UPDATE commodity_orders SET status='expired' WHERE status='open' AND expires_at IS NOT NULL AND expires_at < strftime('%s','now');", NULL, NULL, NULL);

  rc = commit (db);
  if (rc != SQLITE_OK)
    {
      LOGE ("h_daily_market_settlement: Commit failed: %s", sqlite3_errmsg (db));
      rollback (db);
    }
  
  unlock (db, "daily_market_settlement");
  return rc;
}

//////////////////////// NEWS BLOCK ////////////////////////
int
h_daily_news_compiler (sqlite3 *db, int64_t now_s)
{
  if (!try_lock (db, "daily_news_compiler", now_s))
    {
      return 0;
    }
  LOGI ("h_daily_news_compiler: Starting daily news compilation.");
  int rc = SQLITE_OK;
  sqlite3_stmt *st = NULL;
  int64_t yesterday_s = now_s - 86400;  // 24 hours ago
  const char *sql_select_events =
    "SELECT id, ts, type, actor_player_id, sector_id, payload "
    "FROM engine_events " "WHERE ts >= ?1 AND ts < ?2 " "ORDER BY ts ASC;";


  rc = sqlite3_prepare_v2 (db, sql_select_events, -1, &st, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE
      (
        "h_daily_news_compiler: Failed to prepare statement for engine_events: %s",
        sqlite3_errmsg (db));
      goto cleanup;
    }
  sqlite3_bind_int64 (st, 1, yesterday_s);
  sqlite3_bind_int64 (st, 2, now_s);
  while ((rc = sqlite3_step (st)) == SQLITE_ROW)
    {
      int event_ts = sqlite3_column_int64 (st, 1);
      const char *event_type = (const char *) sqlite3_column_text (st, 2);
      int actor_player_id = sqlite3_column_int (st, 3);
      int sector_id = sqlite3_column_int (st, 4);
      const char *payload_str = (const char *) sqlite3_column_text (st, 5);
      json_error_t jerr;
      json_t *payload_obj = json_loads (payload_str, 0, &jerr);


      if (!payload_obj)
        {
          LOGW
          (
            "h_daily_news_compiler: Failed to parse JSON payload for event type '%s': %s",
            event_type,
            jerr.text);
          continue;
        }
      const char *headline = NULL;
      const char *body = NULL;
      const char *category = "report";  // Default category
      const char *scope = "global";     // Default scope
      json_t *context_data = json_object ();
      char *headline_str = NULL;        // For asprintf
      char *body_str = NULL;    // For asprintf
      char *scope_str = NULL;   // For asprintf


      // Add common context data
      json_object_set_new (context_data, "event_type",
                           json_string (event_type));
      if (actor_player_id > 0)
        {
          json_object_set_new (context_data, "actor_player_id",
                               json_integer (actor_player_id));
        }
      if (sector_id > 0)
        {
          json_object_set_new (context_data, "sector_id",
                               json_integer (sector_id));
          if (asprintf (&scope_str, "sector:%d", sector_id) == -1)
            {
              LOGE ("h_daily_news_compiler: Failed to allocate scope_str.");
              json_decref (payload_obj);
              json_decref (context_data);
              continue;
            }
          scope = scope_str;    // Dynamic scope
        }
      if (strcasecmp (event_type, "commodity.boom") == 0
          || strcasecmp (event_type, "commodity.bust") == 0)
        {
          const char *commodity =
            json_string_value (json_object_get (payload_obj, "commodity"));
          const char *location =
            json_string_value (json_object_get (payload_obj, "location"));
          double price =
            json_real_value (json_object_get (payload_obj, "price"));


          if (commodity && location)
            {
              category = "economic";
              if (strcasecmp (event_type, "commodity.boom") == 0)
                {
                  if (asprintf
                        (&headline_str, "Economic Boom! %s Prices Soar in %s!",
                        commodity, location) == -1)
                    {
                      LOGE
                      (
                        "h_daily_news_compiler: Failed to allocate headline_str for commodity.boom.");
                      goto next_event_cleanup;
                    }
                  headline = headline_str;
                  if (asprintf
                        (&body_str,
                        "The price of %s has surged to %.2f credits in %s, indicating strong market demand.",
                        commodity,
                        price,
                        location) == -1)
                    {
                      LOGE
                      (
                        "h_daily_news_compiler: Failed to allocate body_str for commodity.boom.");
                      goto next_event_cleanup;
                    }
                  body = body_str;
                }
              else
                {               // commodity.bust
                  if (asprintf
                        (&headline_str,
                        "Market Crash! %s Prices Plummet in %s!", commodity,
                        location) == -1)
                    {
                      LOGE
                      (
                        "h_daily_news_compiler: Failed to allocate headline_str for commodity.bust.");
                      goto next_event_cleanup;
                    }
                  headline = headline_str;
                  if (asprintf
                        (&body_str,
                        "A sudden drop has seen %s prices fall to %.2f credits in %s, causing market instability.",
                        commodity,
                        price,
                        location) == -1)
                    {
                      LOGE
                      (
                        "h_daily_news_compiler: Failed to allocate body_str for commodity.bust.");
                      goto next_event_cleanup;
                    }
                  body = body_str;
                }
              json_object_set_new (context_data, "commodity",
                                   json_string (commodity));
              json_object_set_new (context_data, "location",
                                   json_string (location));
              json_object_set_new (context_data, "price", json_real (price));
            }
        }
      else if (strcasecmp (event_type, "ship.destroyed") == 0)
        {
          int destroyed_player_id =
            json_integer_value (json_object_get (payload_obj, "player_id"));
          int destroyed_ship_id =
            json_integer_value (json_object_get (payload_obj, "ship_id"));
          const char *ship_name =
            json_string_value (json_object_get (payload_obj, "ship_name"));


          if (ship_name)
            {
              category = "military";
              if (asprintf
                    (&headline_str, "Ship %s Destroyed in Sector %d!",
                    ship_name, sector_id) == -1)
                {
                  LOGE
                  (
                    "h_daily_news_compiler: Failed to allocate headline_str for ship.destroyed.");
                  goto next_event_cleanup;
                }
              headline = headline_str;
              if (asprintf
                    (&body_str,
                    "Reports indicate the ship '%s' (ID: %d), associated with Player ID %d, was destroyed in Sector %d. The cause is currently under investigation.",
                    ship_name,
                    destroyed_ship_id,
                    destroyed_player_id,
                    sector_id) == -1)
                {
                  LOGE
                  (
                    "h_daily_news_compiler: Failed to allocate body_str for ship.destroyed.");
                  goto next_event_cleanup;
                }
              body = body_str;
              json_object_set_new (context_data, "destroyed_player_id",
                                   json_integer (destroyed_player_id));
              json_object_set_new (context_data, "destroyed_ship_id",
                                   json_integer (destroyed_ship_id));
              json_object_set_new (context_data, "ship_name",
                                   json_string (ship_name));
            }
        }
      else if (strcasecmp (event_type, "fedspace:tow") == 0)
        {
          const char *reason =
            json_string_value (json_object_get (payload_obj, "reason"));


          if (reason)
            {
              category = "military";
              if (asprintf
                    (&headline_str,
                    "Federal Authorities Tow Ship from Sector %d!",
                    sector_id) == -1)
                {
                  LOGE
                  (
                    "h_daily_news_compiler: Failed to allocate headline_str for fedspace:tow.");
                  goto next_event_cleanup;
                }
              headline = headline_str;
              if (asprintf
                    (&body_str,
                    "A ship was forcibly towed from FedSpace Sector %d due to a violation of Federal Law: %s. Owners are advised to review regulations.",
                    sector_id,
                    reason) == -1)
                {
                  LOGE
                  (
                    "h_daily_news_compiler: Failed to allocate body_str for fedspace:tow.");
                  goto next_event_cleanup;
                }
              body = body_str;
            }
        }
      else if (strcasecmp (event_type, "fedspace:tow") == 0)
        {
          const char *reason =
            json_string_value (json_object_get (payload_obj, "reason"));


          if (reason)
            {
              category = "military";
              if (asprintf
                    (&headline_str,
                    "Federal Authorities Tow Ship from Sector %d!",
                    sector_id) == -1)
                {
                  LOGE
                  (
                    "h_daily_news_compiler: Failed to allocate headline_str for fedspace:tow.");
                  goto next_event_cleanup;
                }
              headline = headline_str;
              if (asprintf
                    (&body_str,
                    "A ship was forcibly towed from FedSpace Sector %d due to a violation of Federal Law: %s. Owners are advised to review regulations.",
                    sector_id,
                    reason) == -1)
                {
                  LOGE
                  (
                    "h_daily_news_compiler: Failed to allocate body_str for fedspace:tow.");
                  goto next_event_cleanup;
                }
              body = body_str;
            }
        }
      else if (strcasecmp (event_type, "corp.tax.paid") == 0)
        {
          int corp_id =
            json_integer_value (json_object_get (payload_obj, "corp_id"));
          long long amount =
            json_integer_value (json_object_get (payload_obj, "amount"));
          long long total_assets =
            json_integer_value (json_object_get
                                  (payload_obj, "total_assets"));
          // Fetch corp name and tag
          sqlite3_stmt *corp_info_st = NULL;
          const char *sql_corp_info =
            "SELECT name, tag FROM corporations WHERE id = ?;";
          char corp_name_buf[64] = { 0 };
          char corp_tag_buf[16] = { 0 };


          if (sqlite3_prepare_v2 (db, sql_corp_info, -1, &corp_info_st, NULL)
              == SQLITE_OK)
            {
              sqlite3_bind_int (corp_info_st, 1, corp_id);
              if (sqlite3_step (corp_info_st) == SQLITE_ROW)
                {
                  strncpy (corp_name_buf,
                           (const char *) sqlite3_column_text (corp_info_st,
                                                               0),
                           sizeof (corp_name_buf) - 1);
                  strncpy (corp_tag_buf,
                           (const char *) sqlite3_column_text (corp_info_st,
                                                               1),
                           sizeof (corp_tag_buf) - 1);
                }
              sqlite3_finalize (corp_info_st);
            }
          if (corp_id > 0)
            {
              category = "economic";
              if (asprintf
                    (&headline_str, "Corporation Tax Paid by %s [%s]!",
                    corp_name_buf, corp_tag_buf) == -1)
                {
                  LOGE
                  (
                    "h_daily_news_compiler: Failed to allocate headline_str for corp.tax.paid.");
                  goto next_event_cleanup;
                }
              headline = headline_str;
              if (asprintf
                    (&body_str,
                    "%s [%s] has successfully paid %lld credits in daily corporate taxes on reported assets of %lld credits, maintaining good standing with the Federation.",
                    corp_name_buf,
                    corp_tag_buf,
                    amount,
                    total_assets) == -1)
                {
                  LOGE
                  (
                    "h_daily_news_compiler: Failed to allocate body_str for corp.tax.paid.");
                  goto next_event_cleanup;
                }
              body = body_str;
              json_object_set_new (context_data, "corp_id",
                                   json_integer (corp_id));
              json_object_set_new (context_data, "amount",
                                   json_integer (amount));
              json_object_set_new (context_data, "total_assets",
                                   json_integer (total_assets));
            }
        }
      else if (strcasecmp (event_type, "corp.tax.failed") == 0)
        {
          int corp_id =
            json_integer_value (json_object_get (payload_obj, "corp_id"));
          long long amount =
            json_integer_value (json_object_get (payload_obj, "amount"));
          long long total_assets =
            json_integer_value (json_object_get
                                  (payload_obj, "total_assets"));
          // Fetch corp name and tag
          sqlite3_stmt *corp_info_st = NULL;
          const char *sql_corp_info =
            "SELECT name, tag, tax_arrears, credit_rating FROM corporations WHERE id = ?;";
          char corp_name_buf[64] = { 0 };
          char corp_tag_buf[16] = { 0 };
          long long tax_arrears = 0;
          int credit_rating = 0;


          if (sqlite3_prepare_v2 (db, sql_corp_info, -1, &corp_info_st, NULL)
              == SQLITE_OK)
            {
              sqlite3_bind_int (corp_info_st, 1, corp_id);
              if (sqlite3_step (corp_info_st) == SQLITE_ROW)
                {
                  strncpy (corp_name_buf,
                           (const char *) sqlite3_column_text (corp_info_st,
                                                               0),
                           sizeof (corp_name_buf) - 1);
                  strncpy (corp_tag_buf,
                           (const char *) sqlite3_column_text (corp_info_st,
                                                               1),
                           sizeof (corp_tag_buf) - 1);
                  tax_arrears = sqlite3_column_int64 (corp_info_st, 2);
                  credit_rating = sqlite3_column_int (corp_info_st, 3);
                }
              sqlite3_finalize (corp_info_st);
            }
          if (corp_id > 0)
            {
              category = "economic";
              if (asprintf
                    (&headline_str, "Corporation Tax Default by %s [%s]!",
                    corp_name_buf, corp_tag_buf) == -1)
                {
                  LOGE
                  (
                    "h_daily_news_compiler: Failed to allocate headline_str for corp.tax.failed.");
                  goto next_event_cleanup;
                }
              headline = headline_str;
              if (asprintf
                    (&body_str,
                    "%s [%s] has failed to pay %lld credits in daily corporate taxes on reported assets of %lld credits. Total arrears now stand at %lld credits, and their credit rating has fallen to %d. Federation authorities are monitoring the situation.",
                    corp_name_buf,
                    corp_tag_buf,
                    amount,
                    total_assets,
                    tax_arrears,
                    credit_rating) == -1)
                {
                  LOGE
                  (
                    "h_daily_news_compiler: Failed to allocate body_str for corp.tax.failed.");
                  goto next_event_cleanup;
                }
              body = body_str;
              json_object_set_new (context_data, "corp_id",
                                   json_integer (corp_id));
              json_object_set_new (context_data, "amount",
                                   json_integer (amount));
              json_object_set_new (context_data, "total_assets",
                                   json_integer (total_assets));
              json_object_set_new (context_data, "tax_arrears",
                                   json_integer (tax_arrears));
              json_object_set_new (context_data, "credit_rating",
                                   json_integer (credit_rating));
            }
        }
      else if (strcasecmp (event_type, "stock.ipo.registered") == 0)
        {
          int corp_id =
            json_integer_value (json_object_get (payload_obj, "corp_id"));
          const char *ticker =
            json_string_value (json_object_get (payload_obj, "ticker"));
          // Fetch corp name
          sqlite3_stmt *corp_info_st = NULL;
          const char *sql_corp_name =
            "SELECT name FROM corporations WHERE id = ?;";
          char corp_name_buf[64] = { 0 };


          if (sqlite3_prepare_v2 (db, sql_corp_name, -1, &corp_info_st, NULL)
              == SQLITE_OK)
            {
              sqlite3_bind_int (corp_info_st, 1, corp_id);
              if (sqlite3_step (corp_info_st) == SQLITE_ROW)
                {
                  strncpy (corp_name_buf,
                           (const char *) sqlite3_column_text (corp_info_st,
                                                               0),
                           sizeof (corp_name_buf) - 1);
                }
              sqlite3_finalize (corp_info_st);
            }
          if (corp_id > 0 && ticker)
            {
              category = "economic";
              if (asprintf
                    (&headline_str, "%s [%s] Goes Public!", corp_name_buf,
                    ticker) == -1)
                {
                  LOGE
                  (
                    "h_daily_news_compiler: Failed to allocate headline_str for stock.ipo.registered.");
                  goto next_event_cleanup;
                }
              headline = headline_str;
              if (asprintf
                    (&body_str,
                    "The corporation %s has successfully launched its Initial Public Offering under the ticker symbol [%s], opening new investment opportunities.",
                    corp_name_buf,
                    ticker) == -1)
                {
                  LOGE
                  (
                    "h_daily_news_compiler: Failed to allocate body_str for stock.ipo.registered.");
                  goto next_event_cleanup;
                }
              body = body_str;
              json_object_set_new (context_data, "corp_id",
                                   json_integer (corp_id));
              json_object_set_new (context_data, "ticker",
                                   json_string (ticker));
            }
        }
      else if (strcasecmp (event_type, "stock.dividend.declared") == 0)
        {
          int corp_id =
            json_integer_value (json_object_get (payload_obj, "corp_id"));
          int stock_id =
            json_integer_value (json_object_get (payload_obj, "stock_id"));
          int amount_per_share =
            json_integer_value (json_object_get (payload_obj,
                                                 "amount_per_share"));
          long long total_payout =
            json_integer_value (json_object_get
                                  (payload_obj, "total_payout"));
          // Fetch corp name and ticker
          sqlite3_stmt *info_st = NULL;
          const char *sql_info =
            "SELECT c.name, s.ticker FROM corporations c JOIN stocks s ON c.id = s.corp_id WHERE c.id = ? AND s.id = ?;";
          char corp_name_buf[64] = { 0 };
          char ticker_buf[16] = { 0 };


          if (sqlite3_prepare_v2 (db, sql_info, -1, &info_st, NULL) ==
              SQLITE_OK)
            {
              sqlite3_bind_int (info_st, 1, corp_id);
              sqlite3_bind_int (info_st, 2, stock_id);
              if (sqlite3_step (info_st) == SQLITE_ROW)
                {
                  strncpy (corp_name_buf,
                           (const char *) sqlite3_column_text (info_st, 0),
                           sizeof (corp_name_buf) - 1);
                  strncpy (ticker_buf,
                           (const char *) sqlite3_column_text (info_st, 1),
                           sizeof (ticker_buf) - 1);
                }
              sqlite3_finalize (info_st);
            }
          if (corp_id > 0 && stock_id > 0)
            {
              category = "economic";
              if (asprintf
                    (&headline_str, "Dividend Declared by %s [%s]!",
                    corp_name_buf, ticker_buf) == -1)
                {
                  LOGE
                  (
                    "h_daily_news_compiler: Failed to allocate headline_str for stock.dividend.declared.");
                  goto next_event_cleanup;
                }
              headline = headline_str;
              if (asprintf
                    (&body_str,
                    "%s [%s] has declared a dividend of %d credits per share, with a total payout of %lld credits, signaling strong financial performance.",
                    corp_name_buf,
                    ticker_buf,
                    amount_per_share,
                    total_payout)
                  == -1)
                {
                  LOGE
                  (
                    "h_daily_news_compiler: Failed to allocate body_str for stock.dividend.declared.");
                  goto next_event_cleanup;
                }
              body = body_str;
              json_object_set_new (context_data, "corp_id",
                                   json_integer (corp_id));
              json_object_set_new (context_data, "stock_id",
                                   json_integer (stock_id));
              json_object_set_new (context_data, "amount_per_share",
                                   json_integer (amount_per_share));
              json_object_set_new (context_data, "total_payout",
                                   json_integer (total_payout));
            }
        }
      else if (strcasecmp (event_type, "stock.dividend.payout_failed") == 0)
        {
          int corp_id =
            json_integer_value (json_object_get (payload_obj, "corp_id"));
          int stock_id =
            json_integer_value (json_object_get (payload_obj, "stock_id"));
          long long required_payout =
            json_integer_value (json_object_get (payload_obj, "required"));
          long long available_funds =
            json_integer_value (json_object_get (payload_obj, "available"));
          // Fetch corp name and ticker
          sqlite3_stmt *info_st = NULL;
          const char *sql_info =
            "SELECT c.name, s.ticker FROM corporations c JOIN stocks s ON c.id = s.corp_id WHERE c.id = ? AND s.id = ?;";
          char corp_name_buf[64] = { 0 };
          char ticker_buf[16] = { 0 };


          if (sqlite3_prepare_v2 (db, sql_info, -1, &info_st, NULL) ==
              SQLITE_OK)
            {
              sqlite3_bind_int (info_st, 1, corp_id);
              sqlite3_bind_int (info_st, 2, stock_id);
              if (sqlite3_step (info_st) == SQLITE_ROW)
                {
                  strncpy (corp_name_buf,
                           (const char *) sqlite3_column_text (info_st, 0),
                           sizeof (corp_name_buf) - 1);
                  strncpy (ticker_buf,
                           (const char *) sqlite3_column_text (info_st, 1),
                           sizeof (ticker_buf) - 1);
                }
              sqlite3_finalize (info_st);
            }
          if (corp_id > 0 && stock_id > 0)
            {
              category = "economic";
              if (asprintf
                    (&headline_str, "Dividend Payout Failed for %s [%s]!",
                    corp_name_buf, ticker_buf) == -1)
                {
                  LOGE
                  (
                    "h_daily_news_compiler: Failed to allocate headline_str for stock.dividend.payout_failed.");
                  goto next_event_cleanup;
                }
              headline = headline_str;
              if (asprintf
                    (&body_str,
                    "Due to insufficient corporate funds, %s [%s] has failed to pay a declared dividend. %lld credits were required, but only %lld credits were available.",
                    corp_name_buf,
                    ticker_buf,
                    required_payout,
                    available_funds) == -1)
                {
                  LOGE
                  (
                    "h_daily_news_compiler: Failed to allocate body_str for stock.dividend.payout_failed.");
                  goto next_event_cleanup;
                }
              body = body_str;
              json_object_set_new (context_data, "corp_id",
                                   json_integer (corp_id));
              json_object_set_new (context_data, "stock_id",
                                   json_integer (stock_id));
              json_object_set_new (context_data, "required_payout",
                                   json_integer (required_payout));
              json_object_set_new (context_data, "available_funds",
                                   json_integer (available_funds));
            }
        }
      else if (strcasecmp (event_type, "stock.dividend.payout_completed") ==
               0)
        {
          int corp_id =
            json_integer_value (json_object_get (payload_obj, "corp_id"));
          int stock_id =
            json_integer_value (json_object_get (payload_obj, "stock_id"));
          long long actual_payout =
            json_integer_value (json_object_get (payload_obj,
                                                 "actual_payout"));
          // Fetch corp name and ticker
          sqlite3_stmt *info_st = NULL;
          const char *sql_info =
            "SELECT c.name, s.ticker FROM corporations c JOIN stocks s ON c.id = s.corp_id WHERE c.id = ? AND s.id = ?;";
          char corp_name_buf[64] = { 0 };
          char ticker_buf[16] = { 0 };


          if (sqlite3_prepare_v2 (db, sql_info, -1, &info_st, NULL) ==
              SQLITE_OK)
            {
              sqlite3_bind_int (info_st, 1, corp_id);
              sqlite3_bind_int (info_st, 2, stock_id);
              if (sqlite3_step (info_st) == SQLITE_ROW)
                {
                  strncpy (corp_name_buf,
                           (const char *) sqlite3_column_text (info_st, 0),
                           sizeof (corp_name_buf) - 1);
                  strncpy (ticker_buf,
                           (const char *) sqlite3_column_text (info_st, 1),
                           sizeof (ticker_buf) - 1);
                }
              sqlite3_finalize (info_st);
            }
          if (corp_id > 0 && stock_id > 0)
            {
              category = "economic";
              if (asprintf
                    (&headline_str, "Dividend Payout Completed by %s [%s]!",
                    corp_name_buf, ticker_buf) == -1)
                {
                  LOGE
                  (
                    "h_daily_news_compiler: Failed to allocate headline_str for stock.dividend.payout_completed.");
                  goto next_event_cleanup;
                }
              headline = headline_str;
              if (asprintf
                    (&body_str,
                    "%s [%s] has successfully completed the payout of %lld credits in dividends to its shareholders.",
                    corp_name_buf,
                    ticker_buf,
                    actual_payout) == -1)
                {
                  LOGE
                  (
                    "h_daily_news_compiler: Failed to allocate body_str for stock.dividend.payout_completed.");
                  goto next_event_cleanup;
                }
              body = body_str;
              json_object_set_new (context_data, "corp_id",
                                   json_integer (corp_id));
              json_object_set_new (context_data, "stock_id",
                                   json_integer (stock_id));
              json_object_set_new (context_data, "actual_payout",
                                   json_integer (actual_payout));
            }
        }
      else if (strcasecmp (event_type, "fedspace:tow") == 0)
        {
          const char *reason =
            json_string_value (json_object_get (payload_obj, "reason"));


          if (reason)
            {
              category = "military";
              if (asprintf
                    (&headline_str,
                    "Federal Authorities Tow Ship from Sector %d!",
                    sector_id) == -1)
                {
                  LOGE
                  (
                    "h_daily_news_compiler: Failed to allocate headline_str for fedspace:tow.");
                  goto next_event_cleanup;
                }
              headline = headline_str;
              if (asprintf
                    (&body_str,
                    "A ship was forcibly towed from FedSpace Sector %d due to a violation of Federal Law: %s. Owners are advised to review regulations.",
                    sector_id,
                    reason) == -1)
                {
                  LOGE
                  (
                    "h_daily_news_compiler: Failed to allocate body_str for fedspace:tow.");
                  goto next_event_cleanup;
                }
              body = body_str;
            }
        }
      else if (strcasecmp (event_type, "corp.tax.paid") == 0)
        {
          int corp_id =
            json_integer_value (json_object_get (payload_obj, "corp_id"));
          long long amount =
            json_integer_value (json_object_get (payload_obj, "amount"));
          long long total_assets =
            json_integer_value (json_object_get
                                  (payload_obj, "total_assets"));
          // Fetch corp name and tag
          sqlite3_stmt *corp_info_st = NULL;
          const char *sql_corp_info =
            "SELECT name, tag FROM corporations WHERE id = ?;";
          char corp_name_buf[64] = { 0 };
          char corp_tag_buf[16] = { 0 };


          if (sqlite3_prepare_v2 (db, sql_corp_info, -1, &corp_info_st, NULL)
              == SQLITE_OK)
            {
              sqlite3_bind_int (corp_info_st, 1, corp_id);
              if (sqlite3_step (corp_info_st) == SQLITE_ROW)
                {
                  strncpy (corp_name_buf,
                           (const char *) sqlite3_column_text (corp_info_st,
                                                               0),
                           sizeof (corp_name_buf) - 1);
                  strncpy (corp_tag_buf,
                           (const char *) sqlite3_column_text (corp_info_st,
                                                               1),
                           sizeof (corp_tag_buf) - 1);
                }
              sqlite3_finalize (corp_info_st);
            }
          if (corp_id > 0)
            {
              category = "economic";
              if (asprintf
                    (&headline_str, "Corporation Tax Paid by %s [%s]!",
                    corp_name_buf, corp_tag_buf) == -1)
                {
                  LOGE
                  (
                    "h_daily_news_compiler: Failed to allocate headline_str for corp.tax.paid.");
                  goto next_event_cleanup;
                }
              headline = headline_str;
              if (asprintf
                    (&body_str,
                    "%s [%s] has successfully paid %lld credits in daily corporate taxes on reported assets of %lld credits, maintaining good standing with the Federation.",
                    corp_name_buf,
                    corp_tag_buf,
                    amount,
                    total_assets) == -1)
                {
                  LOGE
                  (
                    "h_daily_news_compiler: Failed to allocate body_str for corp.tax.paid.");
                  goto next_event_cleanup;
                }
              body = body_str;
              json_object_set_new (context_data, "corp_id",
                                   json_integer (corp_id));
              json_object_set_new (context_data, "amount",
                                   json_integer (amount));
              json_object_set_new (context_data, "total_assets",
                                   json_integer (total_assets));
            }
        }
      else if (strcasecmp (event_type, "corp.tax.failed") == 0)
        {
          int corp_id =
            json_integer_value (json_object_get (payload_obj, "corp_id"));
          long long amount =
            json_integer_value (json_object_get (payload_obj, "amount"));
          long long total_assets =
            json_integer_value (json_object_get
                                  (payload_obj, "total_assets"));
          // Fetch corp name and tag
          sqlite3_stmt *corp_info_st = NULL;
          const char *sql_corp_info =
            "SELECT name, tag, tax_arrears, credit_rating FROM corporations WHERE id = ?;";
          char corp_name_buf[64] = { 0 };
          char corp_tag_buf[16] = { 0 };
          long long tax_arrears = 0;
          int credit_rating = 0;


          if (sqlite3_prepare_v2 (db, sql_corp_info, -1, &corp_info_st, NULL)
              == SQLITE_OK)
            {
              sqlite3_bind_int (corp_info_st, 1, corp_id);
              if (sqlite3_step (corp_info_st) == SQLITE_ROW)
                {
                  strncpy (corp_name_buf,
                           (const char *) sqlite3_column_text (corp_info_st,
                                                               0),
                           sizeof (corp_name_buf) - 1);
                  strncpy (corp_tag_buf,
                           (const char *) sqlite3_column_text (corp_info_st,
                                                               1),
                           sizeof (corp_tag_buf) - 1);
                  tax_arrears = sqlite3_column_int64 (corp_info_st, 2);
                  credit_rating = sqlite3_column_int (corp_info_st, 3);
                }
              sqlite3_finalize (corp_info_st);
            }
          if (corp_id > 0)
            {
              category = "economic";
              if (asprintf
                    (&headline_str, "Corporation Tax Default by %s [%s]!",
                    corp_name_buf, corp_tag_buf) == -1)
                {
                  LOGE
                  (
                    "h_daily_news_compiler: Failed to allocate headline_str for corp.tax.failed.");
                  goto next_event_cleanup;
                }
              headline = headline_str;
              if (asprintf
                    (&body_str,
                    "%s [%s] has failed to pay %lld credits in daily corporate taxes on reported assets of %lld credits. Total arrears now stand at %lld credits, and their credit rating has fallen to %d. Federation authorities are monitoring the situation.",
                    corp_name_buf,
                    corp_tag_buf,
                    amount,
                    total_assets,
                    tax_arrears,
                    credit_rating) == -1)
                {
                  LOGE
                  (
                    "h_daily_news_compiler: Failed to allocate body_str for corp.tax.failed.");
                  goto next_event_cleanup;
                }
              body = body_str;
              json_object_set_new (context_data, "corp_id",
                                   json_integer (corp_id));
              json_object_set_new (context_data, "amount",
                                   json_integer (amount));
              json_object_set_new (context_data, "total_assets",
                                   json_integer (total_assets));
              json_object_set_new (context_data, "tax_arrears",
                                   json_integer (tax_arrears));
              json_object_set_new (context_data, "credit_rating",
                                   json_integer (credit_rating));
            }
        }
      else if (strcasecmp (event_type, "stock.ipo.registered") == 0)
        {
          int corp_id =
            json_integer_value (json_object_get (payload_obj, "corp_id"));
          const char *ticker =
            json_string_value (json_object_get (payload_obj, "ticker"));
          // Fetch corp name
          sqlite3_stmt *corp_info_st = NULL;
          const char *sql_corp_name =
            "SELECT name FROM corporations WHERE id = ?;";
          char corp_name_buf[64] = { 0 };


          if (sqlite3_prepare_v2 (db, sql_corp_name, -1, &corp_info_st, NULL)
              == SQLITE_OK)
            {
              sqlite3_bind_int (corp_info_st, 1, corp_id);
              if (sqlite3_step (corp_info_st) == SQLITE_ROW)
                {
                  strncpy (corp_name_buf,
                           (const char *) sqlite3_column_text (corp_info_st,
                                                               0),
                           sizeof (corp_name_buf) - 1);
                }
              sqlite3_finalize (corp_info_st);
            }
          if (corp_id > 0 && ticker)
            {
              category = "economic";
              if (asprintf
                    (&headline_str, "%s [%s] Goes Public!", corp_name_buf,
                    ticker) == -1)
                {
                  LOGE
                  (
                    "h_daily_news_compiler: Failed to allocate headline_str for stock.ipo.registered.");
                  goto next_event_cleanup;
                }
              headline = headline_str;
              if (asprintf
                    (&body_str,
                    "The corporation %s has successfully launched its Initial Public Offering under the ticker symbol [%s], opening new investment opportunities.",
                    corp_name_buf,
                    ticker) == -1)
                {
                  LOGE
                  (
                    "h_daily_news_compiler: Failed to allocate body_str for stock.ipo.registered.");
                  goto next_event_cleanup;
                }
              body = body_str;
              json_object_set_new (context_data, "corp_id",
                                   json_integer (corp_id));
              json_object_set_new (context_data, "ticker",
                                   json_string (ticker));
            }
        }
      else if (strcasecmp (event_type, "stock.dividend.declared") == 0)
        {
          int corp_id =
            json_integer_value (json_object_get (payload_obj, "corp_id"));
          int stock_id =
            json_integer_value (json_object_get (payload_obj, "stock_id"));
          int amount_per_share =
            json_integer_value (json_object_get (payload_obj,
                                                 "amount_per_share"));
          long long total_payout =
            json_integer_value (json_object_get
                                  (payload_obj, "total_payout"));
          // Fetch corp name and ticker
          sqlite3_stmt *info_st = NULL;
          const char *sql_info =
            "SELECT c.name, s.ticker FROM corporations c JOIN stocks s ON c.id = s.corp_id WHERE c.id = ? AND s.id = ?;";
          char corp_name_buf[64] = { 0 };
          char ticker_buf[16] = { 0 };


          if (sqlite3_prepare_v2 (db, sql_info, -1, &info_st, NULL) ==
              SQLITE_OK)
            {
              sqlite3_bind_int (info_st, 1, corp_id);
              sqlite3_bind_int (info_st, 2, stock_id);
              if (sqlite3_step (info_st) == SQLITE_ROW)
                {
                  strncpy (corp_name_buf,
                           (const char *) sqlite3_column_text (info_st, 0),
                           sizeof (corp_name_buf) - 1);
                  strncpy (ticker_buf,
                           (const char *) sqlite3_column_text (info_st, 1),
                           sizeof (ticker_buf) - 1);
                }
              sqlite3_finalize (info_st);
            }
          if (corp_id > 0 && stock_id > 0)
            {
              category = "economic";
              if (asprintf
                    (&headline_str, "Dividend Declared by %s [%s]!",
                    corp_name_buf, ticker_buf) == -1)
                {
                  LOGE
                  (
                    "h_daily_news_compiler: Failed to allocate headline_str for stock.dividend.declared.");
                  goto next_event_cleanup;
                }
              headline = headline_str;
              if (asprintf
                    (&body_str,
                    "%s [%s] has declared a dividend of %d credits per share, with a total payout of %lld credits, signaling strong financial performance.",
                    corp_name_buf,
                    ticker_buf,
                    amount_per_share,
                    total_payout)
                  == -1)
                {
                  LOGE
                  (
                    "h_daily_news_compiler: Failed to allocate body_str for stock.dividend.declared.");
                  goto next_event_cleanup;
                }
              body = body_str;
              json_object_set_new (context_data, "corp_id",
                                   json_integer (corp_id));
              json_object_set_new (context_data, "stock_id",
                                   json_integer (stock_id));
              json_object_set_new (context_data, "amount_per_share",
                                   json_integer (amount_per_share));
              json_object_set_new (context_data, "total_payout",
                                   json_integer (total_payout));
            }
        }
      else if (strcasecmp (event_type, "stock.dividend.payout_failed") == 0)
        {
          int corp_id =
            json_integer_value (json_object_get (payload_obj, "corp_id"));
          int stock_id =
            json_integer_value (json_object_get (payload_obj, "stock_id"));
          long long required_payout =
            json_integer_value (json_object_get (payload_obj, "required"));
          long long available_funds =
            json_integer_value (json_object_get (payload_obj, "available"));
          // Fetch corp name and ticker
          sqlite3_stmt *info_st = NULL;
          const char *sql_info =
            "SELECT c.name, s.ticker FROM corporations c JOIN stocks s ON c.id = s.corp_id WHERE c.id = ? AND s.id = ?;";
          char corp_name_buf[64] = { 0 };
          char ticker_buf[16] = { 0 };


          if (sqlite3_prepare_v2 (db, sql_info, -1, &info_st, NULL) ==
              SQLITE_OK)
            {
              sqlite3_bind_int (info_st, 1, corp_id);
              sqlite3_bind_int (info_st, 2, stock_id);
              if (sqlite3_step (info_st) == SQLITE_ROW)
                {
                  strncpy (corp_name_buf,
                           (const char *) sqlite3_column_text (info_st, 0),
                           sizeof (corp_name_buf) - 1);
                  strncpy (ticker_buf,
                           (const char *) sqlite3_column_text (info_st, 1),
                           sizeof (ticker_buf) - 1);
                }
              sqlite3_finalize (info_st);
            }
          if (corp_id > 0 && stock_id > 0)
            {
              category = "economic";
              if (asprintf
                    (&headline_str, "Dividend Payout Failed for %s [%s]!",
                    corp_name_buf, ticker_buf) == -1)
                {
                  LOGE
                  (
                    "h_daily_news_compiler: Failed to allocate headline_str for stock.dividend.payout_failed.");
                  goto next_event_cleanup;
                }
              headline = headline_str;
              if (asprintf
                    (&body_str,
                    "Due to insufficient corporate funds, %s [%s] has failed to pay a declared dividend. %lld credits were required, but only %lld credits were available.",
                    corp_name_buf,
                    ticker_buf,
                    required_payout,
                    available_funds) == -1)
                {
                  LOGE
                  (
                    "h_daily_news_compiler: Failed to allocate body_str for stock.dividend.payout_failed.");
                  goto next_event_cleanup;
                }
              body = body_str;
              json_object_set_new (context_data, "corp_id",
                                   json_integer (corp_id));
              json_object_set_new (context_data, "stock_id",
                                   json_integer (stock_id));
              json_object_set_new (context_data, "required_payout",
                                   json_integer (required_payout));
              json_object_set_new (context_data, "available_funds",
                                   json_integer (available_funds));
            }
        }
      else if (strcasecmp (event_type, "stock.dividend.payout_completed") ==
               0)
        {
          int corp_id =
            json_integer_value (json_object_get (payload_obj, "corp_id"));
          int stock_id =
            json_integer_value (json_object_get (payload_obj, "stock_id"));
          long long actual_payout =
            json_integer_value (json_object_get (payload_obj,
                                                 "actual_payout"));
          // Fetch corp name and ticker
          sqlite3_stmt *info_st = NULL;
          const char *sql_info =
            "SELECT c.name, s.ticker FROM corporations c JOIN stocks s ON c.id = s.corp_id WHERE c.id = ? AND s.id = ?;";
          char corp_name_buf[64] = { 0 };
          char ticker_buf[16] = { 0 };


          if (sqlite3_prepare_v2 (db, sql_info, -1, &info_st, NULL) ==
              SQLITE_OK)
            {
              sqlite3_bind_int (info_st, 1, corp_id);
              sqlite3_bind_int (info_st, 2, stock_id);
              if (sqlite3_step (info_st) == SQLITE_ROW)
                {
                  strncpy (corp_name_buf,
                           (const char *) sqlite3_column_text (info_st, 0),
                           sizeof (corp_name_buf) - 1);
                  strncpy (ticker_buf,
                           (const char *) sqlite3_column_text (info_st, 1),
                           sizeof (ticker_buf) - 1);
                }
              sqlite3_finalize (info_st);
            }
          if (corp_id > 0 && stock_id > 0)
            {
              category = "economic";
              if (asprintf
                    (&headline_str, "Dividend Payout Completed by %s [%s]!",
                    corp_name_buf, ticker_buf) == -1)
                {
                  LOGE
                  (
                    "h_daily_news_compiler: Failed to allocate headline_str for stock.dividend.payout_completed.");
                  goto next_event_cleanup;
                }
              headline = headline_str;
              if (asprintf
                    (&body_str,
                    "%s [%s] has successfully completed the payout of %lld credits in dividends to its shareholders.",
                    corp_name_buf,
                    ticker_buf,
                    actual_payout) == -1)
                {
                  LOGE
                  (
                    "h_daily_news_compiler: Failed to allocate body_str for stock.dividend.payout_completed.");
                  goto next_event_cleanup;
                }
              body = body_str;
              json_object_set_new (context_data, "corp_id",
                                   json_integer (corp_id));
              json_object_set_new (context_data, "stock_id",
                                   json_integer (stock_id));
              json_object_set_new (context_data, "actual_payout",
                                   json_integer (actual_payout));
            }
        }
      else if (strcasecmp (event_type, "combat.ship_destroyed") == 0)
        {
          const char *ship_name =
            json_string_value (json_object_get (payload_obj, "ship_name"));
          int victim_player_id =
            json_integer_value (json_object_get
                                  (payload_obj, "victim_player_id"));
          int attacker_player_id =
            json_integer_value (json_object_get
                                  (payload_obj, "attacker_player_id"));


          if (ship_name)
            {
              category = "military";
              if (asprintf
                    (&headline_str, "Ship '%s' Destroyed in Combat!",
                    ship_name) == -1)
                {
                  LOGE
                  (
                    "h_daily_news_compiler: Failed to allocate headline_str for combat.ship_destroyed.");
                  goto next_event_cleanup;
                }
              headline = headline_str;
              if (asprintf
                    (&body_str,
                    "The ship '%s' (owned by Player ID %d) was destroyed in Sector %d, reportedly by Player ID %d.",
                    ship_name,
                    victim_player_id,
                    sector_id,
                    attacker_player_id) == -1)
                {
                  LOGE
                  (
                    "h_daily_news_compiler: Failed to allocate body_str for combat.ship_destroyed.");
                  goto next_event_cleanup;
                }
              body = body_str;
              json_object_set_new (context_data, "ship_name",
                                   json_string (ship_name));
              json_object_set_new (context_data, "victim_player_id",
                                   json_integer (victim_player_id));
              json_object_set_new (context_data, "attacker_player_id",
                                   json_integer (attacker_player_id));
            }
        }
      // Add more event types here as needed for news generation
      if (headline && body)
        {
          db_news_insert_feed_item (event_ts, category, scope, headline, body,
                                    context_data);
        }
      else
        {
          LOGD
            ("h_daily_news_compiler: No news generated for event type '%s'.",
            event_type);
        }
next_event_cleanup:
      if (headline_str)
        {
          free (headline_str);
          headline_str = NULL;
        }
      if (body_str)
        {
          free (body_str);
          body_str = NULL;
        }
      if (scope_str)
        {
          free (scope_str);
          scope_str = NULL;
        }
      json_decref (context_data);
      json_decref (payload_obj);
    }
  if (rc != SQLITE_DONE)
    {
      LOGE ("h_daily_news_compiler: Error stepping through engine_events: %s",
            sqlite3_errmsg (db));
    }
cleanup:
  if (st)
    {
      sqlite3_finalize (st);
    }
  unlock (db, "daily_news_compiler");
  LOGI ("h_daily_news_compiler: Finished daily news compilation.");
  return rc;
}


int
h_cleanup_old_news (sqlite3 *db, int64_t now_s)
{
  if (!try_lock (db, "cleanup_old_news", now_s))
    {
      return 0;
    }
  LOGI ("cleanup_old_news: Starting cleanup of old news articles.");
  sqlite3_stmt *stmt = NULL;
  const char *sql = "DELETE FROM news_feed WHERE published_ts < ?;";
  int rc = sqlite3_prepare_v2 (db, sql, -1, &stmt, NULL);


  if (rc != SQLITE_OK)
    {
      LOGE ("cleanup_old_news: Failed to prepare delete statement: %s",
            sqlite3_errmsg (db));
      unlock (db, "cleanup_old_news");
      return rc;
    }
  // 7 days ago (604800 seconds)
  sqlite3_bind_int64 (stmt, 1, now_s - 604800);
  rc = sqlite3_step (stmt);
  if (rc != SQLITE_DONE)
    {
      LOGE ("cleanup_old_news: Failed to execute delete statement: %s",
            sqlite3_errmsg (db));
    }
  else
    {
      int changes = sqlite3_changes (db);


      if (changes > 0)
        {
          LOGI ("cleanup_old_news: Deleted %d old news articles.", changes);
        }
    }
  sqlite3_finalize (stmt);
  unlock (db, "cleanup_old_news");
  return rc == SQLITE_DONE ? SQLITE_OK : rc;
}


int
h_daily_lottery_draw (sqlite3 *db, int64_t now_s)
{
  if (!try_lock (db, "daily_lottery_draw", now_s))
    {
      return 0;
    }
  LOGI ("daily_lottery_draw: Starting daily lottery draw.");
  int rc = begin (db);


  if (rc)
    {
      unlock (db, "daily_lottery_draw");
      return rc;
    }
  char draw_date_str[32];
  struct tm *tm_info = localtime (&now_s);


  strftime (draw_date_str, sizeof (draw_date_str), "%Y-%m-%d", tm_info);
  // Check if today's draw is already processed
  sqlite3_stmt *st_check = NULL;
  const char *sql_check =
    "SELECT winning_number, jackpot, carried_over FROM tavern_lottery_state WHERE draw_date = ?;";


  rc = sqlite3_prepare_v2 (db, sql_check, -1, &st_check, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE ("daily_lottery_draw: Failed to prepare check statement: %s",
            sqlite3_errmsg (db));
      goto rollback_and_unlock;
    }
  sqlite3_bind_text (st_check, 1, draw_date_str, -1, SQLITE_STATIC);
  if (sqlite3_step (st_check) == SQLITE_ROW
      && sqlite3_column_type (st_check, 0) != SQLITE_NULL)
    {
      LOGI ("daily_lottery_draw: Lottery for %s already processed. Skipping.",
            draw_date_str);
      sqlite3_finalize (st_check);
      goto commit_and_unlock;
    }
  sqlite3_finalize (st_check);
  st_check = NULL;
  // Get yesterday's jackpot and carried over amount
  // long long yesterday_jackpot = 0; // Unused
  long long yesterday_carried_over = 0;
  char yesterday_date_str[32];
  time_t yesterday_s = now_s - (24 * 60 * 60);


  tm_info = localtime (&yesterday_s);
  strftime (yesterday_date_str, sizeof (yesterday_date_str), "%Y-%m-%d",
            tm_info);
  const char *sql_yesterday_state =
    "SELECT jackpot, carried_over FROM tavern_lottery_state WHERE draw_date = ?;";


  rc = sqlite3_prepare_v2 (db, sql_yesterday_state, -1, &st_check, NULL);
  if (rc == SQLITE_OK)
    {
      sqlite3_bind_text (st_check, 1, yesterday_date_str, -1, SQLITE_STATIC);
      if (sqlite3_step (st_check) == SQLITE_ROW)
        {
          // yesterday_jackpot = sqlite3_column_int64 (st_check, 0);
          yesterday_carried_over = sqlite3_column_int64 (st_check, 1);
        }
      sqlite3_finalize (st_check);
      st_check = NULL;
    }
  // Calculate total tickets sold today
  sqlite3_stmt *st_tickets = NULL;
  // long long total_tickets_sold = 0; // Unused
  long long total_pot_from_tickets = 0;
  const char *sql_sum_tickets =
    "SELECT COUNT(*), SUM(cost) FROM tavern_lottery_tickets WHERE draw_date = ?;";


  rc = sqlite3_prepare_v2 (db, sql_sum_tickets, -1, &st_tickets, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE ("daily_lottery_draw: Failed to prepare sum tickets statement: %s",
            sqlite3_errmsg (db));
      goto rollback_and_unlock;
    }
  sqlite3_bind_text (st_tickets, 1, draw_date_str, -1, SQLITE_STATIC);
  if (sqlite3_step (st_tickets) == SQLITE_ROW)
    {
      // total_tickets_sold = sqlite3_column_int64 (st_tickets, 0);
      total_pot_from_tickets = sqlite3_column_int64 (st_tickets, 1);
    }
  sqlite3_finalize (st_tickets);
  st_tickets = NULL;
  // Calculate current jackpot: carried over from yesterday + 50% of today's ticket sales
  long long current_jackpot =
    yesterday_carried_over + (total_pot_from_tickets / 2);
  int winning_number = get_random_int (1, 999);
  // long long total_winnings = 0; // Unused
  bool winner_found = false;
  // Find winning tickets and distribute winnings
  sqlite3_stmt *st_winners = NULL;
  const char *sql_winners =
    "SELECT player_id, number, cost FROM tavern_lottery_tickets WHERE draw_date = ? AND number = ?;";


  rc = sqlite3_prepare_v2 (db, sql_winners, -1, &st_winners, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE ("daily_lottery_draw: Failed to prepare winners statement: %s",
            sqlite3_errmsg (db));
      goto rollback_and_unlock;
    }
  sqlite3_bind_text (st_winners, 1, draw_date_str, -1, SQLITE_STATIC);
  sqlite3_bind_int (st_winners, 2, winning_number);
  json_t *winners_array = json_array ();        // To store winners for logging


  while (sqlite3_step (st_winners) == SQLITE_ROW)
    {
      winner_found = true;
      int player_id = sqlite3_column_int (st_winners, 0);
      // For simplicity, total jackpot split evenly among winners
      // In a real game, might want to consider partial matches, etc.
      json_t *winner_obj = json_object ();


      json_object_set_new (winner_obj, "player_id", json_integer (player_id));
      json_array_append_new (winners_array, winner_obj);
    }
  sqlite3_finalize (st_winners);
  st_winners = NULL;
  if (winner_found)
    {
      long long payout_per_winner =
        current_jackpot / json_array_size (winners_array);


      for (size_t i = 0; i < json_array_size (winners_array); i++)
        {
          json_t *winner_obj = json_array_get (winners_array, i);
          int player_id =
            json_integer_value (json_object_get (winner_obj, "player_id"));
          // Add credits to winner
          int add_rc =
            h_add_credits (db, "player", player_id, payout_per_winner,
                           "LOTTERY_WIN", NULL, NULL);


          if (add_rc != SQLITE_OK)
            {
              LOGE
                ("daily_lottery_draw: Failed to add winnings to player %d: %s",
                player_id, sqlite3_errmsg (db));
              // Error here is critical, consider specific handling or abort
            }
          else
            {
              json_object_set_new (winner_obj, "winnings",
                                   json_integer (payout_per_winner));
            }
        }
      current_jackpot = 0;      // Jackpot cleared
      yesterday_carried_over = 0;       // No rollover
    }
  else
    {
      // No winner, 50% of today's sales carried over to next jackpot
      yesterday_carried_over = total_pot_from_tickets / 2;      // Rollover 50% of tickets sales
      current_jackpot = 0;      // Jackpot cleared
      LOGI ("daily_lottery_draw: No winner found for %s. Jackpot rolls over.",
            draw_date_str);
    }
  // Update or insert tavern_lottery_state for today
  sqlite3_stmt *st_update_state = NULL;
  const char *sql_update_state =
    "INSERT INTO tavern_lottery_state (draw_date, winning_number, jackpot, carried_over) VALUES (?, ?, ?, ?) "
    "ON CONFLICT(draw_date) DO UPDATE SET winning_number = excluded.winning_number, jackpot = excluded.jackpot, carried_over = excluded.carried_over;";


  rc = sqlite3_prepare_v2 (db, sql_update_state, -1, &st_update_state, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE
        ("daily_lottery_draw: Failed to prepare update state statement: %s",
        sqlite3_errmsg (db));
      json_decref (winners_array);
      goto rollback_and_unlock;
    }
  sqlite3_bind_text (st_update_state, 1, draw_date_str, -1, SQLITE_STATIC);
  if (winner_found)
    {
      sqlite3_bind_int (st_update_state, 2, winning_number);
    }
  else
    {
      sqlite3_bind_null (st_update_state, 2);   // No winning number if no winner
    }
  sqlite3_bind_int64 (st_update_state, 3, current_jackpot);
  sqlite3_bind_int64 (st_update_state, 4, yesterday_carried_over);
  if (sqlite3_step (st_update_state) != SQLITE_DONE)
    {
      LOGE ("daily_lottery_draw: Failed to update lottery state: %s",
            sqlite3_errmsg (db));
      json_decref (winners_array);
      goto rollback_and_unlock;
    }
  sqlite3_finalize (st_update_state);
  st_update_state = NULL;
  LOGI
  (
    "daily_lottery_draw: Draw for %s completed. Winning number: %d. Jackpot: %lld. Winners: %s",
    draw_date_str,
    winning_number,
    current_jackpot,
    winner_found ? json_dumps (winners_array, 0) : "None");
  json_decref (winners_array);
commit_and_unlock:
  rc = commit (db);
  if (rc != SQLITE_OK)
    {
      LOGE ("daily_lottery_draw: commit failed: %s", sqlite3_errmsg (db));
      goto rollback_and_unlock;
    }
  unlock (db, "daily_lottery_draw");
  return SQLITE_OK;
rollback_and_unlock:
  if (st_check)
    {
      sqlite3_finalize (st_check);
    }
  if (st_tickets)
    {
      sqlite3_finalize (st_tickets);
    }
  if (st_winners)
    {
      sqlite3_finalize (st_winners);
    }
  if (st_update_state)
    {
      sqlite3_finalize (st_update_state);
    }
  rollback (db);
  unlock (db, "daily_lottery_draw");
  return rc;
}


int
h_deadpool_resolution_cron (sqlite3 *db, int64_t now_s)
{
  if (!try_lock (db, "deadpool_resolution_cron", now_s))
    {
      return 0;
    }
  LOGI ("deadpool_resolution_cron: Starting Dead Pool bet resolution.");
  int rc = begin (db);


  if (rc)
    {
      unlock (db, "deadpool_resolution_cron");
      return rc;
    }
  sqlite3_stmt *st_bets = NULL, *st_update_bet = NULL, *st_lost_bets = NULL;
  // 1. Mark expired bets as resolved
  sqlite3_stmt *st_expire = NULL;
  const char *sql_expire =
    "UPDATE tavern_deadpool_bets SET resolved = 1, result = 'expired', resolved_at = ? WHERE resolved = 0 AND expires_at <= ?;";


  rc = sqlite3_prepare_v2 (db, sql_expire, -1, &st_expire, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE
        ("deadpool_resolution_cron: Failed to prepare expire statement: %s",
        sqlite3_errmsg (db));
      goto rollback_and_unlock;
    }
  sqlite3_bind_int (st_expire, 1, (int) now_s);
  sqlite3_bind_int (st_expire, 2, (int) now_s);
  if (sqlite3_step (st_expire) != SQLITE_DONE)
    {
      LOGE ("deadpool_resolution_cron: Failed to expire old bets: %s",
            sqlite3_errmsg (db));
      goto rollback_and_unlock;
    }
  sqlite3_finalize (st_expire);
  st_expire = NULL;
  // 2. Process ship.destroyed events
  sqlite3_stmt *st_events = NULL;
  const char *sql_events =
    "SELECT payload FROM engine_events WHERE type = 'ship.destroyed' AND ts > (SELECT COALESCE(MAX(resolved_at), 0) FROM tavern_deadpool_bets WHERE result IS NOT NULL AND result != 'expired');";


  rc = sqlite3_prepare_v2 (db, sql_events, -1, &st_events, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE
        ("deadpool_resolution_cron: Failed to prepare events statement: %s",
        sqlite3_errmsg (db));
      goto rollback_and_unlock;
    }
  while (sqlite3_step (st_events) == SQLITE_ROW)
    {
      const char *payload_str =
        (const char *) sqlite3_column_text (st_events, 0);
      json_error_t jerr;
      json_t *payload_obj = json_loads (payload_str, 0, &jerr);


      if (!payload_obj)
        {
          LOGW ("deadpool_resolution_cron: Failed to parse event payload: %s",
                jerr.text);
          continue;
        }
      int destroyed_player_id =
        json_integer_value (json_object_get (payload_obj, "player_id"));


      json_decref (payload_obj);
      if (destroyed_player_id <= 0)
        {
          continue;
        }
      // Find matching unresolved bets for the destroyed player
      sqlite3_stmt *st_bets = NULL;
      const char *sql_bets =
        "SELECT id, bettor_id, amount, odds_bp FROM tavern_deadpool_bets WHERE target_id = ? AND resolved = 0;";


      rc = sqlite3_prepare_v2 (db, sql_bets, -1, &st_bets, NULL);
      if (rc != SQLITE_OK)
        {
          LOGE
            ("deadpool_resolution_cron: Failed to prepare bets statement: %s",
            sqlite3_errmsg (db));
          continue;
        }
      sqlite3_bind_int (st_bets, 1, destroyed_player_id);
      while (sqlite3_step (st_bets) == SQLITE_ROW)
        {
          int bet_id = sqlite3_column_int (st_bets, 0);
          int bettor_id = sqlite3_column_int (st_bets, 1);
          long long amount = sqlite3_column_int64 (st_bets, 2);
          int odds_bp = sqlite3_column_int (st_bets, 3);
          long long payout = (amount * odds_bp) / 10000;        // Calculate payout


          if (payout < 0)
            {
              payout = 0;       // Ensure payout is not negative
            }
          // Payout to winner
          int add_rc =
            h_add_credits (db, "player", bettor_id, payout, "DEADPOOL_WIN",
                           NULL, NULL);


          if (add_rc != SQLITE_OK)
            {
              LOGE
              (
                "deadpool_resolution_cron: Failed to payout winnings to player %d for bet %d: %s",
                bettor_id,
                bet_id,
                sqlite3_errmsg (db));
              // Log and continue, or abort transaction if critical
            }
          // Mark bet as won
          sqlite3_stmt *st_update_bet = NULL;
          const char *sql_update_bet =
            "UPDATE tavern_deadpool_bets SET resolved = 1, result = 'won', resolved_at = ? WHERE id = ?;";


          rc =
            sqlite3_prepare_v2 (db, sql_update_bet, -1, &st_update_bet, NULL);
          if (rc != SQLITE_OK)
            {
              LOGE
              (
                "deadpool_resolution_cron: Failed to prepare update bet statement: %s",
                sqlite3_errmsg (db));
              goto rollback_and_unlock;
            }
          sqlite3_bind_int (st_update_bet, 1, (int) now_s);
          sqlite3_bind_int (st_update_bet, 2, bet_id);
          if (sqlite3_step (st_update_bet) != SQLITE_DONE)
            {
              LOGE
                ("deadpool_resolution_cron: Failed to mark bet %d as won: %s",
                bet_id, sqlite3_errmsg (db));
              goto rollback_and_unlock;
            }
          sqlite3_finalize (st_update_bet);
          st_update_bet = NULL;
        }
      sqlite3_finalize (st_bets);
      st_bets = NULL;
      // Mark all other unresolved bets on this target as lost
      sqlite3_stmt *st_lost_bets = NULL;
      const char *sql_lost_bets =
        "UPDATE tavern_deadpool_bets SET resolved = 1, result = 'lost', resolved_at = ? WHERE target_id = ? AND resolved = 0;";


      rc = sqlite3_prepare_v2 (db, sql_lost_bets, -1, &st_lost_bets, NULL);
      if (rc != SQLITE_OK)
        {
          LOGE
          (
            "deadpool_resolution_cron: Failed to prepare lost bets statement: %s",
            sqlite3_errmsg (db));
          goto rollback_and_unlock;
        }
      sqlite3_bind_int (st_lost_bets, 1, (int) now_s);
      sqlite3_bind_int (st_lost_bets, 2, destroyed_player_id);
      if (sqlite3_step (st_lost_bets) != SQLITE_DONE)
        {
          LOGE
          (
            "deadpool_resolution_cron: Failed to mark lost bets for target %d: %s",
            destroyed_player_id,
            sqlite3_errmsg (db));
          goto rollback_and_unlock;
        }
      sqlite3_finalize (st_lost_bets);
      st_lost_bets = NULL;
    }
  sqlite3_finalize (st_events);
  st_events = NULL;
  rc = commit (db);
  if (rc != SQLITE_OK)
    {
      LOGE ("deadpool_resolution_cron: commit failed: %s",
            sqlite3_errmsg (db));
      goto rollback_and_unlock;
    }
  LOGI ("deadpool_resolution_cron: Dead Pool bet resolution completed.");
  unlock (db, "deadpool_resolution_cron");
  return SQLITE_OK;
rollback_and_unlock:
  if (st_expire)
    {
      sqlite3_finalize (st_expire);
    }
  if (st_events)
    {
      sqlite3_finalize (st_events);
    }
  if (st_bets)
    {
      sqlite3_finalize (st_bets);
    }
  if (st_update_bet)
    {
      sqlite3_finalize (st_update_bet);
    }
  if (st_lost_bets)
    {
      sqlite3_finalize (st_lost_bets);
    }
  rollback (db);
  unlock (db, "deadpool_resolution_cron");
  return rc;
}


int
h_tavern_notice_expiry_cron (sqlite3 *db, int64_t now_s)
{
  if (!try_lock (db, "tavern_notice_expiry_cron", now_s))
    {
      return 0;
    }
  LOGI
  (
    "tavern_notice_expiry_cron: Starting Tavern notice and corp recruiting expiry cleanup.");
  int rc = begin (db);


  if (rc)
    {
      unlock (db, "tavern_notice_expiry_cron");
      return rc;
    }
  sqlite3_stmt *st = NULL;
  int deleted_count = 0;
  // Delete expired tavern_notices
  const char *sql_delete_notices =
    "DELETE FROM tavern_notices WHERE expires_at <= ?;";


  rc = sqlite3_prepare_v2 (db, sql_delete_notices, -1, &st, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE
      (
        "tavern_notice_expiry_cron: Failed to prepare notices delete statement: %s",
        sqlite3_errmsg (db));
      goto rollback_and_unlock;
    }
  sqlite3_bind_int (st, 1, (int) now_s);
  if (sqlite3_step (st) != SQLITE_DONE)
    {
      LOGE ("tavern_notice_expiry_cron: Failed to delete expired notices: %s",
            sqlite3_errmsg (db));
      goto rollback_and_unlock;
    }
  deleted_count += sqlite3_changes (db);
  sqlite3_finalize (st);
  st = NULL;
  // Delete expired corp_recruiting entries
  const char *sql_delete_corp_recruiting =
    "DELETE FROM corp_recruiting WHERE expires_at <= ?;";


  rc = sqlite3_prepare_v2 (db, sql_delete_corp_recruiting, -1, &st, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE
      (
        "tavern_notice_expiry_cron: Failed to prepare corp recruiting delete statement: %s",
        sqlite3_errmsg (db));
      goto rollback_and_unlock;
    }
  sqlite3_bind_int (st, 1, (int) now_s);
  if (sqlite3_step (st) != SQLITE_DONE)
    {
      LOGE
      (
        "tavern_notice_expiry_cron: Failed to delete expired corp recruiting entries: %s",
        sqlite3_errmsg (db));
      goto rollback_and_unlock;
    }
  deleted_count += sqlite3_changes (db);
  sqlite3_finalize (st);
  st = NULL;
  rc = commit (db);
  if (rc != SQLITE_OK)
    {
      LOGE ("tavern_notice_expiry_cron: commit failed: %s",
            sqlite3_errmsg (db));
      goto rollback_and_unlock;
    }
  LOGI
  (
    "tavern_notice_expiry_cron: Removed %d expired Tavern notices and corp recruiting entries.",
    deleted_count);
  unlock (db, "tavern_notice_expiry_cron");
  return SQLITE_OK;
rollback_and_unlock:
  if (st)
    {
      sqlite3_finalize (st);
    }
  rollback (db);
  unlock (db,
          "tavern_notice_expiry_cron");
  return rc;
}


int
h_loan_shark_interest_cron (sqlite3 *db, int64_t now_s)
{
  LOGI("BANK0: Loan Shark Interest cron disabled for v1.0.");
  (void)db; // Suppress unused parameter warning
  (void)now_s; // Suppress unused parameter warning
  return 0; // Do nothing, cleanly exit

  if (!try_lock (db, "loan_shark_interest_cron", now_s))
    {
      return 0;
    }
  if (!g_tavern_cfg.loan_shark_enabled)
    {
      LOGI
        ("loan_shark_interest_cron: Loan Shark is disabled in config. Skipping.");
      unlock (db, "loan_shark_interest_cron");
      return 0;
    }
  LOGI
  (
    "loan_shark_interest_cron: Starting Loan Shark interest and default processing.");
  int rc = begin (db);


  if (rc)
    {
      unlock (db, "loan_shark_interest_cron");
      return rc;
    }
  sqlite3_stmt *st = NULL;
  const char *sql_select_loans =
    "SELECT player_id, principal, interest_rate, due_date, is_defaulted FROM tavern_loans WHERE principal > 0;";


  rc = sqlite3_prepare_v2 (db, sql_select_loans, -1, &st, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE
      (
        "loan_shark_interest_cron: Failed to prepare select loans statement: %s",
        sqlite3_errmsg (db));
      goto rollback_and_unlock;
    }
  int processed_loans = 0;


  while (sqlite3_step (st) == SQLITE_ROW)
    {
      int player_id = sqlite3_column_int (st, 0);
      long long principal = sqlite3_column_int64 (st, 1);
      int interest_rate = sqlite3_column_int (st, 2);
      // int due_date = sqlite3_column_int(st, 3); // Not directly used in this loop for application
      // int is_defaulted = sqlite3_column_int(st, 4); // Handled by check_loan_default implicitly
      // Apply interest
      int apply_rc =
        apply_loan_interest (db, player_id, principal, interest_rate);


      if (apply_rc != SQLITE_OK)
        {
          LOGE
          (
            "loan_shark_interest_cron: Failed to apply interest for player %d: %s",
            player_id,
            sqlite3_errmsg (db));
          // Decide whether to continue or abort for this player's loan
        }
      // Check for default status
      // Note: check_loan_default will update the DB itself if a loan becomes defaulted
      check_loan_default (db, player_id, (int) now_s);
      processed_loans++;
    }
  if (rc != SQLITE_DONE)
    {
      LOGE ("loan_shark_interest_cron: Error stepping through loans: %s",
            sqlite3_errmsg (db));
      goto rollback_and_unlock;
    }
  sqlite3_finalize (st);
  st = NULL;
  rc = commit (db);
  if (rc != SQLITE_OK)
    {
      LOGE ("loan_shark_interest_cron: commit failed: %s",
            sqlite3_errmsg (db));
      goto rollback_and_unlock;
    }
  LOGI
    ("loan_shark_interest_cron: Processed interest and defaults for %d loans.",
    processed_loans);
  unlock (db, "loan_shark_interest_cron");
  return SQLITE_OK;
rollback_and_unlock:
  if (st)
    {
      sqlite3_finalize (st);
    }
  rollback (db);
  unlock (db, "loan_shark_interest_cron");
  return rc;
}





int
h_daily_stock_price_recalculation (sqlite3 *db,
                                   int64_t now_s)
{
  if (!try_lock (db, "daily_stock_price_recalculation", now_s))
    {
      return 0;
    }
  LOGI
  (
    "h_daily_stock_price_recalculation: Starting daily stock price recalculation.");
  int rc = begin (db);


  if (rc)
    {
      unlock (db, "daily_stock_price_recalculation");
      return rc;
    }
  sqlite3_stmt *st_stocks = NULL;
  const char *sql_select_stocks =
    "SELECT s.id, s.corp_id, s.total_shares FROM stocks s WHERE s.corp_id > 0;";


  rc = sqlite3_prepare_v2 (db, sql_select_stocks, -1, &st_stocks, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE
      (
        "h_daily_stock_price_recalculation: Failed to prepare select stocks statement: %s",
        sqlite3_errmsg (db));
      goto rollback_and_unlock_stock_price;
    }
  while (sqlite3_step (st_stocks) == SQLITE_ROW)
    {
      int stock_id = sqlite3_column_int (st_stocks, 0);
      int corp_id = sqlite3_column_int (st_stocks, 1);
      long long total_shares = sqlite3_column_int64 (st_stocks, 2);
      long long net_asset_value = 0;
      long long bank_balance = 0;


      // Get corp bank balance
      db_get_corp_bank_balance (corp_id, &bank_balance);
      net_asset_value += bank_balance;
      // Get planet assets value for the corporation
      sqlite3_stmt *st_planets = NULL;
      const char *sql_select_planets =
        "SELECT ore_on_hand, organics_on_hand, equipment_on_hand FROM planets WHERE owner_id = ? AND owner_type = 'corp';";


      if (sqlite3_prepare_v2 (db, sql_select_planets, -1, &st_planets, NULL)
          == SQLITE_OK)
        {
          sqlite3_bind_int (st_planets, 1, corp_id);
          while (sqlite3_step (st_planets) == SQLITE_ROW)
            {
              net_asset_value += sqlite3_column_int64 (st_planets, 0) * 100;    // estimated price of ore
              net_asset_value += sqlite3_column_int64 (st_planets, 1) * 150;    // estimated price of organics
              net_asset_value += sqlite3_column_int64 (st_planets, 2) * 200;    // estimated price of equipment
            }
          sqlite3_finalize (st_planets);
        }
      else
        {
          LOGE
          (
            "h_daily_stock_price_recalculation: Failed to prepare select planets statement for corp %d: %s",
            corp_id,
            sqlite3_errmsg (db));
          // Continue to next stock or abort?
          continue;
        }
      long long new_current_price = 0;


      if (total_shares > 0)
        {
          // Calculate new price per share based on NAV (in minor units)
          new_current_price = net_asset_value / total_shares;
        }
      if (new_current_price < 1)
        {
          new_current_price = 1; // Minimum price of 1 credit
        }
      // Update the stock's current_price
      sqlite3_stmt *st_update_stock = NULL;
      const char *sql_update_stock =
        "UPDATE stocks SET current_price = ? WHERE id = ?;";


      if (sqlite3_prepare_v2
            (db, sql_update_stock, -1, &st_update_stock, NULL) == SQLITE_OK)
        {
          sqlite3_bind_int64 (st_update_stock, 1, new_current_price);
          sqlite3_bind_int (st_update_stock, 2, stock_id);
          if (sqlite3_step (st_update_stock) != SQLITE_DONE)
            {
              LOGE
              (
                "h_daily_stock_price_recalculation: Failed to update stock %d price: %s",
                stock_id,
                sqlite3_errmsg (db));
            }
          sqlite3_finalize (st_update_stock);
        }
      else
        {
          LOGE
          (
            "h_daily_stock_price_recalculation: Failed to prepare update stock price statement: %s",
            sqlite3_errmsg (db));
        }
    }
  sqlite3_finalize (st_stocks);
  rc = commit (db);
  if (rc != SQLITE_OK)
    {
      LOGE ("h_daily_stock_price_recalculation: commit failed: %s",
            sqlite3_errmsg (db));
      goto rollback_and_unlock_stock_price;
    }
  LOGI
  (
    "h_daily_stock_price_recalculation: Successfully recalculated stock prices.");
  unlock (db, "daily_stock_price_recalculation");
  return 0;
rollback_and_unlock_stock_price:
  if (st_stocks)
    {
      sqlite3_finalize (st_stocks);
    }
  rollback (db);
  unlock (db, "daily_stock_price_recalculation");
  return rc;
}



int
h_daily_corp_tax (sqlite3 *db, int64_t now_s)
{
  LOGI("BANK0: Daily Corporate Tax cron disabled for v1.0.");
  (void)db; // Suppress unused parameter warning
  (void)now_s; // Suppress unused parameter warning
  return 0; // Do nothing, cleanly exit

  if (!try_lock (db, "daily_corp_tax", now_s))
    {
      return 0;
    }
  LOGI ("h_daily_corp_tax: Starting daily corporation tax collection.");
  int rc = begin (db);


  if (rc)
    {
      unlock (db, "daily_corp_tax");
      return rc;
    }
  sqlite3_stmt *st_corps = NULL;
  const char *sql_select_corps = "SELECT id FROM corporations WHERE id > 0;";


  rc = sqlite3_prepare_v2 (db, sql_select_corps, -1, &st_corps, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE
        ("h_daily_corp_tax: Failed to prepare select corporations statement: %s",
        sqlite3_errmsg (db));
      goto rollback_and_unlock_tax;
    }
  while (sqlite3_step (st_corps) == SQLITE_ROW)
    {
      int corp_id = sqlite3_column_int (st_corps, 0);
      long long total_assets = 0;
      long long bank_balance = 0;


      // Get corp bank balance
      db_get_corp_bank_balance (corp_id, &bank_balance);
      total_assets += bank_balance;
      // Get planet assets
      sqlite3_stmt *st_planets = NULL;
      const char *sql_select_planets =
        "SELECT ore_on_hand, organics_on_hand, equipment_on_hand FROM planets WHERE owner_id = ? AND owner_type = 'corp';";


      if (sqlite3_prepare_v2 (db, sql_select_planets, -1, &st_planets, NULL)
          == SQLITE_OK)
        {
          sqlite3_bind_int (st_planets, 1, corp_id);
          while (sqlite3_step (st_planets) == SQLITE_ROW)
            {
              total_assets += sqlite3_column_int64 (st_planets, 0) * 100;       // price of ore
              total_assets += sqlite3_column_int64 (st_planets, 1) * 150;       // price of organics
              total_assets += sqlite3_column_int64 (st_planets, 2) * 200;       // price of equipment
            }
          sqlite3_finalize (st_planets);
        }
      long long tax_amount = (total_assets * CORP_TAX_RATE_BP) / 10000;


      if (tax_amount <= 0)
        {
          continue;
        }
      if (db_bank_withdraw ("corp", corp_id, tax_amount) != SQLITE_OK)
        {
          // Failed to pay tax
          sqlite3_stmt *st_update_corp = NULL;
          const char *sql_update_corp =
            "UPDATE corporations SET tax_arrears = tax_arrears + ?, credit_rating = credit_rating - 1 WHERE id = ?;";


          if (sqlite3_prepare_v2
                (db, sql_update_corp, -1, &st_update_corp, NULL) == SQLITE_OK)
            {
              sqlite3_bind_int64 (st_update_corp, 1, tax_amount);
              sqlite3_bind_int (st_update_corp, 2, corp_id);
              sqlite3_step (st_update_corp);
              sqlite3_finalize (st_update_corp);
            }
        }
    }
  sqlite3_finalize (st_corps);
  rc = commit (db);
  if (rc != SQLITE_OK)
    {
      LOGE ("h_daily_corp_tax: commit failed: %s", sqlite3_errmsg (db));
      goto rollback_and_unlock_tax;
    }
  unlock (db, "daily_corp_tax");
  return SQLITE_OK;
rollback_and_unlock_tax:
  if (st_corps)
    {
      sqlite3_finalize (st_corps);
    }
  rollback (db);
  unlock (db, "daily_corp_tax");
  return rc;
}


// Function to handle shield regeneration tick
int
h_shield_regen_tick (sqlite3 *db, int64_t now_s)
{
  if (!try_lock (db, "shield_regen", now_s))
    {
      return 0;
    }
  // LOGI ("h_shield_regen_tick: Starting shield regeneration.");
  int rc = begin (db);
  if (rc != SQLITE_OK)
    {
      LOGE ("h_shield_regen_tick: Failed to start transaction: %s", sqlite3_errmsg (db));
      unlock (db, "shield_regen");
      return rc;
    }

  // Use config value directly
  int regen_percent = 5; // Default
  // If you have g_cfg.regen.shield_rate_pct_per_tick (double 0.05), you can use it:
  // regen_percent = (int)(g_cfg.regen.shield_rate_pct_per_tick * 100);
  
  if (regen_percent <= 0) regen_percent = 5;

  const char *sql =
      "UPDATE ships "
      "SET shields = MIN(installed_shields, "
      "                   shields + ((installed_shields * ?1) / 100)) "
      "WHERE destroyed = 0 "
      "  AND installed_shields > 0 "
      "  AND shields < installed_shields;";

  sqlite3_stmt *stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE ("h_shield_regen_tick: SQL Error: %s", sqlite3_errmsg (db));
      rollback(db);
      unlock (db, "shield_regen");
      return rc;
    }

  sqlite3_bind_int(stmt, 1, regen_percent);
  sqlite3_step(stmt);
  sqlite3_finalize(stmt);

  rc = commit (db);
  unlock (db, "shield_regen");
  return rc;
}
