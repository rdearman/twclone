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
#include "server_stardock.h" // For Tavern-related declarations

/*
 * Helper function to add credits to an account.
 * Returns SQLITE_OK on success, or an SQLite error code.
 * If new_balance is not NULL, it will be set to the account's new balance.
 */


/*
 * Helper function to deduct credits from an account.
 * Returns SQLITE_OK on success, SQLITE_CONSTRAINT if insufficient funds, or an SQLite error code.
 * If new_balance is not NULL, it will be set to the account's new balance.
 */


/*
 * Helper function to update commodity stock on a planet.
 * Returns SQLITE_OK on success, or an SQLite error code.
 * If new_quantity is not NULL, it will be set to the commodity's new quantity.
 */


/*
 * Helper function to update commodity stock on a port.
 * Returns SQLITE_OK on success, or an SQLite error code.
 * If new_quantity is not NULL, it will be set to the commodity's new quantity.
 */



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



int h_daily_news_compiler(sqlite3 *db, int64_t now_s);
int h_cleanup_old_news(sqlite3 *db, int64_t now_s);
int h_daily_bank_interest_tick(sqlite3 *db, int64_t now_s);

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
  MAX_FED_FIGHTERS = 49
};

typedef struct
{
  const char *name;
  cron_handler_fn fn;
} entry_t;

int cron_limpet_ttl_cleanup(sqlite3 *db, int64_t now_s); // Forward declaration

static entry_t REG[] = {
  {"daily_turn_reset", h_reset_turns_for_player},
  {"fedspace_cleanup", h_fedspace_cleanup},
  {"autouncloak_sweeper", h_autouncloak_sweeper},
  {"terra_replenish", h_terra_replenish},
  {"planet_growth", h_planet_growth},
  {"broadcast_ttl_cleanup", h_broadcast_ttl_cleanup},
  {"traps_process", h_traps_process},
  {"npc_step", h_npc_step},
  {"daily_market_settlement", h_daily_market_settlement},
  {"daily_news_compiler", h_daily_news_compiler},
  {"cleanup_old_news", h_cleanup_old_news},
  {"limpet_ttl_cleanup", cron_limpet_ttl_cleanup},
  {"daily_bank_interest_tick", h_daily_bank_interest_tick},
  {"daily_lottery_draw", h_daily_lottery_draw},
  {"deadpool_resolution_cron", h_deadpool_resolution_cron},
  {"tavern_notice_expiry_cron", h_tavern_notice_expiry_cron},
  {"loan_shark_interest_cron", h_loan_shark_interest_cron},
  {"daily_corp_tax", h_daily_corp_tax},
};

static int g_reg_inited = 0;

int
get_random_sector (sqlite3 *db)
{
  int random_offset = rand () % RANGE_SIZE;
  int random_sector = MIN_UNPROTECTED_SECTOR + random_offset;
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

  const char *sql_select_ship_info = "SELECT T1.sector, T2.id FROM ships T1 LEFT JOIN players T2 ON T1.id = T2.ship WHERE T1.id = ?;";

  if (sqlite3_prepare_v2 (db, sql_select_ship_info, -1, &stmt, NULL) != SQLITE_OK)
    {
      LOGE ("tow_ship: Prepare SELECT failed for ship %d: %s", ship_id, sqlite3_errmsg (db));
      return SQLITE_ERROR;
    }
  sqlite3_bind_int (stmt, 1, ship_id);

  if (sqlite3_step (stmt) != SQLITE_ROW)
    {
      LOGE ("tow_ship: Ship ID %d not found or step failed. Error: %s", ship_id, sqlite3_errmsg (db));
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
      const char *sql_update_player = "UPDATE players SET sector = ? WHERE id = ?;";

      if (sqlite3_prepare_v2 (db, sql_update_player, -1, &stmt, NULL) != SQLITE_OK)
	{
	  LOGE ("tow_ship: Prepare UPDATE player failed: %s", sqlite3_errmsg (db));
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
      snprintf (message_buffer, sizeof (message_buffer),
		"Your ship was found parked in FedSpace (Sector %d) without protection. It has been towed to Sector %d for violating FedLaw: %s. The ship is now exposed to danger.",
		old_sector_id, new_sector_id, reason_str);

      h_send_message_to_player (admin_id, owner_id, subject_str, message_buffer);
    }

  LOGI ("TOW: Ship %d (Owner %d) towed from sector %d to sector %d. Reason: %s (Code %d). Admin: %d.", ship_id, owner_id, old_sector_id, new_sector_id, reason_str, reason_code, admin_id);
  int64_t current_time_s = (int64_t)time(NULL);
  json_t *fedspace_payload = json_object();
  if (fedspace_payload) {
      json_object_set_new(fedspace_payload, "reason", json_string(reason_str));
      json_object_set_new(fedspace_payload, "owner_id", json_integer(owner_id));
      json_object_set_new(fedspace_payload, "ship_id", json_integer(ship_id));
      db_log_engine_event(current_time_s, "fedspace:tow", NULL, admin_id, old_sector_id, fedspace_payload, NULL);
      json_decref(fedspace_payload);
  } else {
      LOGE("Failed to create JSON payload for fedspace:tow event.");
  } 

  return SQLITE_OK;
}

#define MSL_TABLE_NAME "msl_sectors"

static int *
universe_pathfind_get_sectors (sqlite3 *db, int start_sector, int end_sector, const int *avoid_list)
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
  if (sqlite3_prepare_v2 (db, "SELECT MAX(id) FROM sectors;", -1, &max_st, NULL) == SQLITE_OK)
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
  const char *sql_warps = "SELECT to_sector FROM sector_warps WHERE from_sector = ?1;";
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

	  if (neighbor <= 0 || neighbor > max_sector_id || parent[neighbor] != 0)
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
_insert_path_sectors (sqlite3 *db, sqlite3_stmt *insert_st, int start_sector, int end_sector, const int *avoid_list, int *total_unique_sectors_added)
{
  int rc;
  int *s;
  int *current_path = universe_pathfind_get_sectors (db, start_sector, end_sector, avoid_list);

  if (!current_path)
    {
      LOGW ("Could not find path from %d to %d. Check universe connections.", start_sector, end_sector);
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
	  LOGW ("SQL warning inserting sector %d for path %d->%d: %s", *s, start_sector, end_sector, sqlite3_errmsg (db));
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

  snprintf (sql_buffer, sizeof (sql_buffer), "SELECT COUNT(sector_id) FROM %s;", MSL_TABLE_NAME);

  sqlite3_stmt *count_st = NULL;

  if (sqlite3_prepare_v2 (db, sql_buffer, -1, &count_st, NULL) == SQLITE_OK && sqlite3_step (count_st) == SQLITE_ROW)
    {
      total_sectors_in_table = sqlite3_column_int (count_st, 0);
    }
  sqlite3_finalize (count_st);

  if (total_sectors_in_table > 0)
    {
      LOGI ("%s table already populated with %d entries. Skipping MSL calculation.", MSL_TABLE_NAME, total_sectors_in_table);
      return 0;
    }

  LOGI ("%s table is empty. Starting comprehensive MSL path calculation (FedSpace 1-10 <-> Stardocks)...", MSL_TABLE_NAME);

  snprintf (sql_buffer, sizeof (sql_buffer), "CREATE TABLE IF NOT EXISTS %s (sector_id INTEGER PRIMARY KEY);", MSL_TABLE_NAME);

  if (sqlite3_exec (db, sql_buffer, NULL, NULL, NULL) != SQLITE_OK)
    {
      LOGE ("SQL error creating %s table: %s", MSL_TABLE_NAME, sqlite3_errmsg (db));
      return -1;
    }

  sqlite3_stmt *select_st = NULL;
  const char *sql_select_stardocks = "SELECT sector_id FROM stardock_location;";
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
	  int *new_arr = realloc (stardock_sectors, stardock_capacity * sizeof (int));
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
      LOGW ("No stardock locations found in stardock_location table. Skipping MSL calculation.");
      free (stardock_sectors);
      return 0;
    }

  sqlite3_stmt *insert_st = NULL;
  snprintf (sql_buffer, sizeof (sql_buffer), "INSERT OR IGNORE INTO %s (sector_id) VALUES (?);", MSL_TABLE_NAME);
  rc = sqlite3_prepare_v2 (db, sql_buffer, -1, &insert_st, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE ("SQL error preparing insert statement for %s: %s", MSL_TABLE_NAME, sqlite3_errmsg (db));
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

  for (int start_sector = FEDSPACE_SECTOR_START; start_sector <= FEDSPACE_SECTOR_END; start_sector++)
    {
      for (int i = 0; i < stardock_count; i++)
	{
	  int stardock_id = stardock_sectors[i];

	  LOGI ("Calculating path %d -> %d", start_sector, stardock_id);
	  _insert_path_sectors (db, insert_st, start_sector, stardock_id, avoid_list, &total_unique_sectors_added);

	  if (start_sector != stardock_id)
	    {
	      LOGI ("Calculating path %d -> %d (Reverse)", stardock_id, start_sector);
	      _insert_path_sectors (db, insert_st, stardock_id, start_sector, avoid_list, &total_unique_sectors_added);
	    }
	}
    }

  sqlite3_finalize (insert_st);
  free (stardock_sectors);

  if (sqlite3_exec (db, "COMMIT;", NULL, NULL, NULL) != SQLITE_OK)
    {
      LOGE ("SQL error committing master path transaction: %s", sqlite3_errmsg (db));
      return -1;
    }

  LOGI ("Completed MSL setup. Populated %s with %d total unique sectors.", MSL_TABLE_NAME, total_unique_sectors_added);

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

  const char *sql_config = "SELECT turnsperday FROM config WHERE id = 1;";
  rc = sqlite3_prepare_v2 (db, sql_config, -1, &select_st, NULL);
  if (rc == SQLITE_OK && sqlite3_step (select_st) == SQLITE_ROW)
    {
      max_turns = sqlite3_column_int (select_st, 0);
    }
  sqlite3_finalize (select_st);
  select_st = NULL;

  if (max_turns <= 0)
    {
      LOGE ("Turn reset failed: turnsperday is %d or missing in config.", max_turns);
      return -1;
    }

  const char *sql_select_players = "SELECT player FROM turns;";
  rc = sqlite3_prepare_v2 (db, sql_select_players, -1, &select_st, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE ("SQL error preparing player select: %s", sqlite3_errmsg (db));
      return -1;
    }

  const char *sql_update = "UPDATE turns SET turns_remaining = ?, last_update = ? WHERE player = ?;";
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
	  LOGE ("SQL error executing turns update for player %d: %s", player_id, sqlite3_errmsg (db));
	}
    }

  sqlite3_finalize (select_st);
  sqlite3_finalize (update_st);

  if (sqlite3_exec (db, "COMMIT;", NULL, NULL, NULL) != SQLITE_OK)
    {
      LOGE ("SQL error committing transaction: %s", sqlite3_errmsg (db));
      return -1;
    }

  LOGI ("Successfully reset turns for %d players to %d.", updated_count, max_turns);

  return 0;
}

static int
try_lock (sqlite3 *db, const char *name, int64_t now_s)
{
  sqlite3_stmt *st = NULL;
  int rc;
  int64_t now_ms = now_s * 1000;

  const int LOCK_DURATION_S = 60;
  int64_t until_ms = now_ms + (LOCK_DURATION_S * 1000);

  rc = sqlite3_prepare_v2 (db, "DELETE FROM locks WHERE lock_name=?1 AND until_ms < ?2;", -1, &st, NULL);
  if (rc == SQLITE_OK)
    {
      sqlite3_bind_text (st, 1, name, -1, SQLITE_STATIC);
      sqlite3_bind_int64 (st, 2, now_ms);
      sqlite3_step (st);
    }
  sqlite3_finalize (st);
  if (rc != SQLITE_OK)
    return 0;

  rc = sqlite3_prepare_v2 (db, "INSERT INTO locks(lock_name, owner, until_ms) VALUES(?1, 'server', ?2) ON CONFLICT(lock_name) DO NOTHING;", -1, &st, NULL);
  if (rc != SQLITE_OK)
    return 0;

  sqlite3_bind_text (st, 1, name, -1, SQLITE_STATIC);
  sqlite3_bind_int64 (st, 2, until_ms);

  sqlite3_step (st);
  sqlite3_finalize (st);

  rc = sqlite3_prepare_v2 (db, "SELECT owner FROM locks WHERE lock_name=?1;", -1, &st, NULL);
  if (rc != SQLITE_OK)
    return 0;

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

static void
unlock (sqlite3 *db, const char *name)
{
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2 (db, "DELETE FROM locks WHERE lock_name=?1 AND owner='server';", -1, &st, NULL) == SQLITE_OK)
    {
      sqlite3_bind_text (st, 1, name, -1, SQLITE_STATIC);
      sqlite3_step (st);
      sqlite3_finalize (st);
    }
}

void
cron_register_builtins (void)
{
  g_reg_inited = 1;
}

static int
begin (sqlite3 *db)
{
  return sqlite3_exec (db, "BEGIN IMMEDIATE;", NULL, NULL, NULL);
}

static int
commit (sqlite3 *db)
{
  return sqlite3_exec (db, "COMMIT;", NULL, NULL, NULL);
}

static int
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



static int ship_callback(void *count_ptr, int argc, char **argv, char **azColName) {
    int *count = (int *)count_ptr;
    
    if (argv[0]) {
        printf("Found cloaked ship (Timestamp: %s)\n", argv[0]);
        (*count)++;
    }
    
    return 0;
}

int uncloak_ships_in_fedspace(sqlite3 *db) {
    int rc;
    char *err_msg = 0;
    int cloaked_ship_count = 0;

    const char *sql = "UPDATE ships SET cloaked=NULL WHERE cloaked IS NOT NULL AND (sector IN (SELECT sector_id FROM stardock_location) OR sector IN (1, 2, 3, 4, 5, 6, 7, 8, 9, 10));";

    rc = sqlite3_exec(db, sql, ship_callback, &cloaked_ship_count, &err_msg);

    if (rc != SQLITE_OK) {
        LOGE("SQL error: %s", err_msg);
        sqlite3_free(err_msg);
        return -1;
    } 

    return cloaked_ship_count;
}

// Cron handler to clean up expired Limpet mines
int
cron_limpet_ttl_cleanup (sqlite3 *db, int64_t now_s)
{
  if (!g_cfg.mines.limpet.enabled) {
      return 0; // Limpet mines disabled, no cleanup needed
  }
  if (g_cfg.mines.limpet.limpet_ttl_days <= 0) {
      LOGW("limpet_ttl_days is not set or zero. Skipping Limpet TTL cleanup.");
      return 0; // No TTL is set, so no cleanup needed
  }

  if (!try_lock (db, "limpet_ttl_cleanup", now_s))
    return 0;

  LOGI ("limpet_ttl_cleanup: Starting Limpet mine TTL cleanup.");

  int rc = begin (db);
  if (rc)
    {
      unlock (db, "limpet_ttl_cleanup");
      return rc;
    }

  sqlite3_stmt *st = NULL;
  int removed_count = 0;

  // Calculate the expiry timestamp: deployed_at + (limpet_ttl_days * seconds_in_day)
  // Assuming deployed_at is UNIX epoch.
  long long expiry_threshold_s = now_s - ((long long)g_cfg.mines.limpet.limpet_ttl_days * 24 * 3600);

  const char *sql_delete_expired =
      "DELETE FROM sector_assets "
      "WHERE asset_type = ?1 AND deployed_at <= ?2;";

  rc = sqlite3_prepare_v2(db, sql_delete_expired, -1, &st, NULL);
  if (rc != SQLITE_OK) {
      LOGE("limpet_ttl_cleanup: Failed to prepare delete statement: %s", sqlite3_errmsg(db));
      rollback(db);
      unlock(db, "limpet_ttl_cleanup");
      return rc;
  }

  sqlite3_bind_int(st, 1, ASSET_LIMPET_MINE);
  sqlite3_bind_int64(st, 2, expiry_threshold_s);

  rc = sqlite3_step(st);
  if (rc != SQLITE_DONE) {
      LOGE("limpet_ttl_cleanup: Failed to execute delete statement: %s", sqlite3_errmsg(db));
      rollback(db);
      unlock(db, "limpet_ttl_cleanup");
      sqlite3_finalize(st);
      return rc;
  }
  removed_count = sqlite3_changes(db);
  sqlite3_finalize(st);

  rc = commit(db);
  if (rc != SQLITE_OK) {
      LOGE("limpet_ttl_cleanup: Commit failed: %s", sqlite3_errmsg(db));
      rollback(db); // Attempt to rollback if commit fails
      unlock(db, "limpet_ttl_cleanup");
      return rc;
  }

  if (removed_count > 0) {
      LOGI("limpet_ttl_cleanup: Removed %d expired Limpet mines.", removed_count);
  } else {
      LOGD("limpet_ttl_cleanup: No expired Limpet mines found.");
  }

  unlock(db, "limpet_ttl_cleanup");
  return SQLITE_OK;
}

int
h_fedspace_cleanup (sqlite3 *db, int64_t now_s)
{
  int fedadmin = 2;
  int64_t now_ms = now_s * 1000;
  sqlite3_stmt *select_stmt = NULL;
  sqlite3_stmt *delete_stmt = NULL;
  sqlite3_stmt *sector_stmt = NULL;
  int cleared_assets = 0;
  int tows = 0;
  int rc;
  char *err_msg = NULL;

  if (!try_lock (db, "fedspace_cleanup", now_s))
    {
      int64_t until_ms = db_lock_status (db, "fedspace_cleanup");
      int64_t time_left_s = (until_ms - now_ms) / 1000;
      if (until_ms > now_ms)
	{
	  LOGW ("fedspace_cleanup: FAILED to acquire lock. Still held for %lld more seconds.", (long long) time_left_s);
	}
      else
	{
	  LOGW ("fedspace_cleanup: FAILED to acquire lock. Lock is stale (Expires at %lld).", (long long) until_ms);
	}
      return 0;
    }
  else
    {
      LOGI ("fedspace_cleanup: Lock acquired, starting cleanup operations.");
    }

  int uncloak = uncloak_ships_in_fedspace(db);
  if (uncloak > 0) {
      LOGI("Uncloaked %d ships in FedSpace.", uncloak);
  }

  if (populate_msl_if_empty (db) != 0)
    {
      LOGE ("fedspace_cleanup: MSL population failed. Aborting cleanup.");
    }

  const char *select_assets_sql = "SELECT player, asset_type, sector, quantity FROM sector_assets WHERE sector IN (SELECT sector_id FROM msl_sectors) AND player != 0;";

  rc = sqlite3_prepare_v2 (db, select_assets_sql, -1, &select_stmt, NULL);
  if (rc == SQLITE_OK)
    {
      char message[256];
      const char *delete_sql = "DELETE FROM sector_assets WHERE player = ?1 AND asset_type = ?2 AND sector = ?3 AND quantity = ?4;";

      rc = sqlite3_prepare_v2 (db, delete_sql, -1, &delete_stmt, NULL);

      if (rc == SQLITE_OK)
	{
	  while ((rc = sqlite3_step (select_stmt)) == SQLITE_ROW)
	    {
	      int player_id = sqlite3_column_int (select_stmt, 0);
	      int asset_type = sqlite3_column_int (select_stmt, 1);
	      int sector_id = sqlite3_column_int (select_stmt, 2);
	      int quantity = sqlite3_column_int (select_stmt, 3);

	      snprintf (message, sizeof (message), "%d %s(s) deployed in Sector %d (Major Space Lane) were destroyed by Federal Authorities. Deployments in MSL are strictly prohibited.", quantity, get_asset_name (asset_type), sector_id);
	      h_send_message_to_player (player_id, fedadmin, "WARNING: MSL Violation", message);

	      json_t *fedspace_asset_payload = json_object();
	      if (fedspace_asset_payload) {
		  json_object_set_new(fedspace_asset_payload, "reason", json_string("MSL_VIOLATION"));
		  json_object_set_new(fedspace_asset_payload, "player_id", json_integer(player_id));
		  json_object_set_new(fedspace_asset_payload, "asset_type", json_integer(asset_type));
		  db_log_engine_event(now_s, "fedspace:asset_cleared", NULL, fedadmin, sector_id, fedspace_asset_payload, NULL);
		  json_decref(fedspace_asset_payload);
	      } else {
		  LOGE("Failed to create JSON payload for fedspace:asset_cleared event.");
	      }
	      
	      sqlite3_reset (delete_stmt);
	      sqlite3_bind_int (delete_stmt, 1, player_id);
	      sqlite3_bind_int (delete_stmt, 2, asset_type);
	      sqlite3_bind_int (delete_stmt, 3, sector_id);
	      sqlite3_bind_int (delete_stmt, 4, quantity);
	      if (sqlite3_step (delete_stmt) == SQLITE_DONE)
		cleared_assets++;
	    }
	  sqlite3_finalize (delete_stmt);
	  delete_stmt = NULL;
	}
      else
	{
	  LOGE ("h_fedspace_cleanup: MSL DELETE prepare failed: %s", sqlite3_errmsg (db));
	}
      sqlite3_finalize (select_stmt);
      select_stmt = NULL;
    }
  else
    {
      LOGE ("h_fedspace_cleanup: MSL SELECT prepare failed: %s", sqlite3_errmsg (db));
    }

  if (cleared_assets > 0)
    {
      LOGI ("fedspace_cleanup: Completed asset clearing with %d assets cleared.", cleared_assets);
    }

  rc = begin (db);
  if (rc != 0)
    {
      unlock (db, "fedspace_cleanup");
      return rc;
    }

  const char *sql_timeout_logout = "UPDATE players SET loggedin = 0 WHERE loggedin = 1 AND ?1 - last_update > ?2;";

  if (sqlite3_prepare_v2 (db, sql_timeout_logout, -1, &select_stmt, NULL) == SQLITE_OK)
    {
      sqlite3_bind_int64 (select_stmt, 1, now_s);
      sqlite3_bind_int (select_stmt, 2, LOGOUT_TIMEOUT_S);
      rc = sqlite3_step (select_stmt);
      sqlite3_finalize (select_stmt);
      select_stmt = NULL;

      if (rc != SQLITE_DONE)
	{
	  LOGE ("h_fedspace_cleanup: LOGOUT failed: %d", rc);
	}
    }
  else
    {
      LOGE ("h_fedspace_cleanup: LOGOUT prepare failed: %s", sqlite3_errmsg (db));
    }

  const char *sql_create_eligible_table = "CREATE TABLE IF NOT EXISTS eligible_tows (ship_id INTEGER PRIMARY KEY, sector_id INTEGER, owner_id INTEGER, fighters INTEGER, alignment INTEGER, experience INTEGER);";

  rc = sqlite3_exec (db, sql_create_eligible_table, NULL, NULL, &err_msg);
  if (rc != SQLITE_OK)
    {
      LOGE ("h_fedspace_cleanup: PERM TABLE creation failed: %s", err_msg);
      sqlite3_free (err_msg);
      rollback (db);
      unlock (db, "fedspace_cleanup");
      return -1;
    }

  rc = sqlite3_exec (db, "DELETE FROM eligible_tows", NULL, NULL, &err_msg);
  if (rc != SQLITE_OK)
    {
      LOGE ("h_fedspace_cleanup: DELETE TABLE failed: %s", err_msg);
      sqlite3_free (err_msg);
      rollback (db);
      unlock (db, "fedspace_cleanup");
      return -1;
    }

  const char *sql_insert_eligible = "INSERT INTO eligible_tows (ship_id, sector_id, owner_id, fighters, alignment, experience) SELECT T1.id, T1.sector, T2.id, T1.fighters, COALESCE(T2.alignment, 0), COALESCE(T2.experience, 0) FROM ships T1 LEFT JOIN players T2 ON T1.id = T2.ship WHERE T1.sector BETWEEN ?1 AND ?2 AND (T2.loggedin = 0 OR T2.id IS NULL) ORDER BY T1.id ASC;";
  if (sqlite3_prepare_v2 (db, sql_insert_eligible, -1, &select_stmt, NULL) == SQLITE_OK)
    {
      sqlite3_bind_int (select_stmt, 1, FEDSPACE_SECTOR_START);
      sqlite3_bind_int (select_stmt, 2, FEDSPACE_SECTOR_END);
      sqlite3_step (select_stmt);
      sqlite3_finalize (select_stmt);
      select_stmt = NULL;
    }

  const char *sql_delete_eligible = "DELETE FROM eligible_tows WHERE ship_id = ?;";
  if (sqlite3_prepare_v2 (db, sql_delete_eligible, -1, &delete_stmt, NULL) != SQLITE_OK)
    {
      LOGE ("h_fedspace_cleanup: DELETE prepare failed: %s", sqlite3_errmsg (db));
      delete_stmt = NULL;
    }

  const char *sql_evil_alignment = "SELECT ship_id FROM eligible_tows WHERE owner_id IS NOT NULL AND alignment < 0 LIMIT ?1;";
  if (sqlite3_prepare_v2 (db, sql_evil_alignment, -1, &select_stmt, NULL) == SQLITE_OK)
    {
      sqlite3_bind_int (select_stmt, 1, MAX_TOWS_PER_PASS - tows);
      while (sqlite3_step (select_stmt) == SQLITE_ROW && tows < MAX_TOWS_PER_PASS)
	{
	  int ship_id = sqlite3_column_int (select_stmt, 0);
	  tow_ship (db, ship_id, get_random_sector (db), fedadmin, REASON_EVIL_ALIGN);
	  
	  if (delete_stmt)
	    {
	      sqlite3_bind_int (delete_stmt, 1, ship_id);
	      sqlite3_step (delete_stmt);
	      sqlite3_reset (delete_stmt);
	    }
	  tows++;
	}
      sqlite3_finalize (select_stmt);
      select_stmt = NULL;
    }

  const char *sql_excess_fighters = "SELECT ship_id FROM eligible_tows WHERE fighters > ?1 LIMIT ?2;";
  if (sqlite3_prepare_v2 (db, sql_excess_fighters, -1, &select_stmt, NULL) == SQLITE_OK)
    {
      sqlite3_bind_int (select_stmt, 1, 49);
      sqlite3_bind_int (select_stmt, 2, MAX_TOWS_PER_PASS - tows);
      while (sqlite3_step (select_stmt) == SQLITE_ROW && tows < MAX_TOWS_PER_PASS)
	{
	  int ship_id = sqlite3_column_int (select_stmt, 0);
	  tow_ship (db, ship_id, get_random_sector (db), fedadmin, REASON_EXCESS_FIGHTERS);

	  if (delete_stmt)
	    {
	      sqlite3_bind_int (delete_stmt, 1, ship_id);
	      sqlite3_step (delete_stmt);
	      sqlite3_reset (delete_stmt);
	    }
	  tows++;
	}
      sqlite3_finalize (select_stmt);
      select_stmt = NULL;
    }

  const char *sql_high_exp = "SELECT ship_id FROM eligible_tows WHERE owner_id IS NOT NULL AND experience >= 1000 LIMIT ?1;";
  if (sqlite3_prepare_v2 (db, sql_high_exp, -1, &select_stmt, NULL) == SQLITE_OK)
    {
      sqlite3_bind_int (select_stmt, 1, MAX_TOWS_PER_PASS - tows);
      while (sqlite3_step (select_stmt) == SQLITE_ROW && tows < MAX_TOWS_PER_PASS)
	{
	  int ship_id = sqlite3_column_int (select_stmt, 0);
	  tow_ship (db, ship_id, get_random_sector (db), fedadmin, REASON_HIGH_EXP);

	  if (delete_stmt)
	    {
	      sqlite3_bind_int (delete_stmt, 1, ship_id);
	      sqlite3_step (delete_stmt);
	      sqlite3_reset (delete_stmt);
	    }
	  tows++;
	}
      sqlite3_finalize (select_stmt);
      select_stmt = NULL;
    }

  const char *sql_no_owner = "SELECT ship_id FROM eligible_tows WHERE owner_id IS NULL LIMIT ?1;";
  if (sqlite3_prepare_v2 (db, sql_no_owner, -1, &select_stmt, NULL) == SQLITE_OK)
    {
      sqlite3_bind_int (select_stmt, 1, MAX_TOWS_PER_PASS - tows);
      while (sqlite3_step (select_stmt) == SQLITE_ROW && tows < MAX_TOWS_PER_PASS)
	{
	  int ship_id = sqlite3_column_int (select_stmt, 0);
	  tow_ship (db, ship_id, CONFISCATION_SECTOR, fedadmin, REASON_NO_OWNER);

	  if (delete_stmt)
	    {
	      sqlite3_bind_int (delete_stmt, 1, ship_id);
	      sqlite3_step (delete_stmt);
	      sqlite3_reset (delete_stmt);
	    }
	  tows++;
	}
      sqlite3_finalize (select_stmt);
      select_stmt = NULL;
    }

  const char *sql_overcrowded_sectors = "SELECT T1.sector_id, COUNT(T1.ship_id) AS ship_count FROM eligible_tows T1 WHERE T1.sector_id BETWEEN ?1 AND ?2 GROUP BY T1.sector_id HAVING COUNT(T1.ship_id) > ?3 ORDER BY T1.sector_id ASC;";

  if (tows < MAX_TOWS_PER_PASS && sqlite3_prepare_v2 (db, sql_overcrowded_sectors, -1, &sector_stmt, NULL) == SQLITE_OK)
    {
      sqlite3_bind_int (sector_stmt, 1, FEDSPACE_SECTOR_START);
      sqlite3_bind_int (sector_stmt, 2, FEDSPACE_SECTOR_END);
      sqlite3_bind_int (sector_stmt, 3, MAX_SHIPS_PER_FED_SECTOR);

      while (sqlite3_step (sector_stmt) == SQLITE_ROW && tows < MAX_TOWS_PER_PASS)
	{
	  int sector_id = sqlite3_column_int (sector_stmt, 0);
	  int ship_count = sqlite3_column_int (sector_stmt, 1);

	  int excess_ships = ship_count - MAX_SHIPS_PER_FED_SECTOR;
	  int to_tow = (MAX_TOWS_PER_PASS - tows < excess_ships) ? (MAX_TOWS_PER_PASS - tows) : excess_ships;

	  const char *sql_overcrowded_ships = "SELECT ship_id FROM eligible_tows WHERE sector_id = ?1 ORDER BY ship_id DESC LIMIT ?2;";

	  if (sqlite3_prepare_v2 (db, sql_overcrowded_ships, -1, &select_stmt, NULL) == SQLITE_OK)
	    {
	      sqlite3_bind_int (select_stmt, 1, sector_id);
	      sqlite3_bind_int (select_stmt, 2, to_tow);

	      while (sqlite3_step (select_stmt) == SQLITE_ROW)
		{
		  int ship_id = sqlite3_column_int (select_stmt, 0);
		  int new_sector = get_random_sector (db);
		  tow_ship (db, ship_id, new_sector, fedadmin, REASON_OVERCROWDING);

		  if (delete_stmt)
		    {
		      sqlite3_bind_int (delete_stmt, 1, ship_id);
		      sqlite3_step (delete_stmt);
		      sqlite3_reset (delete_stmt);
		    }
		  tows++;
		}
	      sqlite3_finalize (select_stmt);
	      select_stmt = NULL;
	    }
	}
      sqlite3_finalize (sector_stmt);
      sector_stmt = NULL;
    }

  if (delete_stmt)
    {
      sqlite3_finalize (delete_stmt);
      delete_stmt = NULL;
    }
  if (select_stmt)
    {
      sqlite3_finalize (select_stmt);
      select_stmt = NULL;
    }
  if (sector_stmt)
    {
      sqlite3_finalize (sector_stmt);
      sector_stmt = NULL;
    }

  rc = sqlite3_exec (db, "DELETE FROM eligible_tows", NULL, NULL, &err_msg);
  if (rc != SQLITE_OK)
    {
      LOGE ("h_fedspace_cleanup: Final DELETE failed: %s", err_msg);
      sqlite3_free (err_msg);
    }

  commit (db);
  LOGI ("fedspace_cleanup: ok (towed=%d)", tows);
  unlock (db, "fedspace_cleanup");
  return 0;
}

// Helper function to convert Unix epoch seconds to UTC epoch day (YYYYMMDD)
static int get_utc_epoch_day(int64_t unix_timestamp) {
    time_t rawtime = unix_timestamp;
    struct tm *info;
    // Use gmtime for UTC
    info = gmtime(&rawtime);
    if (info == NULL) {
        return 0; // Error
    }
    // Return YYYYMMDD
    return (info->tm_year + 1900) * 10000 + (info->tm_mon + 1) * 100 + info->tm_mday;
}

#define MAX_BACKLOG_DAYS 30 // Cap for how many days of interest can be applied retroactively

int
h_daily_bank_interest_tick (sqlite3 *db, int64_t now_s)
{
  if (!try_lock (db, "daily_bank_interest_tick", now_s))
    return 0;

  LOGI ("daily_bank_interest_tick: Starting daily bank interest accrual.");

  int rc = begin (db);
  if (rc)
    {
      unlock (db, "daily_bank_interest_tick");
      return rc;
    }

  sqlite3_stmt *st = NULL;
  const char *sql_select_accounts =
      "SELECT id, owner_type, owner_id, balance, interest_rate_bp, last_interest_tick "
      "FROM bank_accounts WHERE is_active = 1 AND interest_rate_bp > 0;";

  rc = sqlite3_prepare_v2(db, sql_select_accounts, -1, &st, NULL);
  if (rc != SQLITE_OK) {
      LOGE("daily_bank_interest_tick: Failed to prepare select accounts statement: %s", sqlite3_errmsg(db));
      goto rollback_and_unlock;
  }

  int current_epoch_day = get_utc_epoch_day(now_s);
  int processed_accounts = 0;

  long long min_balance_for_interest = h_get_config_int_unlocked(db, "bank_min_balance_for_interest", 0);
  long long max_daily_per_account = h_get_config_int_unlocked(db, "bank_max_daily_interest_per_account", 9223372036854775807LL);

  while (sqlite3_step(st) == SQLITE_ROW) {
      int account_id = sqlite3_column_int(st, 0);
      const char *owner_type = (const char *)sqlite3_column_text(st, 1);
      int owner_id = sqlite3_column_int(st, 2);
      long long balance = sqlite3_column_int64(st, 3);
      int interest_rate_bp = sqlite3_column_int(st, 4);
      int last_interest_tick = sqlite3_column_int(st, 5);

      if (balance < min_balance_for_interest) {
          continue; // Skip accounts below minimum balance
      }

      int days_to_accrue = current_epoch_day - last_interest_tick;
      if (days_to_accrue <= 0) {
          continue; // Already processed for today or future tick
      }
      if (days_to_accrue > MAX_BACKLOG_DAYS) {
          days_to_accrue = MAX_BACKLOG_DAYS; // Cap the backlog
      }
      
      char tx_group_id[33];
      h_generate_hex_uuid(tx_group_id, sizeof(tx_group_id));

      for (int i = 0; i < days_to_accrue; ++i) {
          if (balance <= 0) break; // No interest on zero or negative balance

          // interest = floor( balance * interest_rate_bp / (10000 * 365) )
          long long daily_interest = (balance * interest_rate_bp) / (10000 * 365);

          if (daily_interest > max_daily_per_account) {
              daily_interest = max_daily_per_account;
          }

          if (daily_interest > 0) {
              // Use h_add_credits_unlocked to record interest and update balance
              int add_rc = h_add_credits_unlocked(db, account_id, daily_interest, "INTEREST", tx_group_id, &balance);
              if (add_rc != SQLITE_OK) {
                  LOGE("daily_bank_interest_tick: Failed to add interest to account %d (owner %s:%d): %s",
                       account_id, owner_type, owner_id, sqlite3_errmsg(db));
                  // Continue to next account or abort? Aborting for now.
                  goto rollback_and_unlock;
              }
          }
      }

      // Update last_interest_tick in bank_accounts
      sqlite3_stmt *update_tick_st = NULL;
      const char *sql_update_tick = "UPDATE bank_accounts SET last_interest_tick = ? WHERE id = ?;";
      int update_rc = sqlite3_prepare_v2(db, sql_update_tick, -1, &update_tick_st, NULL);
      if (update_rc != SQLITE_OK) {
          LOGE("daily_bank_interest_tick: Failed to prepare update last_interest_tick statement: %s", sqlite3_errmsg(db));
          goto rollback_and_unlock;
      }
      sqlite3_bind_int(update_tick_st, 1, current_epoch_day);
      sqlite3_bind_int(update_tick_st, 2, account_id);
      update_rc = sqlite3_step(update_tick_st);
      sqlite3_finalize(update_tick_st);
      if (update_rc != SQLITE_DONE) {
          LOGE("daily_bank_interest_tick: Failed to update last_interest_tick for account %d: %s", account_id, sqlite3_errmsg(db));
          goto rollback_and_unlock;
      }
      processed_accounts++;
  }

  if (rc != SQLITE_DONE) {
      LOGE("daily_bank_interest_tick: Error stepping through bank_accounts: %s", sqlite3_errmsg(db));
      goto rollback_and_unlock;
  }

  sqlite3_finalize(st);
  st = NULL;

  rc = commit (db);
  if (rc != SQLITE_OK)
    {
      LOGE ("daily_bank_interest_tick: commit failed: %s", sqlite3_errmsg (db));
      goto rollback_and_unlock;
    }

  LOGI ("daily_bank_interest_tick: Successfully processed interest for %d accounts.", processed_accounts);
  unlock (db, "daily_bank_interest_tick");
  return SQLITE_OK;

rollback_and_unlock:
  if (st) sqlite3_finalize(st);
  rollback (db);
  unlock (db, "daily_bank_interest_tick");
  return rc;
}

int
h_daily_turn_reset (sqlite3 *db, int64_t now_s)
{
  if (!try_lock (db, "daily_turn_reset", now_s))
    return 0;

  LOGI ("daily_turn_reset: starting daily turn reset.");

  int rc = begin (db);
  if (rc)
    {
      unlock (db, "daily_turn_reset");
      return rc;
    }

  rc = sqlite3_exec (db, "UPDATE turns SET turns_remaining = (SELECT turnsperday FROM config WHERE id=1);", NULL, NULL, NULL);

  if (rc != SQLITE_OK)
    {
      LOGE ("daily_turn_reset: player turn update failed: %s", sqlite3_errmsg (db));
      rollback (db);
      unlock (db, "daily_turn_reset");
      return rc;
    }

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
    return 0;

  sqlite3_stmt *st = NULL;
  sqlite3_stmt *q_ccap = NULL;
  int rc;
  int max_hours = 0;

  rc = sqlite3_prepare_v2 (db, "SELECT max_cloak_duration FROM config;", -1, &q_ccap, NULL);

  if (rc != SQLITE_OK)
    {
      LOGE ("Can't prepare config SELECT: %s", sqlite3_errmsg (db));
      unlock (db, "autouncloak_sweeper");
      return 0;
    }

  if (sqlite3_step (q_ccap) == SQLITE_ROW)
    {
      max_hours = sqlite3_column_int (q_ccap, 0);
    }

  sqlite3_finalize (q_ccap);

  if (max_hours <= 0)
    {
      LOGI ("autouncloak_sweeper: max_cloak_duration is zero/invalid. Skipping sweep.");
      unlock (db, "autouncloak_sweeper");
      return 0;
    }

  const int SECONDS_IN_HOUR = 3600;
  int64_t max_duration_seconds = (int64_t) max_hours * SECONDS_IN_HOUR;
  int64_t uncloak_threshold_s = now_s - max_duration_seconds;

  rc = sqlite3_prepare_v2 (db, "UPDATE ships SET cloaked = NULL WHERE cloaked IS NOT NULL AND cloaked < ?;", -1, &st, NULL);

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
    return 0;
  (void) now_s;
  int rc = begin (db);
  if (rc)
    return rc;

  rc = sqlite3_exec (db, "UPDATE planet_goods SET quantity = max_capacity WHERE planet_id = 1;", NULL, NULL, NULL);

  if (rc)
    {
      rollback (db);
      LOGE ("terra_replenish (Terra resources max) rc=%d", rc);
      return rc;
    }

  rc = sqlite3_exec (db, "UPDATE planets SET terraform_turns_left = 1 WHERE owner > 0;", NULL, NULL, NULL);

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
h_planet_growth (sqlite3 *db, int64_t now_s)
{
  if (!try_lock (db, "planet_growth", now_s))
    return 0;
  (void) now_s;
  int rc = begin (db);
  if (rc)
    return rc;

  rc = sqlite3_exec (db, "UPDATE planets SET population = population + CAST(population*0.001 AS INT) WHERE population > 0 AND owner > 0;", NULL, NULL, NULL);

  if (rc)
    {
      rollback (db);
      LOGE ("planet_growth (pop) rc=%d", rc);
      unlock (db, "planet_growth");
      return rc;
    }

  rc = sqlite3_exec (db, "UPDATE planet_goods SET quantity = MIN(max_capacity, quantity + production_rate) WHERE production_rate > 0;", NULL, NULL, NULL);

  if (rc)
    {
      rollback (db);
      LOGE ("planet_growth (res) rc=%d", rc);
      unlock (db, "planet_growth");
      return rc;
    }

  commit (db);
  unlock (db, "planet_growth");
  return 0;
}

int
h_broadcast_ttl_cleanup (sqlite3 *db, int64_t now_s)
{
  if (!try_lock (db, "broadcast_ttl_cleanup", now_s))
    return 0;
  sqlite3_stmt *st = NULL;
  int rc = sqlite3_prepare_v2 (db, "DELETE FROM broadcasts WHERE ttl_expires_at IS NOT NULL AND ttl_expires_at <= ?1;", -1, &st, NULL);
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
    return 0;
  int rc = begin (db);
  if (rc)
    return rc;

  rc = sqlite3_exec (db, "INSERT INTO jobs(type, payload, created_at) SELECT 'trap.trigger', json_object('trap_id',id), ?1 FROM traps WHERE armed=1 AND trigger_at IS NOT NULL AND trigger_at <= ?1;", NULL, NULL, NULL);
  if (rc)
    {
      rollback (db);
      LOGE ("traps_process insert rc=%d", rc);
      return rc;
    }

  rc = sqlite3_exec (db, "DELETE FROM traps WHERE armed=1 AND trigger_at IS NOT NULL AND trigger_at <= ?1;", NULL, NULL, NULL);
  if (rc)
    {
      rollback (db);
      LOGE ("traps_process delete rc=%d", rc);
      return rc;
    }

  commit (db);
  unlock (db, "traps_process");
  return 0;
}

int
h_npc_step (sqlite3 *db, int64_t now_s)
{
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

//////////////////////// NEWS BLOCK ////////////////////////

int h_daily_news_compiler(sqlite3 *db, int64_t now_s) {
    if (!try_lock(db, "daily_news_compiler", now_s)) {
        return 0;
    }

    LOGI("h_daily_news_compiler: Starting daily news compilation.");

    int rc = SQLITE_OK;
    sqlite3_stmt *st = NULL;
    int64_t yesterday_s = now_s - 86400; // 24 hours ago

    const char *sql_select_events =
        "SELECT id, ts, type, actor_player_id, sector_id, payload "
        "FROM engine_events "
        "WHERE ts >= ?1 AND ts < ?2 "
        "ORDER BY ts ASC;";

    rc = sqlite3_prepare_v2(db, sql_select_events, -1, &st, NULL);
    if (rc != SQLITE_OK) {
        LOGE("h_daily_news_compiler: Failed to prepare statement for engine_events: %s", sqlite3_errmsg(db));
        goto cleanup;
    }

    sqlite3_bind_int64(st, 1, yesterday_s);
    sqlite3_bind_int64(st, 2, now_s);

    while ((rc = sqlite3_step(st)) == SQLITE_ROW) {
        int event_ts = sqlite3_column_int64(st, 1);
        const char *event_type = (const char *)sqlite3_column_text(st, 2);
        int actor_player_id = sqlite3_column_int(st, 3);
        int sector_id = sqlite3_column_int(st, 4);
        const char *payload_str = (const char *)sqlite3_column_text(st, 5);

        json_error_t jerr;
        json_t *payload_obj = json_loads(payload_str, 0, &jerr);
        if (!payload_obj) {
            LOGW("h_daily_news_compiler: Failed to parse JSON payload for event type '%s': %s", event_type, jerr.text);
            continue;
        }

        const char *headline = NULL;
        const char *body = NULL;
        const char *category = "report"; // Default category
        const char *scope = "global";    // Default scope
        json_t *context_data = json_object();
        char *headline_str = NULL; // For asprintf
        char *body_str = NULL;     // For asprintf
        char *scope_str = NULL;    // For asprintf

        // Add common context data
        json_object_set_new(context_data, "event_type", json_string(event_type));
        if (actor_player_id > 0) {
            json_object_set_new(context_data, "actor_player_id", json_integer(actor_player_id));
        }
                    if (sector_id > 0) {
                        json_object_set_new(context_data, "sector_id", json_integer(sector_id));
                        if (asprintf(&scope_str, "sector:%d", sector_id) == -1) {
                            LOGE("h_daily_news_compiler: Failed to allocate scope_str.");
                            json_decref(payload_obj);
                            json_decref(context_data);
                            continue;
                        }
                        scope = scope_str; // Dynamic scope
                    }
        
                    if (strcasecmp(event_type, "commodity.boom") == 0 || strcasecmp(event_type, "commodity.bust") == 0) {
                        const char *commodity = json_string_value(json_object_get(payload_obj, "commodity"));
                        const char *location = json_string_value(json_object_get(payload_obj, "location"));
                        double price = json_real_value(json_object_get(payload_obj, "price"));
        
                        if (commodity && location) {
                            category = "economic";
                            if (strcasecmp(event_type, "commodity.boom") == 0) {
                                if (asprintf(&headline_str, "Economic Boom! %s Prices Soar in %s!", commodity, location) == -1) {
                                    LOGE("h_daily_news_compiler: Failed to allocate headline_str for commodity.boom.");
                                    goto next_event_cleanup;
                                }
                                headline = headline_str;
                                if (asprintf(&body_str, "The price of %s has surged to %.2f credits in %s, indicating strong market demand.", commodity, price, location) == -1) {
                                    LOGE("h_daily_news_compiler: Failed to allocate body_str for commodity.boom.");
                                    goto next_event_cleanup;
                                }
                                body = body_str;
                            } else { // commodity.bust
                                if (asprintf(&headline_str, "Market Crash! %s Prices Plummet in %s!", commodity, location) == -1) {
                                    LOGE("h_daily_news_compiler: Failed to allocate headline_str for commodity.bust.");
                                    goto next_event_cleanup;
                                }
                                headline = headline_str;
                                if (asprintf(&body_str, "A sudden drop has seen %s prices fall to %.2f credits in %s, causing market instability.", commodity, price, location) == -1) {
                                    LOGE("h_daily_news_compiler: Failed to allocate body_str for commodity.bust.");
                                    goto next_event_cleanup;
                                }
                                body = body_str;
                            }
                            json_object_set_new(context_data, "commodity", json_string(commodity));
                            json_object_set_new(context_data, "location", json_string(location));
                            json_object_set_new(context_data, "price", json_real(price));
                        }
                    } else if (strcasecmp(event_type, "ship.destroyed") == 0) {
                        int destroyed_player_id = json_integer_value(json_object_get(payload_obj, "player_id"));
                        int destroyed_ship_id = json_integer_value(json_object_get(payload_obj, "ship_id"));
                        const char *ship_name = json_string_value(json_object_get(payload_obj, "ship_name"));
        
                        if (ship_name) {
                            category = "military";
                            if (asprintf(&headline_str, "Ship %s Destroyed in Sector %d!", ship_name, sector_id) == -1) {
                                LOGE("h_daily_news_compiler: Failed to allocate headline_str for ship.destroyed.");
                                goto next_event_cleanup;
                            }
                            headline = headline_str;
                            if (asprintf(&body_str, "Reports indicate the ship '%s' (ID: %d), associated with Player ID %d, was destroyed in Sector %d. The cause is currently under investigation.", ship_name, destroyed_ship_id, destroyed_player_id, sector_id) == -1) {
                                LOGE("h_daily_news_compiler: Failed to allocate body_str for ship.destroyed.");
                                goto next_event_cleanup;
                            }
                            body = body_str;
                            json_object_set_new(context_data, "destroyed_player_id", json_integer(destroyed_player_id));
                            json_object_set_new(context_data, "destroyed_ship_id", json_integer(destroyed_ship_id));
                            json_object_set_new(context_data, "ship_name", json_string(ship_name));
                        }
                    } else if (strcasecmp(event_type, "fedspace:tow") == 0) {
                        const char *reason = json_string_value(json_object_get(payload_obj, "reason"));
                        if (reason) {
                            category = "military";
                            if (asprintf(&headline_str, "Federal Authorities Tow Ship from Sector %d!", sector_id) == -1) {
                                LOGE("h_daily_news_compiler: Failed to allocate headline_str for fedspace:tow.");
                                goto next_event_cleanup;
                            }
                            headline = headline_str;
                            if (asprintf(&body_str, "A ship was forcibly towed from FedSpace Sector %d due to a violation of Federal Law: %s. Owners are advised to review regulations.", sector_id, reason) == -1) {
                                LOGE("h_daily_news_compiler: Failed to allocate body_str for fedspace:tow.");
                                goto next_event_cleanup;
                            }
                            body = body_str;
                        }
                    } else if (strcasecmp(event_type, "combat.ship_destroyed") == 0) {
                        const char *ship_name = json_string_value(json_object_get(payload_obj, "ship_name"));
                        int victim_player_id = json_integer_value(json_object_get(payload_obj, "victim_player_id"));
                        int attacker_player_id = json_integer_value(json_object_get(payload_obj, "attacker_player_id"));
        
                        if (ship_name) {
                            category = "military";
                            if (asprintf(&headline_str, "Ship '%s' Destroyed in Combat!", ship_name) == -1) {
                                LOGE("h_daily_news_compiler: Failed to allocate headline_str for combat.ship_destroyed.");
                                goto next_event_cleanup;
                            }
                            headline = headline_str;
                            if (asprintf(&body_str, "The ship '%s' (owned by Player ID %d) was destroyed in Sector %d, reportedly by Player ID %d.", ship_name, victim_player_id, sector_id, attacker_player_id) == -1) {
                                LOGE("h_daily_news_compiler: Failed to allocate body_str for combat.ship_destroyed.");
                                goto next_event_cleanup;
                            }
                            body = body_str;
                            json_object_set_new(context_data, "ship_name", json_string(ship_name));
                            json_object_set_new(context_data, "victim_player_id", json_integer(victim_player_id));
                            json_object_set_new(context_data, "attacker_player_id", json_integer(attacker_player_id));
                        }
                    }
                    // Add more event types here as needed for news generation
        
                    if (headline && body) {
                        db_news_insert_feed_item(event_ts, category, scope, headline, body, context_data);
                    } else {
                        LOGD("h_daily_news_compiler: No news generated for event type '%s'.", event_type);
                    }
        
                next_event_cleanup:
                    free(headline_str); // Free strings allocated by asprintf
                    free(body_str);
                    if (scope_str) free(scope_str); // Free dynamic scope string
                    json_decref(payload_obj);
                    json_decref(context_data);
                }
        
                if (rc != SQLITE_DONE) {
                    LOGE("h_daily_news_compiler: Error stepping through engine_events: %s", sqlite3_errmsg(db));
                }
        
            cleanup:
                if (st) {
                    sqlite3_finalize(st);
                }
                unlock(db, "daily_news_compiler");
                LOGI("h_daily_news_compiler: Finished daily news compilation.");
                return rc;
            }
int h_cleanup_old_news(sqlite3 *db, int64_t now_s) {
    if (!try_lock(db, "cleanup_old_news", now_s)) {
        return 0;
    }

    LOGI("cleanup_old_news: Starting cleanup of old news articles.");

    sqlite3_stmt *stmt = NULL;
    const char *sql = "DELETE FROM news_feed WHERE timestamp < ?;";
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);

    if (rc != SQLITE_OK) {
        LOGE("cleanup_old_news: Failed to prepare delete statement: %s", sqlite3_errmsg(db));
        unlock(db, "cleanup_old_news");
        return rc;
    }

    // 7 days ago (604800 seconds)
    sqlite3_bind_int64(stmt, 1, now_s - 604800);

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        LOGE("cleanup_old_news: Failed to execute delete statement: %s", sqlite3_errmsg(db));
    } else {
        int changes = sqlite3_changes(db);
        if (changes > 0) {
            LOGI("cleanup_old_news: Deleted %d old news articles.", changes);
        }
    }

    sqlite3_finalize(stmt);
    unlock(db, "cleanup_old_news");

    return rc == SQLITE_DONE ? SQLITE_OK : rc;
}

int
h_daily_lottery_draw (sqlite3 *db, int64_t now_s)
{
  if (!try_lock (db, "daily_lottery_draw", now_s))
    return 0;

  LOGI ("daily_lottery_draw: Starting daily lottery draw.");

  int rc = begin (db);
  if (rc)
    {
      unlock (db, "daily_lottery_draw");
      return rc;
    }

  char draw_date_str[32];
  struct tm *tm_info = localtime(&now_s);
  strftime(draw_date_str, sizeof(draw_date_str), "%Y-%m-%d", tm_info);

  // Check if today's draw is already processed
  sqlite3_stmt *st_check = NULL;
  const char *sql_check = "SELECT winning_number, jackpot, carried_over FROM tavern_lottery_state WHERE draw_date = ?;";
  rc = sqlite3_prepare_v2(db, sql_check, -1, &st_check, NULL);
  if (rc != SQLITE_OK) {
      LOGE("daily_lottery_draw: Failed to prepare check statement: %s", sqlite3_errmsg(db));
      goto rollback_and_unlock;
  }
  sqlite3_bind_text(st_check, 1, draw_date_str, -1, SQLITE_STATIC);
  if (sqlite3_step(st_check) == SQLITE_ROW && sqlite3_column_type(st_check, 0) != SQLITE_NULL) {
      LOGI("daily_lottery_draw: Lottery for %s already processed. Skipping.", draw_date_str);
      sqlite3_finalize(st_check);
      goto commit_and_unlock;
  }
  sqlite3_finalize(st_check);
  st_check = NULL;

  // Get yesterday's jackpot and carried over amount
  long long yesterday_jackpot = 0;
  long long yesterday_carried_over = 0;
  char yesterday_date_str[32];
  time_t yesterday_s = now_s - (24 * 60 * 60);
  tm_info = localtime(&yesterday_s);
  strftime(yesterday_date_str, sizeof(yesterday_date_str), "%Y-%m-%d", tm_info);

  const char *sql_yesterday_state = "SELECT jackpot, carried_over FROM tavern_lottery_state WHERE draw_date = ?;";
  rc = sqlite3_prepare_v2(db, sql_yesterday_state, -1, &st_check, NULL);
  if (rc == SQLITE_OK) {
      sqlite3_bind_text(st_check, 1, yesterday_date_str, -1, SQLITE_STATIC);
      if (sqlite3_step(st_check) == SQLITE_ROW) {
          yesterday_jackpot = sqlite3_column_int64(st_check, 0);
          yesterday_carried_over = sqlite3_column_int64(st_check, 1);
      }
      sqlite3_finalize(st_check);
      st_check = NULL;
  }

  // Calculate total tickets sold today
  sqlite3_stmt *st_tickets = NULL;
  long long total_tickets_sold = 0;
  long long total_pot_from_tickets = 0;
  const char *sql_sum_tickets = "SELECT COUNT(*), SUM(cost) FROM tavern_lottery_tickets WHERE draw_date = ?;";
  rc = sqlite3_prepare_v2(db, sql_sum_tickets, -1, &st_tickets, NULL);
  if (rc != SQLITE_OK) {
      LOGE("daily_lottery_draw: Failed to prepare sum tickets statement: %s", sqlite3_errmsg(db));
      goto rollback_and_unlock;
  }
  sqlite3_bind_text(st_tickets, 1, draw_date_str, -1, SQLITE_STATIC);
  if (sqlite3_step(st_tickets) == SQLITE_ROW) {
      total_tickets_sold = sqlite3_column_int64(st_tickets, 0);
      total_pot_from_tickets = sqlite3_column_int64(st_tickets, 1);
  }
  sqlite3_finalize(st_tickets);
  st_tickets = NULL;

  // Calculate current jackpot: carried over from yesterday + 50% of today's ticket sales
  long long current_jackpot = yesterday_carried_over + (total_pot_from_tickets / 2);

  int winning_number = get_random_int(1, 999);
  long long total_winnings = 0;
  bool winner_found = false;

  // Find winning tickets and distribute winnings
  sqlite3_stmt *st_winners = NULL;
  const char *sql_winners = "SELECT player_id, number, cost FROM tavern_lottery_tickets WHERE draw_date = ? AND number = ?;";
  rc = sqlite3_prepare_v2(db, sql_winners, -1, &st_winners, NULL);
  if (rc != SQLITE_OK) {
      LOGE("daily_lottery_draw: Failed to prepare winners statement: %s", sqlite3_errmsg(db));
      goto rollback_and_unlock;
  }
  sqlite3_bind_text(st_winners, 1, draw_date_str, -1, SQLITE_STATIC);
  sqlite3_bind_int(st_winners, 2, winning_number);

  json_t *winners_array = json_array(); // To store winners for logging
  while (sqlite3_step(st_winners) == SQLITE_ROW) {
      winner_found = true;
      int player_id = sqlite3_column_int(st_winners, 0);
      
      // For simplicity, total jackpot split evenly among winners
      // In a real game, might want to consider partial matches, etc.
      json_t *winner_obj = json_object();
      json_object_set_new(winner_obj, "player_id", json_integer(player_id));
      json_array_append_new(winners_array, winner_obj);
  }
  sqlite3_finalize(st_winners);
  st_winners = NULL;

  if (winner_found) {
      long long payout_per_winner = current_jackpot / json_array_size(winners_array);
      total_winnings = current_jackpot;

      for (size_t i = 0; i < json_array_size(winners_array); i++) {
          json_t *winner_obj = json_array_get(winners_array, i);
          int player_id = json_integer_value(json_object_get(winner_obj, "player_id"));
          
          // Add credits to winner
          int add_rc = h_add_credits(db, "player", player_id, payout_per_winner, "LOTTERY_WIN", NULL, NULL);
          if (add_rc != SQLITE_OK) {
              LOGE("daily_lottery_draw: Failed to add winnings to player %d: %s", player_id, sqlite3_errmsg(db));
              // Error here is critical, consider specific handling or abort
          } else {
              json_object_set_new(winner_obj, "winnings", json_integer(payout_per_winner));
          }
      }
      current_jackpot = 0; // Jackpot cleared
      yesterday_carried_over = 0; // No rollover
  } else {
      // No winner, 50% of today's sales carried over to next jackpot
      yesterday_carried_over = total_pot_from_tickets / 2; // Rollover 50% of tickets sales
      current_jackpot = 0; // Jackpot cleared
      LOGI("daily_lottery_draw: No winner found for %s. Jackpot rolls over.", draw_date_str);
  }

  // Update or insert tavern_lottery_state for today
  sqlite3_stmt *st_update_state = NULL;
  const char *sql_update_state =
      "INSERT INTO tavern_lottery_state (draw_date, winning_number, jackpot, carried_over) VALUES (?, ?, ?, ?) "
      "ON CONFLICT(draw_date) DO UPDATE SET winning_number = excluded.winning_number, jackpot = excluded.jackpot, carried_over = excluded.carried_over;";
  rc = sqlite3_prepare_v2(db, sql_update_state, -1, &st_update_state, NULL);
  if (rc != SQLITE_OK) {
      LOGE("daily_lottery_draw: Failed to prepare update state statement: %s", sqlite3_errmsg(db));
      json_decref(winners_array);
      goto rollback_and_unlock;
  }
  sqlite3_bind_text(st_update_state, 1, draw_date_str, -1, SQLITE_STATIC);
  if (winner_found) {
      sqlite3_bind_int(st_update_state, 2, winning_number);
  } else {
      sqlite3_bind_null(st_update_state, 2); // No winning number if no winner
  }
  sqlite3_bind_int64(st_update_state, 3, current_jackpot);
  sqlite3_bind_int64(st_update_state, 4, yesterday_carried_over);

  if (sqlite3_step(st_update_state) != SQLITE_DONE) {
      LOGE("daily_lottery_draw: Failed to update lottery state: %s", sqlite3_errmsg(db));
      json_decref(winners_array);
      goto rollback_and_unlock;
  }
  sqlite3_finalize(st_update_state);
  st_update_state = NULL;
  
  LOGI("daily_lottery_draw: Draw for %s completed. Winning number: %d. Jackpot: %lld. Winners: %s",
       draw_date_str, winning_number, current_jackpot, winner_found ? json_dumps(winners_array, 0) : "None");
  
  json_decref(winners_array);

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
  if (st_check) sqlite3_finalize(st_check);
  if (st_tickets) sqlite3_finalize(st_tickets);
  if (st_winners) sqlite3_finalize(st_winners);
  if (st_update_state) sqlite3_finalize(st_update_state);
  rollback (db);
  unlock (db, "daily_lottery_draw");
  return rc;
}

int
h_deadpool_resolution_cron (sqlite3 *db, int64_t now_s)
{
  if (!try_lock (db, "deadpool_resolution_cron", now_s))
    return 0;

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
  const char *sql_expire = "UPDATE tavern_deadpool_bets SET resolved = 1, result = 'expired', resolved_at = ? WHERE resolved = 0 AND expires_at <= ?;";
  rc = sqlite3_prepare_v2(db, sql_expire, -1, &st_expire, NULL);
  if (rc != SQLITE_OK) {
      LOGE("deadpool_resolution_cron: Failed to prepare expire statement: %s", sqlite3_errmsg(db));
      goto rollback_and_unlock;
  }
  sqlite3_bind_int(st_expire, 1, (int)now_s);
  sqlite3_bind_int(st_expire, 2, (int)now_s);
  if (sqlite3_step(st_expire) != SQLITE_DONE) {
      LOGE("deadpool_resolution_cron: Failed to expire old bets: %s", sqlite3_errmsg(db));
      goto rollback_and_unlock;
  }
  sqlite3_finalize(st_expire);
  st_expire = NULL;

  // 2. Process ship.destroyed events
  sqlite3_stmt *st_events = NULL;
  const char *sql_events =
      "SELECT payload FROM engine_events WHERE type = 'ship.destroyed' AND ts > (SELECT COALESCE(MAX(resolved_at), 0) FROM tavern_deadpool_bets WHERE result IS NOT NULL AND result != 'expired');";
  rc = sqlite3_prepare_v2(db, sql_events, -1, &st_events, NULL);
  if (rc != SQLITE_OK) {
      LOGE("deadpool_resolution_cron: Failed to prepare events statement: %s", sqlite3_errmsg(db));
      goto rollback_and_unlock;
  }

  while (sqlite3_step(st_events) == SQLITE_ROW) {
      const char *payload_str = (const char *)sqlite3_column_text(st_events, 0);
      json_error_t jerr;
      json_t *payload_obj = json_loads(payload_str, 0, &jerr);
      if (!payload_obj) {
          LOGW("deadpool_resolution_cron: Failed to parse event payload: %s", jerr.text);
          continue;
      }
      int destroyed_player_id = json_integer_value(json_object_get(payload_obj, "player_id"));
      json_decref(payload_obj);

      if (destroyed_player_id <= 0) continue;

      // Find matching unresolved bets for the destroyed player
      sqlite3_stmt *st_bets = NULL;
      const char *sql_bets = "SELECT id, bettor_id, amount, odds_bp FROM tavern_deadpool_bets WHERE target_id = ? AND resolved = 0;";
      rc = sqlite3_prepare_v2(db, sql_bets, -1, &st_bets, NULL);
      if (rc != SQLITE_OK) {
          LOGE("deadpool_resolution_cron: Failed to prepare bets statement: %s", sqlite3_errmsg(db));
          continue;
      }
      sqlite3_bind_int(st_bets, 1, destroyed_player_id);

      while (sqlite3_step(st_bets) == SQLITE_ROW) {
          int bet_id = sqlite3_column_int(st_bets, 0);
          int bettor_id = sqlite3_column_int(st_bets, 1);
          long long amount = sqlite3_column_int64(st_bets, 2);
          int odds_bp = sqlite3_column_int(st_bets, 3);

          long long payout = (amount * odds_bp) / 10000; // Calculate payout
          if (payout < 0) payout = 0; // Ensure payout is not negative

          // Payout to winner
          int add_rc = h_add_credits(db, "player", bettor_id, payout, "DEADPOOL_WIN", NULL, NULL);
          if (add_rc != SQLITE_OK) {
              LOGE("deadpool_resolution_cron: Failed to payout winnings to player %d for bet %d: %s", bettor_id, bet_id, sqlite3_errmsg(db));
              // Log and continue, or abort transaction if critical
          }

          // Mark bet as won
          sqlite3_stmt *st_update_bet = NULL;
          const char *sql_update_bet = "UPDATE tavern_deadpool_bets SET resolved = 1, result = 'won', resolved_at = ? WHERE id = ?;";
          rc = sqlite3_prepare_v2(db, sql_update_bet, -1, &st_update_bet, NULL);
          if (rc != SQLITE_OK) {
              LOGE("deadpool_resolution_cron: Failed to prepare update bet statement: %s", sqlite3_errmsg(db));
              goto rollback_and_unlock;
          }
          sqlite3_bind_int(st_update_bet, 1, (int)now_s);
          sqlite3_bind_int(st_update_bet, 2, bet_id);
          if (sqlite3_step(st_update_bet) != SQLITE_DONE) {
              LOGE("deadpool_resolution_cron: Failed to mark bet %d as won: %s", bet_id, sqlite3_errmsg(db));
              goto rollback_and_unlock;
          }
          sqlite3_finalize(st_update_bet);
          st_update_bet = NULL;
      }
      sqlite3_finalize(st_bets);
      st_bets = NULL;

      // Mark all other unresolved bets on this target as lost
      sqlite3_stmt *st_lost_bets = NULL;
      const char *sql_lost_bets = "UPDATE tavern_deadpool_bets SET resolved = 1, result = 'lost', resolved_at = ? WHERE target_id = ? AND resolved = 0;";
      rc = sqlite3_prepare_v2(db, sql_lost_bets, -1, &st_lost_bets, NULL);
      if (rc != SQLITE_OK) {
          LOGE("deadpool_resolution_cron: Failed to prepare lost bets statement: %s", sqlite3_errmsg(db));
          goto rollback_and_unlock;
      }
      sqlite3_bind_int(st_lost_bets, 1, (int)now_s);
      sqlite3_bind_int(st_lost_bets, 2, destroyed_player_id);
      if (sqlite3_step(st_lost_bets) != SQLITE_DONE) {
          LOGE("deadpool_resolution_cron: Failed to mark lost bets for target %d: %s", destroyed_player_id, sqlite3_errmsg(db));
          goto rollback_and_unlock;
      }
      sqlite3_finalize(st_lost_bets);
      st_lost_bets = NULL;
  }
  sqlite3_finalize(st_events);
  st_events = NULL;

  rc = commit (db);
  if (rc != SQLITE_OK)
    {
      LOGE ("deadpool_resolution_cron: commit failed: %s", sqlite3_errmsg (db));
      goto rollback_and_unlock;
    }

  LOGI ("deadpool_resolution_cron: Dead Pool bet resolution completed.");
  unlock (db, "deadpool_resolution_cron");
  return SQLITE_OK;

rollback_and_unlock:
  if (st_expire) sqlite3_finalize(st_expire);
  if (st_events) sqlite3_finalize(st_events);
  if (st_bets) sqlite3_finalize(st_bets);
  if (st_update_bet) sqlite3_finalize(st_update_bet);
  if (st_lost_bets) sqlite3_finalize(st_lost_bets);
  rollback (db);
  unlock (db, "deadpool_resolution_cron");
  return rc;
}

int
h_tavern_notice_expiry_cron (sqlite3 *db, int64_t now_s)
{
  if (!try_lock (db, "tavern_notice_expiry_cron", now_s))
    return 0;

  LOGI ("tavern_notice_expiry_cron: Starting Tavern notice and corp recruiting expiry cleanup.");

  int rc = begin (db);
  if (rc)
    {
      unlock (db, "tavern_notice_expiry_cron");
      return rc;
    }

  sqlite3_stmt *st = NULL;
  int deleted_count = 0;

  // Delete expired tavern_notices
  const char *sql_delete_notices = "DELETE FROM tavern_notices WHERE expires_at <= ?;";
  rc = sqlite3_prepare_v2(db, sql_delete_notices, -1, &st, NULL);
  if (rc != SQLITE_OK) {
      LOGE("tavern_notice_expiry_cron: Failed to prepare notices delete statement: %s", sqlite3_errmsg(db));
      goto rollback_and_unlock;
  }
  sqlite3_bind_int(st, 1, (int)now_s);
  if (sqlite3_step(st) != SQLITE_DONE) {
      LOGE("tavern_notice_expiry_cron: Failed to delete expired notices: %s", sqlite3_errmsg(db));
      goto rollback_and_unlock;
  }
  deleted_count += sqlite3_changes(db);
  sqlite3_finalize(st);
  st = NULL;

  // Delete expired corp_recruiting entries
  const char *sql_delete_corp_recruiting = "DELETE FROM corp_recruiting WHERE expires_at <= ?;";
  rc = sqlite3_prepare_v2(db, sql_delete_corp_recruiting, -1, &st, NULL);
  if (rc != SQLITE_OK) {
      LOGE("tavern_notice_expiry_cron: Failed to prepare corp recruiting delete statement: %s", sqlite3_errmsg(db));
      goto rollback_and_unlock;
  }
  sqlite3_bind_int(st, 1, (int)now_s);
  if (sqlite3_step(st) != SQLITE_DONE) {
      LOGE("tavern_notice_expiry_cron: Failed to delete expired corp recruiting entries: %s", sqlite3_errmsg(db));
      goto rollback_and_unlock;
  }
  deleted_count += sqlite3_changes(db);
  sqlite3_finalize(st);
  st = NULL;

  rc = commit (db);
  if (rc != SQLITE_OK)
    {
      LOGE ("tavern_notice_expiry_cron: commit failed: %s", sqlite3_errmsg (db));
      goto rollback_and_unlock;
    }

  LOGI ("tavern_notice_expiry_cron: Removed %d expired Tavern notices and corp recruiting entries.", deleted_count);
  unlock (db, "tavern_notice_expiry_cron");
  return SQLITE_OK;

rollback_and_unlock:
  if (st) sqlite3_finalize(st);
  rollback (db);
  unlock (db, "tavern_notice_expiry_cron");
  return rc;
}

int
h_loan_shark_interest_cron (sqlite3 *db, int64_t now_s)
{
  if (!try_lock (db, "loan_shark_interest_cron", now_s))
    return 0;

  if (!g_tavern_cfg.loan_shark_enabled) {
      LOGI("loan_shark_interest_cron: Loan Shark is disabled in config. Skipping.");
      unlock(db, "loan_shark_interest_cron");
      return 0;
  }

  LOGI ("loan_shark_interest_cron: Starting Loan Shark interest and default processing.");

  int rc = begin (db);
  if (rc)
    {
      unlock (db, "loan_shark_interest_cron");
      return rc;
    }

  sqlite3_stmt *st = NULL;
  const char *sql_select_loans = "SELECT player_id, principal, interest_rate, due_date, is_defaulted FROM tavern_loans WHERE principal > 0;";
  rc = sqlite3_prepare_v2(db, sql_select_loans, -1, &st, NULL);
  if (rc != SQLITE_OK) {
      LOGE("loan_shark_interest_cron: Failed to prepare select loans statement: %s", sqlite3_errmsg(db));
      goto rollback_and_unlock;
  }

  int processed_loans = 0;
  while (sqlite3_step(st) == SQLITE_ROW) {
      int player_id = sqlite3_column_int(st, 0);
      long long principal = sqlite3_column_int64(st, 1);
      int interest_rate = sqlite3_column_int(st, 2);
      // int due_date = sqlite3_column_int(st, 3); // Not directly used in this loop for application
      // int is_defaulted = sqlite3_column_int(st, 4); // Handled by check_loan_default implicitly

      // Apply interest
      int apply_rc = apply_loan_interest(db, player_id, principal, interest_rate);
      if (apply_rc != SQLITE_OK) {
          LOGE("loan_shark_interest_cron: Failed to apply interest for player %d: %s", player_id, sqlite3_errmsg(db));
          // Decide whether to continue or abort for this player's loan
      }

      // Check for default status
      // Note: check_loan_default will update the DB itself if a loan becomes defaulted
      check_loan_default(db, player_id, (int)now_s);
      
      processed_loans++;
  }

  if (rc != SQLITE_DONE) {
      LOGE("loan_shark_interest_cron: Error stepping through loans: %s", sqlite3_errmsg(db));
      goto rollback_and_unlock;
  }
  sqlite3_finalize(st);
  st = NULL;

  rc = commit (db);
  if (rc != SQLITE_OK)
    {
      LOGE ("loan_shark_interest_cron: commit failed: %s", sqlite3_errmsg (db));
      goto rollback_and_unlock;
    }

  LOGI ("loan_shark_interest_cron: Processed interest and defaults for %d loans.", processed_loans);
  unlock (db, "loan_shark_interest_cron");
  return SQLITE_OK;

rollback_and_unlock:
  if (st) sqlite3_finalize(st);
  rollback (db);
  unlock (db, "loan_shark_interest_cron");
  return rc;
}

int
h_daily_market_settlement (sqlite3 *db, int64_t now_s)
{
  if (!try_lock (db, "daily_market_settlement", now_s))
    return 0;

  LOGI ("daily_market_settlement: Starting daily market settlement.");

  int rc = begin (db);
  if (rc)
    {
      unlock (db, "daily_market_settlement");
      return rc;
    }

  LOGI ("daily_market_settlement: Running planet growth.");
  rc = h_planet_growth(db, now_s);
  if (rc != SQLITE_OK) {
      LOGE ("daily_market_settlement: h_planet_growth failed: %s", sqlite3_errmsg (db));
      rollback (db);
      unlock (db, "daily_market_settlement");
      return rc;
  }

  LOGI ("daily_market_settlement: Processing AI Order Placement for NPC Planets and Ports.");

  // --- NPC Planet Order Generation ---
  sqlite3_stmt *planet_stmt = NULL;
  const char *sql_planets = "SELECT id, name FROM planets WHERE owner_player_id IS NULL OR owner_player_id = 0;"; // Assuming NPC planets have no player owner
  rc = sqlite3_prepare_v2(db, sql_planets, -1, &planet_stmt, NULL);
  if (rc != SQLITE_OK) {
      LOGE("daily_market_settlement: Failed to prepare planet select: %s", sqlite3_errmsg(db));
      goto rollback_and_unlock;
  }

  while (sqlite3_step(planet_stmt) == SQLITE_ROW) {
      int planet_id = sqlite3_column_int(planet_stmt, 0);
      const char *planet_name = (const char *)sqlite3_column_text(planet_stmt, 1);

      sqlite3_stmt *goods_stmt = NULL;
      const char *sql_goods = "SELECT commodity, quantity, max_capacity, production_rate FROM planet_goods WHERE planet_id = ?;";
      rc = sqlite3_prepare_v2(db, sql_goods, -1, &goods_stmt, NULL);
      if (rc != SQLITE_OK) {
          LOGE("daily_market_settlement: Failed to prepare planet_goods select for planet %d: %s", planet_id, sqlite3_errmsg(db));
          sqlite3_finalize(goods_stmt); // Ensure cleanup
          continue;
      }
      sqlite3_bind_int(goods_stmt, 1, planet_id);

      while (sqlite3_step(goods_stmt) == SQLITE_ROW) {
          const char *commodity_code = (const char *)sqlite3_column_text(goods_stmt, 0);
          int quantity = sqlite3_column_int(goods_stmt, 1);
          int max_capacity = sqlite3_column_int(goods_stmt, 2);
          int production_rate = sqlite3_column_int(goods_stmt, 3);

          // Simple economic logic:
          // If quantity is low (e.g., < 25% of max_capacity), place a buy order.
          // If quantity is high (e.g., > 75% of max_capacity), place a sell order.
          // Adjust price based on how critical the need/surplus is.

          int order_quantity = 0;
          int order_price = 0;
          const char *order_side = NULL;

          // Get commodity base price and volatility
          sqlite3_stmt *comm_info_stmt = NULL;
          int base_price = 0;
          int volatility = 0;
          const char *sql_comm_info = "SELECT base_price, volatility FROM commodities WHERE code = ?;";
          if (sqlite3_prepare_v2(db, sql_comm_info, -1, &comm_info_stmt, NULL) == SQLITE_OK) {
              sqlite3_bind_text(comm_info_stmt, 1, commodity_code, -1, SQLITE_STATIC);
              if (sqlite3_step(comm_info_stmt) == SQLITE_ROW) {
                  base_price = sqlite3_column_int(comm_info_stmt, 0);
                  volatility = sqlite3_column_int(comm_info_stmt, 1);
              }
              sqlite3_finalize(comm_info_stmt);
          }

          if (base_price == 0) { // Skip if commodity info not found
              LOGW("daily_market_settlement: Commodity %s info not found, skipping order generation for planet %d.", commodity_code, planet_id);
              continue;
          }

          if (quantity < max_capacity * 0.25) { // Low stock, need to buy
              order_side = "buy";
              order_quantity = max_capacity * 0.5 - quantity; // Buy up to 50% of capacity
              order_price = base_price + (base_price * (max_capacity * 0.25 - quantity) / (max_capacity * 0.25)) * (volatility / 100.0); // Higher price for critical need
              if (order_quantity < 1) order_quantity = 1;
              if (order_price < 1) order_price = 1;
          } else if (quantity > max_capacity * 0.75) { // High stock, need to sell
              order_side = "sell";
              order_quantity = quantity - max_capacity * 0.5; // Sell down to 50% of capacity
              order_price = base_price - (base_price * (quantity - max_capacity * 0.75) / (max_capacity * 0.25)) * (volatility / 100.0); // Lower price for surplus
              if (order_quantity < 1) order_quantity = 1;
              if (order_price < 1) order_price = 1;
          }

          if (order_side && order_quantity > 0) {
              sqlite3_stmt *insert_order_stmt = NULL;
              const char *sql_insert_order =
                  "INSERT INTO commodity_orders (actor_type, actor_id, location_type, location_id, commodity_id, side, quantity, price, ts) "
                  "VALUES ('npc_planet', ?, 'planet', ?, (SELECT id FROM commodities WHERE code = ?), ?, ?, ?, strftime('%Y-%m-%dT%H:%M:%fZ','now'));";
              rc = sqlite3_prepare_v2(db, sql_insert_order, -1, &insert_order_stmt, NULL);
              if (rc != SQLITE_OK) {
                  LOGE("daily_market_settlement: Failed to prepare insert order statement for planet %d: %s", planet_id, sqlite3_errmsg(db));
                  sqlite3_finalize(insert_order_stmt);
                  continue;
              }
              sqlite3_bind_int(insert_order_stmt, 1, planet_id);
              sqlite3_bind_int(insert_order_stmt, 2, planet_id);
              sqlite3_bind_text(insert_order_stmt, 3, commodity_code, -1, SQLITE_STATIC);
              sqlite3_bind_text(insert_order_stmt, 4, order_side, -1, SQLITE_STATIC);
              sqlite3_bind_int(insert_order_stmt, 5, order_quantity);
              sqlite3_bind_int(insert_order_stmt, 6, order_price);

              if (sqlite3_step(insert_order_stmt) != SQLITE_DONE) {
                  LOGE("daily_market_settlement: Failed to insert order for planet %d, commodity %s: %s", planet_id, commodity_code, sqlite3_errmsg(db));
              } else {
                  LOGD("daily_market_settlement: Generated %s order for %d units of %s at %d credits for planet %s (ID: %d).", order_side, order_quantity, commodity_code, order_price, planet_name, planet_id);
              }
              sqlite3_finalize(insert_order_stmt);
          }
      }
      sqlite3_finalize(goods_stmt);
  }
  sqlite3_finalize(planet_stmt);

  // --- Port Order Generation (similar logic) ---
  sqlite3_stmt *port_stmt = NULL;
  const char *sql_ports = "SELECT id, name FROM ports;";
  rc = sqlite3_prepare_v2(db, sql_ports, -1, &port_stmt, NULL);
  if (rc != SQLITE_OK) {
      LOGE("daily_market_settlement: Failed to prepare port select: %s", sqlite3_errmsg(db));
      goto rollback_and_unlock;
  }

  while (sqlite3_step(port_stmt) == SQLITE_ROW) {
      int port_id = sqlite3_column_int(port_stmt, 0);
      const char *port_name = (const char *)sqlite3_column_text(port_stmt, 1);

      sqlite3_stmt *stock_stmt = NULL;
      const char *sql_stock = "SELECT 'ore' AS commodity, ore_on_hand AS quantity, DEF_PORT_MAX_ORE AS max_capacity FROM ports WHERE id = ? UNION ALL SELECT 'organics', organics_on_hand, DEF_PORT_MAX_ORG FROM ports WHERE id = ? UNION ALL SELECT 'equipment', equipment_on_hand, DEF_PORT_MAX_EQU FROM ports WHERE id = ?;"; // Assuming port_stock table exists
      rc = sqlite3_prepare_v2(db, sql_stock, -1, &stock_stmt, NULL);
      if (rc != SQLITE_OK) {
          LOGE("daily_market_settlement: Failed to prepare port_stock select for port %d: %s", port_id, sqlite3_errmsg(db));
          sqlite3_finalize(stock_stmt);
          continue;
      }
      sqlite3_bind_int(stock_stmt, 1, port_id);
      sqlite3_bind_int(stock_stmt, 2, port_id);
      sqlite3_bind_int(stock_stmt, 3, port_id);

      while (sqlite3_step(stock_stmt) == SQLITE_ROW) {
          const char *commodity_code = (const char *)sqlite3_column_text(stock_stmt, 0);
          int quantity = sqlite3_column_int(stock_stmt, 1);
          int max_capacity = sqlite3_column_int(stock_stmt, 2);

          int order_quantity = 0;
          int order_price = 0;
          const char *order_side = NULL;

          // Get commodity base price and volatility
          sqlite3_stmt *comm_info_stmt = NULL;
          int base_price = 0;
          int volatility = 0;
          const char *sql_comm_info = "SELECT base_price, volatility FROM commodities WHERE code = ?;";
          if (sqlite3_prepare_v2(db, sql_comm_info, -1, &comm_info_stmt, NULL) == SQLITE_OK) {
              sqlite3_bind_text(comm_info_stmt, 1, commodity_code, -1, SQLITE_STATIC);
              if (sqlite3_step(comm_info_stmt) == SQLITE_ROW) {
                  base_price = sqlite3_column_int(comm_info_stmt, 0);
                  volatility = sqlite3_column_int(comm_info_stmt, 1);
              }
              sqlite3_finalize(comm_info_stmt);
          }

          if (base_price == 0) {
              LOGW("daily_market_settlement: Commodity %s info not found, skipping order generation for port %d.", commodity_code, port_id);
              continue;
          }

          if (quantity < max_capacity * 0.30) { // Low stock, need to buy (slightly higher threshold for ports)
              order_side = "buy";
              order_quantity = max_capacity * 0.6 - quantity;
              order_price = base_price + (base_price * (max_capacity * 0.30 - quantity) / (max_capacity * 0.30)) * (volatility / 100.0);
              if (order_quantity < 1) order_quantity = 1;
              if (order_price < 1) order_price = 1;
          } else if (quantity > max_capacity * 0.70) { // High stock, need to sell (slightly lower threshold for ports)
              order_side = "sell";
              order_quantity = quantity - max_capacity * 0.4;
              order_price = base_price - (base_price * (quantity - max_capacity * 0.70) / (max_capacity * 0.30)) * (volatility / 100.0);
              if (order_quantity < 1) order_quantity = 1;
              if (order_price < 1) order_price = 1;
          }

          if (order_side && order_quantity > 0) {
              sqlite3_stmt *insert_order_stmt = NULL;
              const char *sql_insert_order =
                  "INSERT INTO commodity_orders (actor_type, actor_id, location_type, location_id, commodity_id, side, quantity, price, ts) "
                  "VALUES ('port', ?, 'port', ?, (SELECT id FROM commodities WHERE code = ?), ?, ?, ?, strftime('%Y-%m-%dT%H:%M:%fZ','now'));";
              rc = sqlite3_prepare_v2(db, sql_insert_order, -1, &insert_order_stmt, NULL);
              if (rc != SQLITE_OK) {
                  LOGE("daily_market_settlement: Failed to prepare insert order statement for port %d: %s", port_id, sqlite3_errmsg(db));
                  sqlite3_finalize(insert_order_stmt);
                  continue;
              }
              sqlite3_bind_int(insert_order_stmt, 1, port_id);
              sqlite3_bind_int(insert_order_stmt, 2, port_id);
              sqlite3_bind_text(insert_order_stmt, 3, commodity_code, -1, SQLITE_STATIC);
              sqlite3_bind_text(insert_order_stmt, 4, order_side, -1, SQLITE_STATIC);
              sqlite3_bind_int(insert_order_stmt, 5, order_quantity);
              sqlite3_bind_int(insert_order_stmt, 6, order_price);

              if (sqlite3_step(insert_order_stmt) != SQLITE_DONE) {
                  LOGE("daily_market_settlement: Failed to insert order for port %d, commodity %s: %s", port_id, commodity_code, sqlite3_errmsg(db));
              } else {
                  LOGD("daily_market_settlement: Generated %s order for %d units of %s at %d credits for port %s (ID: %d).", order_side, order_quantity, commodity_code, order_price, port_name, port_id);
              }
              sqlite3_finalize(insert_order_stmt);
          }
      }
      sqlite3_finalize(stock_stmt);
  }
  sqlite3_finalize(port_stmt);

  LOGI ("daily_market_settlement: Starting Order Matching Engine and Trade Execution.");

  // Fetch all commodities
  sqlite3_stmt *commodities_stmt = NULL;
  const char *sql_commodities = "SELECT id, code FROM commodities;";
  rc = sqlite3_prepare_v2(db, sql_commodities, -1, &commodities_stmt, NULL);
  if (rc != SQLITE_OK) {
      LOGE("daily_market_settlement: Failed to prepare commodities select: %s", sqlite3_errmsg(db));
      goto rollback_and_unlock;
  }

  while (sqlite3_step(commodities_stmt) == SQLITE_ROW) {
      int commodity_id = sqlite3_column_int(commodities_stmt, 0);
      const char *commodity_code = (const char *)sqlite3_column_text(commodities_stmt, 1);

      LOGD("daily_market_settlement: Matching orders for commodity: %s", commodity_code);

      // Fetch all open buy orders for this commodity, highest price first, then oldest
      sqlite3_stmt *buy_orders_stmt = NULL;
      const char *sql_buy_orders =
          "SELECT id, actor_type, actor_id, location_type, location_id, quantity, price "
          "FROM commodity_orders WHERE commodity_id = ? AND side = 'buy' AND status = 'open' "
          "ORDER BY price DESC, ts ASC;";
      rc = sqlite3_prepare_v2(db, sql_buy_orders, -1, &buy_orders_stmt, NULL);
      if (rc != SQLITE_OK) {
          LOGE("daily_market_settlement: Failed to prepare buy orders select for %s: %s", commodity_code, sqlite3_errmsg(db));
          sqlite3_finalize(buy_orders_stmt);
          continue;
      }
      sqlite3_bind_int(buy_orders_stmt, 1, commodity_id);

      // Fetch all open sell orders for this commodity, lowest price first, then oldest
      sqlite3_stmt *sell_orders_stmt = NULL;
      const char *sql_sell_orders =
          "SELECT id, actor_type, actor_id, location_type, location_id, quantity, price "
          "FROM commodity_orders WHERE commodity_id = ? AND side = 'sell' AND status = 'open' "
          "ORDER BY price ASC, ts ASC;";
      rc = sqlite3_prepare_v2(db, sql_sell_orders, -1, &sell_orders_stmt, NULL);
      if (rc != SQLITE_OK) {
          LOGE("daily_market_settlement: Failed to prepare sell orders select for %s: %s", commodity_code, sqlite3_errmsg(db));
          sqlite3_finalize(buy_orders_stmt);
          sqlite3_finalize(sell_orders_stmt);
          continue;
      }
      sqlite3_bind_int(sell_orders_stmt, 1, commodity_id);

      // Store orders in memory for matching
      typedef struct {
          int id;
          char actor_type[32];
          int actor_id;
          char location_type[32];
          int location_id;
          int quantity;
          int price;
      } Order;

      // Dynamic arrays for buy and sell orders
      Order *buy_orders = NULL;
      int num_buy_orders = 0;
      int capacity_buy_orders = 0;

      Order *sell_orders = NULL;
      int num_sell_orders = 0;
      int capacity_sell_orders = 0;

      // Populate buy orders
      while (sqlite3_step(buy_orders_stmt) == SQLITE_ROW) {
          if (num_buy_orders >= capacity_buy_orders) {
              capacity_buy_orders = (capacity_buy_orders == 0) ? 10 : capacity_buy_orders * 2;
              buy_orders = realloc(buy_orders, capacity_buy_orders * sizeof(Order));
              if (!buy_orders) {
                  LOGE("daily_market_settlement: Memory allocation failed for buy orders.");
                  goto matching_cleanup;
              }
          }
          buy_orders[num_buy_orders].id = sqlite3_column_int(buy_orders_stmt, 0);
          strncpy(buy_orders[num_buy_orders].actor_type, (const char *)sqlite3_column_text(buy_orders_stmt, 1), sizeof(buy_orders[num_buy_orders].actor_type) - 1);
          buy_orders[num_buy_orders].actor_id = sqlite3_column_int(buy_orders_stmt, 2);
          strncpy(buy_orders[num_buy_orders].location_type, (const char *)sqlite3_column_text(buy_orders_stmt, 3), sizeof(buy_orders[num_buy_orders].location_type) - 1);
          buy_orders[num_buy_orders].location_id = sqlite3_column_int(buy_orders_stmt, 4);
          buy_orders[num_buy_orders].quantity = sqlite3_column_int(buy_orders_stmt, 5);
          buy_orders[num_buy_orders].price = sqlite3_column_int(buy_orders_stmt, 6);
          num_buy_orders++;
      }

      // Populate sell orders
      while (sqlite3_step(sell_orders_stmt) == SQLITE_ROW) {
          if (num_sell_orders >= capacity_sell_orders) {
              capacity_sell_orders = (capacity_sell_orders == 0) ? 10 : capacity_sell_orders * 2;
              sell_orders = realloc(sell_orders, capacity_sell_orders * sizeof(Order));
              if (!sell_orders) {
                  LOGE("daily_market_settlement: Memory allocation failed for sell orders.");
                  goto matching_cleanup;
              }
          }
          sell_orders[num_sell_orders].id = sqlite3_column_int(sell_orders_stmt, 0);
          strncpy(sell_orders[num_sell_orders].actor_type, (const char *)sqlite3_column_text(sell_orders_stmt, 1), sizeof(sell_orders[num_sell_orders].actor_type) - 1);
          sell_orders[num_sell_orders].actor_id = sqlite3_column_int(sell_orders_stmt, 2);
          strncpy(sell_orders[num_sell_orders].location_type, (const char *)sqlite3_column_text(sell_orders_stmt, 3), sizeof(sell_orders[num_sell_orders].location_type) - 1);
          sell_orders[num_sell_orders].location_id = sqlite3_column_int(sell_orders_stmt, 4);
          sell_orders[num_sell_orders].quantity = sqlite3_column_int(sell_orders_stmt, 5);
          sell_orders[num_sell_orders].price = sqlite3_column_int(sell_orders_stmt, 6);
          num_sell_orders++;
      }

      // Matching logic
      for (int i = 0; i < num_buy_orders; ++i) {
          if (buy_orders[i].quantity <= 0) continue;

          for (int j = 0; j < num_sell_orders; ++j) {
              if (sell_orders[j].quantity <= 0) continue;

              if (buy_orders[i].price >= sell_orders[j].price) {
                  // Match found!
                  int trade_quantity = MIN(buy_orders[i].quantity, sell_orders[j].quantity);
                  int trade_price = (buy_orders[i].price + sell_orders[j].price) / 2; // Mid-point price

                  LOGD("daily_market_settlement: Matched %d units of %s at %d credits. Buy Order ID: %d, Sell Order ID: %d",
                       trade_quantity, commodity_code, trade_price, buy_orders[i].id, sell_orders[j].id);

                  // --- Settle Trade ---
                  long long buyer_new_balance = 0;
                  long long seller_new_balance = 0;
                  int credit_transfer_rc;

                  // Deduct credits from buyer
                  credit_transfer_rc = h_deduct_credits(db, buy_orders[i].actor_type, buy_orders[i].actor_id, trade_quantity * trade_price, "TRADE_BUY", NULL, &buyer_new_balance);
                  if (credit_transfer_rc != SQLITE_OK) {
                      LOGW("daily_market_settlement: Buyer %s:%d insufficient funds for trade. Skipping trade.", buy_orders[i].actor_type, buy_orders[i].actor_id);
                      // Mark buy order as failed or cancel it? For now, just skip this match.
                      continue;
                  }

                  // Add credits to seller
                  credit_transfer_rc = h_add_credits(db, sell_orders[j].actor_type, sell_orders[j].actor_id, trade_quantity * trade_price, "TRADE_SELL", NULL, &seller_new_balance);
                  if (credit_transfer_rc != SQLITE_OK) {
                      LOGW("daily_market_settlement: Seller %s:%d failed to receive funds for trade. Rolling back buyer deduction.", sell_orders[j].actor_type, sell_orders[j].actor_id);
                      // Attempt to reverse buyer deduction (this is tricky, might need a separate transaction or more robust error handling)
                      h_add_credits(db, buy_orders[i].actor_type, buy_orders[i].actor_id, trade_quantity * trade_price, "TRADE_REFUND", NULL, NULL); // Refund buyer
                      continue;
                  }

                  // Transfer commodities
                  int commodity_transfer_rc;
                  int dummy_qty; // For new_qty_out

                  // Deduct from seller's stock
                  if (strcasecmp(sell_orders[j].location_type, "planet") == 0) {
                      commodity_transfer_rc = h_update_planet_stock(db, sell_orders[j].location_id, commodity_code, -trade_quantity, &dummy_qty);
                  } else { // port
                      commodity_transfer_rc = h_update_port_stock(db, sell_orders[j].location_id, commodity_code, -trade_quantity, &dummy_qty);
                  }
                  if (commodity_transfer_rc != SQLITE_OK) {
                      LOGW("daily_market_settlement: Seller %s:%d (%s:%d) insufficient stock for trade. Rolling back credit transfers.",
                           sell_orders[j].actor_type, sell_orders[j].actor_id, sell_orders[j].location_type, sell_orders[j].location_id);
                                             // Rollback credit transfers
                                             h_add_credits(db, buy_orders[i].actor_type, buy_orders[i].actor_id, trade_quantity * trade_price, "TRADE_REFUND", NULL, NULL);
                      continue;
                  }

                  // Add to buyer's stock
                  if (strcasecmp(buy_orders[i].location_type, "planet") == 0) {
                      commodity_transfer_rc = h_update_planet_stock(db, buy_orders[i].location_id, commodity_code, trade_quantity, &dummy_qty);
                  } else { // port
                      commodity_transfer_rc = h_update_port_stock(db, buy_orders[i].location_id, commodity_code, trade_quantity, &dummy_qty);
                  }
                  if (commodity_transfer_rc != SQLITE_OK) {
                      LOGW("daily_market_settlement: Buyer %s:%d (%s:%d) max capacity exceeded for trade. Rolling back all previous steps.",
                           buy_orders[i].actor_type, buy_orders[i].actor_id, buy_orders[i].location_type, buy_orders[i].location_id);
                      // Rollback all previous steps
                      h_deduct_credits(db, sell_orders[j].actor_type, sell_orders[j].actor_id, trade_quantity * trade_price, "TRADE_SELL", NULL, NULL);
                      h_add_credits(db, buy_orders[i].actor_type, buy_orders[i].actor_id, trade_quantity * trade_price, "TRADE_REFUND", NULL, NULL);
                      // Re-add commodity to seller (if it was deducted)
                      if (strcasecmp(sell_orders[j].location_type, "planet") == 0) {
                          h_update_planet_stock(db, sell_orders[j].location_id, commodity_code, trade_quantity, NULL);
                      } else {
                          h_update_port_stock(db, sell_orders[j].location_id, commodity_code, trade_quantity, NULL);
                      }
                      continue;
                  }

                  // Record trade
                  sqlite3_stmt *insert_trade_stmt = NULL;
                  const char *sql_insert_trade =
                      "INSERT INTO commodity_trades (commodity_id, buyer_actor_type, buyer_actor_id, buyer_location_type, buyer_location_id, "
                      "seller_actor_type, seller_actor_id, seller_location_type, seller_location_id, quantity, price, ts) "
                      "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, strftime('%Y-%m-%dT%H:%M:%fZ','now'));";
                  rc = sqlite3_prepare_v2(db, sql_insert_trade, -1, &insert_trade_stmt, NULL);
                  if (rc != SQLITE_OK) {
                      LOGE("daily_market_settlement: Failed to prepare insert trade statement: %s", sqlite3_errmsg(db));
                      sqlite3_finalize(insert_trade_stmt);
                      // This is a critical error, attempt to rollback everything for this trade
                      h_deduct_credits(db, sell_orders[j].actor_type, sell_orders[j].actor_id, trade_quantity * trade_price, "TRADE_SELL", NULL, NULL);
                      h_add_credits(db, buy_orders[i].actor_type, buy_orders[i].actor_id, trade_quantity * trade_price, "TRADE_REFUND", NULL, NULL);
                      if (strcasecmp(sell_orders[j].location_type, "planet") == 0) {
                          h_update_planet_stock(db, sell_orders[j].location_id, commodity_code, trade_quantity, NULL);
                      } else {
                          h_update_port_stock(db, sell_orders[j].location_id, commodity_code, trade_quantity, NULL);
                      }
                      if (strcasecmp(buy_orders[i].location_type, "planet") == 0) {
                          h_update_planet_stock(db, buy_orders[i].location_id, commodity_code, -trade_quantity, NULL);
                      } else {
                          h_update_port_stock(db, buy_orders[i].location_id, commodity_code, -trade_quantity, NULL);
                      }
                      continue;
                  }
                  sqlite3_bind_int(insert_trade_stmt, 1, commodity_id);
                  sqlite3_bind_text(insert_trade_stmt, 2, buy_orders[i].actor_type, -1, SQLITE_STATIC);
                  sqlite3_bind_int(insert_trade_stmt, 3, buy_orders[i].actor_id);
                  sqlite3_bind_text(insert_trade_stmt, 4, buy_orders[i].location_type, -1, SQLITE_STATIC);
                  sqlite3_bind_int(insert_trade_stmt, 5, buy_orders[i].location_id);
                  sqlite3_bind_text(insert_trade_stmt, 6, sell_orders[j].actor_type, -1, SQLITE_STATIC);
                  sqlite3_bind_int(insert_trade_stmt, 7, sell_orders[j].actor_id);
                  sqlite3_bind_text(insert_trade_stmt, 8, sell_orders[j].location_type, -1, SQLITE_STATIC);
                  sqlite3_bind_int(insert_trade_stmt, 9, sell_orders[j].location_id);
                  sqlite3_bind_int(insert_trade_stmt, 10, trade_quantity);
                  sqlite3_bind_int(insert_trade_stmt, 11, trade_price);

                  if (sqlite3_step(insert_trade_stmt) != SQLITE_DONE) {
                      LOGE("daily_market_settlement: Failed to insert trade record: %s", sqlite3_errmsg(db));
                  }
                  sqlite3_finalize(insert_trade_stmt);

                  // Update order quantities and status
                  buy_orders[i].quantity -= trade_quantity;
                  sell_orders[j].quantity -= trade_quantity;

                  sqlite3_stmt *update_order_stmt = NULL;
                  const char *sql_update_order = "UPDATE commodity_orders SET quantity = ?, status = ? WHERE id = ?;";

                  // Update buy order
                  rc = sqlite3_prepare_v2(db, sql_update_order, -1, &update_order_stmt, NULL);
                  if (rc == SQLITE_OK) {
                      sqlite3_bind_int(update_order_stmt, 1, buy_orders[i].quantity);
                      sqlite3_bind_text(update_order_stmt, 2, (buy_orders[i].quantity == 0) ? "filled" : "open", -1, SQLITE_STATIC);
                      sqlite3_bind_int(update_order_stmt, 3, buy_orders[i].id);
                      if (sqlite3_step(update_order_stmt) != SQLITE_DONE) {
                          LOGE("daily_market_settlement: Failed to update buy order %d status: %s", buy_orders[i].id, sqlite3_errmsg(db));
                      }
                      sqlite3_finalize(update_order_stmt);
                      update_order_stmt = NULL;
                  }

                  // Update sell order
                  rc = sqlite3_prepare_v2(db, sql_update_order, -1, &update_order_stmt, NULL);
                  if (rc == SQLITE_OK) {
                      sqlite3_bind_int(update_order_stmt, 1, sell_orders[j].quantity);
                      sqlite3_bind_text(update_order_stmt, 2, (sell_orders[j].quantity == 0) ? "filled" : "open", -1, SQLITE_STATIC);
                      sqlite3_bind_int(update_order_stmt, 3, sell_orders[j].id);
                      if (sqlite3_step(update_order_stmt) != SQLITE_DONE) {
                          LOGE("daily_market_settlement: Failed to update sell order %d status: %s", sell_orders[j].id, sqlite3_errmsg(db));
                      }
                      sqlite3_finalize(update_order_stmt);
                      update_order_stmt = NULL;
                  }

                  if (buy_orders[i].quantity == 0) break; // Buy order fully filled, move to next buy order
              }
          }
      }

matching_cleanup:
      free(buy_orders);
      free(sell_orders);
      sqlite3_finalize(buy_orders_stmt);
      sqlite3_finalize(sell_orders_stmt);
  }
  sqlite3_finalize(commodities_stmt);

  // 7. Economic News Generation
  LOGI("daily_market_settlement: Analyzing trades for economic news.");
  sqlite3_stmt *trade_analysis_stmt = NULL;
  const char *sql_trade_analysis = 
      "SELECT T.price, C.code, C.base_price, "
      "CASE T.buyer_location_type WHEN 'port' THEN (SELECT name FROM ports WHERE id = T.buyer_location_id) WHEN 'planet' THEN (SELECT name FROM planets WHERE id = T.buyer_location_id) ELSE 'Unknown' END as location_name, "
      "T.buyer_location_id, T.buyer_location_type "
      "FROM commodity_trades T JOIN commodities C ON T.commodity_id = C.id WHERE T.ts >= strftime('%Y-%m-%dT%H:%M:%fZ', ?, '-1 day');"; // Analyze trades from the last 24 hours for this run
  
  rc = sqlite3_prepare_v2(db, sql_trade_analysis, -1, &trade_analysis_stmt, NULL);
  if (rc != SQLITE_OK) {
      LOGE("daily_market_settlement: Failed to prepare trade analysis statement: %s", sqlite3_errmsg(db));
  } else {
      sqlite3_bind_int64(trade_analysis_stmt, 1, now_s); // Bind current timestamp for comparison

      while (sqlite3_step(trade_analysis_stmt) == SQLITE_ROW) {
          double price = sqlite3_column_double(trade_analysis_stmt, 0);
          const char *commodity_code = (const char *)sqlite3_column_text(trade_analysis_stmt, 1);
          double base_price = sqlite3_column_double(trade_analysis_stmt, 2);
          const char *location_name = (const char *)sqlite3_column_text(trade_analysis_stmt, 3);
          int location_id = sqlite3_column_int(trade_analysis_stmt, 4);
          const char *location_type = (const char *)sqlite3_column_text(trade_analysis_stmt, 5);

          const char *event_type = NULL;
          if (price > base_price * 1.5) {
              event_type = "commodity.boom";
          } else if (price < base_price * 0.5) {
              event_type = "commodity.bust";
          }

          if (event_type) {
              json_t *payload = json_object();
              json_object_set_new(payload, "commodity", json_string(commodity_code));
              json_object_set_new(payload, "location", json_string(location_name));
              json_object_set_new(payload, "price", json_real(price));
              
              int sector_id = 0; // Default sector
              if(strcasecmp(location_type, "port") == 0) {
                  // Get sector from port
                  sqlite3_stmt *sector_lookup_stmt = NULL;
                  const char *sql_sector_lookup = "SELECT sector FROM ports WHERE id = ?;";
                  if (sqlite3_prepare_v2(db, sql_sector_lookup, -1, &sector_lookup_stmt, NULL) == SQLITE_OK) {
                      sqlite3_bind_int(sector_lookup_stmt, 1, location_id);
                      if (sqlite3_step(sector_lookup_stmt) == SQLITE_ROW) {
                          sector_id = sqlite3_column_int(sector_lookup_stmt, 0);
                      }
                      sqlite3_finalize(sector_lookup_stmt);
                  }
              } else if (strcasecmp(location_type, "planet") == 0) {
                  // Get sector from planet
                  sqlite3_stmt *sector_lookup_stmt = NULL;
                  const char *sql_sector_lookup = "SELECT sector FROM planets WHERE id = ?;";
                  if (sqlite3_prepare_v2(db, sql_sector_lookup, -1, &sector_lookup_stmt, NULL) == SQLITE_OK) {
                      sqlite3_bind_int(sector_lookup_stmt, 1, location_id);
                      if (sqlite3_step(sector_lookup_stmt) == SQLITE_ROW) {
                          sector_id = sqlite3_column_int(sector_lookup_stmt, 0);
                      }
                      sqlite3_finalize(sector_lookup_stmt);
                  }
              }

              db_log_engine_event((long long)time(NULL), event_type, NULL, 0, sector_id, payload, NULL);
          }
      }
      sqlite3_finalize(trade_analysis_stmt);
  }

  // Phase 4: Advance Citadel Construction
  LOGI("daily_market_settlement: Advancing Citadel Construction.");
  sqlite3_stmt *citadel_advance_stmt = NULL;
  const char *sql_citadel_advance =
      "UPDATE citadels SET "
      "level = target_level, "
      "construction_status = 'idle', "
      "construction_start_time = 0, "
      "construction_end_time = 0 "
      "WHERE construction_status = 'upgrading' AND construction_end_time <= ?;";
  rc = sqlite3_prepare_v2(db, sql_citadel_advance, -1, &citadel_advance_stmt, NULL);
  if (rc != SQLITE_OK) {
      LOGE("daily_market_settlement: Failed to prepare citadel advance statement: %s", sqlite3_errmsg(db));
  } else {
      sqlite3_bind_int64(citadel_advance_stmt, 1, now_s);
      if (sqlite3_step(citadel_advance_stmt) != SQLITE_DONE) {
          LOGE("daily_market_settlement: Failed to execute citadel advance: %s", sqlite3_errmsg(db));
      } else {
          int changes = sqlite3_changes(db);
          if (changes > 0) {
              LOGI("daily_market_settlement: Completed %d citadel upgrades.", changes);
          }
      }
      sqlite3_finalize(citadel_advance_stmt);
  }

  // Phase 5: Cleanup old orders
  LOGI("daily_market_settlement: Cleaning up old commodity orders.");
  sqlite3_stmt *cleanup_orders_stmt = NULL;
  const char *sql_cleanup_orders = "DELETE FROM commodity_orders WHERE status = 'filled' OR ts < strftime('%Y-%m-%dT%H:%M:%fZ', ?, '-7 day');"; // Delete filled orders or orders older than 7 days
  rc = sqlite3_prepare_v2(db, sql_cleanup_orders, -1, &cleanup_orders_stmt, NULL);
  if (rc != SQLITE_OK) {
      LOGE("daily_market_settlement: Failed to prepare order cleanup statement: %s", sqlite3_errmsg(db));
  } else {
      sqlite3_bind_int64(cleanup_orders_stmt, 1, now_s);
      if (sqlite3_step(cleanup_orders_stmt) != SQLITE_DONE) {
          LOGE("daily_market_settlement: Failed to execute order cleanup: %s", sqlite3_errmsg(db));
      }
      sqlite3_finalize(cleanup_orders_stmt);
  }

  rc = commit (db);
  if (rc != SQLITE_OK)
    {
      LOGE ("daily_market_settlement: commit failed: %s", sqlite3_errmsg (db));
    }

  LOGI ("daily_market_settlement: ok");
  unlock (db, "daily_market_settlement");
  return rc;

rollback_and_unlock:
  rollback (db);
  unlock (db, "daily_market_settlement");
  return rc;
}

int h_daily_corp_tax(sqlite3 *db, int64_t now_s) {
    if (!try_lock(db, "daily_corp_tax", now_s)) {
        return 0;
    }

    LOGI("h_daily_corp_tax: Starting daily corporation tax collection.");

    int rc = begin(db);
    if (rc) {
        unlock(db, "daily_corp_tax");
        return rc;
    }

    sqlite3_stmt *st_corps = NULL;
    const char *sql_select_corps = "SELECT id FROM corporations WHERE id > 0;";
    
    rc = sqlite3_prepare_v2(db, sql_select_corps, -1, &st_corps, NULL);
    if (rc != SQLITE_OK) {
        LOGE("h_daily_corp_tax: Failed to prepare select corporations statement: %s", sqlite3_errmsg(db));
        goto rollback_and_unlock_tax;
    }

    while (sqlite3_step(st_corps) == SQLITE_ROW) {
        int corp_id = sqlite3_column_int(st_corps, 0);
        long long total_assets = 0;
        long long bank_balance = 0;
        
        // Get corp bank balance
        db_get_corp_bank_balance(corp_id, &bank_balance);
        total_assets += bank_balance;

        // Get planet assets
        sqlite3_stmt *st_planets = NULL;
        const char *sql_select_planets = "SELECT ore_on_hand, organics_on_hand, equipment_on_hand FROM planets WHERE owner_id = ? AND owner_type = 'corp';";
        if (sqlite3_prepare_v2(db, sql_select_planets, -1, &st_planets, NULL) == SQLITE_OK) {
            sqlite3_bind_int(st_planets, 1, corp_id);
            while (sqlite3_step(st_planets) == SQLITE_ROW) {
                total_assets += sqlite3_column_int64(st_planets, 0) * 100; // price of ore
                total_assets += sqlite3_column_int64(st_planets, 1) * 150; // price of organics
                total_assets += sqlite3_column_int64(st_planets, 2) * 200; // price of equipment
            }
            sqlite3_finalize(st_planets);
        }

        long long tax_amount = (total_assets * CORP_TAX_RATE_BP) / 10000;
        if (tax_amount <= 0) continue;

        if (db_bank_withdraw("corp", corp_id, tax_amount) != SQLITE_OK) {
            // Failed to pay tax
            sqlite3_stmt *st_update_corp = NULL;
            const char *sql_update_corp = "UPDATE corporations SET tax_arrears = tax_arrears + ?, credit_rating = credit_rating - 1 WHERE id = ?;";
            if (sqlite3_prepare_v2(db, sql_update_corp, -1, &st_update_corp, NULL) == SQLITE_OK) {
                sqlite3_bind_int64(st_update_corp, 1, tax_amount);
                sqlite3_bind_int(st_update_corp, 2, corp_id);
                sqlite3_step(st_update_corp);
                sqlite3_finalize(st_update_corp);
            }
        }
    }
    sqlite3_finalize(st_corps);

    rc = commit(db);
    if (rc != SQLITE_OK) {
        LOGE("h_daily_corp_tax: commit failed: %s", sqlite3_errmsg(db));
        goto rollback_and_unlock_tax;
    }
    
    unlock(db, "daily_corp_tax");
    return SQLITE_OK;

rollback_and_unlock_tax:
    if(st_corps) sqlite3_finalize(st_corps);
    rollback(db);
    unlock(db, "daily_corp_tax");
    return rc;
}





